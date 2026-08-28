// TcpListener is the server side, so these tests drive a real client against it
// and care most about the boundaries a listener adds: a capacity that defers rather
// than drops, and an accepted connection that behaves like any other.

#include <tina/network/NetworkErrors.hpp>
#include <tina/network/TcpConnection.hpp>
#include <tina/network/TcpListener.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace Tina::Tests {
namespace {

using Network::TcpConnection;
using Network::TcpConnectionState;
using Network::TcpListener;
using Network::TcpListenerConfig;

[[nodiscard]] TcpListenerConfig listenerConfig(Core::usize connectionCapacity = 4)
{
    TcpListenerConfig config{};
    // Port 0 asks the OS for an ephemeral port, so tests never collide.
    config.localEndpoint = Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 0};
    config.connectionCapacity = connectionCapacity;
    config.backlog = 8;
    return config;
}

[[nodiscard]] Core::Result<TcpConnection> connectTo(const Network::NetworkEndpoint& endpoint)
{
    Network::TcpConnectionConfig config{};
    config.remoteEndpoint = endpoint;
    config.sendBufferBytes = 16 * 1024;
    config.receiveBufferBytes = 16 * 1024;
    return TcpConnection::Create(config);
}

// Pumps both sides until the client connects and the listener has it ready.
[[nodiscard]] bool establish(
    TcpListener& listener,
    TcpConnection& client,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        if (!listener.pump()) {
            return false;
        }
        if (!client.pump()) {
            return false;
        }
        if (client.state() == TcpConnectionState::Connected
            && listener.readyConnectionCount() > 0) {
            return true;
        }
        if (client.state() == TcpConnectionState::Failed) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

[[nodiscard]] std::span<const std::byte> asBytes(std::string_view text) noexcept
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] std::string asText(std::span<const std::byte> bytes)
{
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Moves bytes between two pumped connections until the expected count arrives.
[[nodiscard]] std::string transfer(
    TcpConnection& from,
    TcpConnection& to,
    std::string_view payload,
    int attemptBudget = 4000)
{
    if (!from.send(asBytes(payload))) {
        return {};
    }

    std::string received;
    for (int attempt = 0; attempt < attemptBudget && received.size() < payload.size();
         ++attempt) {
        if (!from.pump() || !to.pump()) {
            break;
        }
        const auto buffered = to.receive();
        if (!buffered) {
            break;
        }
        if (!buffered->empty()) {
            received += asText(*buffered);
            if (!to.consume(buffered->size())) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return received;
}

} // namespace

TEST(TcpListenerTest, RejectsInvalidConfiguration)
{
    {
        auto config = listenerConfig();
        config.connectionCapacity = 0;
        const auto listener = TcpListener::Create(config);
        ASSERT_FALSE(listener.has_value());
        EXPECT_EQ(listener.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = listenerConfig();
        config.backlog = 0;
        const auto listener = TcpListener::Create(config);
        ASSERT_FALSE(listener.has_value());
        EXPECT_EQ(listener.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = listenerConfig();
        config.connectionSendBufferBytes = 0;
        const auto listener = TcpListener::Create(config);
        ASSERT_FALSE(listener.has_value());
        EXPECT_EQ(listener.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        // No address family means no socket domain to create.
        TcpListenerConfig config{};
        config.connectionCapacity = 4;
        config.backlog = 4;
        const auto listener = TcpListener::Create(config);
        ASSERT_FALSE(listener.has_value());
        EXPECT_EQ(listener.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
}

TEST(TcpListenerTest, BindsAndReportsResolvedPort)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());
    EXPECT_TRUE(static_cast<bool>(*listener));

    const auto bound = listener->localEndpoint();
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(bound->address, Network::IpAddress::v4Loopback());
    // The OS must have replaced the requested port 0.
    EXPECT_NE(bound->port, 0);
    EXPECT_EQ(listener->readyConnectionCount(), 0U);
}

TEST(TcpListenerTest, PumpOnIdleListenerAcceptsNothing)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());

    const auto accepted = listener->pump();
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(*accepted, 0U);
    EXPECT_EQ(listener->readyConnectionCount(), 0U);
    EXPECT_EQ(listener->statistics().pumpCallCount, 1U);
}

TEST(TcpListenerTest, AcceptNextFailsWhenNothingIsReady)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());

    const auto connection = listener->acceptNext();
    ASSERT_FALSE(connection.has_value());
    EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::WouldBlock);
}

