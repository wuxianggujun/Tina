#include "NativeSocket.hpp"

#include <tina/network/NetworkErrors.hpp>

#include <array>
#include <cstring>
#include <mutex>

namespace Tina::Network::Detail {
namespace {

Core::usize g_transportRefCount = 0;

[[nodiscard]] std::mutex& transportMutex() noexcept
{
    static std::mutex instance;
    return instance;
}

} // namespace

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

int lastSocketError() noexcept
{
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

bool isWouldBlockError(int error) noexcept
{
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

bool isMessageSizeError(int error) noexcept
{
#if defined(_WIN32)
    return error == WSAEMSGSIZE;
#else
    return error == EMSGSIZE;
#endif
}

bool isConnectInProgressError(int error) noexcept
{
#if defined(_WIN32)
    // Winsock reports a non-blocking connect as WSAEWOULDBLOCK rather than the
    // POSIX EINPROGRESS, so both spellings map here.
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
    return error == EINPROGRESS || error == EALREADY;
#endif
}

bool isConnectionResetError(int error) noexcept
{
#if defined(_WIN32)
    return error == WSAECONNRESET || error == WSAECONNREFUSED || error == WSAECONNABORTED
        || error == WSAENETRESET;
#else
    return error == ECONNRESET || error == ECONNREFUSED || error == ECONNABORTED
        || error == EPIPE;
#endif
}

bool isInterruptedError(int error) noexcept
{
#if defined(_WIN32)
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

Core::Status setNativeSocketNonBlocking(NativeSocket socket) noexcept
{
#if defined(_WIN32)
    u_long mode = 1;
    if (::ioctlsocket(socket, FIONBIO, &mode) != 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to place socket in non-blocking mode");
    }
    return Core::success();
#else
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        return Core::failure(
            NetworkErrorCode::BackendFailure,
            "Failed to place socket in non-blocking mode");
    }
    return Core::success();
#endif
}

int takePendingSocketError(NativeSocket socket) noexcept
{
    int pending = 0;
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(pending));
    if (::getsockopt(
            socket,
            SOL_SOCKET,
            SO_ERROR,
            reinterpret_cast<char*>(&pending),
            &length)
        != 0) {
        // The query itself failing is a fault in its own right; report it rather
        // than claiming the socket is healthy.
        return lastSocketError();
    }
#else
    socklen_t length = sizeof(pending);
    if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, &pending, &length) != 0) {
        return lastSocketError();
    }
#endif
    return pending;
}

void shutdownNativeSocketSend(NativeSocket socket) noexcept
{
    if (socket == InvalidNativeSocket) {
        return;
    }
#if defined(_WIN32)
    ::shutdown(socket, SD_SEND);
#else
    ::shutdown(socket, SHUT_WR);
#endif
}

void requestNativeSocketBufferSizes(
    NativeSocket socket,
    int sendBytes,
    int receiveBytes) noexcept
{
    if (socket == InvalidNativeSocket) {
        return;
    }
#if defined(_WIN32)
    if (sendBytes > 0) {
        ::setsockopt(
            socket,
            SOL_SOCKET,
            SO_SNDBUF,
            reinterpret_cast<const char*>(&sendBytes),
            static_cast<int>(sizeof(sendBytes)));
    }
    if (receiveBytes > 0) {
        ::setsockopt(
            socket,
            SOL_SOCKET,
            SO_RCVBUF,
            reinterpret_cast<const char*>(&receiveBytes),
            static_cast<int>(sizeof(receiveBytes)));
    }
#else
    if (sendBytes > 0) {
        ::setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &sendBytes, sizeof(sendBytes));
    }
    if (receiveBytes > 0) {
        ::setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &receiveBytes, sizeof(receiveBytes));
    }
#endif
}

NativeAddressLength toNativeAddress(
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

bool fromNativeAddress(const sockaddr_storage& source, NetworkEndpoint& out) noexcept
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

Core::Status TransportScope::acquire() noexcept
{
    const std::lock_guard<std::mutex> guard{transportMutex()};
#if defined(_WIN32)
    if (g_transportRefCount == 0) {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return Core::failure(
                NetworkErrorCode::BackendFailure,
                "WSAStartup failed");
        }
    }
#endif
    ++g_transportRefCount;
    return Core::success();
}

void TransportScope::release() noexcept
{
    const std::lock_guard<std::mutex> guard{transportMutex()};
    if (g_transportRefCount == 0) {
        return;
    }
    --g_transportRefCount;
#if defined(_WIN32)
    if (g_transportRefCount == 0) {
        ::WSACleanup();
    }
#endif
}

} // namespace Tina::Network::Detail
