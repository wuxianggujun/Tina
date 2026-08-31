#include <tina/network/WebSocket.hpp>

#include "detail/WebSocketHandshake.hpp"

#include <tina/core/text/Utf8.hpp>
#include <tina/network/NetworkErrors.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace Tina::Network {
namespace {

constexpr std::string_view Crlf = "\r\n";
constexpr std::string_view HeaderTerminator = "\r\n\r\n";

// RFC 6455 opcodes.
constexpr Core::u8 OpcodeContinuation = 0x0;
constexpr Core::u8 OpcodeText = 0x1;
constexpr Core::u8 OpcodeBinary = 0x2;
constexpr Core::u8 OpcodeClose = 0x8;
constexpr Core::u8 OpcodePing = 0x9;
constexpr Core::u8 OpcodePong = 0xA;

constexpr Core::usize MaskBytes = 4;

[[nodiscard]] constexpr bool isControlOpcode(Core::u8 opcode) noexcept
{
    return (opcode & 0x08U) != 0;
}

[[nodiscard]] constexpr char toLowerAscii(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

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

[[nodiscard]] constexpr bool isValidCloseCode(Core::u16 code) noexcept
{
    // 1004-1006 are reserved and 1015 is never legal on the wire. 3000-4999
    // are application/private-use codes; all other values are invalid.
    const bool registered = code >= 1000U && code <= 1014U && code != 1004U
        && code != 1005U && code != 1006U;
    return registered || (code >= 3000U && code <= 4999U);
}

[[nodiscard]] auto protocolFailure(std::string_view message)
{
    return Core::failure(NetworkErrorCode::WebSocketProtocolError, message);
}

[[nodiscard]] constexpr bool isAsciiControl(char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    return byte <= 0x1FU || byte == 0x7FU;
}

[[nodiscard]] constexpr bool isHttpTokenCharacter(char value) noexcept
{
    if (value >= '0' && value <= '9') {
        return true;
    }
    if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) {
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

[[nodiscard]] std::string_view trimOptionalWhitespace(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool hasHeaderToken(std::string_view value, std::string_view token) noexcept
{
    while (!value.empty()) {
        const Core::usize comma = value.find(',');
        std::string_view candidate = value.substr(0, comma);
        candidate = trimOptionalWhitespace(candidate);
        if (equalsIgnoreAsciiCase(candidate, token)) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1U);
    }
    return false;
}

[[nodiscard]] bool isSwitchingProtocolsStatus(std::string_view statusLine) noexcept
{
    constexpr std::string_view prefix = "HTTP/1.1 101";
    if (statusLine == prefix) {
        return true;
    }
    if (!statusLine.starts_with(prefix) || statusLine.size() <= prefix.size()
        || statusLine[prefix.size()] != ' ') {
        return false;
    }
    return isValidHeaderValue(statusLine.substr(prefix.size() + 1U));
}

void fillRandomBytes(std::random_device& entropy, std::span<std::byte> output)
{
    for (std::byte& value : output) {
        value = static_cast<std::byte>(entropy() & 0xFFU);
    }
}

// Sec-WebSocket-Key is a fresh 16-byte nonce. The same platform entropy source
// is also used directly for frame masks instead of a predictable PRNG.
[[nodiscard]] std::string generateClientKey(std::random_device& entropy)
{
    std::array<std::byte, 16> nonce{};
    fillRandomBytes(entropy, nonce);
    return Detail::base64Encode(nonce);
}

} // namespace

std::string webSocketAcceptToken(std::string_view clientKey)
{
    return Detail::computeWebSocketAccept(clientKey);
}

struct WebSocket::Impl final {
    Impl(std::pmr::memory_resource& resource, IByteStream& streamIn)
        : stream(&streamIn)
        , handshakeBytes(&resource)
        , frameBytes(&resource)
        , messageBytes(&resource)
        , sendScratch(&resource)
    {
    }

    IByteStream* stream = nullptr;
    // RFC 6455 requires an independently generated, unpredictable mask for each
    // client frame. Supported standard libraries back random_device with the
    // platform entropy source, so use it directly rather than seeding a PRNG.
    std::random_device entropy;
    std::thread::id owner{};
    WebSocketState state = WebSocketState::Handshaking;

    std::string clientKey;
    std::string expectedAccept;

    std::pmr::string handshakeBytes;
    // Raw frame bytes not yet parsed.
    std::pmr::vector<std::byte> frameBytes;
    // Reassembled payload of the message being delivered.
    std::pmr::vector<std::byte> messageBytes;
    std::pmr::vector<std::byte> sendScratch;

    // Set while a fragmented message is in progress; a second data frame before
    // the FIN is a protocol error.
    bool assemblingMessage = false;
    WebSocketMessageKind assemblingKind = WebSocketMessageKind::Text;
    bool messageReady = false;
    WebSocketMessageKind messageKind = WebSocketMessageKind::Text;

    Core::usize maximumMessageBytes = 0;
    Core::usize receiveBufferBytes = 0;

    Core::u32 stallPumpLimit = 0;
    Core::u32 stalledPumps = 0;

    bool closeSent = false;
    bool closeReceived = false;

    WebSocketStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    void markFailed() noexcept
    {
        // The stream is borrowed as storage ownership, but one WebSocket is its
        // sole protocol consumer. RFC 6455 requires a protocol error to fail the
        // connection, so leaving the transport live would retain unread attacker
        // bytes and an open socket with no valid parser state.
        stream->closeStream();
        state = WebSocketState::Failed;
    }

    void noteProgress() noexcept { stalledPumps = 0; }

    [[nodiscard]] bool hasStalled() noexcept
    {
        if (stallPumpLimit == 0) {
            return false;
        }
        ++stalledPumps;
        return stalledPumps > stallPumpLimit;
    }

    // Builds a masked client frame. Masking is mandatory for client-to-server
    // frames; a server must fail the connection on an unmasked one.
    [[nodiscard]] Core::Status queueFrame(
        Core::u8 opcode,
        std::span<const std::byte> payload)
    {
        if (isControlOpcode(opcode) && payload.size() > 125U) {
            return protocolFailure("WebSocket control frame exceeds 125 bytes");
        }
        if (opcode != OpcodeClose && opcode != OpcodePing && opcode != OpcodePong
            && opcode != OpcodeContinuation && opcode != OpcodeText && opcode != OpcodeBinary) {
            return protocolFailure("WebSocket frame used an unknown opcode");
        }
        if (payload.size() > (std::numeric_limits<Core::usize>::max)() - 14U) {
            return Core::failure(
                NetworkErrorCode::CapacityExceeded,
                "WebSocket frame size overflows the send buffer");
        }
        std::array<std::byte, 14> header{};
        Core::usize headerSize = 0;

        header[0] = static_cast<std::byte>(0x80U | opcode);  // FIN set
        ++headerSize;

        const Core::usize length = payload.size();
        if (length < 126) {
            header[1] = static_cast<std::byte>(0x80U | length);  // mask bit
            ++headerSize;
        } else if (length <= 0xFFFFU) {
            header[1] = static_cast<std::byte>(0x80U | 126U);
            header[2] = static_cast<std::byte>((length >> 8) & 0xFFU);
            header[3] = static_cast<std::byte>(length & 0xFFU);
            headerSize += 3;
        } else {
            header[1] = static_cast<std::byte>(0x80U | 127U);
            for (int index = 0; index < 8; ++index) {
                header[2 + static_cast<Core::usize>(index)] = static_cast<std::byte>(
                    (static_cast<Core::u64>(length) >> ((7 - index) * 8)) & 0xFFU);
            }
            headerSize += 9;
        }

        std::array<std::byte, MaskBytes> mask{};
        try {
            fillRandomBytes(entropy, mask);
        } catch (...) {
            return Core::failure(
                NetworkErrorCode::BackendFailure,
                "WebSocket could not obtain entropy for a frame mask");
        }
        for (Core::usize index = 0; index < MaskBytes; ++index) {
            header[headerSize + index] = mask[index];
        }
        headerSize += MaskBytes;

        try {
            sendScratch.resize(headerSize + length);
        } catch (const std::bad_alloc&) {
            return Core::failure(
                NetworkErrorCode::AllocationFailed,
                "WebSocket frame allocation failed");
        } catch (...) {
            return Core::failure(
                NetworkErrorCode::ConstructionFailed,
                "WebSocket frame allocation failed");
        }

        std::memcpy(sendScratch.data(), header.data(), headerSize);
        for (Core::usize index = 0; index < length; ++index) {
            sendScratch[headerSize + index] = payload[index]
                ^ mask[index % MaskBytes];
        }

        try {
            if (const auto status = stream->sendBytes(sendScratch); !status) {
                return Core::failure(status.error());
            }
        } catch (const std::bad_alloc&) {
            return Core::failure(NetworkErrorCode::AllocationFailed,
                                 "WebSocket stream rejected frame allocation");
        } catch (...) {
            return Core::failure(NetworkErrorCode::BackendFailure,
                                 "WebSocket stream threw while queueing a frame");
        }

        ++stats.sentFrameCount;
        stats.sentPayloadBytes += length;
        return Core::success();
    }
};

WebSocket::WebSocket(Impl* impl) noexcept
    : m_impl(impl)
{
}

WebSocket::WebSocket(WebSocket&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

WebSocket::~WebSocket() noexcept
{
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<WebSocket> WebSocket::Create(WebSocketConfig config)
{
    if (config.stream == nullptr) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "WebSocket requires a byte stream to run over");
    }
    if (config.target.empty() || config.target.front() != '/') {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "WebSocket target must be an origin-form path beginning with '/'");
    }
    if (config.host.empty()) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "WebSocket upgrade requires a Host header value");
    }
    if (config.maximumMessageBytes == 0 || config.receiveBufferBytes == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "WebSocket limits must be greater than zero");
    }
    for (const char value : config.target) {
        if (isAsciiControl(value) || value == ' ' || value == '#') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "WebSocket target contains an invalid origin-form character");
        }
    }
    for (const char value : config.host) {
        if (isAsciiControl(value) || value == ' ') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "WebSocket host contains whitespace or a control character");
        }
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, *config.stream};
        impl->clientKey = generateClientKey(impl->entropy);
        impl->expectedAccept = Detail::computeWebSocketAccept(impl->clientKey);
        impl->handshakeBytes.reserve(1024);
        impl->frameBytes.reserve(4096);
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "WebSocket allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "WebSocket construction failed");
    }

    impl->owner = std::this_thread::get_id();
    impl->maximumMessageBytes = config.maximumMessageBytes;
    impl->receiveBufferBytes = config.receiveBufferBytes;
    impl->stallPumpLimit = config.stallPumpLimit;

    // The upgrade request goes out immediately so the exchange is already in
    // flight by the first pump. Keep every allocation and virtual boundary in
    // the Result-based construction contract.
    try {
        std::string request;
        request.reserve(256);
        request.append("GET ");
        request.append(config.target);
        request.append(" HTTP/1.1\r\nHost: ");
        request.append(config.host);
        request.append("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
        request.append(impl->clientKey);
        request.append("\r\nSec-WebSocket-Version: 13\r\n\r\n");

        const auto payload = std::as_bytes(std::span{request.data(), request.size()});
        if (const auto status = impl->stream->sendBytes(payload); !status) {
            const auto error = status.error();
            delete impl;
            return Core::failure(error);
        }
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(NetworkErrorCode::AllocationFailed,
                             "WebSocket upgrade request allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(NetworkErrorCode::BackendFailure,
                             "WebSocket stream threw while sending the upgrade request");
    }

    return WebSocket{impl};
}

