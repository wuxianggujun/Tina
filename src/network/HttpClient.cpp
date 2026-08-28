#include <tina/network/HttpClient.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/network/NetworkErrors.hpp>
#include <tina/network/TcpConnection.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace Tina::Network {
namespace {

constexpr std::string_view Crlf = "\r\n";
constexpr std::string_view HeaderTerminator = "\r\n\r\n";

[[nodiscard]] std::string_view methodToken(HttpMethod method) noexcept
{
    switch (method) {
    case HttpMethod::Get:
        return "GET";
    case HttpMethod::Head:
        return "HEAD";
    case HttpMethod::Post:
        return "POST";
    case HttpMethod::Put:
        return "PUT";
    case HttpMethod::Delete:
        return "DELETE";
    }
    return "GET";
}

[[nodiscard]] constexpr bool isDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr char toLowerAscii(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

// Header names are case-insensitive per RFC 9110, and only ASCII case folding is
// correct here -- a locale-aware compare would be wrong.
[[nodiscard]] bool equalsIgnoreAsciiCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (Core::usize index = 0; index < left.size(); ++index) {
        if (toLowerAscii(left[index]) != toLowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view trimOptionalWhitespace(std::string_view value) noexcept
{
    // RFC 9110 OWS is space and horizontal tab only. Trimming anything else would
    // silently accept a malformed field.
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

// Parses a non-negative decimal with an explicit overflow guard. Returns false on
// an empty field, a non-digit, or a value that would wrap.
[[nodiscard]] bool parseDecimal(std::string_view text, Core::usize& out) noexcept
{
    if (text.empty()) {
        return false;
    }
    Core::usize value = 0;
    constexpr Core::usize limit = (std::numeric_limits<Core::usize>::max)();
    for (const char digit : text) {
        if (!isDigit(digit)) {
            return false;
        }
        const auto increment = static_cast<Core::usize>(digit - '0');
        if (value > (limit - increment) / 10) {
            return false;
        }
        value = (value * 10) + increment;
    }
    out = value;
    return true;
}

// Chunk sizes are hexadecimal and may carry a ';'-separated extension, which is
// ignored but must not make the size unparseable.
[[nodiscard]] bool parseChunkSize(std::string_view line, Core::usize& out) noexcept
{
    const Core::usize semicolon = line.find(';');
    if (semicolon != std::string_view::npos) {
        line = line.substr(0, semicolon);
    }
    line = trimOptionalWhitespace(line);
    if (line.empty()) {
        return false;
    }

    Core::usize value = 0;
    constexpr Core::usize limit = (std::numeric_limits<Core::usize>::max)();
    for (const char digit : line) {
        int nibble = -1;
        if (digit >= '0' && digit <= '9') {
            nibble = digit - '0';
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = (digit - 'a') + 10;
        } else if (digit >= 'A' && digit <= 'F') {
            nibble = (digit - 'A') + 10;
        } else {
            return false;
        }
        if (value > (limit - static_cast<Core::usize>(nibble)) / 16) {
            return false;
        }
        value = (value * 16) + static_cast<Core::usize>(nibble);
    }
    out = value;
    return true;
}

} // namespace

std::string_view HttpResponse::header(std::string_view name) const noexcept
{
    for (const auto& entry : headers) {
        if (equalsIgnoreAsciiCase(entry.name, name)) {
            return entry.value;
        }
    }
    return {};
}

struct HttpRequest::Impl final {
    Impl(std::pmr::memory_resource& resource, TcpConnection transportIn)
        : transport(std::move(transportIn))
        , requestBytes(&resource)
        , headerBytes(&resource)
        , bodyBytes(&resource)
        , headers(&resource)
    {
    }

    TcpConnection transport;
    std::thread::id owner{};
    HttpRequestState state = HttpRequestState::Connecting;

    std::pmr::vector<std::byte> requestBytes;
    Core::usize requestSent = 0;

    // Raw status line and headers, kept because the parsed views point into it.
    std::pmr::string headerBytes;
    std::pmr::vector<std::byte> bodyBytes;
    std::pmr::vector<HttpHeader> headers;

    Core::u16 statusCode = 0;
    // Offsets rather than views: the string can still grow while headers arrive,
    // and a reallocation would dangle any view taken earlier.
    Core::usize reasonOffset = 0;
    Core::usize reasonLength = 0;

    bool headersParsed = false;
    bool chunked = false;
    bool hasContentLength = false;
    Core::usize contentLength = 0;
    // Set for 204/304 and any HEAD response, where a body must not be read even
    // if the framing headers suggest one.
    bool bodyForbidden = false;
    // No Content-Length and not chunked: the body ends when the peer closes.
    bool bodyEndsAtClose = false;

    Core::usize maximumBodyBytes = 0;
    Core::usize maximumHeaderBytes = 0;
    Core::usize maximumHeaderCount = 0;

    Core::u32 stallPumpLimit = 0;
    // Pumps since the exchange last made observable progress.
    Core::u32 stalledPumps = 0;

    HttpRequestStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    void markFailed() noexcept { state = HttpRequestState::Failed; }

    void noteProgress() noexcept { stalledPumps = 0; }

    // True when too many pumps have passed with nothing moving. Progress means a
    // state change or new bytes, so a slow-but-advancing transfer never trips it.
    [[nodiscard]] bool hasStalled() noexcept
    {
        if (stallPumpLimit == 0) {
            return false;
        }
        ++stalledPumps;
        return stalledPumps > stallPumpLimit;
    }

    // Rebuilds the header views after headerBytes stops growing. Doing this once
    // at the end avoids re-pointing views on every append.
    [[nodiscard]] Core::Status finaliseHeaders()
    {
        const std::string_view all{headerBytes};
        const Core::usize lineEnd = all.find(Crlf);
        if (lineEnd == std::string_view::npos) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP response has no status line");
        }

        // Status line: HTTP-version SP status-code SP [reason-phrase]
        const std::string_view statusLine = all.substr(0, lineEnd);
        if (!statusLine.starts_with("HTTP/1.")) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP response is not HTTP/1.x");
        }
        const Core::usize firstSpace = statusLine.find(' ');
        if (firstSpace == std::string_view::npos) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP status line has no status code");
        }
        std::string_view remainder = statusLine.substr(firstSpace + 1);
        const Core::usize secondSpace = remainder.find(' ');
        const std::string_view codeText = secondSpace == std::string_view::npos
            ? remainder
            : remainder.substr(0, secondSpace);

        Core::usize code = 0;
        if (codeText.size() != 3 || !parseDecimal(codeText, code) || code > 999) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP status code is not three digits");
        }
        statusCode = static_cast<Core::u16>(code);

        if (secondSpace != std::string_view::npos) {
            const std::string_view reason = remainder.substr(secondSpace + 1);
            reasonOffset = static_cast<Core::usize>(reason.data() - all.data());
            reasonLength = reason.size();
        }

        // Field lines.
        Core::usize cursor = lineEnd + Crlf.size();
        while (cursor < all.size()) {
            const Core::usize fieldEnd = all.find(Crlf, cursor);
            if (fieldEnd == std::string_view::npos) {
                break;
            }
            if (fieldEnd == cursor) {
                break;
            }

            const std::string_view field = all.substr(cursor, fieldEnd - cursor);
            const Core::usize colon = field.find(':');
            if (colon == std::string_view::npos || colon == 0) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP header field has no name");
            }
            // A space before the colon is a request-smuggling vector, so it is
            // rejected rather than trimmed.
            const std::string_view name = field.substr(0, colon);
            if (name.back() == ' ' || name.back() == '\t') {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP header name has trailing whitespace before the colon");
            }

            if (headers.size() >= maximumHeaderCount) {
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP response exceeded the header count limit");
            }
            headers.push_back(HttpHeader{
                .name = name,
                .value = trimOptionalWhitespace(field.substr(colon + 1))});

            cursor = fieldEnd + Crlf.size();
        }

