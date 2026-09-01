// First consumer of Tina::Network outside the tests. Headless: no GPU, no window,
// no EngineHost -- just a bounded task system and one pump loop, which is the
// shape a game's frame loop would use.
//
// A unit test drives one component in isolation. This drives all of them in the
// same loop, in one process, across a fixed number of frames, which is where
// lifetime and ordering mistakes show up instead of staying hidden.
//
// Every peer is on loopback. That covers protocol and lifetime behaviour but says
// nothing about loss, reordering, or path MTU.

#include <tina/core/text/JsonWriter.hpp>
#include <tina/network/DnsResolver.hpp>
#include <tina/network/HttpClient.hpp>
#include <tina/network/NetworkEndpoint.hpp>
#include <tina/network/TcpConnection.hpp>
#include <tina/network/TcpListener.hpp>
#include <tina/network/UdpSocket.hpp>
#include <tina/network/WebSocket.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace Tina;

// Bumped whenever a field is added, removed or redefined, so a gate cannot
// silently compare against a different contract.
inline constexpr Core::u32 EvidenceSchema = 1;

inline constexpr Core::u32 DefaultFrameCount = 300;

// What the run observed. Everything a gate asserts on comes from here rather than
// from intent, so a field can only be true if the traffic actually happened.
struct Evidence final {
    Core::u32 frames = 0;

    // UDP
    Core::u64 udpDatagramsSent = 0;
    Core::u64 udpDatagramsReceived = 0;
    bool udpSenderEndpointMatched = false;

    // DNS
    bool dnsResolvedNumericLiteral = false;
    bool dnsRejectedUnresolvableName = false;
    Core::u64 dnsPumpCount = 0;

    // TCP listener plus an accepted connection
    Core::u64 tcpConnectionsAccepted = 0;
    bool tcpClientToServerMatched = false;
    bool tcpServerToClientMatched = false;

    // HTTP over the accepted connection's peer
    bool httpRequestCompleted = false;
    Core::u16 httpStatusCode = 0;
    bool httpBodyMatched = false;

    // WebSocket over its own connection
    bool webSocketHandshakeCompleted = false;
    bool webSocketEchoMatched = false;
    Core::u64 webSocketFramesSent = 0;
};

