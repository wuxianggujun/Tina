// TcpConnection is a client, so these tests stand up a real listening socket to
// talk to. The listener uses the private detail layer directly because the module
// does not expose a server type yet.

#include "detail/NativeSocket.hpp"

#include <tina/network/NetworkErrors.hpp>
#include <tina/network/TcpConnection.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory_resource>
#include <string>
#include <thread>
#include <vector>

namespace Tina::Tests {
namespace {

using Network::TcpConnection;
using Network::TcpConnectionConfig;
using Network::TcpConnectionState;

// A minimal accepting server on loopback. Non-blocking so a test never wedges if
// the client misbehaves.
class TestListener final {
  public:
    TestListener()
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
        if (::bind(m_listen, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
            closeListen();
            return;
        }
        if (::listen(m_listen, 4) != 0) {
            closeListen();
            return;
        }

        sockaddr_storage bound{};
        auto boundLength = static_cast<Network::Detail::NativeAddressLength>(sizeof(bound));
        if (::getsockname(m_listen, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
            (void)Network::Detail::fromNativeAddress(bound, m_endpoint);
        }
    }

    ~TestListener()
    {
        Network::Detail::closeNativeSocket(m_accepted);
        closeListen();
        if (m_scoped) {
            Network::Detail::TransportScope::release();
        }
    }

    TestListener(const TestListener&) = delete;
    TestListener& operator=(const TestListener&) = delete;

    [[nodiscard]] bool isValid() const noexcept
    {
        return m_listen != Network::Detail::InvalidNativeSocket;
    }
    [[nodiscard]] const Network::NetworkEndpoint& endpoint() const noexcept { return m_endpoint; }
    [[nodiscard]] bool hasAccepted() const noexcept
    {
        return m_accepted != Network::Detail::InvalidNativeSocket;
    }

    // Shrinks the listening socket's receive buffer, which accepted connections
    // inherit. Must be called before the client connects. Used to make
    // backpressure reproducible instead of hoping the kernel splits a large send.
    void requestSmallReceiveWindow(int receiveBytes) noexcept
    {
        Network::Detail::requestNativeSocketBufferSizes(m_listen, 0, receiveBytes);
    }

    // Retries because the client's handshake completes asynchronously.
    bool acceptOne(int attemptBudget = 400)
    {
        for (int attempt = 0; attempt < attemptBudget; ++attempt) {
            const auto accepted = ::accept(m_listen, nullptr, nullptr);
            if (accepted != Network::Detail::InvalidNativeSocket) {
                m_accepted = accepted;
                (void)Network::Detail::setNativeSocketNonBlocking(m_accepted);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    }

    void sendToClient(const std::string& text) const
    {
        if (!hasAccepted()) {
            return;
        }
#if defined(_WIN32)
        ::send(m_accepted, text.data(), static_cast<int>(text.size()), 0);
#else
        ::send(m_accepted, text.data(), text.size(), 0);
#endif
    }

    // Drains whatever the client sent, retrying until the expected size arrives.
    [[nodiscard]] std::string receiveFromClient(
        Core::usize expectedBytes,
        int attemptBudget = 400) const
    {
        std::string collected;
        if (!hasAccepted()) {
            return collected;
        }
        char scratch[4096];
        for (int attempt = 0; attempt < attemptBudget && collected.size() < expectedBytes;
             ++attempt) {
#if defined(_WIN32)
            const int got = ::recv(m_accepted, scratch, static_cast<int>(sizeof(scratch)), 0);
#else
            const ssize_t got = ::recv(m_accepted, scratch, sizeof(scratch), 0);
#endif
            if (got > 0) {
                collected.append(scratch, static_cast<Core::usize>(got));
                continue;
            }
            if (got == 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return collected;
    }

    // Closes the accepted socket so the client observes an orderly end-of-stream.
    void closeAccepted() noexcept
    {
        Network::Detail::closeNativeSocket(m_accepted);
        m_accepted = Network::Detail::InvalidNativeSocket;
    }

  private:
    void closeListen() noexcept
    {
        Network::Detail::closeNativeSocket(m_listen);
        m_listen = Network::Detail::InvalidNativeSocket;
    }

    Network::Detail::NativeSocket m_listen = Network::Detail::InvalidNativeSocket;
    Network::Detail::NativeSocket m_accepted = Network::Detail::InvalidNativeSocket;
    Network::NetworkEndpoint m_endpoint{};
    bool m_scoped = false;
};

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] Core::usize allocationCalls() const noexcept { return m_allocationCalls; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++m_allocationCalls;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    Core::usize m_allocationCalls = 0;
};

[[nodiscard]] TcpConnectionConfig configFor(
    const Network::NetworkEndpoint& remote,
    Core::usize sendBytes = 64 * 1024,
    Core::usize receiveBytes = 64 * 1024,
    std::pmr::memory_resource* resource = nullptr)
{
    TcpConnectionConfig config{};
    config.remoteEndpoint = remote;
    config.sendBufferBytes = sendBytes;
    config.receiveBufferBytes = receiveBytes;
    config.memoryResource = resource;
    return config;
}

// Pumps until the connection leaves Connecting. The handshake completes
// asynchronously, so a single pump legitimately makes no progress.
[[nodiscard]] bool pumpUntilConnected(TcpConnection& connection, int attemptBudget = 400)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        if (connection.state() != TcpConnectionState::Connecting) {
            return connection.state() == TcpConnectionState::Connected;
        }
        const auto pumped = connection.pump();
        if (!pumped) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

[[nodiscard]] std::string asText(std::span<const std::byte> bytes)
{
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::span<const std::byte> asBytes(std::string_view text) noexcept
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

// Pumps until at least expectedBytes are buffered, or the budget runs out.
[[nodiscard]] Core::usize pumpUntilReceived(
    TcpConnection& connection,
    Core::usize expectedBytes,
    int attemptBudget = 400)
{
    Core::usize total = 0;
    for (int attempt = 0; attempt < attemptBudget && total < expectedBytes; ++attempt) {
        const auto pumped = connection.pump();
        if (!pumped) {
            break;
        }
        total += *pumped;
        if (*pumped == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    return total;
}

} // namespace

TEST(TcpConnectionTest, RejectsInvalidConfiguration)
{
    const Network::NetworkEndpoint remote{Network::IpAddress::v4Loopback(), 9000};

    {
        auto config = configFor(remote);
        config.sendBufferBytes = 0;
        const auto connection = TcpConnection::Create(config);
        ASSERT_FALSE(connection.has_value());
        EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = configFor(remote);
        config.receiveBufferBytes = 0;
        const auto connection = TcpConnection::Create(config);
        ASSERT_FALSE(connection.has_value());
        EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        // Port 0 is not a connectable destination.
        auto config = configFor(Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 0});
        const auto connection = TcpConnection::Create(config);
        ASSERT_FALSE(connection.has_value());
        EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidEndpoint);
    }
    {
        auto config = configFor(Network::NetworkEndpoint{Network::IpAddress{}, 9000});
        const auto connection = TcpConnection::Create(config);
        ASSERT_FALSE(connection.has_value());
        EXPECT_EQ(connection.error().code, Network::NetworkErrorCode::InvalidEndpoint);
    }
}

// Create must not block on the handshake, so it returns while still Connecting.
TEST(TcpConnectionTest, CreateStartsInConnectingState)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    EXPECT_TRUE(static_cast<bool>(*connection));
    EXPECT_EQ(connection->state(), TcpConnectionState::Connecting);
}

TEST(TcpConnectionTest, CompletesHandshakeDuringPump)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());

    ASSERT_TRUE(pumpUntilConnected(*connection));
    EXPECT_EQ(connection->state(), TcpConnectionState::Connected);
    ASSERT_TRUE(listener.acceptOne());

    // A local endpoint only exists once the OS has assigned a port.
    const auto local = connection->localEndpoint();
    ASSERT_TRUE(local.has_value());
    EXPECT_NE(local->port, 0);

    const auto remote = connection->remoteEndpoint();
    ASSERT_TRUE(remote.has_value());
    EXPECT_EQ(*remote, listener.endpoint());
}

// Connecting to a port nobody listens on must surface as a failure through pump,
// not through Create.
TEST(TcpConnectionTest, RefusedConnectionFailsDuringPump)
{
    Network::NetworkEndpoint deadEnd{};
    {
        // Bind then drop a listener so the port is almost certainly unused.
        TestListener listener;
        ASSERT_TRUE(listener.isValid());
        deadEnd = listener.endpoint();
    }

    auto connection = TcpConnection::Create(configFor(deadEnd));
    ASSERT_TRUE(connection.has_value());

    bool sawFailure = false;
    for (int attempt = 0; attempt < 400; ++attempt) {
        const auto pumped = connection->pump();
        if (!pumped) {
            EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::ConnectionFailed);
            sawFailure = true;
            break;
        }
        if (connection->state() == TcpConnectionState::Failed) {
            sawFailure = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_TRUE(sawFailure);
    EXPECT_EQ(connection->state(), TcpConnectionState::Failed);
}

TEST(TcpConnectionTest, SendsPayloadToPeer)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    constexpr std::string_view payload = "hello-tina";
    ASSERT_TRUE(connection->send(asBytes(payload)).has_value());
    // send() only queues; bytes leave during pump.
    EXPECT_EQ(connection->statistics().queuedSendBytes, payload.size());

    for (int attempt = 0; attempt < 400 && connection->statistics().queuedSendBytes != 0;
         ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
    }

    EXPECT_EQ(connection->statistics().queuedSendBytes, 0U);
    EXPECT_EQ(listener.receiveFromClient(payload.size()), std::string{payload});
    EXPECT_EQ(connection->statistics().totalSentBytes, payload.size());
}

TEST(TcpConnectionTest, ReceivesPayloadFromPeer)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    const std::string payload = "server-says-hi";
    listener.sendToClient(payload);

