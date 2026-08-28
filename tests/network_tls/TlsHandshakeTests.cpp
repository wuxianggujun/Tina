// Completes a real TLS handshake against an in-process mbedTLS server, then runs
// an HTTP exchange over it. The rest of this directory covers configuration
// refusal and state transitions; this file is the only place the handshake, the
// certificate check, and the record layer actually run.
//
// Both sides are pumped from this thread under the same non-blocking discipline,
// so a stalled handshake fails an assertion instead of hanging.

#include "TlsTestServer.hpp"

#include <tina/network/HttpClient.hpp>
#include <tina/network/NetworkErrors.hpp>
#include <tina/network/tls/TlsConnection.hpp>

#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace Tina::Tests {
namespace {

using Network::TlsConnection;
using Network::TlsConnectionConfig;
using Network::TlsConnectionState;

[[nodiscard]] TlsConnectionConfig clientConfig(const TlsTestServer& server)
{
    TlsConnectionConfig config{};
    config.remoteEndpoint = server.endpoint();
    // Must match the certificate CN, or hostname verification fails by design.
    config.serverName = TlsTestCertificates::ServerName;
    config.trustAnchorsPem = server.certificatePem();
    config.verification = Network::TlsVerificationMode::Required;
    return config;
}

// Interleaves both sides until the client connects or the budget runs out.
// Neither side can progress alone: each needs the other's records.
[[nodiscard]] bool completeHandshake(
    TlsConnection& client,
    TlsTestServer& server,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        server.pump();
        const auto pumped = client.pump();
        if (!pumped) {
            return false;
        }
        if (client.state() == TlsConnectionState::Connected) {
            return true;
        }
        if (client.state() == TlsConnectionState::Failed) {
            return false;
        }
    }
    return false;
}

// Drives until the client rejects the peer, returning the error it reported.
[[nodiscard]] Core::ErrorCode driveUntilRejected(
    TlsConnection& client,
    TlsTestServer& server,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        server.pump();
        const auto pumped = client.pump();
        if (!pumped) {
            return pumped.error().code;
        }
        if (client.state() == TlsConnectionState::Connected) {
            break;
        }
    }
    return Core::ErrorCode{};
}

} // namespace

TEST(TlsHandshakeTest, CompletesHandshakeWithTrustedCertificate)
{
    TlsTestServer server;
    ASSERT_TRUE(server.isValid()) << server.lastError();

    auto client = TlsConnection::Create(clientConfig(server));
    ASSERT_TRUE(client.has_value());

    ASSERT_TRUE(completeHandshake(*client, server));
    EXPECT_EQ(client->state(), TlsConnectionState::Connected);

    const auto stats = client->statistics();
    EXPECT_TRUE(stats.handshakeComplete);
    // A handshake is a multi-round exchange, so finishing in a single pump would
    // mean records were never really exchanged.
    EXPECT_GT(stats.handshakePumpCount, 1U);
}

// The chain is trusted but issued for another name, so verification must still
// fail -- otherwise any certificate from a trusted CA would impersonate any host.
TEST(TlsHandshakeTest, RejectsCertificateWithMismatchedHostname)
{
    TlsTestServer server;
    ASSERT_TRUE(server.isValid()) << server.lastError();

    auto config = clientConfig(server);
    config.serverName = "wrong.example";

    auto client = TlsConnection::Create(config);
    ASSERT_TRUE(client.has_value());

    EXPECT_EQ(
        driveUntilRejected(*client, server),
        Network::NetworkErrorCode::TlsVerificationFailed);
    EXPECT_EQ(client->state(), TlsConnectionState::Failed);
    EXPECT_FALSE(client->statistics().handshakeComplete);
}

// An unrelated anchor must fail the chain even though the hostname matches.
TEST(TlsHandshakeTest, RejectsCertificateFromUntrustedIssuer)
{
    TlsTestServer server;
    ASSERT_TRUE(server.isValid()) << server.lastError();
    ASSERT_FALSE(unrelatedCertificatePem().empty());

    auto config = clientConfig(server);
    config.trustAnchorsPem = unrelatedCertificatePem();

    auto client = TlsConnection::Create(config);
    ASSERT_TRUE(client.has_value());

    EXPECT_EQ(
        driveUntilRejected(*client, server),
        Network::NetworkErrorCode::TlsVerificationFailed);
    EXPECT_EQ(client->state(), TlsConnectionState::Failed);
}

