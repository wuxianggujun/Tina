// Drives HttpRequest against a scripted HTTP server on loopback. The server
// replies with bytes the test chooses verbatim, which is the only way to cover
// framing cases a real server would never produce -- both Content-Length and
// Transfer-Encoding, a truncated body, a malformed status line.

#include "detail/NativeSocket.hpp"

#include <tina/network/HttpClient.hpp>
#include <tina/network/NetworkErrors.hpp>
#include <tina/network/TcpConnection.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace Tina::Tests {
namespace {

using Network::HttpMethod;
using Network::HttpRequest;
using Network::HttpRequestConfig;
using Network::HttpRequestState;

// Accepts one connection, records the request, and sends a scripted reply.
class ScriptedHttpServer final {
  public:
    ScriptedHttpServer()
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

    ~ScriptedHttpServer() { close(); }

    ScriptedHttpServer(const ScriptedHttpServer&) = delete;
    ScriptedHttpServer& operator=(const ScriptedHttpServer&) = delete;

    [[nodiscard]] bool isValid() const noexcept
    {
        return m_listen != Network::Detail::InvalidNativeSocket;
    }
    [[nodiscard]] const Network::NetworkEndpoint& endpoint() const noexcept { return m_endpoint; }
    [[nodiscard]] const std::string& request() const noexcept { return m_request; }

    // Sets what the server sends once it has seen the end of the request headers.
    void setReply(std::string reply) { m_reply = std::move(reply); }

    // Closes the connection after replying, which is what delimits a body with no
    // Content-Length.
    void setCloseAfterReply(bool value) noexcept { m_closeAfterReply = value; }

