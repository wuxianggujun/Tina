// Drives WebSocket against a scripted server that speaks RFC 6455 by hand. The
// server emits bytes the test chooses, which is the only way to cover frames a
// conforming peer would never send -- a reserved bit set, an unknown opcode, a
// fragmented control frame, a mismatched accept token.

#include "detail/NativeSocket.hpp"
#include "detail/WebSocketHandshake.hpp"

#include <tina/network/NetworkErrors.hpp>
#include <tina/network/TcpConnection.hpp>
#include <tina/network/WebSocket.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace Tina::Tests {
namespace {

using Network::WebSocket;
using Network::WebSocketConfig;
using Network::WebSocketMessageKind;
using Network::WebSocketState;

// Accepts one connection, completes (or deliberately botches) the upgrade, then
// sends whatever frames the test queued.
class ScriptedWebSocketServer final {
  public:
    ScriptedWebSocketServer()
    {
        const auto status = Network::Detail::TransportScope::acquire();
        m_scoped = status.has_value();

        m_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listen == Network::Detail::InvalidNativeSocket) {
            return;
        }
        (void)Network::Detail::setNativeSocketNonBlocking(m_listen);

        const Network::NetworkEndpoint local{Network::IpAddress::v4Loopback(), 0};
        sockaddr_storage address{};
        const auto length = Network::Detail::toNativeAddress(local, address);
        if (::bind(m_listen, reinterpret_cast<const sockaddr*>(&address), length) != 0
            || ::listen(m_listen, 4) != 0) {
            close();
            return;
        }

        sockaddr_storage bound{};
        auto boundLength = static_cast<Network::Detail::NativeAddressLength>(sizeof(bound));
        if (::getsockname(m_listen, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
            (void)Network::Detail::fromNativeAddress(bound, m_endpoint);
        }
    }

    ~ScriptedWebSocketServer() { close(); }

    ScriptedWebSocketServer(const ScriptedWebSocketServer&) = delete;
    ScriptedWebSocketServer& operator=(const ScriptedWebSocketServer&) = delete;

    [[nodiscard]] bool isValid() const noexcept
    {
        return m_listen != Network::Detail::InvalidNativeSocket;
    }
    [[nodiscard]] const Network::NetworkEndpoint& endpoint() const noexcept { return m_endpoint; }
    [[nodiscard]] const std::string& request() const noexcept { return m_request; }
    [[nodiscard]] const std::string& receivedFrames() const noexcept { return m_received; }
    [[nodiscard]] bool upgraded() const noexcept { return m_upgraded; }

    // Replaces the 101 response with arbitrary bytes, for the negative cases.
    void setHandshakeOverride(std::string response) { m_handshakeOverride = std::move(response); }

    // Corrupts the accept token so the client must reject an otherwise valid 101.
    void setCorruptAccept(bool value) noexcept { m_corruptAccept = value; }

    // Frames to write once the upgrade is done, in order.
    void queueRaw(std::string bytes) { m_outgoing.append(bytes); }

