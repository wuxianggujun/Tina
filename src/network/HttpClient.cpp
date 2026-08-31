#include <tina/network/HttpClient.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/network/NetworkErrors.hpp>
#include <tina/network/ByteStream.hpp>

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
constexpr Core::usize MaximumInformationalResponses = 8;

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

[[nodiscard]] constexpr bool isAsciiControl(char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    return byte <= 0x1FU || byte == 0x7FU;
}

[[nodiscard]] constexpr bool isHttpTokenCharacter(char value) noexcept
{
    if (isDigit(value)
        || (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z')) {
        return true;
    }
    switch (value) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
    case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isValidHeaderName(std::string_view name) noexcept
{
    return !name.empty()
        && std::ranges::all_of(name, [](char value) { return isHttpTokenCharacter(value); });
}

[[nodiscard]] bool isValidHeaderValue(std::string_view value) noexcept
{
    return std::ranges::none_of(value, [](char character) {
        return isAsciiControl(character) && character != '\t';
    });
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
        const std::string_view extensions = line.substr(semicolon + 1U);
        if (extensions.empty() || !isValidHeaderValue(extensions)) {
            return false;
        }
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

[[nodiscard]] Core::Status validateTrailerSection(
    std::string_view trailers,
    Core::usize maximumTrailerCount)
{
    Core::usize count = 0;
    Core::usize cursor = 0;
    while (cursor < trailers.size()) {
        const Core::usize lineEnd = trailers.find(Crlf, cursor);
        const Core::usize end = lineEnd == std::string_view::npos
            ? trailers.size()
            : lineEnd;
        if (end == cursor || count >= maximumTrailerCount) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP chunk trailer is empty or exceeds the field count limit");
        }

        const std::string_view field = trailers.substr(cursor, end - cursor);
        const Core::usize colon = field.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP chunk trailer has no field name");
        }
        const std::string_view name = field.substr(0, colon);
        const std::string_view value = trimOptionalWhitespace(field.substr(colon + 1));
        if (!isValidHeaderName(name) || !isValidHeaderValue(value)) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP chunk trailer contains invalid field syntax");
        }
        if (equalsIgnoreAsciiCase(name, "Content-Length")
            || equalsIgnoreAsciiCase(name, "Transfer-Encoding")
            || equalsIgnoreAsciiCase(name, "Host")) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP chunk trailer attempts to change message framing or routing");
        }

        ++count;
        if (lineEnd == std::string_view::npos) {
            break;
        }
        cursor = lineEnd + Crlf.size();
    }
    return Core::success();
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
    Impl(std::pmr::memory_resource& resource, IByteStream& streamIn)
        : stream(&streamIn)
        , requestBytes(&resource)
        , headerBytes(&resource)
        , bodyBytes(&resource)
        , chunkBytes(&resource)
        , headers(&resource)
    {
    }

    // Borrowed, never owned: the caller decides the transport and therefore
    // whether the bytes are encrypted.
    IByteStream* stream = nullptr;
    std::thread::id owner{};
    HttpRequestState state = HttpRequestState::Connecting;

    std::pmr::vector<std::byte> requestBytes;
    Core::usize requestSent = 0;

    // Raw status line and headers, kept because the parsed views point into it.
    std::pmr::string headerBytes;
    std::pmr::vector<std::byte> bodyBytes;
    // Encoded bytes not yet consumed by the incremental chunk parser. Keeping
    // these separate makes maximumBodyBytes an exact decoded-body cap rather
    // than charging chunk headers and trailers against application data.
    std::pmr::vector<std::byte> chunkBytes;
    std::pmr::vector<HttpHeader> headers;

    enum class ChunkState : Core::u8 {
        SizeLine,
        Data,
        DataTerminator,
        Trailers,
        Complete,
    };
    ChunkState chunkState = ChunkState::SizeLine;
    Core::usize chunkBytesRemaining = 0;

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
    bool headRequest = false;
    // No Content-Length and not chunked: the body ends when the peer closes.
    bool bodyEndsAtClose = false;
    Core::usize informationalResponseCount = 0;

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

    void markFailed() noexcept
    {
        // Borrowed means this object does not destroy the stream owner. The
        // single-request exchange still exclusively consumes that connection,
        // so a malformed/incomplete response must close it rather than leave
        // unframed bytes available for accidental reuse.
        stream->closeStream();
        state = HttpRequestState::Failed;
    }

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
        if (!statusLine.starts_with("HTTP/1.0 ")
            && !statusLine.starts_with("HTTP/1.1 ")) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP response version is not HTTP/1.0 or HTTP/1.1");
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
        if (codeText.size() != 3 || !parseDecimal(codeText, code)
            || code < 100 || code > 599) {
            return Core::failure(
                NetworkErrorCode::HttpMalformedResponse,
                "HTTP status code is outside the 100-599 range");
        }
        statusCode = static_cast<Core::u16>(code);

        if (secondSpace != std::string_view::npos) {
            const std::string_view reason = remainder.substr(secondSpace + 1);
            if (!isValidHeaderValue(reason)) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP reason phrase contains a control character");
            }
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
            const std::string_view name = field.substr(0, colon);
            const std::string_view value = trimOptionalWhitespace(field.substr(colon + 1));
            if (!isValidHeaderName(name) || !isValidHeaderValue(value)) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP header field contains invalid syntax");
            }

            if (headers.size() >= maximumHeaderCount) {
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP response exceeded the header count limit");
            }
            headers.push_back(HttpHeader{
                .name = name,
                .value = value});

            cursor = fieldEnd + Crlf.size();
        }

        // Framing is collected across every occurrence. Looking up only the first Content-Length would
        // accept an inconsistent duplicate, which is precisely the ambiguity request smuggling uses.
        bool sawTransferEncoding = false;
        bool sawContentLength = false;
        Core::usize declaredContentLength = 0;
        for (const HttpHeader& header : headers) {
            if (equalsIgnoreAsciiCase(header.name, "Transfer-Encoding")) {
                if (sawTransferEncoding || !equalsIgnoreAsciiCase(header.value, "chunked")) {
                    return Core::failure(
                        NetworkErrorCode::HttpMalformedResponse,
                        "HTTP Transfer-Encoding must be one unambiguous chunked field");
                }
                sawTransferEncoding = true;
                continue;
            }
            if (!equalsIgnoreAsciiCase(header.name, "Content-Length")) {
                continue;
            }
            Core::usize declared = 0;
            if (!parseDecimal(header.value, declared)) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP Content-Length is not a valid length");
            }
            if (sawContentLength && declared != declaredContentLength) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP response carries inconsistent Content-Length fields");
            }
            sawContentLength = true;
            declaredContentLength = declared;
        }

        bodyForbidden = headRequest || statusCode < 200 || statusCode == 204
            || statusCode == 205 || statusCode == 304;

        if (sawTransferEncoding) {
            if (sawContentLength) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP response carries both Transfer-Encoding and Content-Length");
            }
            if (statusCode < 200 || statusCode == 204) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP response status forbids Transfer-Encoding");
            }
            chunked = true;
            stats.chunkedTransferEncoding = true;
        } else if (sawContentLength) {
            if (statusCode < 200 || statusCode == 204) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP response status forbids Content-Length");
            }
            if (statusCode == 205 && declaredContentLength != 0) {
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP 205 response requires a zero Content-Length");
            }
            if (!bodyForbidden && declaredContentLength > maximumBodyBytes) {
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP Content-Length exceeds the configured body limit");
            }
            hasContentLength = true;
            contentLength = declaredContentLength;
        } else if (!bodyForbidden) {
            bodyEndsAtClose = true;
        }

        stats.headerCount = headers.size();
        headersParsed = true;
        return Core::success();
    }

    void resetForNextResponse() noexcept
    {
        headerBytes.clear();
        headers.clear();
        statusCode = 0;
        reasonOffset = 0;
        reasonLength = 0;
        headersParsed = false;
        chunked = false;
        hasContentLength = false;
        contentLength = 0;
        bodyForbidden = headRequest;
        bodyEndsAtClose = false;
        chunkState = ChunkState::SizeLine;
        chunkBytesRemaining = 0;
        chunkBytes.clear();
        stats.headerCount = 0;
        stats.chunkedTransferEncoding = false;
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
    if (config.stream == nullptr) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "HttpRequest requires a byte stream to run over");
    }
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
    for (const char value : config.target) {
        if (isAsciiControl(value) || value == ' ' || value == '#') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "HTTP target contains an invalid origin-form character");
        }
    }
    for (const char value : config.host) {
        if (isAsciiControl(value) || value == ' ') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "HTTP host contains whitespace or a control character");
        }
    }
    if (!config.contentType.empty() && !isValidHeaderValue(config.contentType)) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "HTTP Content-Type contains a control character");
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    std::string head;
    try {
        head.reserve(256);
        head.append(methodToken(config.method));
        head.push_back(' ');
        head.append(config.target);
        head.append(" HTTP/1.1\r\nHost: ");
        head.append(config.host);
        // No keep-alive: this type performs one request, so asking the server to
        // hold the connection open would delay an unknown-length response.
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
    } catch (const std::bad_alloc&) {
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "HTTP request header allocation failed");
    } catch (...) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "HTTP request header construction failed");
    }

    if (config.body.size() > (std::numeric_limits<Core::usize>::max)() - head.size()) {
        return Core::failure(
            NetworkErrorCode::CapacityExceeded,
            "HTTP request size overflows addressable storage");
    }
    const Core::usize requestSize = head.size() + config.body.size();

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, *config.stream};
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
    impl->headRequest = config.method == HttpMethod::Head;
    impl->bodyForbidden = impl->headRequest;

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

    auto transportPumped = m_impl->stream->pumpStream();
    if (!transportPumped) {
        m_impl->markFailed();
        return Core::failure(std::move(transportPumped.error()));
    }

    if (m_impl->state == HttpRequestState::Connecting) {
        const auto streamState = m_impl->stream->streamState();
        if (streamState == ByteStreamState::Connecting) {
            return false;
        }
        if (streamState == ByteStreamState::Failed
            || streamState == ByteStreamState::Closed) {
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
            if (const auto status = m_impl->stream->sendBytes(payload); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
            m_impl->requestSent = m_impl->requestBytes.size();
            m_impl->stats.sentBytes = m_impl->requestSent;
        }
        // The stream reports no queue depth on purpose -- that is transport
        // detail -- so move on and let later pumps drain it. Reading can begin
        // immediately: a server replies only after it has the whole request, and
        // the parser simply sees nothing until then.
        m_impl->state = HttpRequestState::ReceivingHeaders;
        m_impl->noteProgress();
    }

    // Drain whatever arrived into the header buffer, then the body.
    auto buffered = m_impl->stream->peekReceived();
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
        try {
            m_impl->headerBytes.append(incoming);
        } catch (const std::bad_alloc&) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::AllocationFailed,
                "HTTP header buffer allocation failed");
        } catch (...) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::ConstructionFailed,
                "HTTP header buffer construction failed");
        }
        if (const auto status = m_impl->stream->consumeReceived(buffered->size()); !status) {
            m_impl->markFailed();
            return Core::failure(status.error());
        }
        buffered = std::span<const std::byte>{};

        Core::usize nextSearchStart = searchStart;
        while (m_impl->state == HttpRequestState::ReceivingHeaders) {
            const std::string_view all{m_impl->headerBytes};
            const Core::usize terminator = all.find(HeaderTerminator, nextSearchStart);
            const Core::usize headerSize = terminator == std::string_view::npos
                ? all.size()
                : terminator + HeaderTerminator.size();
            if (headerSize > m_impl->maximumHeaderBytes) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP headers exceeded the configured limit");
            }
            if (terminator == std::string_view::npos) {
                const auto streamState = m_impl->stream->streamState();
                if (streamState == ByteStreamState::PeerClosed
                    || streamState == ByteStreamState::Closed) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::HttpIncompleteResponse,
                        "HTTP transport closed before the response headers completed");
                }
                return false;
            }

            const Core::usize bodyStart = terminator + HeaderTerminator.size();
            std::string leftover;
            try {
                leftover.assign(all.substr(bodyStart));
            } catch (const std::bad_alloc&) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::AllocationFailed,
                    "HTTP response remainder allocation failed");
            } catch (...) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::ConstructionFailed,
                    "HTTP response remainder construction failed");
            }
            m_impl->headerBytes.resize(terminator + Crlf.size());

            try {
                if (const auto status = m_impl->finaliseHeaders(); !status) {
                    m_impl->markFailed();
                    return Core::failure(status.error());
                }
            } catch (const std::bad_alloc&) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::AllocationFailed,
                    "HTTP parsed header allocation failed");
            } catch (...) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::ConstructionFailed,
                    "HTTP parsed header construction failed");
            }

            if (m_impl->statusCode >= 100 && m_impl->statusCode < 200) {
                if (m_impl->statusCode == 101) {
                    m_impl->markFailed();
                    return Core::failure(NetworkErrorCode::HttpMalformedResponse,
                                         "HTTP protocol switching is unsupported by HttpRequest");
                }
                ++m_impl->informationalResponseCount;
                if (m_impl->informationalResponseCount > MaximumInformationalResponses) {
                    m_impl->markFailed();
                    return Core::failure(NetworkErrorCode::HttpMalformedResponse,
                                         "HTTP response contains too many informational responses");
                }
                // Informational responses do not complete an HTTP exchange. Parse the next response from
                // the bytes already received before waiting for another pump.
                m_impl->resetForNextResponse();
                try {
                    m_impl->headerBytes.assign(leftover);
                } catch (const std::bad_alloc&) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::AllocationFailed,
                        "HTTP informational response allocation failed");
                } catch (...) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::ConstructionFailed,
                        "HTTP informational response construction failed");
                }
                nextSearchStart = 0;
                if (m_impl->headerBytes.empty()) {
                    return false;
                }
                continue;
            }

            m_impl->state = HttpRequestState::ReceivingBody;
            m_impl->noteProgress();
            if (!leftover.empty()) {
                if (m_impl->bodyForbidden) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::HttpMalformedResponse,
                        "HTTP response carried bytes where a body is forbidden");
                }
                const Core::usize storageLimit = m_impl->chunked
                    ? ((m_impl->maximumBodyBytes
                        > (std::numeric_limits<Core::usize>::max)()
                            - m_impl->maximumHeaderBytes)
                           ? (std::numeric_limits<Core::usize>::max)()
                           : m_impl->maximumBodyBytes + m_impl->maximumHeaderBytes)
                    : m_impl->maximumBodyBytes;
                if (leftover.size() > storageLimit) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::HttpResponseTooLarge,
                        "HTTP body exceeded the configured storage limit");
                }
                try {
                    auto& destination = m_impl->chunked
                        ? m_impl->chunkBytes
                        : m_impl->bodyBytes;
                    destination.resize(leftover.size());
                    std::memcpy(destination.data(), leftover.data(), leftover.size());
                } catch (const std::bad_alloc&) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::AllocationFailed,
                        "HTTP body buffer allocation failed");
                } catch (...) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::ConstructionFailed,
                        "HTTP body buffer construction failed");
                }
            }
        }
    }

    if (m_impl->state == HttpRequestState::ReceivingHeaders
        && (m_impl->stream->streamState() == ByteStreamState::PeerClosed
            || m_impl->stream->streamState() == ByteStreamState::Closed)) {
        m_impl->markFailed();
        return Core::failure(
            NetworkErrorCode::HttpIncompleteResponse,
            "HTTP transport closed before the response headers completed");
    }

    if (m_impl->state == HttpRequestState::ReceivingBody) {
        if (buffered.has_value() && !buffered->empty()) {
            const Core::usize incoming = buffered->size();
            if (m_impl->bodyForbidden) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpMalformedResponse,
                    "HTTP response carried bytes where a body is forbidden");
            }

            auto& destination = m_impl->chunked
                ? m_impl->chunkBytes
                : m_impl->bodyBytes;
            const Core::usize storageLimit = m_impl->chunked
                ? ((m_impl->maximumBodyBytes
                    > (std::numeric_limits<Core::usize>::max)()
                        - m_impl->maximumHeaderBytes)
                       ? (std::numeric_limits<Core::usize>::max)()
                       : m_impl->maximumBodyBytes + m_impl->maximumHeaderBytes)
                : m_impl->maximumBodyBytes;
            if (destination.size() > storageLimit
                || incoming > storageLimit - destination.size()) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpResponseTooLarge,
                    "HTTP body exceeded the configured storage limit");
            }
            const Core::usize offset = destination.size();
            try {
                destination.resize(offset + incoming);
                std::memcpy(destination.data() + offset, buffered->data(), incoming);
            } catch (const std::bad_alloc&) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::AllocationFailed,
                    "HTTP body buffer allocation failed");
            } catch (...) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::ConstructionFailed,
                    "HTTP body buffer construction failed");
            }
            if (!m_impl->chunked) {
                m_impl->stats.bodyBytes = m_impl->bodyBytes.size();
            }

            if (const auto status = m_impl->stream->consumeReceived(incoming); !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
        }

        const auto streamState = m_impl->stream->streamState();

        if (m_impl->bodyForbidden) {
            if (!m_impl->bodyBytes.empty()) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::HttpMalformedResponse,
                                     "HTTP response carried a body where one is forbidden");
            }
            m_impl->bodyBytes.clear();
            m_impl->stats.bodyBytes = 0;
            m_impl->state = HttpRequestState::Complete;
            return true;
        }

        if (m_impl->chunked) {
            const auto decoded = decodeChunkedBody();
            if (!decoded) {
                m_impl->markFailed();
                // decoded is const, so error() yields a const reference and the
                // move would silently copy anyway.
                return Core::failure(decoded.error());
            }
            if (*decoded) {
                m_impl->state = HttpRequestState::Complete;
                return true;
            }
            if (streamState == ByteStreamState::PeerClosed
                || streamState == ByteStreamState::Closed) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpIncompleteResponse,
                    "HTTP chunked body ended before the terminating chunk");
            }
            return false;
        }

        if (m_impl->hasContentLength) {
            if (m_impl->bodyBytes.size() > m_impl->contentLength) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::HttpMalformedResponse,
                                     "HTTP response contains bytes beyond Content-Length");
            }
            if (m_impl->bodyBytes.size() == m_impl->contentLength) {
                m_impl->state = HttpRequestState::Complete;
                return true;
            }
            if (streamState == ByteStreamState::PeerClosed
                || streamState == ByteStreamState::Closed) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::HttpIncompleteResponse,
                    "HTTP response ended before Content-Length was satisfied");
            }
            return false;
        }

        // No framing headers: the close is the delimiter.
        if (m_impl->bodyEndsAtClose
            && (streamState == ByteStreamState::PeerClosed
                || streamState == ByteStreamState::Closed)) {
            m_impl->state = HttpRequestState::Complete;
            return true;
        }
        return false;
    }

    return false;
}