    EXPECT_EQ(pumpUntilReceived(*connection, payload.size()), payload.size());

    const auto buffered = connection->receive();
    ASSERT_TRUE(buffered.has_value());
    EXPECT_EQ(asText(*buffered), payload);
    EXPECT_EQ(connection->statistics().totalReceivedBytes, payload.size());
}

// A parser consumes a complete frame and leaves a partial one, so consume() must
// be able to take a prefix rather than the whole buffer.
TEST(TcpConnectionTest, ConsumePrefixKeepsRemainder)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    const std::string payload = "FRAME1FRAME2";
    listener.sendToClient(payload);
    ASSERT_EQ(pumpUntilReceived(*connection, payload.size()), payload.size());

    ASSERT_TRUE(connection->consume(6).has_value());

    const auto remaining = connection->receive();
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(asText(*remaining), "FRAME2");
    EXPECT_EQ(connection->statistics().bufferedReceiveBytes, 6U);

    // Consuming the rest empties the buffer.
    ASSERT_TRUE(connection->consume(6).has_value());
    const auto empty = connection->receive();
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->empty());
}

TEST(TcpConnectionTest, ConsumeRejectsMoreThanBuffered)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));

    const auto tooMany = connection->consume(1);
    ASSERT_FALSE(tooMany.has_value());
    EXPECT_EQ(tooMany.error().code, Core::CoreErrorCode::InvalidArgument);

    // Consuming nothing is legal and a no-op.
    EXPECT_TRUE(connection->consume(0).has_value());
}

