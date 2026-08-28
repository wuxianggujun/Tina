#include <tina/network/TcpConnection.hpp>

#include "detail/NativeSocket.hpp"
#include "detail/ReadinessPoller.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/network/NetworkErrors.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace Tina::Network {
namespace {

using Detail::closeNativeSocket;
using Detail::InvalidNativeSocket;
using Detail::isConnectionResetError;
using Detail::isInterruptedError;
using Detail::isWouldBlockError;
using Detail::lastSocketError;
using Detail::NativeAddressLength;
using Detail::NativeSocket;
using Detail::ReadinessInterest;
using Detail::ReadinessPoller;
using Detail::TransportScope;

} // namespace

struct TcpConnection::Impl final {
    explicit Impl(std::pmr::memory_resource& resource, ReadinessPoller pollerIn)
        : poller(std::move(pollerIn))
        , sendBuffer(&resource)
        , receiveBuffer(&resource)
    {
    }

    NativeSocket socket = InvalidNativeSocket;
    std::thread::id owner{};
    TcpConnectionState state = TcpConnectionState::Connecting;

    NetworkEndpoint remote{};

    // One socket, but the poller is what keeps this connection on the same
    // readiness model as every other transport.
    ReadinessPoller poller;
    Core::usize registration = 0;

    // Both are fixed-capacity byte rings kept as flat buffers with a live prefix.
    // Compacting on drain keeps a contiguous span available to the caller, which a
    // ring of two segments could not offer without a copy.
    std::pmr::vector<std::byte> sendBuffer;
    std::pmr::vector<std::byte> receiveBuffer;
    Core::usize sendPending = 0;
    Core::usize receivePending = 0;

    TcpConnectionStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    // Read interest is always on so a peer close is noticed promptly; write
    // interest only while bytes are queued, otherwise poll would report the
    // socket writable every single frame.
    void refreshInterest() noexcept
    {
        if (state != TcpConnectionState::Connecting
            && state != TcpConnectionState::Connected
            && state != TcpConnectionState::PeerClosed) {
            return;
        }

        ReadinessInterest interest = ReadinessInterest::Readable;
        if (state == TcpConnectionState::Connecting) {
            // A completing handshake surfaces as writability.
            interest = ReadinessInterest::ReadableAndWritable;
        } else if (sendPending != 0) {
            interest = ReadinessInterest::ReadableAndWritable;
        }
        (void)poller.setInterest(registration, interest);
    }

    void markFailed() noexcept
    {
        state = TcpConnectionState::Failed;
        (void)poller.setInterest(registration, ReadinessInterest::None);
    }

    void closeSocket() noexcept
    {
        poller.release(registration);
        closeNativeSocket(socket);
        socket = InvalidNativeSocket;
    }
};