// Incrementally decodes complete chunk prefixes. Encoded bytes are compacted as
// they are consumed, so an idle pump does not repeatedly reparse the whole body.
Core::Result<bool> HttpRequest::decodeChunkedBody()
{
    const auto consumeEncoded = [this](Core::usize byteCount) noexcept {
        const Core::usize remaining = m_impl->chunkBytes.size() - byteCount;
        if (remaining != 0) {
            std::memmove(
                m_impl->chunkBytes.data(),
                m_impl->chunkBytes.data() + byteCount,
                remaining);
        }
        m_impl->chunkBytes.resize(remaining);
    };

    try {
        while (true) {
            const char* encodedData = m_impl->chunkBytes.empty()
                ? ""
                : reinterpret_cast<const char*>(m_impl->chunkBytes.data());
            const std::string_view encoded{
                encodedData,
                m_impl->chunkBytes.size()};

            switch (m_impl->chunkState) {
            case Impl::ChunkState::SizeLine: {
                const Core::usize lineEnd = encoded.find(Crlf);
                if (lineEnd == std::string_view::npos) {
                    if (encoded.size() > m_impl->maximumHeaderBytes) {
                        return Core::failure(
                            NetworkErrorCode::HttpResponseTooLarge,
                            "HTTP chunk size line exceeded the header limit");
                    }
                    return false;
                }

                Core::usize chunkSize = 0;
                if (!parseChunkSize(encoded.substr(0, lineEnd), chunkSize)) {
                    return Core::failure(
                        NetworkErrorCode::HttpMalformedResponse,
                        "HTTP chunk size or extension is malformed");
                }
                if (chunkSize > m_impl->maximumBodyBytes - m_impl->bodyBytes.size()) {
                    return Core::failure(
                        NetworkErrorCode::HttpResponseTooLarge,
                        "HTTP chunked body exceeded the configured limit");
                }

                consumeEncoded(lineEnd + Crlf.size());
                if (chunkSize == 0) {
                    m_impl->chunkState = Impl::ChunkState::Trailers;
                } else {
                    m_impl->chunkBytesRemaining = chunkSize;
                    m_impl->chunkState = Impl::ChunkState::Data;
                }
                break;
            }
            case Impl::ChunkState::Data: {
                if (m_impl->chunkBytes.empty()) {
                    return false;
                }
                const Core::usize take = (std::min)(
                    m_impl->chunkBytesRemaining,
                    m_impl->chunkBytes.size());
                if (take > m_impl->maximumBodyBytes - m_impl->bodyBytes.size()) {
                    return Core::failure(
                        NetworkErrorCode::HttpResponseTooLarge,
                        "HTTP chunked body exceeded the configured limit");
                }

                const Core::usize outputOffset = m_impl->bodyBytes.size();
                m_impl->bodyBytes.resize(outputOffset + take);
                std::memcpy(
                    m_impl->bodyBytes.data() + outputOffset,
                    m_impl->chunkBytes.data(),
                    take);
                consumeEncoded(take);
                m_impl->chunkBytesRemaining -= take;
                m_impl->stats.bodyBytes = m_impl->bodyBytes.size();
                if (m_impl->chunkBytesRemaining != 0) {
                    return false;
                }
                m_impl->chunkState = Impl::ChunkState::DataTerminator;
                break;
            }
            case Impl::ChunkState::DataTerminator:
                if (encoded.size() < Crlf.size()) {
                    return false;
                }
                if (encoded.substr(0, Crlf.size()) != Crlf) {
                    return Core::failure(
                        NetworkErrorCode::HttpMalformedResponse,
                        "HTTP chunk is not terminated by CRLF");
                }
                consumeEncoded(Crlf.size());
                m_impl->chunkState = Impl::ChunkState::SizeLine;
                break;
            case Impl::ChunkState::Trailers: {
                if (encoded.size() < Crlf.size()) {
                    return false;
                }

                Core::usize trailerBytes = 0;
                if (encoded.substr(0, Crlf.size()) == Crlf) {
                    trailerBytes = Crlf.size();
                } else {
                    const Core::usize terminator = encoded.find(HeaderTerminator);
                    if (terminator == std::string_view::npos) {
                        if (encoded.size() > m_impl->maximumHeaderBytes) {
                            return Core::failure(
                                NetworkErrorCode::HttpResponseTooLarge,
                                "HTTP chunk trailers exceeded the header limit");
                        }
                        return false;
                    }
                    if (terminator + HeaderTerminator.size() > m_impl->maximumHeaderBytes) {
                        return Core::failure(
                            NetworkErrorCode::HttpResponseTooLarge,
                            "HTTP chunk trailers exceeded the header limit");
                    }
                    const Core::usize remainingHeaderSlots =
                        m_impl->headers.size() >= m_impl->maximumHeaderCount
                        ? 0
                        : m_impl->maximumHeaderCount - m_impl->headers.size();
                    if (const auto status = validateTrailerSection(
                            encoded.substr(0, terminator),
                            remainingHeaderSlots);
                        !status) {
                        return Core::failure(status.error());
                    }
                    trailerBytes = terminator + HeaderTerminator.size();
                }

                consumeEncoded(trailerBytes);
                if (!m_impl->chunkBytes.empty()) {
                    return Core::failure(
                        NetworkErrorCode::HttpMalformedResponse,
                        "HTTP chunked response contains bytes after its terminator");
                }
                m_impl->chunkState = Impl::ChunkState::Complete;
                return true;
            }
            case Impl::ChunkState::Complete:
                return true;
            }
        }
    } catch (const std::bad_alloc&) {
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "HTTP chunk decode allocation failed");
    } catch (...) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "HTTP chunk decode failed");
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
    m_impl->stream->closeStream();
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
