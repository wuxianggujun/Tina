#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/network/ByteStream.hpp>
#include <tina/network/NetworkEndpoint.hpp>

#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::Network {

enum class HttpMethod : Core::u8 {
    Get,
    Head,
    Post,
    Put,
    Delete,
};

enum class HttpRequestState : Core::u8 {
    // Transport handshake in flight.
    Connecting,
    SendingRequest,
    // Request sent; reading status line and headers.
    ReceivingHeaders,
    ReceivingBody,
    Complete,
    Failed,
};

// One response header. Both views borrow client storage and stay valid until the
// next pump() or destruction.
struct HttpHeader final {
    std::string_view name{};
    std::string_view value{};
};

struct HttpResponse final {
    Core::u16 statusCode = 0;
    std::string_view reasonPhrase{};
    std::span<const HttpHeader> headers{};
    std::span<const std::byte> body{};

    // 2xx only. A 404 is a successfully completed request that failed at the
    // application level, so it is not an error of the client.
    [[nodiscard]] constexpr bool isSuccess() const noexcept
    {
        return statusCode >= 200 && statusCode < 300;
    }

    [[nodiscard]] std::string_view header(std::string_view name) const noexcept;
};

struct HttpRequestConfig final {
    // Borrowed, and must outlive the request. Supplying the stream rather than an
    // endpoint is what makes HTTPS possible without Tina::Network depending on the
    // optional TLS adapter: hand it a TlsConnection and the same parser runs over
    // an encrypted stream.
    //
    // The stream may still be Connecting; pump() waits for it. A failed exchange
    // closes the stream but never destroys its owner.
    IByteStream* stream = nullptr;

    HttpMethod method = HttpMethod::Get;

    // Origin-form target, e.g. "/index.html". Must begin with '/'.
    std::string_view target{};

    // Sent verbatim as the Host header, which a 1.1 server requires. Also used
    // for SNI and certificate matching when the transport is TLS.
    std::string_view host{};

    std::span<const std::byte> body{};
    std::string_view contentType{};

    // Caps the decoded body. A response that would exceed it fails rather than
    // being truncated, because a truncated body is indistinguishable from a
    // complete one to a parser.
    Core::usize maximumBodyBytes = 4 * 1024 * 1024;

    // Caps the status line plus all headers.
    Core::usize maximumHeaderBytes = 32 * 1024;

    Core::usize maximumHeaderCount = 64;

    // Caps how many pumps may pass without the exchange advancing. Zero disables
    // it.
    //
    // Counted in pumps rather than wall time because this type owns no clock: the
    // caller decides the pump cadence, so it already knows what a stall costs in
    // seconds. It also makes the limit reproducible in a test.
    Core::u32 stallPumpLimit = 6000;

    // Borrowed when non-null and must outlive the request.
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct HttpRequestStatistics final {
    Core::u64 pumpCallCount = 0;
    Core::u64 sentBytes = 0;
    Core::u64 receivedBytes = 0;
    Core::usize headerCount = 0;
    Core::usize bodyBytes = 0;
    bool chunkedTransferEncoding = false;
};

// Non-blocking HTTP/1.1 client for a single request over a caller-supplied stream.
// Every method must be called from the thread that created the stream.
//
// One request per object rather than a reusable client: a queue inside the client
// would need a policy for ordering, cancellation and head-of-line blocking that
// the caller is better placed to choose.
//
// The request borrows the stream and never owns it, so the same parser serves
// http:// over TcpConnection and https:// over TlsConnection. That also keeps the
// choice of transport -- and therefore whether verification happened -- visible at
// the call site rather than hidden behind a scheme string.
//
// No worker thread. pump() advances the stream and the parser, so a request makes
// no progress unless pump() is called.
class HttpRequest final {
  public:
    [[nodiscard]] static Core::Result<HttpRequest> Create(HttpRequestConfig config);

    ~HttpRequest() noexcept;

    HttpRequest(const HttpRequest&) = delete;
    HttpRequest& operator=(const HttpRequest&) = delete;
    HttpRequest(HttpRequest&& other) noexcept;
    HttpRequest& operator=(HttpRequest&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    [[nodiscard]] HttpRequestState state() const noexcept;

    // Advances the exchange. Never blocks. Returns true once the response is
    // complete, at which point response() is readable and further pumps are
    // no-ops.
    [[nodiscard]] Core::Result<bool> pump();

    // Valid only when state() is Complete. All views borrow client storage.
    [[nodiscard]] Core::Result<HttpResponse> response() const noexcept;

    // Stops the exchange and closes the transport. Idempotent. Bytes already sent
    // cannot be unsent, so the server may still process the request.
    void cancel() noexcept;

    [[nodiscard]] HttpRequestStatistics statistics() const noexcept;

  private:
    struct Impl;

    explicit HttpRequest(Impl* impl) noexcept;

    // Incrementally decodes buffered chunk prefixes. True once the terminating
    // zero-length chunk and trailers have been seen.
    [[nodiscard]] Core::Result<bool> decodeChunkedBody();

    Impl* m_impl = nullptr;
};

} // namespace Tina::Network