WebSocketState WebSocket::state() const noexcept
{
    if (m_impl == nullptr) {
        return WebSocketState::Closed;
    }
    return m_impl->state;
}

Core::Status WebSocket::sendText(std::string_view text)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "WebSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "WebSocket must be used from its owner thread");
    }
    if (m_impl->state != WebSocketState::Open) {
        return Core::failure(
            NetworkErrorCode::WebSocketClosed,
            "WebSocket is not open");
    }
    if (text.size() > m_impl->maximumMessageBytes) {
        return Core::failure(
            NetworkErrorCode::WebSocketMessageTooLarge,
            "WebSocket text exceeds the configured message limit");
    }
    if (!Core::isStrictUtf8(text)) {
        return protocolFailure("WebSocket text payload is not valid UTF-8");
    }
    return m_impl->queueFrame(
        OpcodeText,
        std::as_bytes(std::span{text.data(), text.size()}));
}

Core::Status WebSocket::sendBinary(std::span<const std::byte> payload)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "WebSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "WebSocket must be used from its owner thread");
    }
    if (m_impl->state != WebSocketState::Open) {
        return Core::failure(
            NetworkErrorCode::WebSocketClosed,
            "WebSocket is not open");
    }
    if (payload.size() > m_impl->maximumMessageBytes) {
        return Core::failure(
            NetworkErrorCode::WebSocketMessageTooLarge,
            "WebSocket payload exceeds the configured message limit");
    }
    return m_impl->queueFrame(OpcodeBinary, payload);
}