TEST(TcpListenerTest, AcceptsAClientConnection)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());
    const auto endpoint = listener->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());

    auto client = connectTo(*endpoint);
    ASSERT_TRUE(client.has_value());

    ASSERT_TRUE(establish(*listener, *client));
    EXPECT_EQ(listener->readyConnectionCount(), 1U);

    auto server = listener->acceptNext();
    ASSERT_TRUE(server.has_value());
    // An accepted connection skips the handshake, so it starts Connected.
    EXPECT_EQ(server->state(), TcpConnectionState::Connected);
    EXPECT_EQ(listener->readyConnectionCount(), 0U);
    EXPECT_EQ(listener->statistics().acceptedConnectionCount, 1U);

    // The peer address must be the client's, not the listener's.
    const auto peer = server->remoteEndpoint();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->address, Network::IpAddress::v4Loopback());
    EXPECT_NE(peer->port, endpoint->port);
}

// An accepted connection must be indistinguishable from a dialled one, which is
// the whole point of adopting the socket into the same type.
TEST(TcpListenerTest, AcceptedConnectionTransfersBothDirections)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());
    const auto endpoint = listener->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());

    auto client = connectTo(*endpoint);
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(establish(*listener, *client));

    auto server = listener->acceptNext();
    ASSERT_TRUE(server.has_value());

    EXPECT_EQ(transfer(*client, *server, "client-to-server"), "client-to-server");
    EXPECT_EQ(transfer(*server, *client, "server-to-client"), "server-to-client");
}

TEST(TcpListenerTest, AcceptsSeveralConnectionsInOrder)
{
    auto listener = TcpListener::Create(listenerConfig(4));
    ASSERT_TRUE(listener.has_value());
    const auto endpoint = listener->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());

    std::vector<TcpConnection> clients;
    for (int index = 0; index < 3; ++index) {
        auto client = connectTo(*endpoint);
        ASSERT_TRUE(client.has_value());
        clients.push_back(std::move(*client));
    }

    for (int attempt = 0; attempt < 4000 && listener->readyConnectionCount() < 3; ++attempt) {
        ASSERT_TRUE(listener->pump().has_value());
        for (auto& client : clients) {
            ASSERT_TRUE(client.pump().has_value());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_EQ(listener->readyConnectionCount(), 3U);
    EXPECT_EQ(listener->statistics().acceptedConnectionCount, 3U);

    // Each accept hands over one and leaves the rest.
    for (Core::usize expected = 3; expected > 0; --expected) {
        EXPECT_EQ(listener->readyConnectionCount(), expected);
        const auto server = listener->acceptNext();
        ASSERT_TRUE(server.has_value());
    }
    EXPECT_EQ(listener->readyConnectionCount(), 0U);
}

// Capacity defers rather than drops: the OS backlog still holds the peer, so a
// later pump picks it up once a slot frees.
TEST(TcpListenerTest, CapacityDefersRatherThanDrops)
{
    constexpr Core::usize capacity = 1;
    auto listener = TcpListener::Create(listenerConfig(capacity));
    ASSERT_TRUE(listener.has_value());
    EXPECT_EQ(listener->connectionCapacity(), capacity);
    const auto endpoint = listener->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());

    auto firstClient = connectTo(*endpoint);
    ASSERT_TRUE(firstClient.has_value());
    auto secondClient = connectTo(*endpoint);
    ASSERT_TRUE(secondClient.has_value());

    // Pump enough that both clients have certainly connected at the TCP level.
    for (int attempt = 0; attempt < 400; ++attempt) {
        ASSERT_TRUE(listener->pump().has_value());
        ASSERT_TRUE(firstClient->pump().has_value());
        ASSERT_TRUE(secondClient->pump().has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Only one may be held at a time.
    EXPECT_LE(listener->readyConnectionCount(), capacity);

    const auto first = listener->acceptNext();
    ASSERT_TRUE(first.has_value());

    // With the slot free, the second becomes available -- it was never lost.
    bool secondAccepted = false;
    for (int attempt = 0; attempt < 4000 && !secondAccepted; ++attempt) {
        ASSERT_TRUE(listener->pump().has_value());
        ASSERT_TRUE(secondClient->pump().has_value());
        if (listener->readyConnectionCount() > 0) {
            secondAccepted = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_TRUE(secondAccepted) << "the deferred connection was dropped";
    EXPECT_EQ(listener->statistics().acceptedConnectionCount, 2U);
}

TEST(TcpListenerTest, DropPendingConnectionsClearsTheQueue)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());
    const auto endpoint = listener->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());

    auto client = connectTo(*endpoint);
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(establish(*listener, *client));
    ASSERT_EQ(listener->readyConnectionCount(), 1U);

    listener->dropPendingConnections();
    EXPECT_EQ(listener->readyConnectionCount(), 0U);

    const auto nothing = listener->acceptNext();
    EXPECT_FALSE(nothing.has_value());
}

// Two listeners cannot hold the same port; the second must fail rather than
// silently share it.
TEST(TcpListenerTest, BindingAnAlreadyBoundPortFails)
{
    auto first = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(first.has_value());
    const auto endpoint = first->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());

    auto config = listenerConfig();
    config.localEndpoint = *endpoint;
    const auto second = TcpListener::Create(config);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Network::NetworkErrorCode::AddressUnavailable);
}