// The peer closing its sending half is an orderly end-of-stream, not a failure,
// and bytes already buffered must stay readable.
TEST(TcpConnectionTest, PeerCloseYieldsPeerClosedAndKeepsBufferedBytes)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    const std::string payload = "final-bytes";
    listener.sendToClient(payload);
    ASSERT_EQ(pumpUntilReceived(*connection, payload.size()), payload.size());

    listener.closeAccepted();

    for (int attempt = 0; attempt < 400 && connection->state() == TcpConnectionState::Connected;
         ++attempt) {
        const auto pumped = connection->pump();
        if (!pumped) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_EQ(connection->state(), TcpConnectionState::PeerClosed);

    // The bytes received before the close are still there.
    const auto buffered = connection->receive();
    ASSERT_TRUE(buffered.has_value());
    EXPECT_EQ(asText(*buffered), payload);
}

TEST(TcpConnectionTest, SendRejectsPayloadLargerThanBuffer)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint(), 64, 64));
    ASSERT_TRUE(connection.has_value());
    EXPECT_EQ(connection->sendBufferCapacity(), 64U);

    const std::vector<std::byte> tooBig(65, std::byte{0x21});
    const auto status = connection->send(tooBig);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Network::NetworkErrorCode::CapacityExceeded);
}

TEST(TcpConnectionTest, SendRejectsEmptyPayload)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());

    const auto status = connection->send({});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Network::NetworkErrorCode::InvalidDatagram);
}

