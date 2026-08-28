#include <tina/network/UdpSocket.hpp>

#include <tina/network/NetworkErrors.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Tina::Network {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
inline constexpr NativeSocket InvalidNativeSocket = INVALID_SOCKET;
using NativeAddressLength = int;
#else
using NativeSocket = int;
inline constexpr NativeSocket InvalidNativeSocket = -1;
using NativeAddressLength = socklen_t;
#endif

void closeNativeSocket(NativeSocket socket) noexcept
{
    if (socket == InvalidNativeSocket) {
        return;
    }
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

[[nodiscard]] int lastSocketError() noexcept
{
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

[[nodiscard]] bool isWouldBlockError(int error) noexcept
{
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

// A datagram larger than the receive buffer. Windows reports this as an error on
// the call, POSIX silently truncates and sets MSG_TRUNC.
[[nodiscard]] bool isMessageSizeError(int error) noexcept
{
#if defined(_WIN32)
    return error == WSAEMSGSIZE;
#else
    return error == EMSGSIZE;
#endif
}

// Winsock needs process-wide init. Refcounted so that constructing and dropping
// sockets repeatedly across a run cannot leave the library torn down while a
// socket is still open.
#if defined(_WIN32)
class WinsockScope final {
  public:
    [[nodiscard]] static Core::Status acquire() noexcept
    {
        static std::mutex mutex;
        const std::lock_guard<std::mutex> guard{mutex};
        if (s_refCount == 0) {
            WSADATA data{};
            if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                return Core::failure(
                    NetworkErrorCode::BackendFailure,
                    "WSAStartup failed");
            }
        }
        ++s_refCount;
        return Core::success();
    }

    static void release() noexcept
    {
        static std::mutex mutex;
        const std::lock_guard<std::mutex> guard{mutex};
        if (s_refCount == 0) {
            return;
        }
        --s_refCount;
        if (s_refCount == 0) {
            ::WSACleanup();
        }
    }

  private:
    static Core::usize s_refCount;
};

Core::usize WinsockScope::s_refCount = 0;
#endif

[[nodiscard]] Core::Status setNonBlocking(NativeSocket socket) noexcept
{
#if defined(_WIN32)
    u_long mode = 1;
    if (::ioctlsocket(socket, FIONBIO, &mode) != 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to place UDP socket in non-blocking mode");
    }
    return Core::success();
#else
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to place UDP socket in non-blocking mode");
    }
    return Core::success();
#endif
}

// Fills a sockaddr from a Tina endpoint. Returns 0 on an unsupported family.
[[nodiscard]] NativeAddressLength toNativeAddress(
    const NetworkEndpoint& endpoint,
    sockaddr_storage& out) noexcept
{
    std::memset(&out, 0, sizeof(out));
    if (endpoint.address.family() == IpFamily::V4) {
        auto* v4 = reinterpret_cast<sockaddr_in*>(&out);
        v4->sin_family = AF_INET;
        v4->sin_port = ::htons(endpoint.port);
        std::memcpy(&v4->sin_addr, endpoint.address.bytes().data(), IpAddress::V4ByteCount);
        return static_cast<NativeAddressLength>(sizeof(sockaddr_in));
    }
    if (endpoint.address.family() == IpFamily::V6) {
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&out);
        v6->sin6_family = AF_INET6;
        v6->sin6_port = ::htons(endpoint.port);
        std::memcpy(&v6->sin6_addr, endpoint.address.bytes().data(), IpAddress::V6ByteCount);
        return static_cast<NativeAddressLength>(sizeof(sockaddr_in6));
    }
    return 0;
}

[[nodiscard]] bool fromNativeAddress(
    const sockaddr_storage& source,
    NetworkEndpoint& out) noexcept
{
    if (source.ss_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(&source);
        std::array<Core::u8, IpAddress::V4ByteCount> bytes{};
        std::memcpy(bytes.data(), &v4->sin_addr, IpAddress::V4ByteCount);
        out.address = IpAddress::v4(bytes[0], bytes[1], bytes[2], bytes[3]);
        out.port = ::ntohs(v4->sin_port);
        return true;
    }
    if (source.ss_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&source);
        std::array<Core::u8, IpAddress::V6ByteCount> bytes{};
        std::memcpy(bytes.data(), &v6->sin6_addr, IpAddress::V6ByteCount);
        out.address = IpAddress::v6(bytes);
        out.port = ::ntohs(v6->sin6_port);
        return true;
    }
    return false;
}

} // namespace