        // Framing. Transfer-Encoding wins over Content-Length; a message carrying
        // both is a smuggling attempt and is refused outright.
        const std::string_view transferEncoding = findHeader("Transfer-Encoding");
        const std::string_view contentLengthField = findHeader("Content-Length");

        if (!transferEncoding.empty()) {
            if (!equalsIgnoreAsciiCase(transferEncoding, "chunked")) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP Transfer-Encoding other than chunked is unsupported");
            }
            if (!contentLengthField.empty()) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP response carries both Transfer-Encoding and Content-Length");
            }
            chunked = true;
            stats.chunkedTransferEncoding = true;
        } else if (!contentLengthField.empty()) {
            Core::usize declared = 0;
            if (!parseDecimal(contentLengthField, declared)) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP Content-Length is not a valid length");
            }
            if (declared > maximumBodyBytes) {
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP Content-Length exceeds the configured body limit");
            }
            hasContentLength = true;
            contentLength = declared;
        } else {
            bodyEndsAtClose = true;
        }

        // 1xx, 204 and 304 never carry a body, whatever the framing says.
        if (statusCode == 204 || statusCode == 304 || (statusCode >= 100 && statusCode < 200)) {
            bodyForbidden = true;
        }

        stats.headerCount = headers.size();
        headersParsed = true;
        return Core::success();
    }

    [[nodiscard]] std::string_view findHeader(std::string_view name) const noexcept
    {
        for (const auto& entry : headers) {
            if (equalsIgnoreAsciiCase(entry.name, name)) {
                return entry.value;
            }
        }
        return {};
    }
};

