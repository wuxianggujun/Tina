#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/ByteStream.hpp>

#include <memory_resource>
#include <string>
#include <span>
#include <string_view>

namespace Tina::Network {

// base64(sha1(clientKey + RFC 6455 GUID)) -- the value a server must echo in
// Sec-WebSocket-Accept, and the value this client verifies.
//
// Exposed because a server needs exactly this and nothing else. It is not a
// general hashing utility: the input is a public nonce plus a published constant,
// and SHA-1 stays private to the module so it cannot be reached for hashing that
// matters. Core::ContentHash is for that.
[[nodiscard]] std::string webSocketAcceptToken(std::string_view clientKey);

enum class WebSocketState : Core::u8 {
    // HTTP upgrade request sent; waiting for the 101 response.
    Handshaking,
    Open,
    // Close frame exchanged in at least one direction. Buffered messages stay
    // readable. A peer close enters Closing first so the mandatory echo can be
    // handed to the transport, then transitions to Closed.
    Closing,
    Closed,
    Failed,
};

// Which payload kind a message carries. RFC 6455 keeps these distinct because a
// text frame must be valid UTF-8 and a binary one must not be interpreted.
enum class WebSocketMessageKind : Core::u8 {
    Text,
    Binary,
};

struct WebSocketMessage final {
    WebSocketMessageKind kind = WebSocketMessageKind::Text;
    // Borrows client storage; valid until the next pump() or consumeMessage().
    std::span<const std::byte> payload{};
};

struct WebSocketConfig final {
    // Borrowed, and must outlive the socket. Supplying the stream is what lets wss
    // work without Tina::Network depending on the optional TLS adapter. A
    // protocol failure closes the stream but never destroys its owner.
    IByteStream* stream = nullptr;

    // Origin-form target for the upgrade request, e.g. "/socket".
    std::string_view target{};
    std::string_view host{};

    // Caps one reassembled message. A message that would exceed it fails the
    // connection rather than being truncated, since a truncated frame is
    // indistinguishable from a complete one.
    Core::usize maximumMessageBytes = 1 * 1024 * 1024;

    // Caps bytes held for frames that have arrived but not yet been parsed.
    Core::usize receiveBufferBytes = 256 * 1024;

    // Caps how many pumps may pass with no progress. Zero disables it. Counted in
    // pumps rather than wall time because this type owns no clock.
    Core::u32 stallPumpLimit = 6000;

    std::pmr::memory_resource* memoryResource = nullptr;
};

struct WebSocketStatistics final {
    Core::u64 pumpCallCount = 0;
    Core::u64 sentFrameCount = 0;
    Core::u64 receivedFrameCount = 0;
    Core::u64 sentPayloadBytes = 0;
    Core::u64 receivedPayloadBytes = 0;
    // Continuation frames reassembled into a message. A protocol that never
    // fragments leaves this at zero.
    Core::u64 reassembledFragmentCount = 0;
    Core::u64 pongCount = 0;
    bool handshakeComplete = false;
    // Close code the peer sent, or zero if it has not closed.
    Core::u16 peerCloseCode = 0;
};

// Non-blocking RFC 6455 client over a caller-supplied byte stream.
//
// No worker thread: pump() advances the handshake, the frame parser, and the send
// queue, so nothing moves unless it is called. Every method must be called from
// the thread that created the stream.
//
// Client-to-server frames are always masked, as RFC 6455 requires; a server must
// close the connection on an unmasked client frame, so this is not optional.
class WebSocket final {
  public:
    [[nodiscard]] static Core::Result<WebSocket> Create(WebSocketConfig config);

    ~WebSocket() noexcept;

    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;
    WebSocket(WebSocket&& other) noexcept;
    WebSocket& operator=(WebSocket&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    [[nodiscard]] WebSocketState state() const noexcept;

    // Queues a message. Fragmentation is not performed: a payload larger than the
    // send buffer is refused rather than split, because a caller that cares about
    // framing should choose its own boundaries.
    [[nodiscard]] Core::Status sendText(std::string_view text);
    [[nodiscard]] Core::Status sendBinary(std::span<const std::byte> payload);

    // Advances the connection. Never blocks. Returns true when a complete message
    // is available from message().
    [[nodiscard]] Core::Result<bool> pump();

    // The reassembled message, valid only when the last pump() returned true.
    [[nodiscard]] Core::Result<WebSocketMessage> message() const noexcept;

    // Releases the current message so the next pump() can deliver another.
    [[nodiscard]] Core::Status consumeMessage();

    // Sends a close frame with the given code. The peer is expected to echo it;
    // pump() then moves to Closed. 1000 means a normal closure.
    [[nodiscard]] Core::Status close(Core::u16 code = 1000, std::string_view reason = {});

    [[nodiscard]] WebSocketStatistics statistics() const noexcept;

  private:
    struct Impl;

    explicit WebSocket(Impl* impl) noexcept;

    // Drops the parsed prefix of the frame buffer and compacts the remainder.
    void eraseConsumed(Core::usize byteCount) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