Core::Result<bool> WebSocket::pump()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "WebSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "WebSocket must be used from its owner thread");
    }

    ++m_impl->stats.pumpCallCount;

    if (m_impl->state == WebSocketState::Failed) {
        return Core::failure(
            NetworkErrorCode::WebSocketProtocolError,
            "WebSocket has failed");
    }
    if (m_impl->state == WebSocketState::Closed) {
        return Core::failure(NetworkErrorCode::WebSocketClosed, "WebSocket is closed");
    }
    if (m_impl->messageReady) {
        // The caller has not consumed the previous message, so no new one may be
        // delivered -- doing so would silently drop it.
        return true;
    }

    if (m_impl->hasStalled()) {
        m_impl->markFailed();
        return Core::failure(
            NetworkErrorCode::WebSocketTimeout,
            "WebSocket made no progress within the stall limit");
    }

    auto pumped = m_impl->stream->pumpStream();
    if (!pumped) {
        m_impl->markFailed();
        return Core::failure(std::move(pumped.error()));
    }

    // A close echo is queued on the borrowed stream. Closed is observable only
    // after every byte accepted by the stream has reached its platform socket;
    // this proves local handoff, not peer delivery, but avoids abandoning a
    // backpressured TCP/TLS queue after an arbitrary single pump.
    if (m_impl->state == WebSocketState::Closing && m_impl->closeReceived
        && m_impl->stream->pendingSendBytes() == 0) {
        m_impl->state = WebSocketState::Closed;
        return false;
    }

    auto buffered = m_impl->stream->peekReceived();
    if (!buffered) {
        m_impl->markFailed();
        return Core::failure(std::move(buffered.error()));
    }

    if (!buffered->empty()) {
        m_impl->noteProgress();
    } else if (!m_impl->closeReceived
        && (m_impl->stream->streamState() == ByteStreamState::PeerClosed
            || m_impl->stream->streamState() == ByteStreamState::Closed)) {
        m_impl->markFailed();
        return protocolFailure("WebSocket transport closed without a close frame");
    }

    if (m_impl->state == WebSocketState::Handshaking) {
        if (buffered->empty()) {
            return false;
        }

        const std::string_view incoming{
            reinterpret_cast<const char*>(buffered->data()),
            buffered->size()};
        const Core::usize searchStart = m_impl->handshakeBytes.size() >= HeaderTerminator.size()
            ? m_impl->handshakeBytes.size() - (HeaderTerminator.size() - 1)
            : 0;
        try {
            m_impl->handshakeBytes.append(incoming);
        } catch (const std::bad_alloc&) {
            m_impl->markFailed();
            return Core::failure(NetworkErrorCode::AllocationFailed,
                                 "WebSocket handshake allocation failed");
        } catch (...) {
            m_impl->markFailed();
            return Core::failure(NetworkErrorCode::ConstructionFailed,
                                 "WebSocket handshake allocation failed");
        }

        if (m_impl->handshakeBytes.size() > m_impl->receiveBufferBytes) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket handshake response exceeded the buffer");
        }

        const std::string_view all{m_impl->handshakeBytes};
        const Core::usize terminator = all.find(HeaderTerminator, searchStart);
        if (terminator == std::string_view::npos) {
            if (const auto status = m_impl->stream->consumeReceived(buffered->size());
                !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
            return false;
        }

        // Anything past the terminator is already frame data.
        const Core::usize frameStart = terminator + HeaderTerminator.size();
        std::string leftover;
        try {
            leftover.assign(all.substr(frameStart));
        } catch (const std::bad_alloc&) {
            m_impl->markFailed();
            return Core::failure(NetworkErrorCode::AllocationFailed,
                                 "WebSocket handshake remainder allocation failed");
        } catch (...) {
            m_impl->markFailed();
            return Core::failure(NetworkErrorCode::ConstructionFailed,
                                 "WebSocket handshake remainder allocation failed");
        }
        const std::string_view head = all.substr(0, terminator + Crlf.size());

        if (const auto status = m_impl->stream->consumeReceived(buffered->size()); !status) {
            m_impl->markFailed();
            return Core::failure(status.error());
        }

        // 101 with a matching accept token is the only success. Anything else --
        // including a 200 -- means the peer did not understand the upgrade.
        const Core::usize statusLineEnd = head.find(Crlf);
        if (statusLineEnd == std::string_view::npos
            || !isSwitchingProtocolsStatus(head.substr(0, statusLineEnd))) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket server did not return 101 Switching Protocols");
        }

        std::string_view accept;
        bool sawUpgrade = false;
        bool sawConnectionUpgrade = false;
        bool sawAccept = false;
        Core::usize cursor = head.find(Crlf);
        cursor = (cursor == std::string_view::npos) ? head.size() : cursor + Crlf.size();
        while (cursor < head.size()) {
            const Core::usize lineEnd = head.find(Crlf, cursor);
            if (lineEnd == std::string_view::npos || lineEnd == cursor) {
                break;
            }
            const std::string_view field = head.substr(cursor, lineEnd - cursor);
            const Core::usize colon = field.find(':');
            if (colon == std::string_view::npos || colon == 0) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketHandshakeFailed,
                    "WebSocket response contains a malformed header");
            }
            const std::string_view name = field.substr(0, colon);
            const std::string_view value = trimOptionalWhitespace(field.substr(colon + 1));
            if (!isValidHeaderName(name) || !isValidHeaderValue(value)) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketHandshakeFailed,
                    "WebSocket response contains invalid HTTP header syntax");
            }
            if (equalsIgnoreAsciiCase(name, "Sec-WebSocket-Accept")) {
                if (sawAccept) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::WebSocketHandshakeFailed,
                        "WebSocket response contains duplicate accept headers");
                }
                sawAccept = true;
                accept = value;
            } else if (equalsIgnoreAsciiCase(name, "Upgrade")) {
                sawUpgrade = sawUpgrade || hasHeaderToken(value, "websocket");
            } else if (equalsIgnoreAsciiCase(name, "Connection")) {
                sawConnectionUpgrade = sawConnectionUpgrade || hasHeaderToken(value, "Upgrade");
            } else if (equalsIgnoreAsciiCase(name, "Sec-WebSocket-Extensions")
                       || equalsIgnoreAsciiCase(name, "Sec-WebSocket-Protocol")) {
                // This client offered neither. Accepting a server-selected value
                // would enable semantics the frame parser does not implement.
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketHandshakeFailed,
                    "WebSocket server selected an extension or subprotocol that was not offered");
            }
            cursor = lineEnd + Crlf.size();
        }

        if (!sawUpgrade || !sawConnectionUpgrade) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket response lacked the required upgrade headers");
        }
        // The token proves the server processed this specific key, which is what
        // makes a cached or replayed response fail.
        if (!sawAccept || accept != m_impl->expectedAccept) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket Sec-WebSocket-Accept did not match the sent key");
        }

        m_impl->state = WebSocketState::Open;
        m_impl->stats.handshakeComplete = true;
        m_impl->noteProgress();

        if (!leftover.empty()) {
            if (leftover.size() > m_impl->receiveBufferBytes - m_impl->frameBytes.size()) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketMessageTooLarge,
                    "WebSocket receive buffer exceeded");
            }
            try {
                const Core::usize offset = m_impl->frameBytes.size();
                m_impl->frameBytes.resize(offset + leftover.size());
                std::memcpy(m_impl->frameBytes.data() + offset, leftover.data(), leftover.size());
            } catch (const std::bad_alloc&) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::AllocationFailed,
                                     "WebSocket frame allocation failed");
            } catch (...) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::ConstructionFailed,
                                     "WebSocket frame allocation failed");
            }
        }
    } else if (!buffered->empty()) {
        if (buffered->size() > m_impl->receiveBufferBytes - m_impl->frameBytes.size()) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketMessageTooLarge,
                "WebSocket receive buffer exceeded");
        }
        try {
            const Core::usize offset = m_impl->frameBytes.size();
            m_impl->frameBytes.resize(offset + buffered->size());
            std::memcpy(m_impl->frameBytes.data() + offset, buffered->data(), buffered->size());
        } catch (const std::bad_alloc&) {
            m_impl->markFailed();
            return Core::failure(NetworkErrorCode::AllocationFailed,
                                 "WebSocket frame allocation failed");
        } catch (...) {
            m_impl->markFailed();
            return Core::failure(NetworkErrorCode::ConstructionFailed,
                                 "WebSocket frame allocation failed");
        }

        if (const auto status = m_impl->stream->consumeReceived(buffered->size()); !status) {
            m_impl->markFailed();
            return Core::failure(status.error());
        }
    }

    // Parse as many frames as are complete. A single read can carry several.
    Core::usize cursor = 0;
    while (true) {
        const Core::usize available = m_impl->frameBytes.size() - cursor;
        if (available < 2) {
            break;
        }

        const auto* raw = reinterpret_cast<const Core::u8*>(m_impl->frameBytes.data()) + cursor;
        const bool fin = (raw[0] & 0x80U) != 0;
        const Core::u8 reserved = raw[0] & 0x70U;
        const Core::u8 opcode = raw[0] & 0x0FU;
        const bool masked = (raw[1] & 0x80U) != 0;
        Core::u64 payloadLength = raw[1] & 0x7FU;
        Core::usize headerSize = 2;

        // Reserved bits are only legal with a negotiated extension, and none is
        // negotiated here.
        if (reserved != 0) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketProtocolError,
                "WebSocket frame set a reserved bit");
        }

        // RFC 6455 requires server-to-client frames to be unmasked. Accepting a
        // mask here would make the payload interpretation depend on a peer-side
        // option and is a protocol violation, not a compatibility feature.
        if (masked) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketProtocolError,
                "WebSocket server frame must not be masked");
        }

        if (payloadLength == 126) {
            if (available < 4) {
                break;
            }
            payloadLength = (static_cast<Core::u64>(raw[2]) << 8)
                | static_cast<Core::u64>(raw[3]);
            if (payloadLength < 126U) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketProtocolError,
                    "WebSocket frame used a non-minimal payload length");
            }
            headerSize = 4;
        } else if (payloadLength == 127) {
            if (available < 10) {
                break;
            }
            payloadLength = 0;
            for (int index = 0; index < 8; ++index) {
                payloadLength = (payloadLength << 8)
                    | static_cast<Core::u64>(raw[2 + static_cast<Core::usize>(index)]);
            }
            // The high bit must be clear per RFC 6455, and anything above the
            // message cap fails regardless.
            if ((payloadLength >> 63) != 0) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketProtocolError,
                    "WebSocket frame declared a negative length");
            }
            if (payloadLength < 65536U) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketProtocolError,
                    "WebSocket frame used a non-minimal payload length");
            }
            headerSize = 10;
        }

        if (isControlOpcode(opcode) && (!fin || payloadLength > 125U)) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketProtocolError,
                "WebSocket control frame was fragmented or oversized");
        }
        if (payloadLength > static_cast<Core::u64>(m_impl->maximumMessageBytes)) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketMessageTooLarge,
                "WebSocket frame exceeds the configured message limit");
        }
        if (headerSize > available
            || payloadLength > static_cast<Core::u64>(available - headerSize)) {
            break;
        }

        const Core::u8* payload = raw + headerSize;
        const std::byte* payloadBytes = reinterpret_cast<const std::byte*>(payload);

        const Core::usize payloadSize = static_cast<Core::usize>(payloadLength);
        cursor += headerSize + payloadSize;
        ++m_impl->stats.receivedFrameCount;
        m_impl->noteProgress();

        switch (opcode) {
        case OpcodeClose: {
            Core::u16 code = 1005;  // no status received
            if (payloadSize == 1U) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketProtocolError,
                    "WebSocket close frame has a one-byte payload");
            }
            if (payloadSize >= 2U) {
                code = static_cast<Core::u16>(
                    (static_cast<Core::u16>(static_cast<Core::u8>(payloadBytes[0])) << 8)
                    | static_cast<Core::u8>(payloadBytes[1]));
                if (!isValidCloseCode(code)) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::WebSocketProtocolError,
                        "WebSocket close frame has an invalid status code");
                }
                const std::string_view reason{
                    reinterpret_cast<const char*>(payloadBytes + 2),
                    payloadSize - 2U};
                if (!Core::isStrictUtf8(reason)) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::WebSocketProtocolError,
                        "WebSocket close reason is not valid UTF-8");
                }
            }
            m_impl->stats.peerCloseCode = code;

            if (!m_impl->closeSent) {
                // Echo the close so the peer sees an orderly shutdown.
                if (const auto status = m_impl->queueFrame(
                    OpcodeClose,
                    std::span<const std::byte>{payloadBytes,
                                               payloadSize});
                    !status) {
                    m_impl->markFailed();
                    return Core::failure(status.error());
                }
                m_impl->closeSent = true;
            }
            m_impl->closeReceived = true;
            m_impl->state = WebSocketState::Closing;
            eraseConsumed(cursor);
            return false;
        }
        case OpcodePing: {
            // A pong must echo the ping payload exactly.
            if (const auto status = m_impl->queueFrame(
                    OpcodePong,
                    std::span<const std::byte>{payloadBytes,
                                               static_cast<Core::usize>(payloadLength)});
                !status) {
                m_impl->markFailed();
                return Core::failure(status.error());
            }
            ++m_impl->stats.pongCount;
            break;
        }
        case OpcodePong:
            break;
        case OpcodeText:
        case OpcodeBinary: {
            if (m_impl->assemblingMessage) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketProtocolError,
                    "WebSocket data frame arrived while a message was fragmented");
            }
            m_impl->messageKind = (opcode == OpcodeText)
                ? WebSocketMessageKind::Text
                : WebSocketMessageKind::Binary;
            if (payloadSize > m_impl->maximumMessageBytes) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketMessageTooLarge,
                    "WebSocket message exceeds the configured limit");
            }
            try {
                m_impl->messageBytes.assign(payloadBytes, payloadBytes + payloadSize);
            } catch (const std::bad_alloc&) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::AllocationFailed,
                                     "WebSocket message allocation failed");
            } catch (...) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::ConstructionFailed,
                                     "WebSocket message allocation failed");
            }
            m_impl->stats.receivedPayloadBytes += payloadLength;

            if (fin) {
                const char* textData = m_impl->messageBytes.empty()
                    ? ""
                    : reinterpret_cast<const char*>(m_impl->messageBytes.data());
                if (opcode == OpcodeText
                    && !Core::isStrictUtf8(std::string_view{
                        textData,
                        m_impl->messageBytes.size()})) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::WebSocketProtocolError,
                        "WebSocket text payload is not valid UTF-8");
                }
                m_impl->messageReady = true;
            } else {
                m_impl->assemblingMessage = true;
                m_impl->assemblingKind = m_impl->messageKind;
            }
            break;
        }
        case OpcodeContinuation: {
            if (!m_impl->assemblingMessage) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketProtocolError,
                    "WebSocket continuation frame arrived with no message in progress");
            }
            if (payloadSize > m_impl->maximumMessageBytes - m_impl->messageBytes.size()) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketMessageTooLarge,
                    "WebSocket reassembled message exceeds the configured limit");
            }
            const Core::usize offset = m_impl->messageBytes.size();
            try {
                m_impl->messageBytes.resize(offset + payloadSize);
                std::memcpy(m_impl->messageBytes.data() + offset, payloadBytes, payloadSize);
            } catch (const std::bad_alloc&) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::AllocationFailed,
                                     "WebSocket message allocation failed");
            } catch (...) {
                m_impl->markFailed();
                return Core::failure(NetworkErrorCode::ConstructionFailed,
                                     "WebSocket message allocation failed");
            }
            m_impl->stats.receivedPayloadBytes += payloadLength;
            ++m_impl->stats.reassembledFragmentCount;

            if (fin) {
                m_impl->messageKind = m_impl->assemblingKind;
                m_impl->assemblingMessage = false;
                const char* textData = m_impl->messageBytes.empty()
                    ? ""
                    : reinterpret_cast<const char*>(m_impl->messageBytes.data());
                if (m_impl->messageKind == WebSocketMessageKind::Text
                    && !Core::isStrictUtf8(std::string_view{
                        textData,
                        m_impl->messageBytes.size()})) {
                    m_impl->markFailed();
                    return Core::failure(
                        NetworkErrorCode::WebSocketProtocolError,
                        "WebSocket text payload is not valid UTF-8");
                }
                m_impl->messageReady = true;
            }
            break;
        }
        default:
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketProtocolError,
                "WebSocket frame used an unknown opcode");
        }

        if (m_impl->messageReady) {
            break;
        }
    }

    eraseConsumed(cursor);
    return m_impl->messageReady;
}