TcpConnection::TcpConnection(Impl* impl) noexcept
    : m_impl(impl)
{
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

TcpConnection::~TcpConnection() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    closeNativeSocket(m_impl->socket);
    TransportScope::release();
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<TcpConnection> TcpConnection::Create(TcpConnectionConfig config)
{
    if (config.sendBufferBytes == 0 || config.receiveBufferBytes == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TcpConnection send and receive buffer sizes must be greater than zero");
    }
    if (!config.remoteEndpoint.address.hasValue() || config.remoteEndpoint.port == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "TcpConnection requires a remote address and a non-zero port");
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    if (const Core::Status status = TransportScope::acquire(); !status) {
        return Core::failure(status.error());
    }
    auto transportGuard = Core::makeScopeExit([]() noexcept { TransportScope::release(); });

    // One registration slot: this type owns exactly one socket.
    auto poller = ReadinessPoller::Create(1, *resource);
    if (!poller) {
        return Core::failure(std::move(poller.error()));
    }

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, std::move(*poller)};
        impl->sendBuffer.resize(config.sendBufferBytes);
        impl->receiveBuffer.resize(config.receiveBufferBytes);
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "TcpConnection fixed buffer allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "TcpConnection construction failed");
    }

    auto implGuard = Core::makeScopeExit([&impl]() noexcept {
        if (impl != nullptr) {
            closeNativeSocket(impl->socket);
            delete impl;
        }
    });

    impl->owner = std::this_thread::get_id();
    impl->remote = config.remoteEndpoint;

    const int addressFamily = config.remoteEndpoint.address.family() == IpFamily::V4
        ? AF_INET
        : AF_INET6;
    impl->socket = ::socket(addressFamily, SOCK_STREAM, IPPROTO_TCP);
    if (impl->socket == InvalidNativeSocket) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to create TCP socket");
    }

    if (const Core::Status status = Detail::setNativeSocketNonBlocking(impl->socket); !status) {
        return Core::failure(status.error());
    }

    // Must precede connect: on some stacks a buffer size change is only honoured
    // before the handshake, because the window is negotiated during it.
    if (config.kernelSendBufferBytes != 0 || config.kernelReceiveBufferBytes != 0) {
        const auto clamp = [](Core::usize value) noexcept {
            const auto limit = static_cast<Core::usize>((std::numeric_limits<int>::max)());
            return static_cast<int>(value > limit ? limit : value);
        };
        Detail::requestNativeSocketBufferSizes(
            impl->socket,
            clamp(config.kernelSendBufferBytes),
            clamp(config.kernelReceiveBufferBytes));
    }

    sockaddr_storage address{};
    const NativeAddressLength addressLength =
        Detail::toNativeAddress(config.remoteEndpoint, address);
    if (addressLength == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "TcpConnection remote endpoint family is unsupported");
    }

    // A non-blocking connect nearly always reports "in progress" rather than
    // succeeding outright, so only a hard error is fatal here.
    if (::connect(impl->socket, reinterpret_cast<const sockaddr*>(&address), addressLength) != 0) {
        const int error = lastSocketError();
        if (!Detail::isConnectInProgressError(error)) {
            return Core::failure(
                NetworkErrorCode::ConnectionFailed,
                "TCP connect failed immediately");
        }
    }

    auto registration = impl->poller.register_(
        impl->socket,
        ReadinessInterest::ReadableAndWritable);
    if (!registration) {
        return Core::failure(std::move(registration.error()));
    }
    impl->registration = *registration;
    impl->state = TcpConnectionState::Connecting;

    implGuard.release();
    transportGuard.release();
    return TcpConnection{impl};
}

TcpConnectionState TcpConnection::state() const noexcept
{
    if (m_impl == nullptr) {
        return TcpConnectionState::Closed;
    }
    return m_impl->state;
}

Core::Result<NetworkEndpoint> TcpConnection::localEndpoint() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }
    if (m_impl->socket == InvalidNativeSocket) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection socket is closed");
    }

    sockaddr_storage address{};
    auto length = static_cast<NativeAddressLength>(sizeof(address));
    if (::getsockname(m_impl->socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to read the local endpoint");
    }

    NetworkEndpoint endpoint{};
    if (!Detail::fromNativeAddress(address, endpoint)) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Local endpoint has an unsupported family");
    }
    return endpoint;
}

Core::Result<NetworkEndpoint> TcpConnection::remoteEndpoint() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }
    return m_impl->remote;
}

Core::Status TcpConnection::send(std::span<const std::byte> payload)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }
    if (payload.empty()) {
        return Core::failure(
            NetworkErrorCode::InvalidDatagram,
            "TcpConnection cannot send an empty payload");
    }

    switch (m_impl->state) {
    case TcpConnectionState::Connecting:
    case TcpConnectionState::Connected:
    case TcpConnectionState::PeerClosed:
        // A peer that closed its sending half can still receive, so queuing is
        // legal in PeerClosed.
        break;
    case TcpConnectionState::Closed:
        return Core::failure(
            NetworkErrorCode::ConnectionClosed,
            "TcpConnection is closed");
    case TcpConnectionState::Failed:
        return Core::failure(
            NetworkErrorCode::ConnectionFailed,
            "TcpConnection has failed");
    }

    const Core::usize available = m_impl->sendBuffer.size() - m_impl->sendPending;
    if (payload.size() > available) {
        // Accepting part of a payload would hand the caller a split it never
        // asked for, and it could not tell how much was taken without extra API.
        return Core::failure(
            NetworkErrorCode::CapacityExceeded,
            "TcpConnection send buffer cannot hold the payload");
    }

    std::memcpy(m_impl->sendBuffer.data() + m_impl->sendPending, payload.data(), payload.size());
    m_impl->sendPending += payload.size();
    m_impl->stats.queuedSendBytes = m_impl->sendPending;
    m_impl->refreshInterest();
    return Core::success();
}

