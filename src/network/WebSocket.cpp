#include <tina/network/WebSocket.hpp>

#include "detail/WebSocketHandshake.hpp"

#include <tina/network/NetworkErrors.hpp>

#include <algorithm>
#include <chrono>
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

// Sec-WebSocket-Key is 16 random bytes, base64-encoded. It is not a secret: its
// only job is to make a cached HTTP response fail the accept check, so a
// non-cryptographic generator is adequate and avoids pulling one in.
[[nodiscard]] std::string generateClientKey()
{
    const auto seed = static_cast<Core::u64>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::mt19937_64 rng{seed ^ 0x9E3779B97F4A7C15ULL};

    std::array<std::byte, 16> nonce{};
    for (Core::usize index = 0; index < nonce.size(); index += 8) {
        const Core::u64 value = rng();
        for (Core::usize byte = 0; byte < 8 && index + byte < nonce.size(); ++byte) {
            nonce[index + byte] = static_cast<std::byte>((value >> (byte * 8)) & 0xFFU);
        }
    }
    return Detail::base64Encode(nonce);
}

} // namespace

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

    WebSocketStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    void markFailed() noexcept { state = WebSocketState::Failed; }

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

        // A fresh mask per frame, as required: reusing one would leak plaintext
        // structure across frames.
        const auto seed = static_cast<Core::u64>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::mt19937_64 rng{seed ^ (0xD1B54A32D192ED03ULL + stats.sentFrameCount)};
        const Core::u64 maskValue = rng();
        std::array<std::byte, MaskBytes> mask{};
        for (Core::usize index = 0; index < MaskBytes; ++index) {
            mask[index] = static_cast<std::byte>((maskValue >> (index * 8)) & 0xFFU);
            header[headerSize + index] = mask[index];
        }
        headerSize += MaskBytes;

        try {
            sendScratch.resize(headerSize + length);
        } catch (const std::bad_alloc&) {
            return Core::failure(
                NetworkErrorCode::AllocationFailed,
                "WebSocket frame allocation failed");
        }

        std::memcpy(sendScratch.data(), header.data(), headerSize);
        for (Core::usize index = 0; index < length; ++index) {
            sendScratch[headerSize + index] = payload[index]
                ^ mask[index % MaskBytes];
        }

        if (const auto status = stream->sendBytes(sendScratch); !status) {
            return Core::failure(status.error());
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
        if (value == '\r' || value == '\n' || value == ' ') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "WebSocket target must not contain whitespace or CRLF");
        }
    }
    for (const char value : config.host) {
        if (value == '\r' || value == '\n') {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "WebSocket host must not contain CRLF");
        }
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, *config.stream};
        impl->clientKey = generateClientKey();
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
    // flight by the first pump.
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
            NetworkErrorCode::HttpTimeout,
            "WebSocket made no progress within the stall limit");
    }

    auto pumped = m_impl->stream->pumpStream();
    if (!pumped) {
        m_impl->markFailed();
        return Core::failure(std::move(pumped.error()));
    }

    auto buffered = m_impl->stream->peekReceived();
    if (!buffered) {
        m_impl->markFailed();
        return Core::failure(std::move(buffered.error()));
    }

    if (!buffered->empty()) {
        m_impl->noteProgress();
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
        m_impl->handshakeBytes.append(incoming);

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
        std::string leftover{all.substr(frameStart)};
        const std::string_view head = all.substr(0, terminator + Crlf.size());

        if (const auto status = m_impl->stream->consumeReceived(buffered->size()); !status) {
            m_impl->markFailed();
            return Core::failure(status.error());
        }

        // 101 with a matching accept token is the only success. Anything else --
        // including a 200 -- means the peer did not understand the upgrade.
        if (!head.starts_with("HTTP/1.1 101")) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket server did not return 101 Switching Protocols");
        }

        std::string_view accept;
        bool sawUpgrade = false;
        Core::usize cursor = head.find(Crlf);
        cursor = (cursor == std::string_view::npos) ? head.size() : cursor + Crlf.size();
        while (cursor < head.size()) {
            const Core::usize lineEnd = head.find(Crlf, cursor);
            if (lineEnd == std::string_view::npos || lineEnd == cursor) {
                break;
            }
            const std::string_view field = head.substr(cursor, lineEnd - cursor);
            const Core::usize colon = field.find(':');
            if (colon != std::string_view::npos) {
                const std::string_view name = field.substr(0, colon);
                const std::string_view value = trimOptionalWhitespace(field.substr(colon + 1));
                if (equalsIgnoreAsciiCase(name, "Sec-WebSocket-Accept")) {
                    accept = value;
                } else if (equalsIgnoreAsciiCase(name, "Upgrade")) {
                    sawUpgrade = equalsIgnoreAsciiCase(value, "websocket");
                }
            }
            cursor = lineEnd + Crlf.size();
        }

        if (!sawUpgrade) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket response lacked an Upgrade: websocket header");
        }
        // The token proves the server processed this specific key, which is what
        // makes a cached or replayed response fail.
        if (accept != m_impl->expectedAccept) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketHandshakeFailed,
                "WebSocket Sec-WebSocket-Accept did not match the sent key");
        }

        m_impl->state = WebSocketState::Open;
        m_impl->stats.handshakeComplete = true;
        m_impl->noteProgress();

        if (!leftover.empty()) {
            const Core::usize offset = m_impl->frameBytes.size();
            m_impl->frameBytes.resize(offset + leftover.size());
            std::memcpy(m_impl->frameBytes.data() + offset, leftover.data(), leftover.size());
        }
    } else if (!buffered->empty()) {
        if (m_impl->frameBytes.size() + buffered->size() > m_impl->receiveBufferBytes) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketMessageTooLarge,
                "WebSocket receive buffer exceeded");
        }
        const Core::usize offset = m_impl->frameBytes.size();
        m_impl->frameBytes.resize(offset + buffered->size());
        std::memcpy(m_impl->frameBytes.data() + offset, buffered->data(), buffered->size());

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

        if (payloadLength == 126) {
            if (available < 4) {
                break;
            }
            payloadLength = (static_cast<Core::u64>(raw[2]) << 8)
                | static_cast<Core::u64>(raw[3]);
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
            headerSize = 10;
        }

        // A server must not mask, but tolerate and skip the key if present rather
        // than mis-parsing the payload.
        if (masked) {
            headerSize += MaskBytes;
        }

        if (payloadLength > m_impl->maximumMessageBytes) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketMessageTooLarge,
                "WebSocket frame exceeds the configured message limit");
        }
        if (available < headerSize + payloadLength) {
            break;
        }

        // Control frames must not be fragmented and are capped at 125 bytes.
        if (isControlOpcode(opcode) && (!fin || payloadLength > 125)) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::WebSocketProtocolError,
                "WebSocket control frame was fragmented or oversized");
        }

        const Core::u8* payload = raw + headerSize;
        std::vector<std::byte> unmasked;
        if (masked) {
            unmasked.resize(static_cast<Core::usize>(payloadLength));
            const Core::u8* mask = raw + headerSize - MaskBytes;
            for (Core::usize index = 0; index < unmasked.size(); ++index) {
                unmasked[index] = static_cast<std::byte>(
                    payload[index] ^ mask[index % MaskBytes]);
            }
        }
        const std::byte* payloadBytes = masked
            ? unmasked.data()
            : reinterpret_cast<const std::byte*>(payload);

        cursor += headerSize + static_cast<Core::usize>(payloadLength);
        ++m_impl->stats.receivedFrameCount;
        m_impl->noteProgress();

        switch (opcode) {
        case OpcodeClose: {
            Core::u16 code = 1005;  // no status received
            if (payloadLength >= 2) {
                code = static_cast<Core::u16>(
                    (static_cast<Core::u16>(static_cast<Core::u8>(payloadBytes[0])) << 8)
                    | static_cast<Core::u8>(payloadBytes[1]));
            }
            m_impl->stats.peerCloseCode = code;

            if (!m_impl->closeSent) {
                // Echo the close so the peer sees an orderly shutdown.
                (void)m_impl->queueFrame(
                    OpcodeClose,
                    std::span<const std::byte>{payloadBytes,
                                               static_cast<Core::usize>(payloadLength)});
                m_impl->closeSent = true;
            }
            m_impl->state = WebSocketState::Closed;
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
            m_impl->messageBytes.assign(
                payloadBytes,
                payloadBytes + static_cast<Core::usize>(payloadLength));
            m_impl->stats.receivedPayloadBytes += payloadLength;

            if (fin) {
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
            if (m_impl->messageBytes.size() + payloadLength > m_impl->maximumMessageBytes) {
                m_impl->markFailed();
                return Core::failure(
                    NetworkErrorCode::WebSocketMessageTooLarge,
                    "WebSocket reassembled message exceeds the configured limit");
            }
            const Core::usize offset = m_impl->messageBytes.size();
            m_impl->messageBytes.resize(offset + static_cast<Core::usize>(payloadLength));
            std::memcpy(
                m_impl->messageBytes.data() + offset,
                payloadBytes,
                static_cast<Core::usize>(payloadLength));
            m_impl->stats.receivedPayloadBytes += payloadLength;
            ++m_impl->stats.reassembledFragmentCount;

            if (fin) {
                m_impl->messageKind = m_impl->assemblingKind;
                m_impl->assemblingMessage = false;
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
    // A close payload is a two-byte code plus an optional reason, capped at 125
    // bytes total because it is a control frame.
    if (reason.size() > 123) {
        return Core::failure(
            NetworkErrorCode::WebSocketProtocolError,
            "WebSocket close reason exceeds the control frame limit");
    }

    std::vector<std::byte> payload;
    payload.resize(2 + reason.size());
    payload[0] = static_cast<std::byte>((code >> 8) & 0xFFU);
    payload[1] = static_cast<std::byte>(code & 0xFFU);
    if (!reason.empty()) {
        std::memcpy(payload.data() + 2, reason.data(), reason.size());
    }

    if (const auto status = m_impl->queueFrame(OpcodeClose, payload); !status) {
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
