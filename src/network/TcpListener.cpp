#include <tina/network/TcpListener.hpp>

#include "detail/NativeSocket.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/network/NetworkErrors.hpp>

#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace Tina::Network {
namespace {

using Detail::closeNativeSocket;
using Detail::InvalidNativeSocket;
using Detail::isInterruptedError;
using Detail::isWouldBlockError;
using Detail::lastSocketError;
using Detail::NativeAddressLength;
using Detail::NativeSocket;
using Detail::TransportScope;

} // namespace

struct TcpListener::Impl final {
    explicit Impl(std::pmr::memory_resource& resource)
        : ready(&resource)
    {
    }

    NativeSocket socket = InvalidNativeSocket;
    IpFamily family = IpFamily::Unspecified;
    std::thread::id owner{};

    Core::usize connectionCapacity = 0;
    TcpConnectionConfig connectionTemplate{};

    // Accepted and not yet claimed, oldest first. Held by pointer because
    // TcpConnection deliberately deletes move-assignment -- an owning handle that
    // can be overwritten in place is how a socket gets leaked -- and erasing from
    // the middle of a vector needs assignment.
    std::pmr::vector<std::unique_ptr<TcpConnection>> ready;

    TcpListenerStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }
};

TcpListener::TcpListener(Impl* impl) noexcept
    : m_impl(impl)
{
}

TcpListener::TcpListener(TcpListener&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

TcpListener::~TcpListener() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    // Accepted connections own their own sockets and release their own transport
    // scope, so clearing them here is enough.
    m_impl->ready.clear();
    closeNativeSocket(m_impl->socket);
    TransportScope::release();
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<TcpListener> TcpListener::Create(TcpListenerConfig config)
{
    if (config.connectionCapacity == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TcpListener connectionCapacity must be greater than zero");
    }
    if (config.backlog == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TcpListener backlog must be greater than zero");
    }
    if (config.connectionSendBufferBytes == 0 || config.connectionReceiveBufferBytes == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TcpListener connection buffer sizes must be greater than zero");
    }
    if (!config.localEndpoint.address.hasValue()) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TcpListener localEndpoint requires an address family");
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    if (const Core::Status status = TransportScope::acquire(); !status) {
        return Core::failure(status.error());
    }
    auto transportGuard = Core::makeScopeExit([]() noexcept { TransportScope::release(); });

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource};
        impl->ready.reserve(config.connectionCapacity);
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "TcpListener allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "TcpListener construction failed");
    }

    auto implGuard = Core::makeScopeExit([&impl]() noexcept {
        if (impl != nullptr) {
            closeNativeSocket(impl->socket);
            delete impl;
        }
    });

    impl->owner = std::this_thread::get_id();
    impl->connectionCapacity = config.connectionCapacity;
    impl->family = config.localEndpoint.address.family();
    impl->connectionTemplate.sendBufferBytes = config.connectionSendBufferBytes;
    impl->connectionTemplate.receiveBufferBytes = config.connectionReceiveBufferBytes;
    impl->connectionTemplate.memoryResource = config.memoryResource;

    const int addressFamily = impl->family == IpFamily::V4 ? AF_INET : AF_INET6;
    impl->socket = ::socket(addressFamily, SOCK_STREAM, IPPROTO_TCP);
    if (impl->socket == InvalidNativeSocket) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to create a listening socket");
    }

    // V6 stays V6-only: dual-stack mapping differs across platforms and would make
    // an accepted peer's family ambiguous.
    if (impl->family == IpFamily::V6) {
#if defined(_WIN32)
        DWORD v6Only = 1;
        ::setsockopt(
            impl->socket,
            IPPROTO_IPV6,
            IPV6_V6ONLY,
            reinterpret_cast<const char*>(&v6Only),
            sizeof(v6Only));
#else
        int v6Only = 1;
        ::setsockopt(impl->socket, IPPROTO_IPV6, IPV6_V6ONLY, &v6Only, sizeof(v6Only));
#endif
    }

