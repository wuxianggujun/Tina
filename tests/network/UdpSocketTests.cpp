#include <tina/network/NetworkErrors.hpp>
#include <tina/network/UdpSocket.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace Tina::Tests {
namespace {

// Counts allocation traffic so steady-state claims can be asserted rather than
// assumed.
class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] Core::usize allocationCalls() const noexcept { return m_allocationCalls; }
    [[nodiscard]] Core::usize outstandingAllocations() const noexcept
    {
        return m_allocationCalls - m_deallocationCalls;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++m_allocationCalls;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        ++m_deallocationCalls;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    Core::usize m_allocationCalls = 0;
    Core::usize m_deallocationCalls = 0;
};

[[nodiscard]] Network::UdpSocketConfig loopbackConfig(
    Core::usize queueCapacity = 8,
    std::pmr::memory_resource* resource = nullptr)
{
    Network::UdpSocketConfig config{};
    // Port 0 asks the OS for an ephemeral port, so tests never collide on a
    // fixed one.
    config.localEndpoint = Network::NetworkEndpoint{Network::IpAddress::v4Loopback(), 0};
    config.receiveQueueCapacity = queueCapacity;
    config.memoryResource = resource;
    return config;
}

[[nodiscard]] std::span<const std::byte> asBytes(std::string_view text) noexcept
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] std::string_view asText(std::span<const std::byte> payload) noexcept
{
    return std::string_view{reinterpret_cast<const char*>(payload.data()), payload.size()};
}

// Drains until at least one datagram arrives or the budget is exhausted. UDP on
// loopback is effectively immediate but not synchronous, so a single receive()
// can legitimately return empty.
[[nodiscard]] std::vector<std::string> drainTexts(
    Network::UdpSocket& socket,
    Core::usize expectedCount,
    int attemptBudget = 200)
{
    std::vector<std::string> texts;
    for (int attempt = 0; attempt < attemptBudget && texts.size() < expectedCount; ++attempt) {
        auto received = socket.receive();
        if (!received) {
            break;
        }
        for (const auto& datagram : *received) {
            texts.emplace_back(asText(datagram.payload));
        }
        if (received->empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    return texts;
}

} // namespace

TEST(UdpSocketTest, CreateBindsAndReportsResolvedPort)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(socket.has_value());
    EXPECT_TRUE(static_cast<bool>(*socket));

    const auto bound = socket->localEndpoint();
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(bound->address, Network::IpAddress::v4Loopback());
    // The OS must have replaced the requested port 0 with a real one.
    EXPECT_NE(bound->port, 0);
}

TEST(UdpSocketTest, RejectsInvalidConfiguration)
{
    {
        auto config = loopbackConfig();
        config.receiveQueueCapacity = 0;
        const auto socket = Network::UdpSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = loopbackConfig();
        config.maximumDatagramBytes = 0;
        const auto socket = Network::UdpSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        auto config = loopbackConfig();
        config.maximumDatagramBytes = Network::MaximumDatagramBytes + 1;
        const auto socket = Network::UdpSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        // An endpoint with no family cannot select a socket domain.
        Network::UdpSocketConfig config{};
        config.receiveQueueCapacity = 4;
        const auto socket = Network::UdpSocket::Create(config);
        ASSERT_FALSE(socket.has_value());
        EXPECT_EQ(socket.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
}

// Every failed Create must give back whatever it acquired. On Windows that
// includes the process-wide Winsock refcount; a leak there would keep the
// library initialised forever, and an over-release would tear it down under a
// live socket. Neither is observable directly, so this drives the failure path
// repeatedly and then proves a real socket still works.
TEST(UdpSocketTest, RepeatedFailedCreateLeavesTransportUsable)
{
    for (int iteration = 0; iteration < 50; ++iteration) {
        auto badCapacity = loopbackConfig();
        badCapacity.receiveQueueCapacity = 0;
        EXPECT_FALSE(Network::UdpSocket::Create(badCapacity).has_value());

        auto badSize = loopbackConfig();
        badSize.maximumDatagramBytes = Network::MaximumDatagramBytes + 1;
        EXPECT_FALSE(Network::UdpSocket::Create(badSize).has_value());

        Network::UdpSocketConfig noFamily{};
        noFamily.receiveQueueCapacity = 4;
        EXPECT_FALSE(Network::UdpSocket::Create(noFamily).has_value());
    }

    // If the failure paths had unbalanced the refcount, this would fail.
    auto receiver = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());
    ASSERT_TRUE(sender->send(*target, asBytes("after-failures")).has_value());

    const auto texts = drainTexts(*receiver, 1);
    ASSERT_EQ(texts.size(), 1U);
    EXPECT_EQ(texts.front(), "after-failures");
}

// Binding a port that is already bound must fail cleanly rather than silently
// producing a second socket on the same endpoint.
TEST(UdpSocketTest, BindingAnAlreadyBoundPortFails)
{
    auto first = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(first.has_value());
    const auto bound = first->localEndpoint();
    ASSERT_TRUE(bound.has_value());

    Network::UdpSocketConfig sameEndpoint = loopbackConfig();
    sameEndpoint.localEndpoint = *bound;

    const auto second = Network::UdpSocket::Create(sameEndpoint);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Network::NetworkErrorCode::AddressUnavailable);

    // The original socket is unaffected by the failed attempt.
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(sender->send(*bound, asBytes("still-mine")).has_value());

    const auto texts = drainTexts(*first, 1);
    ASSERT_EQ(texts.size(), 1U);
    EXPECT_EQ(texts.front(), "still-mine");
}