    // Builds an unmasked server frame, which is what RFC 6455 requires of a server.
    void queueFrame(Core::u8 opcode, std::string_view payload, bool fin = true)
    {
        std::string frame;
        frame.push_back(static_cast<char>((fin ? 0x80U : 0x00U) | opcode));
        if (payload.size() < 126) {
            frame.push_back(static_cast<char>(payload.size()));
        } else {
            frame.push_back(static_cast<char>(126));
            frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFFU));
            frame.push_back(static_cast<char>(payload.size() & 0xFFU));
        }
        frame.append(payload);
        m_outgoing.append(frame);
    }

    void pump()
    {
        if (m_accepted == Network::Detail::InvalidNativeSocket) {
            const auto accepted = ::accept(m_listen, nullptr, nullptr);
            if (accepted == Network::Detail::InvalidNativeSocket) {
                return;
            }
            m_accepted = accepted;
            (void)Network::Detail::setNativeSocketNonBlocking(m_accepted);
        }

        char scratch[4096];
#if defined(_WIN32)
        const int got = ::recv(m_accepted, scratch, static_cast<int>(sizeof(scratch)), 0);
#else
        const ssize_t got = ::recv(m_accepted, scratch, sizeof(scratch), 0);
#endif
        if (got > 0) {
            if (!m_upgraded) {
                m_request.append(scratch, static_cast<Core::usize>(got));
            } else {
                m_received.append(scratch, static_cast<Core::usize>(got));
            }
        }

        if (!m_upgraded && m_request.find("\r\n\r\n") != std::string::npos) {
            send(buildHandshakeResponse());
            m_upgraded = true;
        }

        if (m_upgraded && !m_outgoing.empty()) {
            send(m_outgoing);
            m_outgoing.clear();
        }
    }

  private:
    [[nodiscard]] std::string buildHandshakeResponse() const
    {
        if (!m_handshakeOverride.empty()) {
            return m_handshakeOverride;
        }

        // Echo back the accept token derived from the client's own key, which is
        // what the client verifies.
        const std::string key = extractHeader("Sec-WebSocket-Key");
        std::string accept = Network::Detail::computeWebSocketAccept(key);
        if (m_corruptAccept && !accept.empty()) {
            accept[0] = (accept[0] == 'A') ? 'B' : 'A';
        }

        std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
        response += "Upgrade: websocket\r\n";
        response += "Connection: Upgrade\r\n";
        response += "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        return response;
    }

    [[nodiscard]] std::string extractHeader(std::string_view name) const
    {
        const std::string needle = std::string{name} + ": ";
        const Core::usize start = m_request.find(needle);
        if (start == std::string::npos) {
            return {};
        }
        const Core::usize valueStart = start + needle.size();
        const Core::usize end = m_request.find("\r\n", valueStart);
        if (end == std::string::npos) {
            return {};
        }
        return m_request.substr(valueStart, end - valueStart);
    }

    void send(const std::string& bytes) const
    {
        if (bytes.empty()) {
            return;
        }
#if defined(_WIN32)
        ::send(m_accepted, bytes.data(), static_cast<int>(bytes.size()), 0);
#else
        ::send(m_accepted, bytes.data(), bytes.size(), 0);
#endif
    }

    void close() noexcept
    {
        Network::Detail::closeNativeSocket(m_accepted);
        m_accepted = Network::Detail::InvalidNativeSocket;
        Network::Detail::closeNativeSocket(m_listen);
        m_listen = Network::Detail::InvalidNativeSocket;
        if (m_scoped) {
            Network::Detail::TransportScope::release();
            m_scoped = false;
        }
    }

    Network::Detail::NativeSocket m_listen = Network::Detail::InvalidNativeSocket;
    Network::Detail::NativeSocket m_accepted = Network::Detail::InvalidNativeSocket;
    Network::NetworkEndpoint m_endpoint{};
    std::string m_request;
    std::string m_received;
    std::string m_outgoing;
    std::string m_handshakeOverride;
    bool m_upgraded = false;
    bool m_corruptAccept = false;
    bool m_scoped = false;
};

[[nodiscard]] Core::Result<Network::TcpConnection> connectTo(
    const Network::NetworkEndpoint& endpoint)
{
    Network::TcpConnectionConfig config{};
    config.remoteEndpoint = endpoint;
    config.sendBufferBytes = 256 * 1024;
    config.receiveBufferBytes = 256 * 1024;
    return Network::TcpConnection::Create(config);
}

[[nodiscard]] WebSocketConfig configFor(Network::IByteStream& stream)
{
    WebSocketConfig config{};
    config.stream = &stream;
    config.target = "/socket";
    config.host = "tina.test";
    return config;
}