[[nodiscard]] std::span<const std::byte> asBytes(std::string_view text) noexcept
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] std::string asText(std::span<const std::byte> bytes)
{
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Accepts zero or one --frames=N. A malformed argument is a usage error rather
// than something to guess at.
[[nodiscard]] std::optional<Core::u32> parseFrameCount(int argumentCount, char** arguments)
{
    if (argumentCount == 1) {
        return DefaultFrameCount;
    }
    if (argumentCount != 2) {
        return std::nullopt;
    }

    const std::string_view argument{arguments[1]};
    constexpr std::string_view prefix = "--frames=";
    if (!argument.starts_with(prefix)) {
        return std::nullopt;
    }

    const std::string_view digits = argument.substr(prefix.size());
    Core::u32 value = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

void writeEvidence(std::ostream& stream, std::string_view status, const Evidence& evidence)
{
    // Emitted identically on success and failure so a failing run is diagnosable
    // from the same output shape.
    Core::JsonWriter writer(stream);
    writer.beginObject();
    writer.member("status", status);
    writer.member("sample", "tina_sample_network");
    writer.member("evidenceSchema", EvidenceSchema);
    writer.member("frames", evidence.frames);
    writer.member("udpDatagramsSent", evidence.udpDatagramsSent);
    writer.member("udpDatagramsReceived", evidence.udpDatagramsReceived);
    writer.member("udpSenderEndpointMatched", evidence.udpSenderEndpointMatched);
    writer.member("dnsResolvedNumericLiteral", evidence.dnsResolvedNumericLiteral);
    writer.member("dnsRejectedUnresolvableName", evidence.dnsRejectedUnresolvableName);
    writer.member("dnsPumpCount", evidence.dnsPumpCount);
    writer.member("tcpConnectionsAccepted", evidence.tcpConnectionsAccepted);
    writer.member("tcpClientToServerMatched", evidence.tcpClientToServerMatched);
    writer.member("tcpServerToClientMatched", evidence.tcpServerToClientMatched);
    writer.member("httpRequestCompleted", evidence.httpRequestCompleted);
    writer.member("httpStatusCode", static_cast<unsigned>(evidence.httpStatusCode));
    writer.member("httpBodyMatched", evidence.httpBodyMatched);
    writer.member("webSocketHandshakeCompleted", evidence.webSocketHandshakeCompleted);
    writer.member("webSocketEchoMatched", evidence.webSocketEchoMatched);
    writer.member("webSocketFramesSent", evidence.webSocketFramesSent);
    writer.endObject();
    stream << '\n';
}

void writeError(const Core::Error& error)
{
    Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("domain", static_cast<unsigned>(error.code.domain));
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

// Serves one HTTP request and one WebSocket session over accepted connections.
// Deliberately minimal: it exists so the client side has a real peer, not to be a
// server implementation.
class LoopbackServer final {
  public:
    explicit LoopbackServer(Network::TcpListener listener)
        : m_listener(std::move(listener))
    {
    }

    void setHttpReply(std::string reply) { m_httpReply = std::move(reply); }

    // Echoes WebSocket text frames back, which is what proves the frame layer
    // works in both directions rather than only outbound.
    void setWebSocketEcho(bool value) noexcept { m_webSocketEcho = value; }

    [[nodiscard]] Core::usize acceptedCount() const noexcept { return m_accepted; }
    [[nodiscard]] Core::usize webSocketFramesEchoed() const noexcept
    {
        return m_framesEchoed;
    }

    [[nodiscard]] Core::Status pump()
    {
        auto accepted = m_listener.pump();
        if (!accepted) {
            return Core::failure(accepted.error());
        }

        while (m_listener.readyConnectionCount() > 0 && m_peers.size() < MaximumConnections) {
            auto connection = m_listener.acceptNext();
            if (!connection) {
                break;
            }
            auto peer = std::make_unique<Peer>();
            // unique_ptr because TcpConnection deletes move-assignment on purpose.
            peer->connection =
                std::make_unique<Network::TcpConnection>(std::move(*connection));
            m_peers.push_back(std::move(peer));
            ++m_accepted;
        }

        for (auto& peer : m_peers) {
            auto& connection = *peer->connection;
            if (!connection.pump()) {
                continue;
            }

            const auto buffered = connection.receive();
            if (!buffered) {
                continue;
            }
            if (!buffered->empty()) {
                peer->pending.append(asText(*buffered));
                if (!connection.consume(buffered->size())) {
                    continue;
                }
            }

            // Once upgraded the connection speaks frames, not requests, so the
            // handshake parser must not run again on frame bytes.
            if (peer->upgraded) {
                serveFrames(*peer);
            } else {
                serveRequest(*peer);
            }
        }
        return Core::success();
    }

  private:
    static constexpr Core::usize MaximumConnections = 4;

    struct Peer final {
        std::unique_ptr<Network::TcpConnection> connection;
        std::string pending;
        bool upgraded = false;
    };

    // A WebSocket upgrade and a plain request both start with a request line, so
    // the Upgrade header is what tells them apart.
    void serveRequest(Peer& peer)
    {
        const Core::usize terminator = peer.pending.find("\r\n\r\n");
        if (terminator == std::string::npos) {
            return;
        }
        const Core::usize bodyStart = terminator + 4;
        const std::string head = peer.pending.substr(0, bodyStart);
        // Anything past the terminator already belongs to the next layer.
        peer.pending.erase(0, bodyStart);

        if (head.find("Upgrade: websocket") != std::string::npos) {
            (void)peer.connection->send(asBytes(buildUpgradeResponse(head)));
            peer.upgraded = true;
            return;
        }

        if (!m_httpReply.empty()) {
            (void)peer.connection->send(asBytes(m_httpReply));
            // Deliberately no shutdownSend here: it drops queued unsent bytes, and
            // send() has only queued the reply -- a later pump still has to flush
            // it. The reply carries Content-Length, so the client does not need a
            // close to know where the body ends.
        }
    }

    // Parses client frames and echoes text ones back. Client frames are always
    // masked (RFC 6455 requires it), server frames never are.
    void serveFrames(Peer& peer)
    {
        while (true) {
            const auto* raw = reinterpret_cast<const unsigned char*>(peer.pending.data());
            const Core::usize available = peer.pending.size();
            if (available < 2) {
                return;
            }

            const bool fin = (raw[0] & 0x80U) != 0;
            const unsigned char opcode = raw[0] & 0x0FU;
            const bool masked = (raw[1] & 0x80U) != 0;
            Core::usize payloadLength = raw[1] & 0x7FU;
            Core::usize headerSize = 2;

            if (payloadLength == 126) {
                if (available < 4) {
                    return;
                }
                payloadLength = (static_cast<Core::usize>(raw[2]) << 8)
                    | static_cast<Core::usize>(raw[3]);
                headerSize = 4;
            } else if (payloadLength == 127) {
                // The sample never sends anything this large, so refusing is
                // honest rather than pretending to support it.
                return;
            }
            if (masked) {
                headerSize += 4;
            }
            if (available < headerSize + payloadLength) {
                return;
            }

            std::string payload;
            payload.resize(payloadLength);
            const unsigned char* body = raw + headerSize;
            if (masked) {
                const unsigned char* mask = raw + headerSize - 4;
                for (Core::usize index = 0; index < payloadLength; ++index) {
                    payload[index] = static_cast<char>(body[index] ^ mask[index % 4]);
                }
            } else {
                payload.assign(reinterpret_cast<const char*>(body), payloadLength);
            }

            peer.pending.erase(0, headerSize + payloadLength);

            // Text frame, complete, and echo requested: send it straight back.
            if (fin && opcode == 0x1 && m_webSocketEcho) {
                (void)peer.connection->send(asBytes(buildTextFrame(payload)));
                ++m_framesEchoed;
            } else if (opcode == 0x8) {
                // Echo the close so the client sees an orderly shutdown.
                (void)peer.connection->send(asBytes(buildCloseFrame(payload)));
            }
        }
    }

    // Unmasked server frame. A client must accept it; masking here would be a
    // protocol violation in the other direction.
    [[nodiscard]] static std::string buildTextFrame(std::string_view payload)
    {
        return buildFrame(0x1, payload);
    }

    [[nodiscard]] static std::string buildCloseFrame(std::string_view payload)
    {
        return buildFrame(0x8, payload);
    }

    [[nodiscard]] static std::string buildFrame(unsigned char opcode, std::string_view payload)
    {
        std::string frame;
        frame.push_back(static_cast<char>(0x80U | opcode));
        if (payload.size() < 126) {
            frame.push_back(static_cast<char>(payload.size()));
        } else {
            frame.push_back(static_cast<char>(126));
            frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFFU));
            frame.push_back(static_cast<char>(payload.size() & 0xFFU));
        }
        frame.append(payload);
        return frame;
    }

    // The accept token proves the server processed this specific key, so it is
    // derived from the request rather than fixed.
    [[nodiscard]] std::string buildUpgradeResponse(const std::string& request) const
    {
        const std::string key = extractHeader(request, "Sec-WebSocket-Key");
        std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
        response += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
        response += "Sec-WebSocket-Accept: " + Network::webSocketAcceptToken(key) + "\r\n\r\n";
        return response;
    }

    [[nodiscard]] static std::string extractHeader(
        const std::string& request,
        std::string_view name)
    {
        const std::string needle = std::string{name} + ": ";
        const Core::usize start = request.find(needle);
        if (start == std::string::npos) {
            return {};
        }
        const Core::usize valueStart = start + needle.size();
        const Core::usize end = request.find("\r\n", valueStart);
        if (end == std::string::npos) {
            return {};
        }
        return request.substr(valueStart, end - valueStart);
    }

    Network::TcpListener m_listener;
    std::vector<std::unique_ptr<Peer>> m_peers;
    std::string m_httpReply;
    Core::usize m_accepted = 0;
    Core::usize m_framesEchoed = 0;
    bool m_webSocketEcho = false;
};

} // namespace