TEST(UdpSocketTest, SendsAndReceivesDatagramWithSenderEndpoint)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto receiverEndpoint = receiver->localEndpoint();
    ASSERT_TRUE(receiverEndpoint.has_value());
    const auto senderEndpoint = sender->localEndpoint();
    ASSERT_TRUE(senderEndpoint.has_value());

    ASSERT_TRUE(sender->send(*receiverEndpoint, asBytes("tina")).has_value());

    std::vector<Network::NetworkEndpoint> observedSenders;
    std::vector<std::string> texts;
    for (int attempt = 0; attempt < 200 && texts.empty(); ++attempt) {
        auto received = receiver->receive();
        ASSERT_TRUE(received.has_value());
        for (const auto& datagram : *received) {
            texts.emplace_back(asText(datagram.payload));
            observedSenders.push_back(datagram.sender);
        }
        if (received->empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    ASSERT_EQ(texts.size(), 1U);
    EXPECT_EQ(texts.front(), "tina");
    ASSERT_EQ(observedSenders.size(), 1U);
    EXPECT_EQ(observedSenders.front(), *senderEndpoint);
}

TEST(UdpSocketTest, PreservesArrivalOrderWithinOneReceive)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig(16));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    constexpr std::array<std::string_view, 4> payloads{"one", "two", "three", "four"};
    for (const auto payload : payloads) {
        ASSERT_TRUE(sender->send(*target, asBytes(payload)).has_value());
    }

    const auto texts = drainTexts(*receiver, payloads.size());
    ASSERT_EQ(texts.size(), payloads.size());
    for (Core::usize index = 0; index < payloads.size(); ++index) {
        EXPECT_EQ(texts[index], payloads[index]);
    }
}

TEST(UdpSocketTest, RejectsEmptyAndOversizedPayloads)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(socket.has_value());
    const auto target = socket->localEndpoint();
    ASSERT_TRUE(target.has_value());

    const auto empty = socket->send(*target, {});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, Network::NetworkErrorCode::InvalidDatagram);

    const std::vector<std::byte> tooLarge(socket->maximumDatagramBytes() + 1, std::byte{0x5A});
    const auto oversized = socket->send(*target, tooLarge);
    ASSERT_FALSE(oversized.has_value());
    EXPECT_EQ(oversized.error().code, Network::NetworkErrorCode::DatagramTooLarge);
}

