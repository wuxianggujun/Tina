// Exercises the private readiness layer directly. Tests reach into src/network
// because the poller deals in native socket handles, which must never appear in
// a public header.

#include "detail/NativeSocket.hpp"
#include "detail/ReadinessPoller.hpp"

#include <tina/network/NetworkErrors.hpp>
#include <tina/network/UdpSocket.hpp>

#include <gtest/gtest.h>

#include <chrono>
// std::strlen is used below; MSVC leaks it in transitively, GCC does not.
#include <cstring>
#include <memory_resource>
#include <thread>
#include <vector>

namespace Tina::Tests {
namespace {

using Network::Detail::ReadinessEvent;
using Network::Detail::ReadinessInterest;
using Network::Detail::ReadinessPoller;

// A real bound UDP socket, so the poller is driven by the OS rather than a stub.
// Owns the descriptor and the process-wide transport refcount.
class BoundSocket final {
  public:
    BoundSocket()
    {
        const auto status = Network::Detail::TransportScope::acquire();
        m_scoped = status.has_value();

        m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == Network::Detail::InvalidNativeSocket) {
            return;
        }
        (void)Network::Detail::setNativeSocketNonBlocking(m_socket);

        const Network::NetworkEndpoint local{Network::IpAddress::v4Loopback(), 0};
        sockaddr_storage address{};
        const auto length = Network::Detail::toNativeAddress(local, address);
        if (::bind(m_socket, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
            Network::Detail::closeNativeSocket(m_socket);
            m_socket = Network::Detail::InvalidNativeSocket;
            return;
        }

        sockaddr_storage bound{};
        auto boundLength = static_cast<Network::Detail::NativeAddressLength>(sizeof(bound));
        if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
            (void)Network::Detail::fromNativeAddress(bound, m_endpoint);
        }
    }

    ~BoundSocket()
    {
        Network::Detail::closeNativeSocket(m_socket);
        if (m_scoped) {
            Network::Detail::TransportScope::release();
        }
    }

    BoundSocket(const BoundSocket&) = delete;
    BoundSocket& operator=(const BoundSocket&) = delete;

    [[nodiscard]] bool isValid() const noexcept
    {
        return m_socket != Network::Detail::InvalidNativeSocket;
    }
    [[nodiscard]] Network::Detail::NativeSocket handle() const noexcept { return m_socket; }
    [[nodiscard]] const Network::NetworkEndpoint& endpoint() const noexcept { return m_endpoint; }

    void sendTo(const Network::NetworkEndpoint& destination, const char* text) const
    {
        sockaddr_storage address{};
        const auto length = Network::Detail::toNativeAddress(destination, address);
        const auto size = static_cast<int>(std::strlen(text));
#if defined(_WIN32)
        ::sendto(m_socket, text, size, 0, reinterpret_cast<const sockaddr*>(&address), length);
#else
        ::sendto(
            m_socket,
            text,
            static_cast<size_t>(size),
            0,
            reinterpret_cast<const sockaddr*>(&address),
            length);
#endif
    }

  private:
    Network::Detail::NativeSocket m_socket = Network::Detail::InvalidNativeSocket;
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

[[nodiscard]] ReadinessPoller makePoller(Core::usize capacity = 8)
{
    auto poller = ReadinessPoller::Create(capacity, *std::pmr::get_default_resource());
    EXPECT_TRUE(poller.has_value());
    return std::move(*poller);
}

// Loopback delivery is prompt but not synchronous, so a single poll can legally
// see nothing.
[[nodiscard]] std::vector<ReadinessEvent> pollUntilAny(
    ReadinessPoller& poller,
    int attemptBudget = 200)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        auto events = poller.poll();
        EXPECT_TRUE(events.has_value());
        if (!events.has_value()) {
            return {};
        }
        if (!events->empty()) {
            return std::vector<ReadinessEvent>{events->begin(), events->end()};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return {};
}

} // namespace

TEST(ReadinessPollerTest, RejectsInvalidCapacity)
{
    const auto zero = ReadinessPoller::Create(0, *std::pmr::get_default_resource());
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, Network::NetworkErrorCode::InvalidConfiguration);

