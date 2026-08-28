// TLS contract tests. There is no TLS server here, so these cover configuration
// refusal, state progression, and the owner-thread/lifecycle rules. A real
// handshake needs a peer and belongs in a separate gate.
//
// The refusal tests matter most: a transport that silently accepts any certificate
// gives encryption without authentication, which is worse than a visible failure
// because it looks like it works.

#include <tina/network/NetworkErrors.hpp>
#include <tina/network/tls/TlsConnection.hpp>

#include <gtest/gtest.h>

#include <memory_resource>
#include <string_view>
#include <thread>

namespace Tina::Tests {
namespace {

using Network::TlsConnection;
using Network::TlsConnectionConfig;
using Network::TlsConnectionState;
using Network::TlsVerificationMode;

// A real self-signed certificate (CN=tina.test, RSA-2048, expires 2036). It only
// has to parse: these tests never complete a handshake, so nothing chains to it.
//
// A hand-written base64 body will not do -- mbedtls_x509_crt_parse decodes the
// DER and rejects a fabricated one, which is how the first version of this file
// failed.
constexpr std::string_view SamplePem = R"(-----BEGIN CERTIFICATE-----
MIIC1zCCAb+gAwIBAgIUU3JG9q9ULiWIvPbMIJhGAn1PeWcwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJdGluYS50ZXN0MB4XDTI2MDgyODExNTg0MVoXDTM2MDgy
NTExNTg0MVowFDESMBAGA1UEAwwJdGluYS50ZXN0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAt+TLn1O4vbJ8IUkJ5tt1BAHeNw7pPgDNI/YfIaPEjALp
W7HxS/JlcCzqudsmY2VgiufAEJ9SCsBYfsg66FUYINinBVBoBnSUPzJZBu53qmmv
rYTCg4cqXOaiB4oYQ9lZGNR3dTqKmzi84shcGtAcQIArKVVS8o5dwKkVhL6cga0j
o5i1/mSnQtXY4ds4OU3+iXhiMxhX7ngc6Jdf6hMHYzotaL0VxaoSXP/RHCBOmihg
oblMTBrFlDrOQvjQ44OTxSk4x/68ClfS4U7XCVh8V9aZZMVI/2s3cfXjfbP5zTgx
KMiOVgg8yhJnzrfIO5tAVRxIjwOKopD24ksr0weqlQIDAQABoyEwHzAdBgNVHQ4E
FgQUrvjmSBe2zOpwYO/aUUnZpWtQG0QwDQYJKoZIhvcNAQELBQADggEBAAloFu9Q
FpjbGzM00Fj1m2TZ3TyenTg0i/gB6h95MxLoGBxVlmxu8cj0Oh34H2jMougaXX21
QbydLrgK5VkKsdJWjkZWNDsE07HDSW1PuM+rAtglYSFOqugKXK2CcPF1OaJrZLF6
0e8gmNFqSEbG4fEpP0W0Jn/QuQnWJPoLSYvbTShXVDX1/Vc/+VlNY4UUDjotUdc4
AimseOdcV3FlmsWJ0SUS7dxN9OLDXEikkFB3HC/Lxlye7/d2sFqGjqyFwhKEN0+k
S1IMg3c4mWpbe0OvIpCd8qIgYixvmR98bHRuieyr7mUoSCoZvmWxVg/GKYgWMihQ
PsI/SZ5Se9pHd5M=
-----END CERTIFICATE-----
)";

// Port 9 is discard: nothing listens on loopback, so Create succeeds and the
// failure surfaces later through pump(), which is the contract under test.
[[nodiscard]] Network::NetworkEndpoint unreachableEndpoint()
{
    return Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 9};
}

[[nodiscard]] TlsConnectionConfig secureConfig()
{
    TlsConnectionConfig config{};
    config.remoteEndpoint = unreachableEndpoint();
    config.serverName = "tina.test";
    config.trustAnchorsPem = SamplePem;
    config.verification = TlsVerificationMode::Required;
    return config;
}

} // namespace

// Verification requires a name to match against; without it "Required" would
// verify the chain but not that the certificate belongs to this peer.
TEST(TlsConnectionTest, RequiredVerificationRejectsMissingServerName)
{
    auto config = secureConfig();
    config.serverName = {};

    const auto connection = TlsConnection::Create(config);
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
}