// A queued payload must survive until pump can move it, so send before the
// handshake completes is legal.
TEST(TcpConnectionTest, SendBeforeHandshakeIsQueuedAndLaterDelivered)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_EQ(connection->state(), TcpConnectionState::Connecting);

    constexpr std::string_view payload = "queued-early";
    ASSERT_TRUE(connection->send(asBytes(payload)).has_value());
    EXPECT_EQ(connection->statistics().queuedSendBytes, payload.size());

    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    for (int attempt = 0; attempt < 400 && connection->statistics().queuedSendBytes != 0;
         ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
    }

    EXPECT_EQ(listener.receiveFromClient(payload.size()), std::string{payload});
}

TEST(TcpConnectionTest, CloseMovesToClosedAndIsIdempotent)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));

    connection->close();
    EXPECT_EQ(connection->state(), TcpConnectionState::Closed);

    // A second close must not fault or reopen anything.
    connection->close();
    EXPECT_EQ(connection->state(), TcpConnectionState::Closed);

    const auto pumped = connection->pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::ConnectionClosed);

    const auto sent = connection->send(asBytes("after-close"));
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error().code, Network::NetworkErrorCode::ConnectionClosed);
}

// shutdownSend lets the peer observe end-of-stream while this side keeps reading.
TEST(TcpConnectionTest, ShutdownSendLetsPeerSeeEndOfStream)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    constexpr std::string_view payload = "then-eof";
    ASSERT_TRUE(connection->send(asBytes(payload)).has_value());
    for (int attempt = 0; attempt < 400 && connection->statistics().queuedSendBytes != 0;
         ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
    }
    EXPECT_EQ(listener.receiveFromClient(payload.size()), std::string{payload});

    ASSERT_TRUE(connection->shutdownSend().has_value());
    // The connection is still readable, so the state must not be Closed.
    EXPECT_NE(connection->state(), TcpConnectionState::Closed);

    // The peer now reads zero, i.e. end-of-stream.
    EXPECT_TRUE(listener.receiveFromClient(1, 100).empty());

    // And the server can still reach this side.
    const std::string reply = "server-reply";
    listener.sendToClient(reply);
    EXPECT_EQ(pumpUntilReceived(*connection, reply.size()), reply.size());
}