HttpRequest::HttpRequest(Impl* impl) noexcept
    : m_impl(impl)
{
}

HttpRequest::HttpRequest(HttpRequest&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

HttpRequest::~HttpRequest() noexcept
{
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<HttpRequest> HttpRequest::Create(HttpRequestConfig config)
{
    if (config.target.empty() || config.target.front() != '/') {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "HTTP target must be an origin-form path beginning with '/'");
    }
    if (config.host.empty()) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "HTTP/1.1 requires a Host header value");
    }
    if (config.maximumBodyBytes == 0 || config.maximumHeaderBytes == 0
        || config.maximumHeaderCount == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "HTTP response limits must be greater than zero");
    }
    // A control character in either would let a caller inject a header line.
    for (const char value : config.target) {
        if (value == '\r' || value == '\n' || value == ' ') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "HTTP target must not contain whitespace or CRLF");
        }
    }
    for (const char value : config.host) {
        if (value == '\r' || value == '\n') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "HTTP host must not contain CRLF");
        }
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    std::string head;
    head.reserve(256);
    head.append(methodToken(config.method));
    head.push_back(' ');
    head.append(config.target);
    head.append(" HTTP/1.1\r\nHost: ");
    head.append(config.host);
    // No keep-alive: this type performs one request, so asking the server to hold
    // the connection open would only delay the close that ends an
    // unknown-length body.
    head.append("\r\nConnection: close\r\n");
    if (!config.body.empty()) {
        head.append("Content-Length: ");
        head.append(std::to_string(config.body.size()));
        head.append(Crlf);
        if (!config.contentType.empty()) {
            head.append("Content-Type: ");
            head.append(config.contentType);
            head.append(Crlf);
        }
    }
    head.append(Crlf);

    const Core::usize requestSize = head.size() + config.body.size();

    TcpConnectionConfig transportConfig{};
    transportConfig.remoteEndpoint = config.remoteEndpoint;
    // The transport send buffer must hold the whole request, since send() refuses
    // a payload it cannot take whole.
    transportConfig.sendBufferBytes = (std::max)(requestSize, Core::usize{4096});
    transportConfig.receiveBufferBytes = 64 * 1024;
    transportConfig.memoryResource = resource;

    auto transport = TcpConnection::Create(transportConfig);
    if (!transport) {
        return Core::failure(std::move(transport.error()));
    }

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, std::move(*transport)};
        impl->requestBytes.resize(requestSize);
        std::memcpy(impl->requestBytes.data(), head.data(), head.size());
        if (!config.body.empty()) {
            std::memcpy(
                impl->requestBytes.data() + head.size(),
                config.body.data(),
                config.body.size());
        }
        impl->headerBytes.reserve(1024);
        impl->headers.reserve(config.maximumHeaderCount);
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "HTTP request allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "HTTP request construction failed");
    }

    impl->owner = std::this_thread::get_id();
    impl->maximumBodyBytes = config.maximumBodyBytes;
    impl->maximumHeaderBytes = config.maximumHeaderBytes;
    impl->maximumHeaderCount = config.maximumHeaderCount;
    impl->stallPumpLimit = config.stallPumpLimit;
    impl->bodyForbidden = config.method == HttpMethod::Head;

    return HttpRequest{impl};
}

HttpRequestState HttpRequest::state() const noexcept
{
    if (m_impl == nullptr) {
        return HttpRequestState::Failed;
    }
    return m_impl->state;
}