struct UdpSocket::Impl final {
    explicit Impl(std::pmr::memory_resource& resource)
        : storage(&resource)
        , datagrams(&resource)
    {
    }

    NativeSocket socket = InvalidNativeSocket;
    IpFamily family = IpFamily::Unspecified;
    std::thread::id owner{};

    Core::usize receiveQueueCapacity = 0;
    Core::usize maximumDatagramBytes = 0;

    // One contiguous block, sliced per queue slot. Sized at Create and never
    // resized, so a payload span stays valid until the next receive().
    std::pmr::vector<std::byte> storage;
    std::pmr::vector<ReceivedDatagram> datagrams;

    UdpSocketStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }
};

UdpSocket::UdpSocket(Impl* impl) noexcept
    : m_impl(impl)
{
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

UdpSocket::~UdpSocket() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    closeNativeSocket(m_impl->socket);
#if defined(_WIN32)
    WinsockScope::release();
#endif
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<UdpSocket> UdpSocket::Create(UdpSocketConfig config)
{
    if (config.receiveQueueCapacity == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "UdpSocket receiveQueueCapacity must be greater than zero");
    }
    if (config.maximumDatagramBytes == 0 || config.maximumDatagramBytes > MaximumDatagramBytes) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "UdpSocket maximumDatagramBytes must be in [1, MaximumDatagramBytes]");
    }
    if (!config.localEndpoint.address.hasValue()) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "UdpSocket localEndpoint requires an address family");
    }

    // Guard the multiply before it reaches allocate(), where an overflowed size
    // would silently under-allocate.
    const Core::usize maximumStorageBytes = (std::numeric_limits<Core::usize>::max)();
    if (config.receiveQueueCapacity > maximumStorageBytes / (config.maximumDatagramBytes + 1)) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "UdpSocket receive storage size overflows");
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

#if defined(_WIN32)
    if (const Core::Status status = WinsockScope::acquire(); !status) {
        return Core::failure(status.error());
    }
#endif

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource};
        // One extra byte per slot. recvfrom cannot report "this datagram was
        // larger than the buffer" on POSIX -- it truncates silently -- so the
        // slot is deliberately larger than the advertised maximum. A datagram
        // that reaches into that spare byte is therefore known to be oversized
        // rather than merely maximum-sized.
        impl->storage.resize(config.receiveQueueCapacity * (config.maximumDatagramBytes + 1));
        impl->datagrams.resize(config.receiveQueueCapacity);
    } catch (const std::bad_alloc&) {
        delete impl;
#if defined(_WIN32)
        WinsockScope::release();
#endif
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "UdpSocket fixed receive storage allocation failed");
    } catch (...) {
        delete impl;
#if defined(_WIN32)
        WinsockScope::release();
#endif
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "UdpSocket construction failed");
    }

    impl->owner = std::this_thread::get_id();
    impl->receiveQueueCapacity = config.receiveQueueCapacity;
    impl->maximumDatagramBytes = config.maximumDatagramBytes;
    impl->family = config.localEndpoint.address.family();

    const int addressFamily = impl->family == IpFamily::V4 ? AF_INET : AF_INET6;
    impl->socket = ::socket(addressFamily, SOCK_DGRAM, IPPROTO_UDP);
    if (impl->socket == InvalidNativeSocket) {
        delete impl;
#if defined(_WIN32)
        WinsockScope::release();
#endif
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to create UDP socket");
    }

    // V6 sockets stay V6-only. Dual-stack mapping differs across platforms and
    // would make the family of a received sender address ambiguous.
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

    if (const Core::Status status = setNonBlocking(impl->socket); !status) {
        closeNativeSocket(impl->socket);
        delete impl;
#if defined(_WIN32)
        WinsockScope::release();
#endif
        return Core::failure(status.error());
    }

    sockaddr_storage bindAddress{};
    const NativeAddressLength bindLength = toNativeAddress(config.localEndpoint, bindAddress);
    if (bindLength == 0) {
        closeNativeSocket(impl->socket);
        delete impl;
#if defined(_WIN32)
        WinsockScope::release();
#endif
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "UdpSocket localEndpoint family is unsupported");
    }

    if (::bind(impl->socket, reinterpret_cast<const sockaddr*>(&bindAddress), bindLength) != 0) {
        closeNativeSocket(impl->socket);
        delete impl;
#if defined(_WIN32)
        WinsockScope::release();
#endif
        return Core::failure(
            NetworkErrorCode::AddressUnavailable,
            "Failed to bind UDP socket to the requested local endpoint");
    }

    return UdpSocket{impl};
}