TEST(TcpConnectionTest, RejectsUseFromNonOwnerThread)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));

    Core::ErrorCode sendCode{};
    Core::ErrorCode pumpCode{};
    Core::ErrorCode receiveCode{};
    Core::ErrorCode consumeCode{};

    std::thread other{[&]() {
        if (const auto status = connection->send(asBytes("x")); !status) {
            sendCode = status.error().code;
        }
        if (const auto pumped = connection->pump(); !pumped) {
            pumpCode = pumped.error().code;
        }
        if (const auto received = connection->receive(); !received) {
            receiveCode = received.error().code;
        }
        if (const auto consumed = connection->consume(0); !consumed) {
            consumeCode = consumed.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(sendCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(pumpCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(receiveCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(consumeCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(TcpConnectionTest, MovedFromConnectionAnswersQueriesInertly)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint(), 128, 256));
    ASSERT_TRUE(connection.has_value());
    TcpConnection moved{std::move(*connection)};

    EXPECT_FALSE(static_cast<bool>(*connection));
    EXPECT_EQ(connection->state(), TcpConnectionState::Closed);
    EXPECT_EQ(connection->sendBufferCapacity(), 0U);
    EXPECT_EQ(connection->receiveBufferCapacity(), 0U);
    EXPECT_EQ(connection->statistics().pumpCallCount, 0U);

    const auto pumped = connection->pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::SocketClosed);

    // The destination owns everything.
    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved.sendBufferCapacity(), 128U);
    EXPECT_EQ(moved.receiveBufferCapacity(), 256U);
}

// Create is the only allocation point; pumping must not grow storage.
TEST(TcpConnectionTest, SteadyStatePumpPerformsNoAllocations)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    TrackingMemoryResource memory;
    auto connection = TcpConnection::Create(
        configFor(listener.endpoint(), 4096, 4096, &memory));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    // Warm up so first-call bookkeeping is already paid for.
    ASSERT_TRUE(connection->send(asBytes("warmup")).has_value());
    for (int attempt = 0; attempt < 100 && connection->statistics().queuedSendBytes != 0;
         ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
    }

    const Core::usize baseline = memory.allocationCalls();

    for (int iteration = 0; iteration < 100; ++iteration) {
        ASSERT_TRUE(connection->send(asBytes("steady")).has_value());
        ASSERT_TRUE(connection->pump().has_value());
        const auto buffered = connection->receive();
        ASSERT_TRUE(buffered.has_value());
        if (!buffered->empty()) {
            ASSERT_TRUE(connection->consume(buffered->size()).has_value());
        }
    }

    EXPECT_EQ(memory.allocationCalls(), baseline);
}

TEST(TcpConnectionTest, PumpCallCountTracksEveryCall)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    auto connection = TcpConnection::Create(configFor(listener.endpoint()));
    ASSERT_TRUE(connection.has_value());

    const auto before = connection->statistics().pumpCallCount;
    for (int iteration = 0; iteration < 3; ++iteration) {
        (void)connection->pump();
    }
    EXPECT_EQ(connection->statistics().pumpCallCount, before + 3);
}

// A large transfer must arrive byte-exact across however many pumps it takes.
// Loopback send buffers are generous, so the number of pumps is not fixed -- what
// matters is that no bytes are lost or reordered when it does take several.
TEST(TcpConnectionTest, LargeTransferArrivesByteExact)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    constexpr Core::usize payloadBytes = 256 * 1024;
    auto connection = TcpConnection::Create(
        configFor(listener.endpoint(), payloadBytes, 64 * 1024));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    // A repeating pattern rather than a constant, so a duplicated or dropped run
    // of bytes shows up instead of cancelling out.
    std::vector<std::byte> payload(payloadBytes);
    for (Core::usize index = 0; index < payloadBytes; ++index) {
        payload[index] = static_cast<std::byte>(index % 251);
    }
    ASSERT_TRUE(connection->send(payload).has_value());

    std::string collected;
    for (int attempt = 0; attempt < 20000 && collected.size() < payloadBytes; ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
        collected += listener.receiveFromClient(payloadBytes - collected.size(), 1);
    }

    ASSERT_EQ(collected.size(), payloadBytes);
    EXPECT_EQ(connection->statistics().totalSentBytes, payloadBytes);
    EXPECT_EQ(connection->statistics().queuedSendBytes, 0U);

    for (Core::usize index = 0; index < payloadBytes; ++index) {
        ASSERT_EQ(
            static_cast<unsigned char>(collected[index]),
            static_cast<unsigned char>(index % 251))
            << "payload diverged at byte " << index;
    }
}

