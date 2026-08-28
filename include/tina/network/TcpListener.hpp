#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/NetworkEndpoint.hpp>
#include <tina/network/TcpConnection.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Network {

struct TcpListenerConfig final {
    // Address and port to bind. Port 0 asks the OS for an ephemeral port, readable
    // afterwards through localEndpoint().
    NetworkEndpoint localEndpoint{};

    // Connections that may be accepted and held at once. Reaching it does not
    // refuse peers at the TCP level -- the OS backlog still holds them -- it stops
    // this type from handing over more until one is taken.
    Core::usize connectionCapacity = 16;

    // Pending-connection depth requested from the OS. Distinct from
    // connectionCapacity: this bounds what the kernel queues before accept, that
    // bounds what this object holds after.
    Core::u32 backlog = 16;

    // Applied to every accepted connection, since a listener has no way to know
    // what a caller will do with one.
    Core::usize connectionSendBufferBytes = 64 * 1024;
    Core::usize connectionReceiveBufferBytes = 64 * 1024;

    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TcpListenerStatistics final {
    Core::usize readyConnectionCount = 0;

    Core::u64 pumpCallCount = 0;
    Core::u64 acceptedConnectionCount = 0;
    // Peers dropped because connectionCapacity was full when they arrived. Counted
    // apart from accepted ones because it means the caller is not taking
    // connections fast enough, not that anything failed.
    Core::u64 rejectedConnectionCount = 0;
    Core::u64 acceptFailureCount = 0;
};

// Non-blocking TCP listener. Every method must be called from the thread that
// called Create.
//
// No worker thread and no callback: pump() accepts whatever the OS already has
// queued, so connections appear only where the caller asked for them. That is the
// same discipline the client transports use, which is what lets a server and its
// connections share one pump loop.
class TcpListener final {
  public:
    [[nodiscard]] static Core::Result<TcpListener> Create(TcpListenerConfig config);

    ~TcpListener() noexcept;

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    // Bound local address, with the resolved port when config requested 0.
    [[nodiscard]] Core::Result<NetworkEndpoint> localEndpoint() const noexcept;

    // Accepts pending connections into internal storage. Never blocks. Returns how
    // many became available during this call.
    //
    // Stops at connectionCapacity; the rest stay in the OS backlog for a later
    // pump, so a burst is deferred rather than dropped.
    [[nodiscard]] Core::Result<Core::usize> pump();

    // Number of accepted connections waiting to be taken.
    [[nodiscard]] Core::usize readyConnectionCount() const noexcept;

    // Hands over the oldest accepted connection. The caller owns it from here and
    // must pump it itself. Fails when none is ready.
    [[nodiscard]] Core::Result<TcpConnection> acceptNext();

    // Drops every accepted-but-unclaimed connection. Peers see a reset, so this is
    // for shutdown rather than routine backpressure.
    void dropPendingConnections() noexcept;

    [[nodiscard]] TcpListenerStatistics statistics() const noexcept;

    [[nodiscard]] Core::usize connectionCapacity() const noexcept;

  private:
    struct Impl;

    explicit TcpListener(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