Core::Result<NetworkEndpoint> UdpSocket::localEndpoint() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            NetworkErrorCode::SocketClosed,
            "UdpSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "UdpSocket must be used from its owner thread");
    }

    sockaddr_storage address{};
    auto length = static_cast<NativeAddressLength>(sizeof(address));
    if (::getsockname(m_impl->socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to read the bound local endpoint");
    }

    NetworkEndpoint endpoint{};
    if (!fromNativeAddress(address, endpoint)) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Bound local endpoint has an unsupported family");
    }
    return endpoint;
}

Core::Status UdpSocket::send(
    const NetworkEndpoint& destination,
    std::span<const std::byte> payload)
{
    if (m_impl == nullptr) {
        return Core::failure(
            NetworkErrorCode::SocketClosed,
            "UdpSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "UdpSocket must be used from its owner thread");
    }
    if (payload.empty()) {
        return Core::failure(
            NetworkErrorCode::InvalidDatagram,
            "UdpSocket cannot send an empty datagram");
    }
    if (payload.size() > m_impl->maximumDatagramBytes) {
        return Core::failure(
            NetworkErrorCode::DatagramTooLarge,
            "Datagram payload exceeds the configured maximum");
    }
    if (!destination.address.hasValue() || destination.port == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "UdpSocket destination requires an address and a non-zero port");
    }
    if (destination.address.family() != m_impl->family) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "UdpSocket destination family differs from the bound socket family");
    }

    sockaddr_storage address{};
    const NativeAddressLength addressLength = toNativeAddress(destination, address);
    if (addressLength == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidEndpoint,
            "UdpSocket destination family is unsupported");
    }

#if defined(_WIN32)
    const int sent = ::sendto(
        m_impl->socket,
        reinterpret_cast<const char*>(payload.data()),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        addressLength);
#else
    const ssize_t sent = ::sendto(
        m_impl->socket,
        payload.data(),
        payload.size(),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        addressLength);
#endif

    if (sent < 0) {
        const int error = lastSocketError();
        if (isWouldBlockError(error)) {
            return Core::failure(
                NetworkErrorCode::WouldBlock,
                "UdpSocket send buffer is full; retry on a later frame");
        }
        if (isMessageSizeError(error)) {
            return Core::failure(
                NetworkErrorCode::DatagramTooLarge,
                "Datagram rejected by the transport as too large");
        }
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "UdpSocket send failed");
    }

    // A short send would mean a partially transmitted datagram, which UDP has no
    // way to express -- treat it as a backend fault rather than success.
    if (static_cast<Core::usize>(sent) != payload.size()) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "UdpSocket sent a partial datagram");
    }

    ++m_impl->stats.totalSentDatagramCount;
    m_impl->stats.totalSentBytes += payload.size();
    return Core::success();
}