#if !defined(_WIN32)
    // SO_REUSEADDR on POSIX lets a restarted server rebind a port still in
    // TIME_WAIT. It is deliberately not set on Windows, where it permits two live
    // sockets on the same port and would silently steal traffic.
    int reuse = 1;
    ::setsockopt(impl->socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    if (const Core::Status status = Detail::setNativeSocketNonBlocking(impl->socket); !status) {
        return Core::failure(status.error());
    }

    sockaddr_storage bindAddress{};
    const NativeAddressLength bindLength =
        Detail::toNativeAddress(config.localEndpoint, bindAddress);
    if (bindLength == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "TcpListener localEndpoint family is unsupported");
    }
    if (::bind(impl->socket, reinterpret_cast<const sockaddr*>(&bindAddress), bindLength) != 0) {
        return Core::failure(
            NetworkErrorCode::AddressUnavailable,
            "Failed to bind the listening socket");
    }

    if (::listen(impl->socket, static_cast<int>(config.backlog)) != 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to listen on the bound socket");
    }

    implGuard.release();
    transportGuard.release();
    return TcpListener{impl};
}

Core::Result<NetworkEndpoint> TcpListener::localEndpoint() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpListener is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpListener must be used from its owner thread");
    }

    sockaddr_storage address{};
    auto length = static_cast<NativeAddressLength>(sizeof(address));
    if (::getsockname(m_impl->socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to read the bound local endpoint");
    }

    NetworkEndpoint endpoint{};
    if (!Detail::fromNativeAddress(address, endpoint)) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Bound local endpoint has an unsupported family");
    }
    return endpoint;
}

Core::Result<Core::usize> TcpListener::pump()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpListener is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpListener must be used from its owner thread");
    }

    ++m_impl->stats.pumpCallCount;

    Core::usize accepted = 0;
    while (m_impl->ready.size() < m_impl->connectionCapacity) {
        sockaddr_storage peer{};
        auto peerLength = static_cast<NativeAddressLength>(sizeof(peer));
        const auto socket = ::accept(
            m_impl->socket,
            reinterpret_cast<sockaddr*>(&peer),
            &peerLength);

        if (socket == InvalidNativeSocket) {
            const int error = lastSocketError();
            if (isWouldBlockError(error) || isInterruptedError(error)) {
                break;
            }
            // A single failed accept must not end the listener: a peer that aborted
            // between the SYN and accept() is routine, and the next one may be fine.
            ++m_impl->stats.acceptFailureCount;
            break;
        }

        NetworkEndpoint remote{};
        if (!Detail::fromNativeAddress(peer, remote)) {
            // An address family this build cannot represent. Closing is the honest
            // answer; handing over a connection with no usable peer address is not.
            closeNativeSocket(socket);
            ++m_impl->stats.rejectedConnectionCount;
            continue;
        }

        auto acceptedSocket = socket;
        auto connection = TcpConnection::adoptAcceptedSocket(
            &acceptedSocket,
            remote,
            m_impl->connectionTemplate);
        if (!connection) {
            closeNativeSocket(socket);
            ++m_impl->stats.acceptFailureCount;
            continue;
        }

        try {
            m_impl->ready.push_back(
                std::make_unique<TcpConnection>(std::move(*connection)));
        } catch (...) {
            // The vector was reserved to capacity at Create, so this should not
            // happen; if it does, the connection is dropped rather than leaked.
            ++m_impl->stats.acceptFailureCount;
            continue;
        }

        ++accepted;
        ++m_impl->stats.acceptedConnectionCount;
    }

    m_impl->stats.readyConnectionCount = m_impl->ready.size();
    return accepted;
}

Core::usize TcpListener::readyConnectionCount() const noexcept
{
    return m_impl != nullptr ? m_impl->ready.size() : 0;
}

Core::Result<TcpConnection> TcpListener::acceptNext()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TcpListener is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TcpListener must be used from its owner thread");
    }
    if (m_impl->ready.empty()) {
        return Core::failure(
            NetworkErrorCode::WouldBlock,
            "TcpListener has no accepted connection ready");
    }

    TcpConnection connection = std::move(*m_impl->ready.front());
    m_impl->ready.erase(m_impl->ready.begin());
    m_impl->stats.readyConnectionCount = m_impl->ready.size();
    return connection;
}

void TcpListener::dropPendingConnections() noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return;
    }
    m_impl->ready.clear();
    m_impl->stats.readyConnectionCount = 0;
}

TcpListenerStatistics TcpListener::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

Core::usize TcpListener::connectionCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->connectionCapacity : 0;
}

} // namespace Tina::Network