TEST(UdpSocketTest, RejectsInvalidDestinations)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(socket.has_value());

    {
        // Port 0 is not a deliverable destination.
        const Network::NetworkEndpoint zeroPort{Network::IpAddress::v4Loopback(), 0};
        const auto status = socket->send(zeroPort, asBytes("x"));
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, Network::NetworkErrorCode::InvalidEndpoint);
    }
    {
        const Network::NetworkEndpoint noAddress{Network::IpAddress{}, 9000};
        const auto status = socket->send(noAddress, asBytes("x"));
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, Network::NetworkErrorCode::InvalidEndpoint);
    }
    {
        // A V4-bound socket cannot address a V6 peer.
        const Network::NetworkEndpoint mismatched{Network::IpAddress::v6Loopback(), 9000};
        const auto status = socket->send(mismatched, asBytes("x"));
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, Network::NetworkErrorCode::InvalidEndpoint);
    }
}

TEST(UdpSocketTest, ReceiveOnIdleSocketReturnsEmptyWithoutBlocking)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(socket.has_value());

    const auto received = socket->receive();
    ASSERT_TRUE(received.has_value());
    EXPECT_TRUE(received->empty());
    EXPECT_EQ(socket->statistics().lastReceivedDatagramCount, 0U);
    EXPECT_EQ(socket->statistics().receiveCallCount, 1U);
}