TEST(TcpListenerTest, SupportsV6Loopback)
{
    auto config = listenerConfig();
    config.localEndpoint = Network::NetworkEndpoint{Network::IpAddress::v6Loopback(), 0};

    auto listener = TcpListener::Create(config);
    if (!listener.has_value()) {
        GTEST_SKIP() << "IPv6 loopback unavailable on this host";
    }
    const auto endpoint = listener->localEndpoint();
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->address.family(), Network::IpFamily::V6);

    Network::TcpConnectionConfig clientConfig{};
    clientConfig.remoteEndpoint = *endpoint;
    auto client = TcpConnection::Create(clientConfig);
    ASSERT_TRUE(client.has_value());

    ASSERT_TRUE(establish(*listener, *client));
    auto server = listener->acceptNext();
    ASSERT_TRUE(server.has_value());
    EXPECT_EQ(transfer(*client, *server, "v6"), "v6");
}

TEST(TcpListenerTest, RejectsUseFromNonOwnerThread)
{
    auto listener = TcpListener::Create(listenerConfig());
    ASSERT_TRUE(listener.has_value());

    Core::ErrorCode pumpCode{};
    Core::ErrorCode acceptCode{};
    Core::ErrorCode endpointCode{};

    std::thread other{[&]() {
        if (const auto pumped = listener->pump(); !pumped) {
            pumpCode = pumped.error().code;
        }
        if (const auto connection = listener->acceptNext(); !connection) {
            acceptCode = connection.error().code;
        }
        if (const auto endpoint = listener->localEndpoint(); !endpoint) {
            endpointCode = endpoint.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(pumpCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(acceptCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(endpointCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(TcpListenerTest, MovedFromListenerAnswersQueriesInertly)
{
    auto listener = TcpListener::Create(listenerConfig(4));
    ASSERT_TRUE(listener.has_value());
    TcpListener moved{std::move(*listener)};

    EXPECT_FALSE(static_cast<bool>(*listener));
    EXPECT_EQ(listener->connectionCapacity(), 0U);
    EXPECT_EQ(listener->readyConnectionCount(), 0U);
    EXPECT_EQ(listener->statistics().pumpCallCount, 0U);

    const auto pumped = listener->pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::SocketClosed);

    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved.connectionCapacity(), 4U);
}

} // namespace Tina::Tests
