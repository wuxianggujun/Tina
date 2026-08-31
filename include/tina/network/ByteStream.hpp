#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>

namespace Tina::Network {

// Where a stream is in its lifecycle, independent of which transport implements
// it. TCP and TLS have the same shape here; a TLS handshake is one more reason to
// be Connecting rather than a separate state a protocol needs to know about.
enum class ByteStreamState : Core::u8 {
    // Transport, and where applicable the security handshake, still in flight.
    Connecting,
    Connected,
    // The peer ended its side of the stream. Buffered bytes stay readable.
    PeerClosed,
    Closed,
    Failed,
};

// A caller-pumped, ordered, reliable byte stream.
//
// This is the seam that lets a protocol run over plain TCP or over TLS without
// knowing which. It exists because Tina::Network cannot depend on the optional
// TLS adapter -- that would invert the dependency and make an optional module
// mandatory -- so the protocol takes a stream it is handed instead of building
// one.
//
// Every method must be called from the thread that created the implementation.
// There is no worker thread: nothing moves unless pump() is called.
class IByteStream {
  public:
    virtual ~IByteStream() = default;

    [[nodiscard]] virtual ByteStreamState streamState() const noexcept = 0;

    // Queues bytes for sending. Implementations copy into a fixed buffer and
    // refuse a payload they cannot take whole, so a caller never has to track a
    // partial acceptance it did not ask for.
    [[nodiscard]] virtual Core::Status sendBytes(std::span<const std::byte> payload) = 0;

    // Bytes accepted by sendBytes() but not yet handed to the platform socket.
    // Zero proves local handoff only, not peer delivery or acknowledgement. This
    // lets a protocol keep pumping an orderly close without depending on a
    // transport-specific statistics type.
    [[nodiscard]] virtual Core::usize pendingSendBytes() const noexcept = 0;

    // Advances the stream. Never blocks. Returns the number of bytes newly
    // available to peekReceived(); zero is the normal idle answer.
    [[nodiscard]] virtual Core::Result<Core::usize> pumpStream() = 0;

    // Bytes received and not yet consumed. The span borrows implementation storage
    // and is invalidated by the next pumpStream() or consumeReceived().
    [[nodiscard]] virtual Core::Result<std::span<const std::byte>> peekReceived() = 0;

    // Discards leading bytes. A protocol parser finishes one frame and leaves a
    // partial one, so this must accept a prefix rather than all-or-nothing.
    [[nodiscard]] virtual Core::Status consumeReceived(Core::usize byteCount) = 0;

    virtual void closeStream() noexcept = 0;
};

} // namespace Tina::Network