Core::Result<std::span<const ReceivedDatagram>> UdpSocket::receive()
{
    if (m_impl == nullptr) {
        return Core::failure(
            NetworkErrorCode::SocketClosed,
            "UdpSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "UdpSocket must be used from its owner thread");
    }

    ++m_impl->stats.receiveCallCount;
    m_impl->stats.lastReceivedDatagramCount = 0;
    m_impl->stats.lastDiscardedDatagramCount = 0;

    // Slots are one byte larger than the advertised maximum so an oversized
    // datagram is detectable rather than silently truncated.
    const Core::usize slotBytes = m_impl->maximumDatagramBytes + 1;

    // A discarded datagram does not consume a slot, so without a separate budget
    // a peer streaming unusable datagrams could keep this loop syscalling for as
    // long as it keeps sending. Bound the work, not just the output.
    const Core::usize syscallBudget = m_impl->receiveQueueCapacity * 2;

    Core::usize count = 0;
    Core::usize syscalls = 0;
    while (count < m_impl->receiveQueueCapacity && syscalls < syscallBudget) {
        std::byte* slot = m_impl->storage.data() + (count * slotBytes);
        sockaddr_storage address{};
        auto addressLength = static_cast<NativeAddressLength>(sizeof(address));

        ++syscalls;

#if defined(_WIN32)
        const int received = ::recvfrom(
            m_impl->socket,
            reinterpret_cast<char*>(slot),
            static_cast<int>(slotBytes),
            0,
            reinterpret_cast<sockaddr*>(&address),
            &addressLength);
#else
        const ssize_t received = ::recvfrom(
            m_impl->socket,
            slot,
            slotBytes,
            0,
            reinterpret_cast<sockaddr*>(&address),
            &addressLength);
#endif

        if (received < 0) {
            const int error = lastSocketError();
            if (isWouldBlockError(error)) {
                break;
            }
            if (isMessageSizeError(error)) {
                // Windows reports an oversized datagram here and has already
                // dropped it from the queue. Count it and keep draining.
                ++m_impl->stats.lastDiscardedDatagramCount;
                ++m_impl->stats.totalDiscardedDatagramCount;
                ++m_impl->stats.totalOversizedDatagramCount;
                continue;
            }
            // A hard failure mid-drain still reports what was already collected;
            // discarding it would lose datagrams the caller cannot re-read.
            if (count == 0) {
                return Core::failure(
                    NetworkErrorCode::BackendFailure,
                    "UdpSocket receive failed");
            }
            break;
        }

        const auto receivedBytes = static_cast<Core::usize>(received);

        // Reaching into the spare byte proves the datagram exceeded the advertised
        // maximum. POSIX would have truncated it; handing a possibly-truncated
        // payload over as complete is worse than discarding it.
        if (receivedBytes > m_impl->maximumDatagramBytes) {
            ++m_impl->stats.lastDiscardedDatagramCount;
            ++m_impl->stats.totalDiscardedDatagramCount;
            ++m_impl->stats.totalOversizedDatagramCount;
            continue;
        }

        // A zero-length datagram is legal on the wire but carries nothing, and
        // send() refuses to produce one. Discarding it keeps an empty payload from
        // ever reaching a caller that would have to special-case it.
        if (receivedBytes == 0) {
            ++m_impl->stats.lastDiscardedDatagramCount;
            ++m_impl->stats.totalDiscardedDatagramCount;
            continue;
        }

        NetworkEndpoint sender{};
        if (!fromNativeAddress(address, sender)) {
            ++m_impl->stats.lastDiscardedDatagramCount;
            ++m_impl->stats.totalDiscardedDatagramCount;
            continue;
        }

        m_impl->datagrams[count] = ReceivedDatagram{
            .sender = sender,
            .payload = std::span<const std::byte>{slot, receivedBytes}};
        ++count;

        ++m_impl->stats.totalReceivedDatagramCount;
        m_impl->stats.totalReceivedBytes += receivedBytes;
    }

    m_impl->stats.lastReceivedDatagramCount = count;

    return std::span<const ReceivedDatagram>{m_impl->datagrams.data(), count};
}

UdpSocketStatistics UdpSocket::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

Core::usize UdpSocket::receiveQueueCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->receiveQueueCapacity : 0;
}

Core::usize UdpSocket::maximumDatagramBytes() const noexcept
{
    return m_impl != nullptr ? m_impl->maximumDatagramBytes : 0;
}

} // namespace Tina::Network