// Interleaves both sides until the socket opens or the budget runs out.
[[nodiscard]] bool openSocket(
    WebSocket& socket,
    ScriptedWebSocketServer& server,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        server.pump();
        const auto pumped = socket.pump();
        if (!pumped) {
            return false;
        }
        if (socket.state() == WebSocketState::Open) {
            return true;
        }
        if (socket.state() == WebSocketState::Failed) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

// Pumps until a message is delivered, returning its payload as text.
[[nodiscard]] std::string awaitMessage(
    WebSocket& socket,
    ScriptedWebSocketServer& server,
    WebSocketMessageKind& kindOut,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        server.pump();
        const auto pumped = socket.pump();
        if (!pumped) {
            return {};
        }
        if (*pumped) {
            const auto message = socket.message();
            if (!message) {
                return {};
            }
            kindOut = message->kind;
            std::string payload{
                reinterpret_cast<const char*>(message->payload.data()),
                message->payload.size()};
            (void)socket.consumeMessage();
            return payload;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return {};
}

// Drives until the client reports an error, returning its code.
[[nodiscard]] Core::ErrorCode driveUntilError(
    WebSocket& socket,
    ScriptedWebSocketServer& server,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        server.pump();
        const auto pumped = socket.pump();
        if (!pumped) {
            return pumped.error().code;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return Core::ErrorCode{};
}

} // namespace

TEST(WebSocketTest, RejectsInvalidConfiguration)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    {
        WebSocketConfig config{};
        config.target = "/socket";
        config.host = "tina.test";
        // No stream: nowhere to send the upgrade.
        const auto socket = WebSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = configFor(*stream);
        config.target = "socket";  // missing leading slash
        const auto socket = WebSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = configFor(*stream);
        config.host = {};
        const auto socket = WebSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = configFor(*stream);
        config.target = "/a\r\nX-Injected: 1";
        const auto socket = WebSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
}

TEST(WebSocketTest, CompletesUpgradeHandshake)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    EXPECT_EQ(socket->state(), WebSocketState::Handshaking);

    ASSERT_TRUE(openSocket(*socket, server));
    EXPECT_EQ(socket->state(), WebSocketState::Open);
    EXPECT_TRUE(socket->statistics().handshakeComplete);

    // The request must carry the mandatory upgrade fields.
    EXPECT_TRUE(server.request().starts_with("GET /socket HTTP/1.1\r\n"));
    EXPECT_NE(server.request().find("Upgrade: websocket\r\n"), std::string::npos);
    EXPECT_NE(server.request().find("Sec-WebSocket-Version: 13\r\n"), std::string::npos);
    EXPECT_NE(server.request().find("Sec-WebSocket-Key: "), std::string::npos);
}

// The accept token proves the server processed this specific key. A wrong token
// means a cached or replayed response, which must not be accepted.
TEST(WebSocketTest, RejectsMismatchedAcceptToken)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    server.setCorruptAccept(true);
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketHandshakeFailed);
    EXPECT_EQ(socket->state(), WebSocketState::Failed);
}

// A 200 means the peer treated the upgrade as an ordinary request.
TEST(WebSocketTest, RejectsNonUpgradeResponse)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    server.setHandshakeOverride("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketHandshakeFailed);
}

// A 101 without the Upgrade header is malformed, even with a valid token.
TEST(WebSocketTest, RejectsResponseMissingUpgradeHeader)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    server.setHandshakeOverride(
        "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\n\r\n");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketHandshakeFailed);
}

TEST(WebSocketTest, ReceivesTextMessage)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x1, "hello tina");

    WebSocketMessageKind kind{};
    EXPECT_EQ(awaitMessage(*socket, server, kind), "hello tina");
    EXPECT_EQ(kind, WebSocketMessageKind::Text);
    EXPECT_EQ(socket->statistics().receivedPayloadBytes, 10U);
}

TEST(WebSocketTest, ReceivesBinaryMessage)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x2, std::string("\x01\x02\x03\x04", 4));

    WebSocketMessageKind kind{};
    const std::string payload = awaitMessage(*socket, server, kind);
    ASSERT_EQ(payload.size(), 4U);
    // Text and binary must stay distinguishable: a binary payload is not text.
    EXPECT_EQ(kind, WebSocketMessageKind::Binary);
    EXPECT_EQ(static_cast<unsigned char>(payload[0]), 0x01);
    EXPECT_EQ(static_cast<unsigned char>(payload[3]), 0x04);
}

// A fragmented message must be reassembled in order, and only the final frame
// delivers it.
TEST(WebSocketTest, ReassemblesFragmentedMessage)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x1, "frag", /*fin=*/false);
    server.queueFrame(0x0, "mented", /*fin=*/true);

    WebSocketMessageKind kind{};
    EXPECT_EQ(awaitMessage(*socket, server, kind), "fragmented");
    EXPECT_EQ(kind, WebSocketMessageKind::Text);
    EXPECT_GT(socket->statistics().reassembledFragmentCount, 0U);
}