Core::Result<Core::usize> TcpConnection::pump()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }

    ++m_impl->stats.pumpCallCount;

    if (m_impl->state == TcpConnectionState::Closed) {
        return Core::failure(
            NetworkErrorCode::ConnectionClosed,
            "TcpConnection is closed");
    }
    if (m_impl->state == TcpConnectionState::Failed) {
        return Core::failure(
            NetworkErrorCode::ConnectionFailed,
            "TcpConnection has failed");
    }

    auto events = m_impl->poller.poll();
    if (!events) {
        return Core::failure(std::move(events.error()));
    }

    bool readable = false;
    bool writable = false;
    bool errored = false;
    for (const auto& event : *events) {
        if (event.registrationIndex != m_impl->registration) {
            continue;
        }
        readable = readable || event.readable;
        writable = writable || event.writable;
        errored = errored || event.errored;
    }

    // Completing the handshake comes first: nothing else may touch the socket
    // until the outcome is known.
    if (m_impl->state == TcpConnectionState::Connecting) {
        if (!writable && !readable && !errored) {
            return Core::usize{0};
        }

        const int pending = Detail::takePendingSocketError(m_impl->socket);
        if (pending != 0) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::ConnectionFailed,
                "TCP handshake failed");
        }
        if (errored) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::ConnectionFailed,
                "TCP handshake reported a socket error");
        }
        m_impl->state = TcpConnectionState::Connected;
        m_impl->refreshInterest();
    }

    // Drain the send buffer before reading, so a request-then-response exchange
    // completes within a single pump when the peer is fast.
    if (writable && m_impl->sendPending != 0) {
        const char* data = reinterpret_cast<const char*>(m_impl->sendBuffer.data());
#if defined(_WIN32)
        const int sent = ::send(
            m_impl->socket,
            data,
            static_cast<int>(m_impl->sendPending),
            0);
#else
        const ssize_t sent = ::send(m_impl->socket, data, m_impl->sendPending, 0);
#endif
        if (sent < 0) {
            const int error = lastSocketError();
            if (isConnectionResetError(error)) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::PeerReset,
                    "TCP peer reset the connection during send");
            }
            if (!isWouldBlockError(error) && !isInterruptedError(error)) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::BackendFailure,
                    "TCP send failed");
            }
            // WouldBlock and Interrupted both mean "try again", so the queue is
            // left intact for the next pump.
        } else {
            const auto sentBytes = static_cast<Core::usize>(sent);
            if (sentBytes < m_impl->sendPending) {
                // A partial send is normal TCP behaviour under backpressure, not
                // an error: keep the tail and compact it to the front.
                std::memmove(
                    m_impl->sendBuffer.data(),
                    m_impl->sendBuffer.data() + sentBytes,
                    m_impl->sendPending - sentBytes);
                ++m_impl->stats.partialSendCount;
            }
            m_impl->sendPending -= sentBytes;
            m_impl->stats.totalSentBytes += sentBytes;
            m_impl->stats.queuedSendBytes = m_impl->sendPending;
            m_impl->refreshInterest();
        }
    }

    // A hangup or error report must also drive a read attempt. poll cannot
    // portably say whether it means "the peer closed cleanly" or "the connection
    // broke", and only recv distinguishes them: 0 for end-of-stream, an error
    // otherwise. Skipping the read here would leave an orderly close undetected
    // and the connection stuck in Connected forever.
    Core::usize newlyReceived = 0;
    if ((readable || errored) && m_impl->state != TcpConnectionState::PeerClosed) {
        const Core::usize space = m_impl->receiveBuffer.size() - m_impl->receivePending;
        if (space != 0) {
            char* target = reinterpret_cast<char*>(
                m_impl->receiveBuffer.data() + m_impl->receivePending);
#if defined(_WIN32)
            const int got = ::recv(m_impl->socket, target, static_cast<int>(space), 0);
#else
            const ssize_t got = ::recv(m_impl->socket, target, space, 0);
#endif
            if (got > 0) {
                newlyReceived = static_cast<Core::usize>(got);
                m_impl->receivePending += newlyReceived;
                m_impl->stats.totalReceivedBytes += newlyReceived;
                m_impl->stats.bufferedReceiveBytes = m_impl->receivePending;
            } else if (got == 0) {
                // Zero from recv means the peer closed its sending half. That is
                // an orderly end-of-stream, not a failure, and already-buffered
                // bytes stay readable.
                m_impl->state = TcpConnectionState::PeerClosed;
                m_impl->refreshInterest();
            } else {
                const int error = lastSocketError();
                if (isConnectionResetError(error)) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::PeerReset,
                        "TCP peer reset the connection during receive");
                }
                if (!isWouldBlockError(error) && !isInterruptedError(error)) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::BackendFailure,
                        "TCP receive failed");
                }
            }
        }
    }

    // Only consult the socket error if the read above did not already explain the
    // event. A clean peer close also raises the hangup flag, and treating that as
    // a fault would turn an orderly end-of-stream into a failure.
    if (errored && m_impl->state == TcpConnectionState::Connected && newlyReceived == 0) {
        const int pending = Detail::takePendingSocketError(m_impl->socket);
        if (pending != 0) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::PeerReset,
                "TCP connection reported a socket error");
        }
    }

    return newlyReceived;
}