// No trust anchors means nothing to chain to. This module ships no bundled roots
// and reads no system store, so an empty set can never verify.
TEST(TlsConnectionTest, RequiredVerificationRejectsMissingTrustAnchors)
{
    auto config = secureConfig();
    config.trustAnchorsPem = {};

    const auto connection = TlsConnection::Create(config);
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
}

TEST(TlsConnectionTest, RequiredVerificationRejectsMalformedTrustAnchors)
{
    auto config = secureConfig();
    config.trustAnchorsPem = "not a certificate at all";

    const auto connection = TlsConnection::Create(config);
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
}

// Asking to skip verification needs a second, differently named opt-in, so one
// mistyped field cannot disable authentication.
TEST(TlsConnectionTest, InsecureModeRequiresSecondExplicitOptIn)
{
    auto config = secureConfig();
    config.verification = TlsVerificationMode::InsecureSkipVerify;
    config.allowInsecureVerification = false;

    const auto connection = TlsConnection::Create(config);
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(
        connection.error().code,
        Network::NetworkErrorCode::TlsInsecureConfigurationRejected);
}

// With both opt-ins present, insecure mode is available in a debug build and
// refused outright in a release build.
TEST(TlsConnectionTest, InsecureModeHonoursBuildConfiguration)
{
    auto config = secureConfig();
    config.verification = TlsVerificationMode::InsecureSkipVerify;
    config.allowInsecureVerification = true;

    const auto connection = TlsConnection::Create(config);
#if defined(NDEBUG)
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(
        connection.error().code,
        Network::NetworkErrorCode::TlsInsecureConfigurationRejected);
#else
    ASSERT_TRUE(connection.has_value());
    EXPECT_EQ(connection->state(), TlsConnectionState::ConnectingTransport);
#endif
}

TEST(TlsConnectionTest, RejectsInvalidBufferSizes)
{
    {
        auto config = secureConfig();
        config.sendBufferBytes = 0;
        const auto connection = TlsConnection::Create(config);
        ASSERT_FALSE(connection.has_value());
        EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = secureConfig();
        config.receiveBufferBytes = 0;
        const auto connection = TlsConnection::Create(config);
        ASSERT_FALSE(connection.has_value());
        EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
}

TEST(TlsConnectionTest, RejectsInvalidEndpoint)
{
    auto config = secureConfig();
    config.remoteEndpoint = Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 0};

    const auto connection = TlsConnection::Create(config);
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidEndpoint);
}

// Create must not block on either handshake, so it returns before the transport
// is even up.
TEST(TlsConnectionTest, CreateStartsInConnectingTransportState)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value())
        << "domain=" << static_cast<int>(connection.error().code.domain)
        << " value=" << connection.error().code.value
        << " message=" << connection.error().message;
    EXPECT_TRUE(static_cast<bool>(*connection));
    EXPECT_EQ(connection->state(), TlsConnectionState::ConnectingTransport);
    EXPECT_FALSE(connection->statistics().handshakeComplete);
}

TEST(TlsConnectionTest, ReportsConfiguredBufferCapacities)
{
    auto config = secureConfig();
    config.sendBufferBytes = 2048;
    config.receiveBufferBytes = 4096;

    auto connection = TlsConnection::Create(config);
    ASSERT_TRUE(connection.has_value());
    EXPECT_EQ(connection->sendBufferCapacity(), 2048U);
    EXPECT_EQ(connection->receiveBufferCapacity(), 4096U);
}

// An unreachable peer must fail through pump rather than at Create, and must not
// report a TLS-level error: the transport never came up, so there was no
// handshake to fail.
TEST(TlsConnectionTest, UnreachablePeerFailsDuringPump)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    Core::ErrorCode observed{};
    bool failed = false;
    for (int attempt = 0; attempt < 400; ++attempt) {
        const auto pumped = connection->pump();
        if (!pumped) {
            observed = pumped.error().code;
            failed = true;
            break;
        }
        if (connection->state() == TlsConnectionState::Failed) {
            failed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_TRUE(failed);
    EXPECT_EQ(connection->state(), TlsConnectionState::Failed);
    if (observed.value != 0) {
        EXPECT_EQ(observed, Network::NetworkErrorCode::ConnectionFailed);
    }
    EXPECT_FALSE(connection->statistics().handshakeComplete);
}

TEST(TlsConnectionTest, SendQueuesPlaintextWithoutTouchingTheSocket)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    constexpr std::string_view payload = "queued-plaintext";
    const auto bytes = std::as_bytes(std::span{payload.data(), payload.size()});
    ASSERT_TRUE(connection->send(bytes).has_value());

    // Queued, not encrypted: encryption needs a completed handshake.
    EXPECT_EQ(connection->statistics().queuedSendBytes, payload.size());
    EXPECT_EQ(connection->statistics().totalSentBytes, 0U);
}