// A continuation with nothing in progress is a protocol error, not an empty
// message.
TEST(WebSocketTest, RejectsOrphanContinuationFrame)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x0, "orphan");

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketProtocolError);
}

// Reserved bits are only legal with a negotiated extension, and none is.
TEST(WebSocketTest, RejectsReservedBit)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    // FIN + RSV1 + text, two-byte payload.
    server.queueRaw(std::string("\xC1\x02hi", 4));

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketProtocolError);
}

TEST(WebSocketTest, RejectsUnknownOpcode)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    // Opcode 0x3 is reserved for future data frames.
    server.queueFrame(0x3, "x");

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketProtocolError);
}

// Control frames must not be fragmented: they have to be deliverable immediately.
TEST(WebSocketTest, RejectsFragmentedControlFrame)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x9, "ping", /*fin=*/false);

    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketProtocolError);
}

// A ping must be answered with a pong carrying the identical payload.
TEST(WebSocketTest, AnswersPingWithMatchingPong)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x9, "keepalive");

    for (int attempt = 0; attempt < 4000 && socket->statistics().pongCount == 0; ++attempt) {
        server.pump();
        ASSERT_TRUE(socket->pump().has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_EQ(socket->statistics().pongCount, 1U);

    // Drain so the pong reaches the server, then check the opcode and payload.
    for (int attempt = 0; attempt < 400; ++attempt) {
        server.pump();
        ASSERT_TRUE(socket->pump().has_value());
    }

    const std::string& frames = server.receivedFrames();
    ASSERT_GE(frames.size(), 2U);
    // Client frames are masked, so the payload is not directly comparable; the
    // opcode byte is, and it must be a FIN pong.
    EXPECT_EQ(static_cast<unsigned char>(frames[0]), 0x8A);
    EXPECT_NE(static_cast<unsigned char>(frames[1]) & 0x80U, 0U)
        << "client frames must be masked";
}

TEST(WebSocketTest, SendsMaskedTextFrame)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    ASSERT_TRUE(socket->sendText("client speaking").has_value());

    for (int attempt = 0; attempt < 4000 && server.receivedFrames().size() < 6; ++attempt) {
        server.pump();
        ASSERT_TRUE(socket->pump().has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    const std::string& frames = server.receivedFrames();
    ASSERT_GE(frames.size(), 2U);
    EXPECT_EQ(static_cast<unsigned char>(frames[0]), 0x81);  // FIN + text
    // A server must fail the connection on an unmasked client frame, so the mask
    // bit is mandatory rather than an optimisation.
    EXPECT_NE(static_cast<unsigned char>(frames[1]) & 0x80U, 0U);
    EXPECT_EQ(static_cast<unsigned char>(frames[1]) & 0x7FU, 15U);
    EXPECT_EQ(socket->statistics().sentPayloadBytes, 15U);
}

TEST(WebSocketTest, SendBeforeOpenIsRejected)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_EQ(socket->state(), WebSocketState::Handshaking);

    const auto status = socket->sendText("too early");
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Network::NetworkErrorCode::WebSocketClosed);
}

TEST(WebSocketTest, RejectsMessageExceedingLimit)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.maximumMessageBytes = 16;
    auto socket = WebSocket::Create(config);
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    // Sending over the cap is refused locally.
    const auto sent = socket->sendText(std::string(32, 'x'));
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error().code, Network::NetworkErrorCode::WebSocketMessageTooLarge);

    // Receiving over the cap fails the connection rather than truncating.
    server.queueFrame(0x1, std::string(32, 'y'));
    EXPECT_EQ(
        driveUntilError(*socket, server),
        Network::NetworkErrorCode::WebSocketMessageTooLarge);
}