TEST(TlsHandshakeTest, SendsAndReceivesApplicationData)
{
    TlsTestServer server;
    ASSERT_TRUE(server.isValid()) << server.lastError();
    server.setEcho(true);

    auto client = TlsConnection::Create(clientConfig(server));
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(completeHandshake(*client, server));

    constexpr std::string_view payload = "tina-over-tls";
    ASSERT_TRUE(
        client->send(std::as_bytes(std::span{payload.data(), payload.size()})).has_value());

    std::string received;
    for (int attempt = 0; attempt < 4000 && received.size() < payload.size(); ++attempt) {
        server.pump();
        const auto pumped = client->pump();
        ASSERT_TRUE(pumped.has_value());
        const auto buffered = client->receive();
        ASSERT_TRUE(buffered.has_value());
        if (!buffered->empty()) {
            received.append(
                reinterpret_cast<const char*>(buffered->data()),
                buffered->size());
            ASSERT_TRUE(client->consume(buffered->size()).has_value());
        }
    }

    EXPECT_EQ(received, payload);
    // Plaintext counters, so they match what was handed in rather than wire size.
    EXPECT_EQ(client->statistics().totalSentBytes, payload.size());
    EXPECT_EQ(client->statistics().totalReceivedBytes, payload.size());
}

// close_notify is what separates an orderly shutdown from a truncated stream, so
// the client must report PeerClosed rather than a failure.
TEST(TlsHandshakeTest, PeerCloseNotifyYieldsPeerClosed)
{
    TlsTestServer server;
    ASSERT_TRUE(server.isValid()) << server.lastError();

    auto client = TlsConnection::Create(clientConfig(server));
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(completeHandshake(*client, server));

    server.closeNotify();

    // Pump both sides: the alert has to leave the server before the client can see
    // it, and on a non-blocking socket that can take more than one turn.
    for (int attempt = 0; attempt < 4000; ++attempt) {
        server.pump();
        const auto pumped = client->pump();
        if (!pumped) {
            break;
        }
        if (client->state() == TlsConnectionState::PeerClosed) {
            break;
        }
    }

    // Separates a fixture that never sent the alert from a client that missed it.
    ASSERT_TRUE(server.closeNotifySent()) << "fixture never emitted close_notify";
    EXPECT_EQ(client->state(), TlsConnectionState::PeerClosed);
}

// The point of the IByteStream seam: the same HTTP parser runs over TLS unchanged,
// so https is a transport choice rather than a second implementation.
TEST(TlsHandshakeTest, HttpRequestRunsOverTlsStream)
{
    TlsTestServer server;
    ASSERT_TRUE(server.isValid()) << server.lastError();
    server.setHttpReply("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nencrypted");

    auto client = TlsConnection::Create(clientConfig(server));
    ASSERT_TRUE(client.has_value());

    Network::HttpRequestConfig config{};
    config.stream = &*client;
    config.target = "/secure";
    config.host = std::string{TlsTestCertificates::ServerName};

    auto request = Network::HttpRequest::Create(config);
    ASSERT_TRUE(request.has_value());

    bool complete = false;
    for (int attempt = 0; attempt < 8000 && !complete; ++attempt) {
        server.pump();
        const auto done = request->pump();
        ASSERT_TRUE(done.has_value())
            << "domain=" << static_cast<int>(done.error().code.domain)
            << " value=" << done.error().code.value << " message=" << done.error().message;
        complete = *done;
    }

    ASSERT_TRUE(complete);
    const auto response = request->response();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->statusCode, 200);
    EXPECT_TRUE(response->isSuccess());
    EXPECT_EQ(
        std::string(
            reinterpret_cast<const char*>(response->body.data()),
            response->body.size()),
        "encrypted");

    // The exchange really was encrypted: the handshake completed, and the server
    // only ever saw the request after it.
    EXPECT_TRUE(client->statistics().handshakeComplete);
    EXPECT_NE(server.observedRequest().find("GET /secure "), std::string::npos);
}

} // namespace Tina::Tests