// A datagram exactly at the advertised maximum must arrive intact. The slot is
// deliberately one byte larger so this case is distinguishable from truncation
// rather than being discarded with it.
TEST(UdpSocketTest, ExactlyMaximumSizedPayloadIsDelivered)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig(4));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    const std::vector<std::byte> payload(
        receiver->maximumDatagramBytes(),
        std::byte{0x3C});
    ASSERT_EQ(payload.size(), Network::MaximumDatagramBytes);
    ASSERT_TRUE(sender->send(*target, payload).has_value());

    Core::usize receivedBytes = 0;
    for (int attempt = 0; attempt < 200 && receivedBytes == 0; ++attempt) {
        auto received = receiver->receive();
        ASSERT_TRUE(received.has_value());
        for (const auto& datagram : *received) {
            receivedBytes = datagram.payload.size();
        }
        if (received->empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    EXPECT_EQ(receivedBytes, payload.size());
    const auto stats = receiver->statistics();
    EXPECT_EQ(stats.totalReceivedDatagramCount, 1U);
    // Nothing was discarded: this payload is exactly at the limit, not over it.
    EXPECT_EQ(stats.totalOversizedDatagramCount, 0U);
    EXPECT_EQ(stats.totalDiscardedDatagramCount, 0U);
}

// A sender configured with a larger maximum can emit a datagram this receiver
// cannot represent. It must be counted and discarded, never truncated.
TEST(UdpSocketTest, OversizedDatagramIsDiscardedAndCounted)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig(4));
    ASSERT_TRUE(receiver.has_value());

    // Bind a second socket whose cap is smaller, then send from the default-cap
    // socket so the payload exceeds what the small receiver accepts.
    Network::UdpSocketConfig smallConfig = loopbackConfig(4);
    smallConfig.maximumDatagramBytes = 64;
    auto smallReceiver = Network::UdpSocket::Create(smallConfig);
    ASSERT_TRUE(smallReceiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = smallReceiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    const std::vector<std::byte> payload(200, std::byte{0x11});
    ASSERT_GT(payload.size(), smallReceiver->maximumDatagramBytes());
    ASSERT_TRUE(sender->send(*target, payload).has_value());

    // Also send one the receiver can represent, so the drain has something to
    // return and the test does not depend on timing alone.
    const std::vector<std::byte> small(8, std::byte{0x22});
    ASSERT_TRUE(sender->send(*target, small).has_value());

    Core::usize delivered = 0;
    Core::usize deliveredBytes = 0;
    for (int attempt = 0; attempt < 200 && delivered == 0; ++attempt) {
        auto received = smallReceiver->receive();
        ASSERT_TRUE(received.has_value());
        for (const auto& datagram : *received) {
            ++delivered;
            deliveredBytes = datagram.payload.size();
        }
        if (received->empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    // The oversized one never surfaces; the small one does, unmangled.
    EXPECT_EQ(delivered, 1U);
    EXPECT_EQ(deliveredBytes, small.size());

    const auto stats = smallReceiver->statistics();
    EXPECT_EQ(stats.totalOversizedDatagramCount, 1U);
    EXPECT_EQ(stats.totalDiscardedDatagramCount, 1U);
    EXPECT_EQ(stats.totalReceivedDatagramCount, 1U);
    EXPECT_EQ(stats.totalReceivedBytes, small.size());
}

// A full queue defers rather than discards, so the discard counter must stay at
// zero even when a burst exceeds capacity.
TEST(UdpSocketTest, FullQueueDefersWithoutCountingDiscards)
{
    constexpr Core::usize queueCapacity = 2;
    auto receiver = Network::UdpSocket::Create(loopbackConfig(queueCapacity));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    constexpr std::array<std::string_view, 4> payloads{"w", "x", "y", "z"};
    for (const auto payload : payloads) {
        ASSERT_TRUE(sender->send(*target, asBytes(payload)).has_value());
    }

    const auto texts = drainTexts(*receiver, payloads.size());

    // Every datagram eventually arrives, in order, across multiple calls.
    ASSERT_EQ(texts.size(), payloads.size());
    for (Core::usize index = 0; index < payloads.size(); ++index) {
        EXPECT_EQ(texts[index], payloads[index]);
    }
    EXPECT_EQ(receiver->statistics().totalDiscardedDatagramCount, 0U);
}

// The queue is a hard bound: a burst larger than capacity is split across
// receive() calls rather than growing storage.
TEST(UdpSocketTest, ReceiveStopsAtQueueCapacity)
{
    constexpr Core::usize queueCapacity = 2;
    auto receiver = Network::UdpSocket::Create(loopbackConfig(queueCapacity));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    for (const auto payload : {"a", "b", "c", "d"}) {
        ASSERT_TRUE(sender->send(*target, asBytes(payload)).has_value());
    }

    Core::usize totalReceived = 0;
    Core::usize maximumPerCall = 0;
    for (int attempt = 0; attempt < 200 && totalReceived < 4; ++attempt) {
        auto received = receiver->receive();
        ASSERT_TRUE(received.has_value());
        maximumPerCall = std::max(maximumPerCall, received->size());
        totalReceived += received->size();
        if (received->empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    EXPECT_LE(maximumPerCall, queueCapacity);
    EXPECT_EQ(receiver->receiveQueueCapacity(), queueCapacity);
}

// Payload spans borrow socket storage, so the previous batch must be consumed
// before calling receive() again.
TEST(UdpSocketTest, ReceiveInvalidatesPreviousBatchStorage)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig(4));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    ASSERT_TRUE(sender->send(*target, asBytes("first")).has_value());
    const auto firstTexts = drainTexts(*receiver, 1);
    ASSERT_EQ(firstTexts.size(), 1U);
    EXPECT_EQ(firstTexts.front(), "first");

    ASSERT_TRUE(sender->send(*target, asBytes("second")).has_value());
    const auto secondTexts = drainTexts(*receiver, 1);
    ASSERT_EQ(secondTexts.size(), 1U);
    EXPECT_EQ(secondTexts.front(), "second");
}

TEST(UdpSocketTest, StatisticsTrackSentAndReceivedTotals)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig(8));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    constexpr std::string_view payload = "metrics";
    ASSERT_TRUE(sender->send(*target, asBytes(payload)).has_value());
    ASSERT_TRUE(sender->send(*target, asBytes(payload)).has_value());

    const auto senderStats = sender->statistics();
    EXPECT_EQ(senderStats.totalSentDatagramCount, 2U);
    EXPECT_EQ(senderStats.totalSentBytes, payload.size() * 2U);

    const auto texts = drainTexts(*receiver, 2);
    ASSERT_EQ(texts.size(), 2U);

    const auto receiverStats = receiver->statistics();
    EXPECT_EQ(receiverStats.totalReceivedDatagramCount, 2U);
    EXPECT_EQ(receiverStats.totalReceivedBytes, payload.size() * 2U);
    EXPECT_GT(receiverStats.receiveCallCount, 0U);
}