// A peer close must yield Closed with the code recorded, and the client echoes it.
TEST(WebSocketTest, PeerCloseFrameYieldsClosedWithCode)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    // 1001 = going away.
    server.queueFrame(0x8, std::string("\x03\xE9", 2));

    for (int attempt = 0; attempt < 4000; ++attempt) {
        server.pump();
        const auto pumped = socket->pump();
        if (!pumped) {
            break;
        }
        if (socket->state() == WebSocketState::Closed) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_EQ(socket->state(), WebSocketState::Closed);
    EXPECT_EQ(socket->statistics().peerCloseCode, 1001);
}

TEST(WebSocketTest, CloseSendsFrameAndIsIdempotent)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    ASSERT_TRUE(socket->close(1000, "done").has_value());
    EXPECT_EQ(socket->state(), WebSocketState::Closing);

    // A second close must not emit another frame.
    const Core::u64 framesAfterFirst = socket->statistics().sentFrameCount;
    ASSERT_TRUE(socket->close().has_value());
    EXPECT_EQ(socket->statistics().sentFrameCount, framesAfterFirst);
}

// The reason travels in a control frame, so it cannot exceed 123 bytes once the
// two-byte code is accounted for.
TEST(WebSocketTest, CloseRejectsOversizedReason)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    const auto status = socket->close(1000, std::string(200, 'r'));
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Network::NetworkErrorCode::WebSocketProtocolError);
}

// An unconsumed message must block delivery of the next one rather than being
// silently overwritten.
TEST(WebSocketTest, UnconsumedMessageBlocksTheNext)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(openSocket(*socket, server));

    server.queueFrame(0x1, "first");
    server.queueFrame(0x1, "second");

    bool ready = false;
    for (int attempt = 0; attempt < 4000 && !ready; ++attempt) {
        server.pump();
        const auto pumped = socket->pump();
        ASSERT_TRUE(pumped.has_value());
        ready = *pumped;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(ready);

    const auto first = socket->message();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(
        std::string(
            reinterpret_cast<const char*>(first->payload.data()),
            first->payload.size()),
        "first");

    // Pumping again keeps reporting the same message until it is consumed.
    const auto again = socket->pump();
    ASSERT_TRUE(again.has_value());
    EXPECT_TRUE(*again);
    const auto stillFirst = socket->message();
    ASSERT_TRUE(stillFirst.has_value());
    EXPECT_EQ(
        std::string(
            reinterpret_cast<const char*>(stillFirst->payload.data()),
            stillFirst->payload.size()),
        "first");

    ASSERT_TRUE(socket->consumeMessage().has_value());

    WebSocketMessageKind kind{};
    EXPECT_EQ(awaitMessage(*socket, server, kind), "second");
}

TEST(WebSocketTest, MessageUnavailableBeforeDelivery)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());

    const auto message = socket->message();
    ASSERT_FALSE(message.has_value());
    EXPECT_EQ(message.error().code, Network::NetworkErrorCode::WebSocketProtocolError);
}

// A server that accepts the connection but never replies must not hang the caller.
TEST(WebSocketTest, StallLimitEndsASilentServer)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    // Empty override means the fixture writes nothing back.
    server.setHandshakeOverride(std::string{" "});
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.stallPumpLimit = 20;
    auto socket = WebSocket::Create(config);
    ASSERT_TRUE(socket.has_value());

    Core::ErrorCode observed{};
    for (int attempt = 0; attempt < 400; ++attempt) {
        const auto pumped = socket->pump();
        if (!pumped) {
            observed = pumped.error().code;
            break;
        }
    }

    EXPECT_EQ(observed, Network::NetworkErrorCode::WebSocketTimeout);
    EXPECT_EQ(socket->state(), WebSocketState::Failed);
}