Core::Result<bool> HttpRequest::pump()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "HttpRequest is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "HttpRequest must be used from its owner thread");
    }

    ++m_impl->stats.pumpCallCount;

    if (m_impl->state == HttpRequestState::Complete) {
        return true;
    }
    if (m_impl->state == HttpRequestState::Failed) {
        return Core::failure(
            NetworkErrorCode::ConnectionFailed,
            "HttpRequest has failed");
    }

    if (m_impl->hasStalled()) {
        m_impl->markFailed();
        return Core::failure(
            NetworkErrorCode::HttpTimeout,
            "HTTP request made no progress within the stall limit");
    }

    auto transportPumped = m_impl->transport.pump();
    if (!transportPumped) {
        m_impl->markFailed();
        return Core::failure(std::move(transportPumped.error()));
    }

    if (m_impl->state == HttpRequestState::Connecting) {
        const auto transportState = m_impl->transport.state();
        if (transportState == TcpConnectionState::Connecting) {
            return false;
        }
        if (transportState == TcpConnectionState::Failed
            || transportState == TcpConnectionState::Closed) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::ConnectionFailed,
                "HTTP transport failed before the request was sent");
        }
        m_impl->state = HttpRequestState::SendingRequest;
        m_impl->noteProgress();
    }

    if (m_impl->state == HttpRequestState::SendingRequest) {
        if (m_impl->requestSent == 0) {
            const auto payload = std::span<const std::byte>{
                m_impl->requestBytes.data(),
                m_impl->requestBytes.size()};
            if (const auto status = m_impl->transport.send(payload); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
            m_impl->requestSent = m_impl->requestBytes.size();
            m_impl->stats.sentBytes = m_impl->requestSent;
        }
        if (m_impl->transport.statistics().queuedSendBytes == 0) {
            m_impl->state = HttpRequestState::ReceivingHeaders;
            m_impl->noteProgress();
        } else {
            return false;
        }
    }

    // Drain whatever arrived into the header buffer, then the body.
    auto buffered = m_impl->transport.receive();
    if (!buffered) {
        m_impl->markFailed();
        return Core::failure(std::move(buffered.error()));
    }

    if (!buffered->empty()) {
        m_impl->stats.receivedBytes += buffered->size();
        m_impl->noteProgress();
    }

    if (m_impl->state == HttpRequestState::ReceivingHeaders && !buffered->empty()) {
        const std::string_view incoming{
            reinterpret_cast<const char*>(buffered->data()),
            buffered->size()};

        // Look for the terminator across the boundary: it can straddle two reads,
        // so the search starts a few bytes before the newly appended region.
        const Core::usize searchStart = m_impl->headerBytes.size() >= HeaderTerminator.size()
            ? m_impl->headerBytes.size() - (HeaderTerminator.size() - 1)
            : 0;
        m_impl->headerBytes.append(incoming);

        if (m_impl->headerBytes.size() > m_impl->maximumHeaderBytes) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::HttpResponseTooLarge,
                "HTTP headers exceeded the configured limit");
        }

        const std::string_view all{m_impl->headerBytes};
        const Core::usize terminator = all.find(HeaderTerminator, searchStart);
        if (terminator != std::string_view::npos) {
            const Core::usize bodyStart = terminator + HeaderTerminator.size();
            // Anything past the terminator is body, so it is moved out before the
            // header text is finalised.
            std::string leftover{all.substr(bodyStart)};
            m_impl->headerBytes.resize(terminator + Crlf.size());

            if (const auto status = m_impl->finaliseHeaders(); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }

            if (const auto status = m_impl->transport.consume(buffered->size()); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
            buffered = std::span<const std::byte>{};

            m_impl->state = HttpRequestState::ReceivingBody;
            m_impl->noteProgress();

            if (!leftover.empty()) {
                if (leftover.size() > m_impl->maximumBodyBytes) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::HttpResponseTooLarge,
                        "HTTP body exceeded the configured limit");
                }
                m_impl->bodyBytes.resize(leftover.size());
                std::memcpy(m_impl->bodyBytes.data(), leftover.data(), leftover.size());
                m_impl->stats.bodyBytes = m_impl->bodyBytes.size();
            }
        } else {
            if (const auto status = m_impl->transport.consume(buffered->size()); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
            return false;
        }
    }

    if (m_impl->state == HttpRequestState::ReceivingBody) {
        if (buffered.has_value() && !buffered->empty()) {
            const Core::usize incoming = buffered->size();
            if (m_impl->bodyBytes.size() + incoming > m_impl->maximumBodyBytes) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP body exceeded the configured limit");
            }
            const Core::usize offset = m_impl->bodyBytes.size();
            m_impl->bodyBytes.resize(offset + incoming);
            std::memcpy(m_impl->bodyBytes.data() + offset, buffered->data(), incoming);
            m_impl->stats.bodyBytes = m_impl->bodyBytes.size();

            if (const auto status = m_impl->transport.consume(incoming); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
        }

        const auto transportState = m_impl->transport.state();

        if (m_impl->bodyForbidden) {
            m_impl->bodyBytes.clear();
            m_impl->stats.bodyBytes = 0;
            m_impl->state = HttpRequestState::Complete;
            return true;
        }

        if (m_impl->chunked) {
            const auto decoded = decodeChunkedBody();
            if (!decoded) {
                m_impl->markFailed();
                return Core::failure(std::move(decoded.error()));
            }
            if (*decoded) {
                m_impl->state = HttpRequestState::Complete;
                return true;
            }
            if (transportState == TcpConnectionState::PeerClosed
                || transportState == TcpConnectionState::Closed) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpIncompleteResponse,
                    "HTTP chunked body ended before the terminating chunk");
            }
            return false;
        }

        if (m_impl->hasContentLength) {
            if (m_impl->bodyBytes.size() >= m_impl->contentLength) {
                // A server that sends more than it declared is framing the message
                // ambiguously, so the extra is dropped rather than guessed at.
                m_impl->bodyBytes.resize(m_impl->contentLength);
                m_impl->stats.bodyBytes = m_impl->bodyBytes.size();
                m_impl->state = HttpRequestState::Complete;
                return true;
            }
            if (transportState == TcpConnectionState::PeerClosed
                || transportState == TcpConnectionState::Closed) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpIncompleteResponse,
                    "HTTP response ended before Content-Length was satisfied");
            }
            return false;
        }

        // No framing headers: the close is the delimiter.
        if (m_impl->bodyEndsAtClose
            && (transportState == TcpConnectionState::PeerClosed
                || transportState == TcpConnectionState::Closed)) {
            m_impl->state = HttpRequestState::Complete;
            return true;
        }
        return false;
    }

    return false;
}