    // Advances the server. Returns true once the reply has been fully written.
    bool pump()
    {
        if (m_accepted == Network::Detail::InvalidNativeSocket) {
            const auto accepted = ::accept(m_listen, nullptr, nullptr);
            if (accepted == Network::Detail::InvalidNativeSocket) {
                return false;
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
            m_request.append(scratch, static_cast<Core::usize>(got));
        }

        if (!m_replied && m_request.find("\r\n\r\n") != std::string::npos) {
            if (!m_reply.empty()) {
#if defined(_WIN32)
                ::send(m_accepted, m_reply.data(), static_cast<int>(m_reply.size()), 0);
#else
                ::send(m_accepted, m_reply.data(), m_reply.size(), 0);
#endif
            }
            m_replied = true;
            if (m_closeAfterReply) {
                Network::Detail::closeNativeSocket(m_accepted);
                m_accepted = Network::Detail::InvalidNativeSocket;
            }
        }
        return m_replied;
    }

  private:
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
    std::string m_reply;
    bool m_replied = false;
    bool m_closeAfterReply = true;
    bool m_scoped = false;
};

[[nodiscard]] HttpRequestConfig configFor(Network::IByteStream& stream)
{
    HttpRequestConfig config{};
    config.stream = &stream;
    config.target = "/";
    config.host = "tina.test";
    return config;
}

// Interleaves client and server pumps until the request finishes or the budget
// runs out. Neither side can progress alone.
// Connects a TcpConnection to the scripted server, so a test says what it means
// (an HTTP exchange) rather than restating transport setup each time.
[[nodiscard]] Core::Result<Network::TcpConnection> connectTo(
    const Network::NetworkEndpoint& endpoint,
    Core::usize sendBufferBytes = 64 * 1024)
{
    Network::TcpConnectionConfig config{};
    config.remoteEndpoint = endpoint;
    config.sendBufferBytes = sendBufferBytes;
    config.receiveBufferBytes = 64 * 1024;
    return Network::TcpConnection::Create(config);
}

[[nodiscard]] Core::Result<bool> driveToCompletion(
    HttpRequest& request,
    ScriptedHttpServer& server,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        (void)server.pump();
        auto done = request.pump();
        if (!done) {
            return done;
        }
        if (*done) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

[[nodiscard]] std::string bodyText(const Network::HttpResponse& response)
{
    return std::string{
        reinterpret_cast<const char*>(response.body.data()),
        response.body.size()};
}

} // namespace

TEST(HttpRequestTest, RejectsMissingStream)
{
    HttpRequestConfig config{};
    config.target = "/";
    config.host = "tina.test";

    // A request with nowhere to send bytes is a configuration error, not something
    // to discover on the first pump.
    const auto request = HttpRequest::Create(config);
    ASSERT_FALSE(request.has_value());
    EXPECT_EQ(request.error().code, Network::NetworkErrorCode::InvalidConfiguration);
}

TEST(HttpRequestTest, RejectsInvalidConfiguration)
{
    // Never pumped, so the stream only has to exist.
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    {
        auto config = configFor(*stream);
        config.target = "index.html";  // missing leading slash
        const auto request = HttpRequest::Create(config);
        ASSERT_FALSE(request.has_value());
        EXPECT_EQ(request.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = configFor(*stream);
        config.host = {};
        const auto request = HttpRequest::Create(config);
        ASSERT_FALSE(request.has_value());
        EXPECT_EQ(request.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = configFor(*stream);
        config.maximumBodyBytes = 0;
        const auto request = HttpRequest::Create(config);
        ASSERT_FALSE(request.has_value());
        EXPECT_EQ(request.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
}

// CRLF in a caller-supplied field would let it inject a header line, so it is
// refused rather than escaped.
TEST(HttpRequestTest, RejectsHeaderInjectionAttempts)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    for (const std::string_view target : {
             "/a\r\nX-Injected: 1",
             "/a\nX-Injected: 1",
             "/a b",
         }) {
        auto config = configFor(*stream);
        config.target = target;
        const auto request = HttpRequest::Create(config);
        EXPECT_FALSE(request.has_value()) << "accepted target " << target;
    }

    auto config = configFor(*stream);
    config.host = "tina.test\r\nX-Injected: 1";
    const auto request = HttpRequest::Create(config);
    EXPECT_FALSE(request.has_value());
}

TEST(HttpRequestTest, SendsWellFormedRequestLineAndHost)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.target = "/api/thing";
    auto request = HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    EXPECT_TRUE(server.request().starts_with("GET /api/thing HTTP/1.1\r\n"));
    EXPECT_NE(server.request().find("Host: tina.test\r\n"), std::string::npos);
}

TEST(HttpRequestTest, ParsesStatusHeadersAndContentLengthBody)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello tina!");

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);
    EXPECT_EQ(request->state(), HttpRequestState::Complete);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->statusCode, 200);
    EXPECT_EQ(response->reasonPhrase, "OK");
    EXPECT_TRUE(response->isSuccess());
    EXPECT_EQ(bodyText(*response), "hello tina!");
    EXPECT_EQ(response->header("Content-Type"), "text/plain");
    // Header lookup is case-insensitive per RFC 9110.
    EXPECT_EQ(response->header("content-type"), "text/plain");
    EXPECT_EQ(response->header("Missing"), "");
}

// A 404 is a completed request that failed at the application level, not a client
// error, so pump must succeed and isSuccess must be false.
TEST(HttpRequestTest, NonSuccessStatusStillCompletes)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot here!");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->statusCode, 404);
    EXPECT_FALSE(response->isSuccess());
    EXPECT_EQ(bodyText(*response), "not here!");
}

TEST(HttpRequestTest, DecodesChunkedBody)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "1\r\n \r\n"
        "4\r\ntina\r\n"
        "0\r\n\r\n");

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(bodyText(*response), "hello tina");
    EXPECT_TRUE(request->statistics().chunkedTransferEncoding);
}

// A chunk extension is legal and must be ignored without breaking the size parse.
TEST(HttpRequestTest, IgnoresChunkExtensions)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4;name=value\r\ndata\r\n"
        "0\r\n\r\n");

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(bodyText(*response), "data");
}