TEST(TlsConnectionTest, SendRejectsEmptyAndOversizedPayloads)
{
    auto config = secureConfig();
    config.sendBufferBytes = 64;
    auto connection = TlsConnection::Create(config);
    ASSERT_TRUE(connection.has_value());

    const auto empty = connection->send({});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, Network::NetworkErrorCode::InvalidDatagram);

    const std::vector<std::byte> tooBig(65, std::byte{0x33});
    const auto oversized = connection->send(tooBig);
    ASSERT_FALSE(oversized.has_value());
    EXPECT_EQ(oversized.error().code, Network::NetworkErrorCode::CapacityExceeded);
}

TEST(TlsConnectionTest, ConsumeRejectsMoreThanBuffered)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    const auto tooMany = connection->consume(1);
    ASSERT_FALSE(tooMany.has_value());
    EXPECT_EQ(tooMany.error().code, Core::CoreErrorCode::InvalidArgument);

    EXPECT_TRUE(connection->consume(0).has_value());
}

// shutdownTls needs an established session; before that there is no session to
// send close_notify over.
TEST(TlsConnectionTest, ShutdownBeforeHandshakeIsRejected)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    const auto status = connection->shutdownTls();
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Network::NetworkErrorCode::NotConnected);
}

TEST(TlsConnectionTest, CloseMovesToClosedAndIsIdempotent)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    connection->close();
    EXPECT_EQ(connection->state(), TlsConnectionState::Closed);

    connection->close();
    EXPECT_EQ(connection->state(), TlsConnectionState::Closed);

    const auto pumped = connection->pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::ConnectionClosed);

    const auto sent = connection->send(std::as_bytes(std::span{"x", 1}));
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error().code, Network::NetworkErrorCode::ConnectionClosed);
}

TEST(TlsConnectionTest, RejectsUseFromNonOwnerThread)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    Core::ErrorCode sendCode{};
    Core::ErrorCode pumpCode{};
    Core::ErrorCode receiveCode{};
    Core::ErrorCode shutdownCode{};

    std::thread other{[&]() {
        if (const auto status = connection->send(std::as_bytes(std::span{"x", 1})); !status) {
            sendCode = status.error().code;
        }
        if (const auto pumped = connection->pump(); !pumped) {
            pumpCode = pumped.error().code;
        }
        if (const auto received = connection->receive(); !received) {
            receiveCode = received.error().code;
        }
        if (const auto status = connection->shutdownTls(); !status) {
            shutdownCode = status.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(sendCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(pumpCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(receiveCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(shutdownCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(TlsConnectionTest, MovedFromConnectionAnswersQueriesInertly)
{
    auto config = secureConfig();
    config.sendBufferBytes = 512;
    config.receiveBufferBytes = 1024;

    auto connection = TlsConnection::Create(config);
    ASSERT_TRUE(connection.has_value());
    TlsConnection moved{std::move(*connection)};

    EXPECT_FALSE(static_cast<bool>(*connection));
    EXPECT_EQ(connection->state(), TlsConnectionState::Closed);
    EXPECT_EQ(connection->sendBufferCapacity(), 0U);
    EXPECT_EQ(connection->receiveBufferCapacity(), 0U);
    EXPECT_EQ(connection->statistics().pumpCallCount, 0U);

    const auto pumped = connection->pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::SocketClosed);

    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved.sendBufferCapacity(), 512U);
    EXPECT_EQ(moved.receiveBufferCapacity(), 1024U);
}

TEST(TlsConnectionTest, PumpCallCountTracksEveryCall)
{
    auto connection = TlsConnection::Create(secureConfig());
    ASSERT_TRUE(connection.has_value());

    for (int iteration = 0; iteration < 3; ++iteration) {
        if (!connection->pump()) {
            break;
        }
    }
    EXPECT_GT(connection->statistics().pumpCallCount, 0U);
}

} // namespace Tina::Tests