// Decodes in place. Returns true once the terminating zero-length chunk is seen.
Core::Result<bool> HttpRequest::decodeChunkedBody()
{
    // bodyBytes holds the raw chunked stream; decode into a separate buffer and
    // swap, because a chunk header is longer than nothing and in-place shifting
    // would overwrite unread input.
    std::pmr::vector<std::byte> decoded{m_impl->bodyBytes.get_allocator()};
    const std::string_view raw{
        reinterpret_cast<const char*>(m_impl->bodyBytes.data()),
        m_impl->bodyBytes.size()};

    Core::usize cursor = 0;
    while (true) {
        const Core::usize lineEnd = raw.find(Crlf, cursor);
        if (lineEnd == std::string_view::npos) {
            // Incomplete chunk header; wait for more bytes.
            return false;
        }

        Core::usize chunkSize = 0;
        if (!parseChunkSize(raw.substr(cursor, lineEnd - cursor), chunkSize)) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP chunk size is malformed");
        }

        const Core::usize dataStart = lineEnd + Crlf.size();
        if (chunkSize == 0) {
            // Terminating chunk. Any trailer section is ignored, but the final
            // CRLF must be present for the message to be complete.
            const Core::usize trailerEnd = raw.find(Crlf, dataStart);
            if (trailerEnd == std::string_view::npos && raw.size() < dataStart + Crlf.size()) {
                return false;
            }
            m_impl->bodyBytes = std::move(decoded);
            m_impl->stats.bodyBytes = m_impl->bodyBytes.size();
            return true;
        }

        if (decoded.size() + chunkSize > m_impl->maximumBodyBytes) {
            return Core::failure(
                NetworkErrorCode::HttpResponseTooLarge,
                "HTTP chunked body exceeded the configured limit");
        }
        if (dataStart + chunkSize + Crlf.size() > raw.size()) {
            // The chunk body has not fully arrived yet.
            return false;
        }

        const Core::usize offset = decoded.size();
        decoded.resize(offset + chunkSize);
        std::memcpy(decoded.data() + offset, raw.data() + dataStart, chunkSize);

        cursor = dataStart + chunkSize;
        if (raw.substr(cursor, Crlf.size()) != Crlf) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP chunk is not terminated by CRLF");
        }
        cursor += Crlf.size();
    }
}

Core::Result<HttpResponse> HttpRequest::response() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "HttpRequest is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "HttpRequest must be used from its owner thread");
    }
    if (m_impl->state != HttpRequestState::Complete) {
        return Core::failure(
            NetworkErrorCode::HttpIncompleteResponse,
            "HTTP response is not complete");
    }

    HttpResponse result{};
    result.statusCode = m_impl->statusCode;
    if (m_impl->reasonLength != 0) {
        result.reasonPhrase = std::string_view{m_impl->headerBytes}.substr(
            m_impl->reasonOffset,
            m_impl->reasonLength);
    }
    result.headers = std::span<const HttpHeader>{m_impl->headers.data(), m_impl->headers.size()};
    result.body = std::span<const std::byte>{m_impl->bodyBytes.data(), m_impl->bodyBytes.size()};
    return result;
}

void HttpRequest::cancel() noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return;
    }
    m_impl->transport.close();
    if (m_impl->state != HttpRequestState::Complete) {
        m_impl->state = HttpRequestState::Failed;
    }
}

HttpRequestStatistics HttpRequest::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

} // namespace Tina::Network