// With neither Content-Length nor chunked framing, the close delimits the body.
TEST(HttpRequestTest, BodyDelimitedByConnectionClose)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\n\r\nunbounded body");
    server.setCloseAfterReply(true);
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(bodyText(*response), "unbounded body");
}

// A valid 204 has no framing metadata or body and completes at the header boundary.
TEST(HttpRequestTest, NoContentStatusHasNoBody)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 204 No Content\r\n\r\n");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->statusCode, 204);
    EXPECT_TRUE(response->body.empty());
}

// A HEAD response carries the headers of the equivalent GET, including
// Content-Length, but no body.
TEST(HttpRequestTest, HeadResponseHasNoBodyDespiteContentLength)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\nContent-Length: 1234\r\n\r\n");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.method = HttpMethod::Head;
    auto request = HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(response->body.empty());
    EXPECT_EQ(response->header("Content-Length"), "1234");
    EXPECT_TRUE(server.request().starts_with("HEAD "));
}

TEST(HttpRequestTest, SendsRequestBodyWithContentLength)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");

    constexpr std::string_view payload = R"({"key":"value"})";
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.method = HttpMethod::Post;
    config.body = std::as_bytes(std::span{payload.data(), payload.size()});
    config.contentType = "application/json";

    auto request = HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(*done);

    EXPECT_TRUE(server.request().starts_with("POST "));
    EXPECT_NE(server.request().find("Content-Length: 15\r\n"), std::string::npos);
    EXPECT_NE(server.request().find("Content-Type: application/json\r\n"), std::string::npos);
    EXPECT_NE(server.request().find(payload), std::string::npos);

    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->statusCode, 201);
}

// Carrying both framing headers is a request-smuggling shape, so it is refused
// rather than resolved by precedence.
TEST(HttpRequestTest, RejectsBothContentLengthAndTransferEncoding)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n\r\n");

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_FALSE(done.has_value());
    EXPECT_EQ(done.error().code, Network::NetworkErrorCode::HttpMalformedResponse);
}

// Whitespace before the colon is another smuggling vector.
TEST(HttpRequestTest, RejectsWhitespaceBeforeHeaderColon)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\nContent-Length : 2\r\n\r\nhi");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_FALSE(done.has_value());
    EXPECT_EQ(done.error().code, Network::NetworkErrorCode::HttpMalformedResponse);
}

TEST(HttpRequestTest, RejectsMalformedStatusLine)
{
    for (const std::string_view reply : {
             "NOTHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
             "HTTP/1.1 20 OK\r\nContent-Length: 0\r\n\r\n",
             "HTTP/1.1 abc OK\r\nContent-Length: 0\r\n\r\n",
             "HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
         }) {
        ScriptedHttpServer server;
        ASSERT_TRUE(server.isValid());
        server.setReply(std::string{reply});

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

        auto request = HttpRequest::Create(configFor(*stream));
        ASSERT_TRUE(request.has_value());

        const auto done = driveToCompletion(*request, server, 600);
        EXPECT_FALSE(done.has_value()) << "accepted reply: " << reply;
        if (!done.has_value()) {
            EXPECT_EQ(done.error().code, Network::NetworkErrorCode::HttpMalformedResponse);
        }
    }
}

// A body shorter than the declared length is incomplete, not a short success:
// treating it as complete would hand a parser a truncated document.
TEST(HttpRequestTest, TruncatedBodyIsReportedAsIncomplete)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nonly-part");
    server.setCloseAfterReply(true);
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_FALSE(done.has_value());
    EXPECT_EQ(done.error().code, Network::NetworkErrorCode::HttpIncompleteResponse);
}

TEST(HttpRequestTest, RejectsBodyExceedingLimit)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.maximumBodyBytes = 10;
    auto request = HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_FALSE(done.has_value());
    EXPECT_EQ(done.error().code, Network::NetworkErrorCode::HttpResponseTooLarge);
}