// Drops the parsed prefix and compacts the remainder to the front.
void WebSocket::eraseConsumed(Core::usize byteCount) noexcept
{
    if (m_impl == nullptr || byteCount == 0) {
        return;
    }
    const Core::usize remaining = m_impl->frameBytes.size() - byteCount;
    if (remaining != 0) {
        std::memmove(
            m_impl->frameBytes.data(),
            m_impl->frameBytes.data() + byteCount,
            remaining);
    }
    m_impl->frameBytes.resize(remaining);
}

Core::Result<WebSocketMessage> WebSocket::message() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "WebSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "WebSocket must be used from its owner thread");
    }
    if (!m_impl->messageReady) {
        return Core::failure(
            NetworkErrorCode::WebSocketProtocolError,
            "WebSocket has no complete message available");
    }

    WebSocketMessage result{};
    result.kind = m_impl->messageKind;
    result.payload = std::span<const std::byte>{
        m_impl->messageBytes.data(),
        m_impl->messageBytes.size()};
    return result;
}

Core::Status WebSocket::consumeMessage()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "WebSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "WebSocket must be used from its owner thread");
    }
    m_impl->messageReady = false;
    m_impl->messageBytes.clear();
    return Core::success();
}

Core::Status WebSocket::close(Core::u16 code, std::string_view reason)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "WebSocket is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "WebSocket must be used from its owner thread");
    }
    if (m_impl->state == WebSocketState::Closed || m_impl->state == WebSocketState::Failed) {
        return Core::success();
    }
    if (m_impl->closeSent) {
        return Core::success();
    }
    if (m_impl->state != WebSocketState::Open) {
        return Core::failure(
            NetworkErrorCode::WebSocketClosed,
            "WebSocket cannot close before the handshake is complete");
    }
    if (!isValidCloseCode(code)) {
        return protocolFailure("WebSocket close frame has an invalid status code");
    }
    // A close payload is a two-byte code plus an optional reason, capped at 125
    // bytes total because it is a control frame.
    if (reason.size() > 123) {
        return protocolFailure("WebSocket close reason exceeds the control frame limit");
    }
    if (!Core::isStrictUtf8(reason)) {
        return protocolFailure("WebSocket close reason is not valid UTF-8");
    }

    std::array<std::byte, 125> payload{};
    const Core::usize payloadSize = 2U + reason.size();
    payload[0] = static_cast<std::byte>((code >> 8) & 0xFFU);
    payload[1] = static_cast<std::byte>(code & 0xFFU);
    if (!reason.empty()) {
        std::memcpy(payload.data() + 2, reason.data(), reason.size());
    }

    if (const auto status = m_impl->queueFrame(
            OpcodeClose,
            std::span<const std::byte>{payload.data(), payloadSize});
        !status) {
        return Core::failure(status.error());
    }
    m_impl->closeSent = true;
    m_impl->state = WebSocketState::Closing;
    return Core::success();
}

WebSocketStatistics WebSocket::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

} // namespace Tina::Network