TEST(WebSocketTest, RejectsUseFromNonOwnerThread)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());

    Core::ErrorCode pumpCode{};
    Core::ErrorCode sendCode{};
    Core::ErrorCode messageCode{};
    Core::ErrorCode closeCode{};

    std::thread other{[&]() {
        if (const auto pumped = socket->pump(); !pumped) {
            pumpCode = pumped.error().code;
        }
        if (const auto status = socket->sendText("x"); !status) {
            sendCode = status.error().code;
        }
        if (const auto message = socket->message(); !message) {
            messageCode = message.error().code;
        }
        if (const auto status = socket->close(); !status) {
            closeCode = status.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(pumpCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(sendCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(messageCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(closeCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(WebSocketTest, MovedFromSocketAnswersQueriesInertly)
{
    ScriptedWebSocketServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto socket = WebSocket::Create(configFor(*stream));
    ASSERT_TRUE(socket.has_value());
    WebSocket moved{std::move(*socket)};

    EXPECT_FALSE(static_cast<bool>(*socket));
    EXPECT_EQ(socket->state(), WebSocketState::Closed);
    EXPECT_EQ(socket->statistics().pumpCallCount, 0U);

    const auto pumped = socket->pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::SocketClosed);

    EXPECT_TRUE(static_cast<bool>(moved));
}

// The handshake primitives are private, so they are exercised here rather than
// through a public surface. The RFC 6455 section 1.3 example pins the expected
// token, which is what makes the SHA-1 and base64 implementations verifiable.
TEST(WebSocketHandshakeTest, ComputesTheRfcExampleAcceptToken)
{
    EXPECT_EQ(
        Network::Detail::computeWebSocketAccept("dGhlIHNhbXBsZSBub25jZQ=="),
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocketHandshakeTest, Sha1MatchesKnownDigests)
{
    const auto empty = Network::Detail::sha1({});
    EXPECT_EQ(
        Network::Detail::base64Encode(std::as_bytes(std::span{empty.data(), empty.size()})),
        "2jmj7l5rSw0yVb/vlWAYkK/YBwk=");

    constexpr std::string_view abc = "abc";
    const auto digest = Network::Detail::sha1(
        std::as_bytes(std::span{abc.data(), abc.size()}));
    EXPECT_EQ(
        Network::Detail::base64Encode(
            std::as_bytes(std::span{digest.data(), digest.size()})),
        "qZk+NkcGgWq6PiVxeFDCbJzQ2J0=");
}

// Multi-block input exercises the padding path that needs a second block.
TEST(WebSocketHandshakeTest, Sha1HandlesBlockBoundaries)
{
    for (const Core::usize length : {Core::usize{55}, Core::usize{56}, Core::usize{64},
                                     Core::usize{119}, Core::usize{120}}) {
        const std::string input(length, 'a');
        const auto digest = Network::Detail::sha1(
            std::as_bytes(std::span{input.data(), input.size()}));
        // Not comparing to a fixed value here: the point is that every length
        // produces a distinct, non-zero digest rather than falling into the
        // padding bug that returns the same block twice.
        bool allZero = true;
        for (const auto byte : digest) {
            if (byte != 0) {
                allZero = false;
                break;
            }
        }
        EXPECT_FALSE(allZero) << "length " << length;
    }
}

TEST(WebSocketHandshakeTest, Base64RoundTrips)
{
    for (const std::string_view input : {"", "f", "fo", "foo", "foob", "fooba", "foobar"}) {
        const auto encoded = Network::Detail::base64Encode(
            std::as_bytes(std::span{input.data(), input.size()}));
        std::string decoded;
        ASSERT_TRUE(Network::Detail::base64Decode(encoded, decoded)) << "input " << input;
        EXPECT_EQ(decoded, input);
    }

    // RFC 4648 test vectors.
    EXPECT_EQ(Network::Detail::base64Encode(std::as_bytes(std::span{"f", 1})), "Zg==");
    EXPECT_EQ(Network::Detail::base64Encode(std::as_bytes(std::span{"fo", 2})), "Zm8=");
    EXPECT_EQ(Network::Detail::base64Encode(std::as_bytes(std::span{"foo", 3})), "Zm9v");
}

TEST(WebSocketHandshakeTest, Base64RejectsMalformedInput)
{
    std::string decoded;
    // Wrong length, illegal character, and padding in the middle.
    EXPECT_FALSE(Network::Detail::base64Decode("abc", decoded));
    EXPECT_FALSE(Network::Detail::base64Decode("ab$d", decoded));
    EXPECT_FALSE(Network::Detail::base64Decode("ab=c", decoded));
    EXPECT_FALSE(Network::Detail::base64Decode("=abc", decoded));
}

} // namespace Tina::Tests