// Forces real backpressure by never draining the server: once both kernel buffers
// fill, send cannot take everything and the queue must persist across pumps
// without losing the tail. This is the path partialSendCount counts.
TEST(TcpConnectionTest, BackpressureKeepsUnsentTailAcrossPumps)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());
    // Shrink both windows before connecting. Merely sending "something large" was
    // not reliable -- loopback swallowed 8 MB whole -- so the buffers that bound a
    // single send are constrained explicitly.
    listener.requestSmallReceiveWindow(2048);

    constexpr Core::usize payloadBytes = 8 * 1024 * 1024;
    auto config = configFor(listener.endpoint(), payloadBytes, 4096);
    config.kernelSendBufferBytes = 2048;
    config.kernelReceiveBufferBytes = 2048;
    auto connection = TcpConnection::Create(config);
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    const std::vector<std::byte> payload(payloadBytes, std::byte{0x5A});
    ASSERT_TRUE(connection->send(payload).has_value());
    ASSERT_EQ(connection->statistics().queuedSendBytes, payloadBytes);

    // Pump without reading on the server until the queue stops shrinking for
    // several consecutive pumps. Requiring a run of no-progress avoids mistaking
    // a single unchanged reading for a genuine stall.
    Core::usize previousQueue = payloadBytes;
    int noProgressStreak = 0;
    for (int attempt = 0; attempt < 4000 && noProgressStreak < 20; ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
        const Core::usize queued = connection->statistics().queuedSendBytes;
        if (queued == 0) {
            break;
        }
        noProgressStreak = (queued == previousQueue) ? noProgressStreak + 1 : 0;
        previousQueue = queued;
    }

    const auto stats = connection->statistics();

    // Whether a stall is reachable depends on the platform's socket buffers, and
    // this host absorbs multi-megabyte sends on loopback even with SO_SNDBUF
    // shrunk. So the invariant asserted here is the one that must hold either way:
    // no byte is lost or double-counted, whatever the split.
    EXPECT_EQ(stats.totalSentBytes + stats.queuedSendBytes, payloadBytes);

    if (stats.queuedSendBytes == 0) {
        // Fully absorbed: partialSendCount may legitimately be zero, and there is
        // no retained tail to verify.
        EXPECT_EQ(stats.totalSentBytes, payloadBytes);
        GTEST_SKIP() << "host absorbed the whole payload; backpressure not reachable here";
    }

    // A stall did happen, so the split path ran and the tail must survive.
    EXPECT_GT(stats.partialSendCount, 0U);

    for (int attempt = 0; attempt < 20000 && connection->statistics().queuedSendBytes != 0;
         ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
        (void)listener.receiveFromClient(64 * 1024, 1);
    }

    EXPECT_EQ(connection->statistics().queuedSendBytes, 0U);
    EXPECT_EQ(connection->statistics().totalSentBytes, payloadBytes);
}

// The partial-send path must be verifiable without depending on kernel buffer
// behaviour. A send buffer smaller than the payload makes the split deterministic:
// the caller has to issue several sends, and each queued chunk must go out whole
// and in order.
TEST(TcpConnectionTest, SmallSendBufferForcesSequentialChunks)
{
    TestListener listener;
    ASSERT_TRUE(listener.isValid());

    constexpr Core::usize chunkBytes = 1024;
    constexpr int chunkCount = 64;
    auto connection = TcpConnection::Create(
        configFor(listener.endpoint(), chunkBytes, 4096));
    ASSERT_TRUE(connection.has_value());
    ASSERT_TRUE(pumpUntilConnected(*connection));
    ASSERT_TRUE(listener.acceptOne());

    std::string expected;
    for (int chunk = 0; chunk < chunkCount; ++chunk) {
        // Distinct content per chunk so a reordering or duplication is visible.
        const std::string payload(chunkBytes, static_cast<char>('A' + (chunk % 26)));
        expected += payload;

        // The buffer holds exactly one chunk, so it must be drained before the
        // next send is accepted.
        for (int attempt = 0; attempt < 2000; ++attempt) {
            if (connection->send(asBytes(payload)).has_value()) {
                break;
            }
            ASSERT_TRUE(connection->pump().has_value());
        }
        ASSERT_TRUE(connection->pump().has_value());
    }

    std::string collected;
    for (int attempt = 0; attempt < 20000 && collected.size() < expected.size(); ++attempt) {
        ASSERT_TRUE(connection->pump().has_value());
        collected += listener.receiveFromClient(expected.size() - collected.size(), 1);
    }

    ASSERT_EQ(collected.size(), expected.size());
    EXPECT_EQ(collected, expected);
    EXPECT_EQ(connection->statistics().totalSentBytes, expected.size());
}

} // namespace Tina::Tests