Core::Result<std::span<const std::byte>> TcpConnection::receive()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }
    return std::span<const std::byte>{m_impl->receiveBuffer.data(), m_impl->receivePending};
}

Core::Status TcpConnection::consume(Core::usize byteCount)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }
    if (byteCount > m_impl->receivePending) {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "TcpConnection cannot consume more bytes than are buffered");
    }
    if (byteCount == 0) {
        return Core::success();
    }

    const Core::usize remaining = m_impl->receivePending - byteCount;
    if (remaining != 0) {
        std::memmove(
            m_impl->receiveBuffer.data(),
            m_impl->receiveBuffer.data() + byteCount,
            remaining);
    }
    m_impl->receivePending = remaining;
    m_impl->stats.bufferedReceiveBytes = remaining;
    return Core::success();
}

Core::Status TcpConnection::shutdownSend()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpConnection must be used from its owner thread");
    }
    if (m_impl->state == TcpConnectionState::Closed
        || m_impl->state == TcpConnectionState::Failed) {
        return Core::failure(
            NetworkErrorCode::NotConnected,
            "TcpConnection is not connected");
    }

    Detail::shutdownNativeSocketSend(m_impl->socket);
    // Queued bytes can no longer leave, so drop them rather than leave a queue
    // that will never drain.
    m_impl->sendPending = 0;
    m_impl->stats.queuedSendBytes = 0;
    m_impl->refreshInterest();
    return Core::success();
}

void TcpConnection::close() noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return;
    }
    if (m_impl->state == TcpConnectionState::Closed) {
        return;
    }
    m_impl->closeSocket();
    m_impl->state = TcpConnectionState::Closed;
    m_impl->sendPending = 0;
    m_impl->stats.queuedSendBytes = 0;
}

TcpConnectionStatistics TcpConnection::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

Core::usize TcpConnection::sendBufferCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->sendBuffer.size() : 0;
}

Core::usize TcpConnection::receiveBufferCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->receiveBuffer.size() : 0;
}

ByteStreamState TcpConnection::streamState() const noexcept
{
    switch (state()) {
    case TcpConnectionState::Connecting:
        return ByteStreamState::Connecting;
    case TcpConnectionState::Connected:
        return ByteStreamState::Connected;
    case TcpConnectionState::PeerClosed:
        return ByteStreamState::PeerClosed;
    case TcpConnectionState::Closed:
        return ByteStreamState::Closed;
    case TcpConnectionState::Failed:
        return ByteStreamState::Failed;
    }
    return ByteStreamState::Failed;
}

Core::Status TcpConnection::sendBytes(std::span<const std::byte> payload)
{
    return send(payload);
}

Core::Result<Core::usize> TcpConnection::pumpStream()
{
    return pump();
}

Core::Result<std::span<const std::byte>> TcpConnection::peekReceived()
{
    return receive();
}

Core::Status TcpConnection::consumeReceived(Core::usize byteCount)
{
    return consume(byteCount);
}

void TcpConnection::closeStream() noexcept
{
    close();
}

} // namespace Tina::Network
