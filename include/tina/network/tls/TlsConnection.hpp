#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/ByteStream.hpp>
#include <tina/network/NetworkEndpoint.hpp>

#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::Network {

// TLS over TCP. Shares the TcpConnection lifecycle plus a handshake stage, which
// is why the states are named the same where they mean the same thing.
enum class TlsConnectionState : Core::u8 {
    // TCP handshake in flight.
    ConnectingTransport,
    // TCP is up; the TLS handshake is exchanging records.
    HandshakingTls,
    Connected,
    // The peer sent close_notify. Buffered plaintext stays readable.
    PeerClosed,
    Closed,
    Failed,
};

// Whether the platform trust store could be read, and what it yielded. Useful
// because an empty store is not a failure of any one connection -- it means this
// build cannot verify a public endpoint until the caller supplies anchors.
struct TlsTrustStoreInfo final {
    Core::usize certificateCount = 0;
    // Entries the platform offered that would not parse. Counted rather than
    // fatal: one malformed enterprise root must not disable every other anchor.
    Core::usize skippedEntryCount = 0;
    bool available = false;
};

// Reads the platform trust anchors once per process and reports what was found.
// Safe to call before any connection exists, which is the point: a caller can
// decide at startup whether it needs to ship its own anchors.
//
// What this does NOT do is delegate the verification verdict to the OS. It
// extracts a flat anchor list and mbedTLS decides, so platform distrust records,
// EKU constraints, CTL status and revocation are not consulted. Every engine
// surveyed (Godot, UE5 on non-Apple, Unity) has the same limitation; only UE5 on
// Apple and rustls-platform-verifier hand the verdict over. A concrete
// consequence: macOS still lists StartCom anchors that Apple blocks by date, and
// a flat snapshot cannot express that.
[[nodiscard]] TlsTrustStoreInfo tlsTrustStoreInfo();

// How the server certificate is checked. There is deliberately no "skip
// verification" value: a transport that silently accepts any certificate provides
// encryption without authentication, which is worse than a visible failure
// because it looks like it works.
enum class TlsVerificationMode : Core::u8 {
    // Full chain and hostname verification against the supplied trust anchors.
    // The only mode acceptable outside a test.
    Required,
    // Accepts any certificate. Rejected unless allowInsecureVerification is also
    // set, and unavailable in a release build.
    InsecureSkipVerify,
};

struct TlsConnectionConfig final {
    NetworkEndpoint remoteEndpoint{};

    // Name presented in SNI and matched against the certificate. Required when
    // verification is Required: an IP literal cannot be matched against a
    // certificate's DNS names, so omitting it would make verification vacuous.
    std::string_view serverName{};

    // PEM-encoded trust anchors. Leave empty to use the platform trust store,
    // which is what a client talking to a public endpoint wants.
    //
    // Supplying anchors replaces the platform set rather than adding to it, so a
    // caller pinning a private CA does not silently keep trusting every public
    // one. This matches Godot's precedence: an explicit bundle wins outright.
    //
    // No CA bundle is shipped with the engine. A bundle in-tree is a liability
    // that expires -- Godot keeps one only as a fallback for platforms with no
    // readable store, and that fallback is exactly what goes stale.
    std::string_view trustAnchorsPem{};

    TlsVerificationMode verification = TlsVerificationMode::Required;

    // Must be true for InsecureSkipVerify to be honoured. Two independent opt-ins
    // so a single mistyped field cannot disable authentication.
    bool allowInsecureVerification = false;

    Core::usize sendBufferBytes = 64 * 1024;
    Core::usize receiveBufferBytes = 64 * 1024;

    Core::usize kernelSendBufferBytes = 0;
    Core::usize kernelReceiveBufferBytes = 0;

    // Borrowed when non-null and must outlive the connection.
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TlsConnectionStatistics final {
    Core::usize queuedSendBytes = 0;
    Core::usize bufferedReceiveBytes = 0;

    Core::u64 pumpCallCount = 0;
    // Plaintext, so these do not match the byte counts on the wire.
    Core::u64 totalSentBytes = 0;
    Core::u64 totalReceivedBytes = 0;

    Core::u64 handshakePumpCount = 0;
    bool handshakeComplete = false;
};

// Non-blocking TLS client. Every method must be called from the thread that
// called Create.
//
// No worker thread and no callback: the TLS library is driven entirely by
// caller-supplied transport callbacks, so records move only during pump(). The
// handshake therefore spans several pumps rather than blocking one.
class TlsConnection final : public IByteStream {
  public:
    // Starts the TCP connection and prepares the TLS session. Returns immediately
    // in ConnectingTransport. A refused peer, a failed handshake, or a rejected
    // certificate are reported by pump(), not here.
    [[nodiscard]] static Core::Result<TlsConnection> Create(TlsConnectionConfig config);

    ~TlsConnection() noexcept;

    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;
    TlsConnection(TlsConnection&& other) noexcept;
    TlsConnection& operator=(TlsConnection&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    [[nodiscard]] TlsConnectionState state() const noexcept;

    [[nodiscard]] Core::Result<NetworkEndpoint> localEndpoint() const noexcept;
    [[nodiscard]] Core::Result<NetworkEndpoint> remoteEndpoint() const noexcept;

    // Queues plaintext. Encryption happens during pump(). Returns
    // CapacityExceeded when the buffer cannot hold the whole payload.
    [[nodiscard]] Core::Status send(std::span<const std::byte> payload);

    // Advances the transport, the handshake, and record processing. Never blocks.
    // Returns the number of plaintext bytes newly available to receive().
    [[nodiscard]] Core::Result<Core::usize> pump();

    // Buffered plaintext. The span borrows connection storage and is invalidated
    // by the next pump(), receive(), or destruction.
    [[nodiscard]] Core::Result<std::span<const std::byte>> receive();

    [[nodiscard]] Core::Status consume(Core::usize byteCount);

    // Sends close_notify so the peer can distinguish an orderly shutdown from a
    // truncated stream. Best effort: it needs a working transport.
    [[nodiscard]] Core::Status shutdownTls();

    void close() noexcept;

    [[nodiscard]] TlsConnectionStatistics statistics() const noexcept;

    [[nodiscard]] Core::usize sendBufferCapacity() const noexcept;
    [[nodiscard]] Core::usize receiveBufferCapacity() const noexcept;

    // IByteStream. Both TLS handshake states map to Connecting: a protocol above
    // only needs to know the stream is not yet usable, not why.
    [[nodiscard]] ByteStreamState streamState() const noexcept override;
    [[nodiscard]] Core::Status sendBytes(std::span<const std::byte> payload) override;
    [[nodiscard]] Core::Result<Core::usize> pumpStream() override;
    [[nodiscard]] Core::Result<std::span<const std::byte>> peekReceived() override;
    [[nodiscard]] Core::Status consumeReceived(Core::usize byteCount) override;
    void closeStream() noexcept override;

  private:
    struct Impl;

    explicit TlsConnection(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