int main(int argc, char** argv)
{
    const auto frameCount = parseFrameCount(argc, argv);
    if (!frameCount) {
        std::fprintf(stderr, "usage: tina_sample_network [--frames=N]\n");
        return 2;
    }

    Evidence evidence{};

    // DNS is the only part that needs workers, so the task system exists for it.
    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .cpuWorkerCount = 0,
        .ioQueueCapacity = 16,
        .cpuQueueCapacity = 0,
        .mainQueueCapacity = 16,
    });
    if (!taskSystem) {
        writeError(taskSystem.error());
        return 1;
    }

    // Everything below shares one loop, so a failure to release a socket or a
    // slot shows up as a later step failing rather than as a leak nobody sees.
    const int exitCode = [&]() -> int {
        Network::DnsResolverConfig dnsConfig{};
        dnsConfig.taskSystem = taskSystem->get();
        dnsConfig.queryCapacity = 4;
        auto resolver = Network::DnsResolver::Create(dnsConfig);
        if (!resolver) {
            writeError(resolver.error());
            return 1;
        }

        Network::UdpSocketConfig receiverConfig{};
        receiverConfig.localEndpoint =
            Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 0};
        auto udpReceiver = Network::UdpSocket::Create(receiverConfig);
        if (!udpReceiver) {
            writeError(udpReceiver.error());
            return 1;
        }
        auto udpSender = Network::UdpSocket::Create(receiverConfig);
        if (!udpSender) {
            writeError(udpSender.error());
            return 1;
        }

        Network::TcpListenerConfig listenerConfig{};
        listenerConfig.localEndpoint =
            Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 0};
        listenerConfig.connectionCapacity = 4;
        auto listener = Network::TcpListener::Create(listenerConfig);
        if (!listener) {
            writeError(listener.error());
            return 1;
        }
        const auto serverEndpoint = listener->localEndpoint();
        if (!serverEndpoint) {
            writeError(serverEndpoint.error());
            return 1;
        }

        LoopbackServer server{std::move(*listener)};
        server.setHttpReply(
            "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhello tina!");
        server.setWebSocketEcho(true);

        const auto udpTarget = udpReceiver->localEndpoint();
        if (!udpTarget) {
            writeError(udpTarget.error());
            return 1;
        }
        const auto udpSource = udpSender->localEndpoint();
        if (!udpSource) {
            writeError(udpSource.error());
            return 1;
        }

        // Two DNS queries: a numeric literal must resolve without the network, and
        // a reserved .invalid name must fail rather than hang.
        auto numericQuery = resolver->resolve("127.0.0.1", 9000);
        if (!numericQuery) {
            writeError(numericQuery.error());
            return 1;
        }
        auto badQuery = resolver->resolve("tina-sample-does-not-exist.invalid", 80);
        if (!badQuery) {
            writeError(badQuery.error());
            return 1;
        }

        // An HTTP request over its own TCP connection.
        Network::TcpConnectionConfig httpTransportConfig{};
        httpTransportConfig.remoteEndpoint = *serverEndpoint;
        auto httpTransport = Network::TcpConnection::Create(httpTransportConfig);
        if (!httpTransport) {
            writeError(httpTransport.error());
            return 1;
        }

        Network::HttpRequestConfig httpConfig{};
        httpConfig.stream = &*httpTransport;
        httpConfig.target = "/index";
        httpConfig.host = "tina.sample";
        auto request = Network::HttpRequest::Create(httpConfig);
        if (!request) {
            writeError(request.error());
            return 1;
        }

        // A plain TCP exchange on a third connection, so the listener has to serve
        // more than one peer.
        Network::TcpConnectionConfig echoConfig{};
        echoConfig.remoteEndpoint = *serverEndpoint;
        auto echoClient = Network::TcpConnection::Create(echoConfig);
        if (!echoClient) {
            writeError(echoClient.error());
            return 1;
        }

        // A WebSocket session on a fourth connection. This is the only path that
        // exercises the upgrade, the mask, and frame reassembly end to end.
        Network::TcpConnectionConfig socketTransportConfig{};
        socketTransportConfig.remoteEndpoint = *serverEndpoint;
        auto socketTransport = Network::TcpConnection::Create(socketTransportConfig);
        if (!socketTransport) {
            writeError(socketTransport.error());
            return 1;
        }

        Network::WebSocketConfig socketConfig{};
        socketConfig.stream = &*socketTransport;
        socketConfig.target = "/socket";
        socketConfig.host = "tina.sample";
        auto webSocket = Network::WebSocket::Create(socketConfig);
        if (!webSocket) {
            writeError(webSocket.error());
            return 1;
        }

        constexpr std::string_view udpPayload = "udp-frame";
        constexpr std::string_view expectedHttpBody = "hello tina!";
        constexpr std::string_view webSocketPayload = "ws-round-trip";

        bool udpSent = false;
        bool webSocketSent = false;
        std::string httpBody;

        for (Core::u32 frame = 0; frame < *frameCount; ++frame) {
            evidence.frames = frame + 1;

            // A frame's worth of wall time. Without it the whole run finishes in
            // well under a millisecond, and nothing that depends on the network
            // making real progress -- a loopback handshake, a resolver worker --
            // gets a chance to complete. A game loop has this for free.
            std::this_thread::sleep_for(std::chrono::milliseconds{1});

            // One UDP datagram, sent once and then verified on arrival.
            if (!udpSent) {
                if (udpSender->send(*udpTarget, asBytes(udpPayload))) {
                    udpSent = true;
                    ++evidence.udpDatagramsSent;
                }
            }
            if (const auto received = udpReceiver->receive()) {
                for (const auto& datagram : *received) {
                    ++evidence.udpDatagramsReceived;
                    if (datagram.sender == *udpSource
                        && asText(datagram.payload) == udpPayload) {
                        evidence.udpSenderEndpointMatched = true;
                    }
                }
            }

            if (const auto completed = resolver->pump(); !completed) {
                writeError(completed.error());
                return 1;
            }
            if (resolver->queryState(*numericQuery) == Network::DnsQueryState::Resolved) {
                if (const auto addresses = resolver->addresses(*numericQuery)) {
                    if (!addresses->empty()
                        && addresses->front().address == Network::IpAddress::v4Loopback()
                        && addresses->front().port == 9000) {
                        evidence.dnsResolvedNumericLiteral = true;
                    }
                }
            }
            if (resolver->queryState(*badQuery) == Network::DnsQueryState::Failed) {
                evidence.dnsRejectedUnresolvableName = true;
            }

            if (const auto status = server.pump(); !status) {
                writeError(status.error());
                return 1;
            }
            evidence.tcpConnectionsAccepted = server.acceptedCount();

            // HTTP advances only while it has not completed; pumping a finished
            // request is a no-op but there is no reason to keep calling it.
            if (!evidence.httpRequestCompleted) {
                const auto done = request->pump();
                if (!done) {
                    writeError(done.error());
                    return 1;
                }
                if (*done) {
                    evidence.httpRequestCompleted = true;
                    if (const auto response = request->response()) {
                        evidence.httpStatusCode = response->statusCode;
                        httpBody = asText(response->body);
                        evidence.httpBodyMatched = httpBody == expectedHttpBody;
                    }
                }
            }

            if (!echoClient->pump()) {
                writeError(Core::Error{Core::CoreErrorCode::Internal, "echo client pump failed"});
                return 1;
            }
            if (echoClient->state() == Network::TcpConnectionState::Connected
                && !evidence.tcpClientToServerMatched) {
                // The server echoes nothing on a plain connection, so this only
                // proves the client can queue and drain.
                if (echoClient->statistics().totalSentBytes == 0) {
                    (void)echoClient->send(asBytes("tcp-frame"));
                } else if (echoClient->statistics().queuedSendBytes == 0) {
                    evidence.tcpClientToServerMatched = true;
                }
            }

            // WebSocket: pump the handshake, then send once and wait for the echo.
            const auto socketPumped = webSocket->pump();
            if (!socketPumped) {
                writeError(socketPumped.error());
                return 1;
            }
            if (webSocket->state() == Network::WebSocketState::Open) {
                evidence.webSocketHandshakeCompleted = true;
                if (!webSocketSent) {
                    if (webSocket->sendText(webSocketPayload)) {
                        webSocketSent = true;
                    }
                }
            }
            if (*socketPumped) {
                if (const auto message = webSocket->message()) {
                    if (message->kind == Network::WebSocketMessageKind::Text
                        && asText(message->payload) == webSocketPayload) {
                        evidence.webSocketEchoMatched = true;
                    }
                    (void)webSocket->consumeMessage();
                }
            }
            evidence.webSocketFramesSent = webSocket->statistics().sentFrameCount;
        }

        // The reverse direction is proven by the HTTP body: it travelled from the
        // server to the client over an accepted connection.
        evidence.tcpServerToClientMatched = evidence.httpBodyMatched;
        evidence.dnsPumpCount = resolver->statistics().pumpCallCount;

        // Release the DNS slots so a leak here would show up as a rejected query
        // rather than silently holding storage.
        resolver->release(*numericQuery);
        resolver->release(*badQuery);

        return 0;
    }();

    // Joined explicitly on every path: a worker may still be inside getaddrinfo.
    (*taskSystem)->shutdownAndJoin();

    if (exitCode != 0) {
        writeEvidence(std::cerr, "error", evidence);
        return exitCode;
    }

    const bool ok = evidence.udpDatagramsSent == 1
        && evidence.udpDatagramsReceived >= 1
        && evidence.udpSenderEndpointMatched
        && evidence.dnsResolvedNumericLiteral
        && evidence.dnsRejectedUnresolvableName
        && evidence.dnsPumpCount > 0
        && evidence.tcpConnectionsAccepted >= 3
        && evidence.tcpClientToServerMatched
        && evidence.httpRequestCompleted
        && evidence.httpStatusCode == 200
        && evidence.httpBodyMatched
        && evidence.tcpServerToClientMatched
        && evidence.webSocketHandshakeCompleted
        && evidence.webSocketEchoMatched
        && evidence.webSocketFramesSent >= 1;

    if (!ok) {
        writeEvidence(std::cerr, "error", evidence);
        return 1;
    }

    writeEvidence(std::cout, "ok", evidence);
    return 0;
}