    // poll takes the descriptor count as an int, so a capacity beyond that range
    // could never be submitted.
    const auto huge = ReadinessPoller::Create(
        static_cast<Core::usize>((std::numeric_limits<int>::max)()) + 1,
        *std::pmr::get_default_resource());
    ASSERT_FALSE(huge.has_value());
    EXPECT_EQ(huge.error().code, Network::NetworkErrorCode::InvalidConfiguration);
}

TEST(ReadinessPollerTest, CreateReportsCapacityAndStartsEmpty)
{
    auto poller = makePoller(4);
    EXPECT_TRUE(poller.isInitialised());
    EXPECT_EQ(poller.capacity(), 4U);
    EXPECT_EQ(poller.registeredCount(), 0U);
    EXPECT_EQ(poller.pollCallCount(), 0U);
}

// Polling with nothing registered is the normal idle answer. On Windows WSAPoll
// errors on a zero-length set, so this must be short-circuited rather than
// submitted.
TEST(ReadinessPollerTest, PollWithNoRegistrationsReturnsEmpty)
{
    auto poller = makePoller();

    const auto events = poller.poll();
    ASSERT_TRUE(events.has_value());
    EXPECT_TRUE(events->empty());
    EXPECT_EQ(poller.pollCallCount(), 1U);
}

TEST(ReadinessPollerTest, RejectsInvalidSocketRegistration)
{
    auto poller = makePoller();

    const auto registered = poller.register_(
        Network::Detail::InvalidNativeSocket,
        ReadinessInterest::Readable);
    ASSERT_FALSE(registered.has_value());
    EXPECT_EQ(registered.error().code, Network::NetworkErrorCode::BackendFailure);
    EXPECT_EQ(poller.registeredCount(), 0U);
}

TEST(ReadinessPollerTest, RegistrationCapacityIsAHardBound)
{
    constexpr Core::usize capacity = 2;
    auto poller = makePoller(capacity);

    BoundSocket first;
    BoundSocket second;
    BoundSocket third;
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    ASSERT_TRUE(third.isValid());

    ASSERT_TRUE(poller.register_(first.handle(), ReadinessInterest::Readable).has_value());
    ASSERT_TRUE(poller.register_(second.handle(), ReadinessInterest::Readable).has_value());
    EXPECT_EQ(poller.registeredCount(), capacity);

    const auto overflow = poller.register_(third.handle(), ReadinessInterest::Readable);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Network::NetworkErrorCode::CapacityExceeded);
}

