#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/ByteStream.hpp>
#include <tina/network/NetworkEndpoint.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Network {

// Where a connection is in its lifecycle. Monotonic apart from Connecting ->
// Connected: once Closed or Failed, a connection never becomes usable again.
enum class TcpConnectionState : Core::u8 {
    // Handshake in flight. connect() on a non-blocking socket reports "in
    // progress" immediately, so this state is the normal starting point.
    Connecting,
    Connected,
    // The peer closed its sending half. Reads are drained but no more will
    // arrive; this side may still send until close().
    PeerClosed,
    Closed,
    Failed,
};

struct TcpConnectionConfig final {
    NetworkEndpoint remoteEndpoint{};

    // Bytes buffered for sending. send() copies into this and pump() drains it,
    // because a non-blocking TCP send routinely accepts only part of a payload.
    Core::usize sendBufferBytes = 64 * 1024;

    // Bytes held for the caller between pump() and receive().
    Core::usize receiveBufferBytes = 64 * 1024;

    // Requested kernel socket buffer sizes. Zero leaves the platform default,
    // which is the right choice for throughput. Setting them bounds how much
    // unacknowledged data the OS holds per connection, at the cost of more pumps
    // to move the same payload. The OS may clamp or ignore the request.
    Core::usize kernelSendBufferBytes = 0;
    Core::usize kernelReceiveBufferBytes = 0;

    // Borrowed when non-null and must outlive the connection.
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TcpConnectionStatistics final {
    Core::usize queuedSendBytes = 0;
    Core::usize bufferedReceiveBytes = 0;

    Core::u64 pumpCallCount = 0;
    Core::u64 totalSentBytes = 0;
    Core::u64 totalReceivedBytes = 0;

    // Times pump() moved only part of the queued bytes. Not a failure -- it is
    // how TCP behaves under backpressure -- but a persistently rising count
    // means the peer is slower than the producer.
    Core::u64 partialSendCount = 0;
};

// Non-blocking TCP client connection. Every method must be called from the thread
// that called Create.
//
// There is no worker thread. pump() performs one non-blocking readiness check and
// advances the state machine, so the caller decides when bytes move. A connection
// makes no progress unless pump() is called.
class TcpConnection final : public IByteStream {
  public:
    // Starts a connection. Returns immediately in Connecting state; the handshake
    // completes during a later pump(). A refused or unreachable peer is therefore
    // reported by pump()/state(), not here.
    [[nodiscard]] static Core::Result<TcpConnection> Create(TcpConnectionConfig config);

    ~TcpConnection() noexcept;

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    [[nodiscard]] TcpConnectionState state() const noexcept;

    // Available once the handshake completes; fails while still Connecting,
    // because the OS has not assigned a local port yet.
    [[nodiscard]] Core::Result<NetworkEndpoint> localEndpoint() const noexcept;
    [[nodiscard]] Core::Result<NetworkEndpoint> remoteEndpoint() const noexcept;

    // Copies the payload into the send buffer. Does not touch the socket: bytes
    // leave during pump(). Returns CapacityExceeded when the buffer cannot hold
    // the whole payload -- partial acceptance would force the caller to track a
    // split it never asked for.
    [[nodiscard]] Core::Status send(std::span<const std::byte> payload);

    // Advances the state machine: completes the handshake, drains the send buffer,
    // and fills the receive buffer. Never blocks.
    //
    // Returns the number of bytes newly available to receive(). A return of zero
    // is the normal idle answer.
    [[nodiscard]] Core::Result<Core::usize> pump();

    // Hands over buffered bytes. The span borrows connection storage and stays
    // valid until the next pump(), receive(), or destruction.
    [[nodiscard]] Core::Result<std::span<const std::byte>> receive();

    // Discards the given number of leading bytes from the receive buffer. A
    // protocol parser consumes a complete frame and leaves a partial one, so the
    // buffer must not be all-or-nothing.
    [[nodiscard]] Core::Status consume(Core::usize byteCount);

    // Closes the sending half. The peer sees end-of-stream while this side keeps
    // reading whatever is still in flight. Queued unsent bytes are dropped.
    [[nodiscard]] Core::Status shutdownSend();

    // Closes both halves immediately and moves to Closed. Idempotent. Bytes
    // already handed to the OS may still arrive at the peer -- this cannot unsend
    // them.
    void close() noexcept;

    [[nodiscard]] TcpConnectionStatistics statistics() const noexcept;

    [[nodiscard]] Core::usize sendBufferCapacity() const noexcept;
    [[nodiscard]] Core::usize receiveBufferCapacity() const noexcept;

    // IByteStream. Thin forwards to the concrete API above, so a protocol can run
    // over this or over TLS without knowing which.
    [[nodiscard]] ByteStreamState streamState() const noexcept override;
    [[nodiscard]] Core::Status sendBytes(std::span<const std::byte> payload) override;
    [[nodiscard]] Core::usize pendingSendBytes() const noexcept override;
    [[nodiscard]] Core::Result<Core::usize> pumpStream() override;
    [[nodiscard]] Core::Result<std::span<const std::byte>> peekReceived() override;
    [[nodiscard]] Core::Status consumeReceived(Core::usize byteCount) override;
    void closeStream() noexcept override;

  private:
    struct Impl;

    explicit TcpConnection(Impl* impl) noexcept;

    // Adopts a socket that is already connected, which is what accept() returns.
    // Private because a caller has no way to obtain a raw socket through the public
    // surface: TcpListener is the only producer, and it is a friend rather than a
    // second Create overload so no platform type reaches this header.
    friend class TcpListener;
    [[nodiscard]] static Core::Result<TcpConnection> adoptAcceptedSocket(
        void* nativeSocket,
        const NetworkEndpoint& remoteEndpoint,
        const TcpConnectionConfig& config);

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
