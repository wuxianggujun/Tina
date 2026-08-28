#pragma once

// Platform socket primitives. This header is private to src/network: it names
// Winsock/BSD types, which must never reach include/tina.

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/NetworkEndpoint.hpp>

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
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Tina::Network::Detail {

#if defined(_WIN32)
using NativeSocket = SOCKET;
inline constexpr NativeSocket InvalidNativeSocket = INVALID_SOCKET;
using NativeAddressLength = int;
#else
using NativeSocket = int;
inline constexpr NativeSocket InvalidNativeSocket = -1;
using NativeAddressLength = socklen_t;
#endif

void closeNativeSocket(NativeSocket socket) noexcept;

[[nodiscard]] int lastSocketError() noexcept;

// The operation cannot complete right now and should be retried later. On a
// non-blocking socket this is the normal "nothing to do" answer, not a failure.
[[nodiscard]] bool isWouldBlockError(int error) noexcept;

// A datagram exceeded the receive buffer. Windows reports this on the call;
// POSIX truncates silently and only sets MSG_TRUNC.
[[nodiscard]] bool isMessageSizeError(int error) noexcept;

// connect() on a non-blocking socket almost always reports this first; it means
// the handshake started, not that it failed.
[[nodiscard]] bool isConnectInProgressError(int error) noexcept;

// The peer reset or refused the connection.
[[nodiscard]] bool isConnectionResetError(int error) noexcept;

// A signal interrupted the call before it did anything. Retrying is correct and
// must not be reported as an error.
[[nodiscard]] bool isInterruptedError(int error) noexcept;

[[nodiscard]] Core::Status setNativeSocketNonBlocking(NativeSocket socket) noexcept;

// Reads and clears the pending socket error. This is the only portable way to
// learn whether a non-blocking connect succeeded: poll reports the socket
// writable either way, so the outcome lives here rather than in the poll flags.
//
// Returns 0 when the socket has no pending error.
[[nodiscard]] int takePendingSocketError(NativeSocket socket) noexcept;

// Closes the sending half, so the peer observes end-of-stream while this side can
// still read what is already in flight.
void shutdownNativeSocketSend(NativeSocket socket) noexcept;

// Requests kernel send/receive buffer sizes. Exists so tests can shrink them to
// force backpressure deterministically; production paths leave the defaults
// alone. The OS is free to clamp or ignore the request, so callers must not
// assume the exact value took effect.
void requestNativeSocketBufferSizes(
    NativeSocket socket,
    int sendBytes,
    int receiveBytes) noexcept;

// Fills a sockaddr from a Tina endpoint. Returns 0 for an unsupported family.
[[nodiscard]] NativeAddressLength toNativeAddress(
    const NetworkEndpoint& endpoint,
    sockaddr_storage& out) noexcept;

[[nodiscard]] bool fromNativeAddress(
    const sockaddr_storage& source,
    NetworkEndpoint& out) noexcept;

// Winsock needs process-wide init, refcounted so repeated create/destroy across
// a run cannot tear the library down under a live socket.
//
// Both entry points share one mutex on purpose: giving each its own
// function-local static would leave the refcount unsynchronised between them.
class TransportScope final {
  public:
    [[nodiscard]] static Core::Status acquire() noexcept;
    static void release() noexcept;
};

} // namespace Tina::Network::Detail