TEST(HttpRequestTest, RejectsHeadersExceedingLimit)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());

    std::string reply = "HTTP/1.1 200 OK\r\n";
    for (int index = 0; index < 40; ++index) {
        reply += "X-Padding-" + std::to_string(index) + ": "
            + std::string(200, 'x') + "\r\n";
    }
    reply += "Content-Length: 0\r\n\r\n";
    server.setReply(reply);

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.maximumHeaderBytes = 512;
    auto request = HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    const auto done = driveToCompletion(*request, server);
    ASSERT_FALSE(done.has_value());
    EXPECT_EQ(done.error().code, Network::NetworkErrorCode::HttpResponseTooLarge);
}

// A server that accepts the connection but never replies must not hang the caller
// forever.
TEST(HttpRequestTest, StallLimitEndsASilentServer)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply({});  // never answers
    // The connection must stay open, or the client reports the peer close instead and
    // the stall limit -- the only guard against pumping a silent peer forever -- is
    // never reached.
    server.setCloseAfterReply(false);

    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto config = configFor(*stream);
    config.stallPumpLimit = 30;
    auto request = HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    Core::ErrorCode observed{};
    for (int attempt = 0; attempt < 400; ++attempt) {
        (void)server.pump();
        const auto done = request->pump();
        if (!done) {
            observed = done.error().code;
            break;
        }
    }

    EXPECT_EQ(observed, Network::NetworkErrorCode::HttpTimeout);
    EXPECT_EQ(request->state(), HttpRequestState::Failed);
}

TEST(HttpRequestTest, ResponseUnavailableBeforeCompletion)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    const auto premature = request->response();
    ASSERT_FALSE(premature.has_value());
    EXPECT_EQ(premature.error().code, Network::NetworkErrorCode::HttpIncompleteResponse);
}

TEST(HttpRequestTest, PumpAfterCompletionIsIdempotent)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(*driveToCompletion(*request, server));

    // Further pumps must keep reporting completion rather than re-reading.
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto done = request->pump();
        ASSERT_TRUE(done.has_value());
        EXPECT_TRUE(*done);
    }
    EXPECT_EQ(bodyText(*request->response()), "ok");
}

TEST(HttpRequestTest, CancelStopsTheExchange)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    server.setReply({});
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());
    (void)request->pump();

    request->cancel();
    EXPECT_EQ(request->state(), HttpRequestState::Failed);

    // Idempotent.
    request->cancel();
    EXPECT_EQ(request->state(), HttpRequestState::Failed);

    const auto done = request->pump();
    EXPECT_FALSE(done.has_value());
}

TEST(HttpRequestTest, RejectsUseFromNonOwnerThread)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());

    Core::ErrorCode pumpCode{};
    Core::ErrorCode responseCode{};

    std::thread other{[&]() {
        if (const auto done = request->pump(); !done) {
            pumpCode = done.error().code;
        }
        if (const auto response = request->response(); !response) {
            responseCode = response.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(pumpCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(responseCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(HttpRequestTest, MovedFromRequestAnswersQueriesInertly)
{
    ScriptedHttpServer server;
    ASSERT_TRUE(server.isValid());
    auto stream = connectTo(server.endpoint());
    ASSERT_TRUE(stream.has_value());

    auto request = HttpRequest::Create(configFor(*stream));
    ASSERT_TRUE(request.has_value());
    HttpRequest moved{std::move(*request)};

    EXPECT_FALSE(static_cast<bool>(*request));
    EXPECT_EQ(request->state(), HttpRequestState::Failed);
    EXPECT_EQ(request->statistics().pumpCallCount, 0U);

    const auto done = request->pump();
    ASSERT_FALSE(done.has_value());
    EXPECT_EQ(done.error().code, Network::NetworkErrorCode::SocketClosed);

    EXPECT_TRUE(static_cast<bool>(moved));
}

} // namespace Tina::Tests