// A released slot must be reusable, otherwise a long-running transport would
// exhaust capacity through churn alone.
TEST(ReadinessPollerTest, ReleasedSlotIsReused)
{
    auto poller = makePoller(2);

    BoundSocket first;
    BoundSocket second;
    BoundSocket third;
    ASSERT_TRUE(first.isValid());

    const auto firstSlot = poller.register_(first.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(firstSlot.has_value());
    ASSERT_TRUE(poller.register_(second.handle(), ReadinessInterest::Readable).has_value());

    poller.release(*firstSlot);
    EXPECT_EQ(poller.registeredCount(), 1U);

    const auto reused = poller.register_(third.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(*reused, *firstSlot);
    EXPECT_EQ(poller.registeredCount(), 2U);
}

TEST(ReadinessPollerTest, ReleaseIgnoresOutOfRangeAndDoubleRelease)
{
    auto poller = makePoller(2);

    BoundSocket socket;
    ASSERT_TRUE(socket.isValid());
    const auto slot = poller.register_(socket.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(slot.has_value());

    poller.release(*slot);
    EXPECT_EQ(poller.registeredCount(), 0U);

    // Neither of these may drive the count negative or corrupt a slot.
    poller.release(*slot);
    poller.release(9999);
    EXPECT_EQ(poller.registeredCount(), 0U);
}

TEST(ReadinessPollerTest, ReportsReadableWhenDatagramArrives)
{
    auto poller = makePoller();

    BoundSocket receiver;
    BoundSocket sender;
    ASSERT_TRUE(receiver.isValid());
    ASSERT_TRUE(sender.isValid());

    const auto slot = poller.register_(receiver.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(slot.has_value());

    // Nothing sent yet, so the socket must not report readable.
    const auto quiet = poller.poll();
    ASSERT_TRUE(quiet.has_value());
    EXPECT_TRUE(quiet->empty());

    sender.sendTo(receiver.endpoint(), "ready");

    const auto events = pollUntilAny(poller);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.front().registrationIndex, *slot);
    EXPECT_TRUE(events.front().readable);
    EXPECT_FALSE(events.front().errored);
}

// A bound UDP socket is writable immediately, which is the cheapest way to prove
// write interest is honoured independently of read interest.
TEST(ReadinessPollerTest, ReportsWritableForIdleSocket)
{
    auto poller = makePoller();

    BoundSocket socket;
    ASSERT_TRUE(socket.isValid());
    const auto slot = poller.register_(socket.handle(), ReadinessInterest::Writable);
    ASSERT_TRUE(slot.has_value());

    const auto events = pollUntilAny(poller);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.front().registrationIndex, *slot);
    EXPECT_TRUE(events.front().writable);
    EXPECT_FALSE(events.front().readable);
}

// Interest must be changeable between pumps: a connection that has drained its
// send buffer drops Writable so poll stops reporting it every frame.
TEST(ReadinessPollerTest, InterestChangesTakeEffectOnNextPoll)
{
    auto poller = makePoller();

    BoundSocket socket;
    ASSERT_TRUE(socket.isValid());
    const auto slot = poller.register_(socket.handle(), ReadinessInterest::Writable);
    ASSERT_TRUE(slot.has_value());

    ASSERT_FALSE(pollUntilAny(poller).empty());

    // Dropping to None must remove it from the submitted set entirely.
    ASSERT_TRUE(poller.setInterest(*slot, ReadinessInterest::None).has_value());
    const auto silent = poller.poll();
    ASSERT_TRUE(silent.has_value());
    EXPECT_TRUE(silent->empty());

    // Restoring interest brings it back.
    ASSERT_TRUE(poller.setInterest(*slot, ReadinessInterest::Writable).has_value());
    EXPECT_FALSE(pollUntilAny(poller).empty());
}

TEST(ReadinessPollerTest, SetInterestRejectsDeadRegistration)
{
    auto poller = makePoller(2);

    BoundSocket socket;
    ASSERT_TRUE(socket.isValid());
    const auto slot = poller.register_(socket.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(slot.has_value());
    poller.release(*slot);

    const auto released = poller.setInterest(*slot, ReadinessInterest::Writable);
    EXPECT_FALSE(released.has_value());

    const auto outOfRange = poller.setInterest(9999, ReadinessInterest::Writable);
    EXPECT_FALSE(outOfRange.has_value());
}

// The reported index must identify the right registration even when other slots
// are live, since a transport maps it straight back to a connection record.
TEST(ReadinessPollerTest, ReportsCorrectIndexAmongMultipleRegistrations)
{
    auto poller = makePoller();

    BoundSocket idle;
    BoundSocket target;
    BoundSocket sender;
    ASSERT_TRUE(idle.isValid());
    ASSERT_TRUE(target.isValid());
    ASSERT_TRUE(sender.isValid());

    const auto idleSlot = poller.register_(idle.handle(), ReadinessInterest::Readable);
    const auto targetSlot = poller.register_(target.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(idleSlot.has_value());
    ASSERT_TRUE(targetSlot.has_value());
    ASSERT_NE(*idleSlot, *targetSlot);

    sender.sendTo(target.endpoint(), "pick-me");

    const auto events = pollUntilAny(poller);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.front().registrationIndex, *targetSlot);
}

// Only the interested socket may be reported: a readable arrival on a socket
// registered for writes alone must not surface as readable.
TEST(ReadinessPollerTest, DoesNotReportUnrequestedReadability)
{
    auto poller = makePoller();

    BoundSocket receiver;
    BoundSocket sender;
    ASSERT_TRUE(receiver.isValid());
    ASSERT_TRUE(sender.isValid());

    const auto slot = poller.register_(receiver.handle(), ReadinessInterest::Writable);
    ASSERT_TRUE(slot.has_value());

    sender.sendTo(receiver.endpoint(), "unrequested");
    std::this_thread::sleep_for(std::chrono::milliseconds{5});

    const auto events = poller.poll();
    ASSERT_TRUE(events.has_value());
    for (const auto& event : *events) {
        EXPECT_FALSE(event.readable) << "readable reported without read interest";
    }
}

TEST(ReadinessPollerTest, ReportsBothDirectionsWhenBothRequested)
{
    auto poller = makePoller();

    BoundSocket receiver;
    BoundSocket sender;
    ASSERT_TRUE(receiver.isValid());
    ASSERT_TRUE(sender.isValid());

    const auto slot = poller.register_(
        receiver.handle(),
        ReadinessInterest::ReadableAndWritable);
    ASSERT_TRUE(slot.has_value());

    sender.sendTo(receiver.endpoint(), "both");

    bool sawReadable = false;
    bool sawWritable = false;
    for (int attempt = 0; attempt < 200 && !sawReadable; ++attempt) {
        const auto events = poller.poll();
        ASSERT_TRUE(events.has_value());
        for (const auto& event : *events) {
            if (event.registrationIndex != *slot) {
                continue;
            }
            sawReadable = sawReadable || event.readable;
            sawWritable = sawWritable || event.writable;
        }
        if (!sawReadable) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    EXPECT_TRUE(sawReadable);
    EXPECT_TRUE(sawWritable);
}

// Create is the only allocation point; polling must not grow storage no matter
// how many events pass through.
TEST(ReadinessPollerTest, SteadyStatePollPerformsNoAllocations)
{
    TrackingMemoryResource memory;
    auto poller = ReadinessPoller::Create(8, memory);
    ASSERT_TRUE(poller.has_value());

    BoundSocket receiver;
    BoundSocket sender;
    ASSERT_TRUE(receiver.isValid());
    ASSERT_TRUE(sender.isValid());

    const auto slot = poller->register_(receiver.handle(), ReadinessInterest::Readable);
    ASSERT_TRUE(slot.has_value());

    // Warm up so any first-call bookkeeping is already paid for.
    sender.sendTo(receiver.endpoint(), "warmup");
    (void)pollUntilAny(*poller);

    const Core::usize baseline = memory.allocationCalls();

    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto events = poller->poll();
        ASSERT_TRUE(events.has_value());
    }

    EXPECT_EQ(memory.allocationCalls(), baseline);
}

TEST(ReadinessPollerTest, PollCallCountTracksEveryCallIncludingIdle)
{
    auto poller = makePoller();

    for (int iteration = 0; iteration < 5; ++iteration) {
        const auto events = poller.poll();
        ASSERT_TRUE(events.has_value());
    }
    EXPECT_EQ(poller.pollCallCount(), 5U);
}

// The event span borrows internal storage, so a second poll must be free to
// overwrite it. This pins the lifetime contract the transport relies on.
TEST(ReadinessPollerTest, SecondPollInvalidatesPreviousEvents)
{
    auto poller = makePoller();

    BoundSocket receiver;
    BoundSocket sender;
    ASSERT_TRUE(receiver.isValid());
    ASSERT_TRUE(sender.isValid());
    ASSERT_TRUE(poller.register_(receiver.handle(), ReadinessInterest::Readable).has_value());

    sender.sendTo(receiver.endpoint(), "first");
    ASSERT_FALSE(pollUntilAny(poller).empty());

    // Drain the socket so it stops reporting readable.
    char scratch[64];
    sockaddr_storage from{};
    auto fromLength = static_cast<Network::Detail::NativeAddressLength>(sizeof(from));
#if defined(_WIN32)
    ::recvfrom(
        receiver.handle(),
        scratch,
        static_cast<int>(sizeof(scratch)),
        0,
        reinterpret_cast<sockaddr*>(&from),
        &fromLength);
#else
    ::recvfrom(
        receiver.handle(),
        scratch,
        sizeof(scratch),
        0,
        reinterpret_cast<sockaddr*>(&from),
        &fromLength);
#endif

    const auto afterDrain = poller.poll();
    ASSERT_TRUE(afterDrain.has_value());
    EXPECT_TRUE(afterDrain->empty());
}

} // namespace Tina::Tests