// Create performs the only allocation; send/receive must not grow storage.
TEST(UdpSocketTest, SteadyStateSendReceivePerformsNoPmrAllocations)
{
    TrackingMemoryResource receiverMemory;
    auto receiver = Network::UdpSocket::Create(loopbackConfig(8, &receiverMemory));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    // Warm up so any first-call bookkeeping is already paid for.
    ASSERT_TRUE(sender->send(*target, asBytes("warmup")).has_value());
    (void)drainTexts(*receiver, 1);

    const Core::usize allocationBaseline = receiverMemory.allocationCalls();

    for (Core::usize iteration = 0; iteration < 100U; ++iteration) {
        ASSERT_TRUE(sender->send(*target, asBytes("steady")).has_value());
        (void)drainTexts(*receiver, 1);
    }

    EXPECT_EQ(receiverMemory.allocationCalls(), allocationBaseline);
}

TEST(UdpSocketTest, RejectsUseFromNonOwnerThread)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(socket.has_value());
    const auto target = socket->localEndpoint();
    ASSERT_TRUE(target.has_value());

    Core::ErrorCode sendCode{};
    Core::ErrorCode receiveCode{};
    Core::ErrorCode endpointCode{};

    std::thread other{[&]() {
        const auto sendStatus = socket->send(*target, asBytes("x"));
        if (!sendStatus) {
            sendCode = sendStatus.error().code;
        }
        const auto received = socket->receive();
        if (!received) {
            receiveCode = received.error().code;
        }
        const auto endpoint = socket->localEndpoint();
        if (!endpoint) {
            endpointCode = endpoint.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(sendCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(receiveCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(endpointCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(UdpSocketTest, MoveConstructionTransfersOwnership)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(socket.has_value());
    const auto boundBefore = socket->localEndpoint();
    ASSERT_TRUE(boundBefore.has_value());

    Network::UdpSocket moved{std::move(*socket)};
    EXPECT_TRUE(static_cast<bool>(moved));
    // The moved-from socket must be inert rather than sharing the descriptor.
    EXPECT_FALSE(static_cast<bool>(*socket));

    const auto boundAfter = moved.localEndpoint();
    ASSERT_TRUE(boundAfter.has_value());
    EXPECT_EQ(*boundAfter, *boundBefore);

    const auto closed = socket->localEndpoint();
    ASSERT_FALSE(closed.has_value());
    EXPECT_EQ(closed.error().code, Network::NetworkErrorCode::SocketClosed);
}

// A moved-from socket must answer every query inertly rather than dereferencing
// a null impl. These are the accessors a caller is most likely to reach for
// without first checking operator bool().
TEST(UdpSocketTest, MovedFromSocketAnswersQueriesInertly)
{
    auto socket = Network::UdpSocket::Create(loopbackConfig(8));
    ASSERT_TRUE(socket.has_value());
    Network::UdpSocket moved{std::move(*socket)};

    EXPECT_EQ(socket->receiveQueueCapacity(), 0U);
    EXPECT_EQ(socket->maximumDatagramBytes(), 0U);

    // Zeroed rather than stale: reading a live socket's counters through a
    // moved-from handle would be worse than reporting nothing.
    const auto stats = socket->statistics();
    EXPECT_EQ(stats.receiveCallCount, 0U);
    EXPECT_EQ(stats.totalSentDatagramCount, 0U);
    EXPECT_EQ(stats.totalReceivedDatagramCount, 0U);

    const auto received = socket->receive();
    ASSERT_FALSE(received.has_value());
    EXPECT_EQ(received.error().code, Network::NetworkErrorCode::SocketClosed);

    const Network::NetworkEndpoint target{Network::IpAddress::v4Loopback(), 9000};
    const auto sent = socket->send(target, asBytes("x"));
    ASSERT_FALSE(sent.has_value());
    EXPECT_EQ(sent.error().code, Network::NetworkErrorCode::SocketClosed);

    // The destination socket is fully functional.
    EXPECT_EQ(moved.receiveQueueCapacity(), 8U);
    EXPECT_EQ(moved.maximumDatagramBytes(), Network::MaximumDatagramBytes);
}

// The advertised config values must be readable back, including a non-default
// maximumDatagramBytes -- a caller sizing its own buffers depends on this.
TEST(UdpSocketTest, ReportsConfiguredCapacities)
{
    Network::UdpSocketConfig config = loopbackConfig(16);
    config.maximumDatagramBytes = 512;

    auto socket = Network::UdpSocket::Create(config);
    ASSERT_TRUE(socket.has_value());
    EXPECT_EQ(socket->receiveQueueCapacity(), 16U);
    EXPECT_EQ(socket->maximumDatagramBytes(), 512U);

    // Defaults are what the header documents.
    auto defaulted = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(defaulted.has_value());
    EXPECT_EQ(defaulted->maximumDatagramBytes(), Network::MaximumDatagramBytes);
}

// receive() bounds its syscalls, not just its output, because discarded
// datagrams consume no slot.
TEST(UdpSocketTest, ReceiveBoundsSyscallsWhenEveryDatagramIsDiscarded)
{
    constexpr Core::usize queueCapacity = 2;

    // send() refuses empty payloads, so oversized datagrams are the way to make
    // every arrival discardable: they are dropped without consuming a slot, which
    // is exactly the branch the syscall budget exists to bound.
    Network::UdpSocketConfig smallConfig = loopbackConfig(queueCapacity);
    smallConfig.maximumDatagramBytes = 32;
    auto smallReceiver = Network::UdpSocket::Create(smallConfig);
    ASSERT_TRUE(smallReceiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto smallTarget = smallReceiver->localEndpoint();
    ASSERT_TRUE(smallTarget.has_value());

    const std::vector<std::byte> oversized(200, std::byte{0x44});
    for (int index = 0; index < 12; ++index) {
        ASSERT_TRUE(sender->send(*smallTarget, oversized).has_value());
    }

    // One call must terminate on its own budget rather than draining all 12.
    const auto received = smallReceiver->receive();
    ASSERT_TRUE(received.has_value());
    EXPECT_TRUE(received->empty());

    const auto stats = smallReceiver->statistics();
    EXPECT_EQ(stats.receiveCallCount, 1U);
    // Budget is capacity * 2, so a single call cannot have discarded all of them.
    EXPECT_LE(stats.lastDiscardedDatagramCount, queueCapacity * 2);
    EXPECT_GT(stats.lastDiscardedDatagramCount, 0U);
    EXPECT_EQ(stats.totalReceivedDatagramCount, 0U);
}

// last* counters describe the most recent call only, so a later quiet call must
// reset them while total* keeps accumulating.
TEST(UdpSocketTest, LastCountersResetPerCallWhileTotalsAccumulate)
{
    auto receiver = Network::UdpSocket::Create(loopbackConfig(8));
    ASSERT_TRUE(receiver.has_value());
    auto sender = Network::UdpSocket::Create(loopbackConfig());
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());

    ASSERT_TRUE(sender->send(*target, asBytes("first")).has_value());
    ASSERT_EQ(drainTexts(*receiver, 1).size(), 1U);
    EXPECT_EQ(receiver->statistics().lastReceivedDatagramCount, 1U);

    // An idle call clears last* but leaves total* alone.
    const auto idle = receiver->receive();
    ASSERT_TRUE(idle.has_value());
    EXPECT_TRUE(idle->empty());

    const auto stats = receiver->statistics();
    EXPECT_EQ(stats.lastReceivedDatagramCount, 0U);
    EXPECT_EQ(stats.totalReceivedDatagramCount, 1U);
    EXPECT_EQ(stats.totalReceivedBytes, 5U);
}

TEST(UdpSocketTest, SupportsV6Loopback)
{
    Network::UdpSocketConfig config{};
    config.localEndpoint = Network::NetworkEndpoint{Network::IpAddress::v6Loopback(), 0};
    config.receiveQueueCapacity = 4;

    auto receiver = Network::UdpSocket::Create(config);
    if (!receiver.has_value()) {
        // A host with IPv6 disabled cannot bind; that is an environment property,
        // not a contract failure.
        GTEST_SKIP() << "IPv6 loopback unavailable on this host";
    }
    auto sender = Network::UdpSocket::Create(config);
    ASSERT_TRUE(sender.has_value());

    const auto target = receiver->localEndpoint();
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->address.family(), Network::IpFamily::V6);

    ASSERT_TRUE(sender->send(*target, asBytes("v6")).has_value());
    const auto texts = drainTexts(*receiver, 1);
    ASSERT_EQ(texts.size(), 1U);
    EXPECT_EQ(texts.front(), "v6");
}

} // namespace Tina::Tests
