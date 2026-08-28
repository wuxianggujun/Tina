// DnsResolver is the one part of Tina::Network that uses a worker thread, so these
// tests care most about the boundary: a stale handle, a cancel while the worker is
// still running, and a slot table that must not hand out an answer twice.
//
// Only loopback names are resolved. A public name would make the suite depend on
// the host's DNS, and a failure would say nothing about this code.

#include <tina/network/DnsResolver.hpp>
#include <tina/network/NetworkErrors.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

namespace Tina::Tests {
namespace {

using Network::DnsAddressFamily;
using Network::DnsQueryHandle;
using Network::DnsQueryState;
using Network::DnsResolver;
using Network::DnsResolverConfig;

// Owns a task system for the lifetime of one test. Shutdown is explicit because a
// worker may still be inside getaddrinfo when the test ends.
class ResolverFixture final {
  public:
    ResolverFixture(Core::usize queryCapacity = 4, Core::usize addressesPerQuery = 4)
    {
        auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
            .ioWorkerCount = 2,
            .cpuWorkerCount = 0,
            .ioQueueCapacity = 16,
            .cpuQueueCapacity = 0,
            .mainQueueCapacity = 16,
        });
        if (!taskSystem) {
            return;
        }
        m_taskSystem = std::move(*taskSystem);

        DnsResolverConfig config{};
        config.taskSystem = m_taskSystem.get();
        config.queryCapacity = queryCapacity;
        config.maximumAddressesPerQuery = addressesPerQuery;

        auto resolver = DnsResolver::Create(config);
        if (!resolver) {
            return;
        }
        m_resolver = std::make_unique<DnsResolver>(std::move(*resolver));
    }

    ~ResolverFixture()
    {
        // The resolver goes first: its shared request state must outlive any worker
        // still writing to it, which the shared_ptr guarantees, but the task system
        // must not be joined while it holds work referencing that state.
        m_resolver.reset();
        if (m_taskSystem) {
            m_taskSystem->shutdownAndJoin();
        }
    }

    ResolverFixture(const ResolverFixture&) = delete;
    ResolverFixture& operator=(const ResolverFixture&) = delete;

    [[nodiscard]] bool isValid() const noexcept { return m_resolver != nullptr; }
    [[nodiscard]] DnsResolver& resolver() noexcept { return *m_resolver; }

  private:
    std::unique_ptr<Task::ITaskSystem> m_taskSystem;
    std::unique_ptr<DnsResolver> m_resolver;
};

// Pumps until the query leaves Pending or the budget runs out. The worker runs on
// another thread, so a single pump legitimately sees nothing.
[[nodiscard]] DnsQueryState awaitQuery(
    DnsResolver& resolver,
    DnsQueryHandle handle,
    int attemptBudget = 4000)
{
    for (int attempt = 0; attempt < attemptBudget; ++attempt) {
        const auto pumped = resolver.pump();
        if (!pumped) {
            return DnsQueryState::Failed;
        }
        const auto state = resolver.queryState(handle);
        if (state != DnsQueryState::Pending) {
            return state;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return DnsQueryState::Pending;
}

} // namespace

TEST(DnsResolverTest, RejectsInvalidConfiguration)
{
    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 8,
        .mainQueueCapacity = 8,
    });
    ASSERT_TRUE(taskSystem.has_value());

    {
        // getaddrinfo blocks, so there is no sensible fallback without a worker.
        DnsResolverConfig config{};
        config.taskSystem = nullptr;
        const auto resolver = DnsResolver::Create(config);
        ASSERT_FALSE(resolver.has_value());
        EXPECT_EQ(resolver.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        DnsResolverConfig config{};
        config.taskSystem = taskSystem->get();
        config.queryCapacity = 0;
        const auto resolver = DnsResolver::Create(config);
        ASSERT_FALSE(resolver.has_value());
        EXPECT_EQ(resolver.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }
    {
        DnsResolverConfig config{};
        config.taskSystem = taskSystem->get();
        config.maximumAddressesPerQuery = 0;
        const auto resolver = DnsResolver::Create(config);
        ASSERT_FALSE(resolver.has_value());
        EXPECT_EQ(resolver.error().code, Network::NetworkErrorCode::InvalidConfiguration);
    }

    (*taskSystem)->shutdownAndJoin();
}

TEST(DnsResolverTest, RejectsMalformedQueries)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    {
        const auto handle = fixture.resolver().resolve({}, 80);
        ASSERT_FALSE(handle.has_value());
        EXPECT_EQ(handle.error().code, Network::NetworkErrorCode::InvalidQuery);
    }
    {
        // A DNS name cannot exceed 253 characters, so this never needed a worker.
        const auto handle = fixture.resolver().resolve(std::string(300, 'a'), 80);
        ASSERT_FALSE(handle.has_value());
        EXPECT_EQ(handle.error().code, Network::NetworkErrorCode::InvalidQuery);
    }
    {
        // An embedded NUL would truncate the name inside getaddrinfo, resolving
        // something other than what was asked for.
        const auto handle = fixture.resolver().resolve(std::string_view{"a\0b", 3}, 80);
        ASSERT_FALSE(handle.has_value());
        EXPECT_EQ(handle.error().code, Network::NetworkErrorCode::InvalidQuery);
    }
    {
        const auto handle = fixture.resolver().resolve("localhost", 0);
        ASSERT_FALSE(handle.has_value());
        EXPECT_EQ(handle.error().code, Network::NetworkErrorCode::InvalidQuery);
    }
}

// A numeric literal must resolve without touching the network, which is what lets
// a caller pass either a name or an address and not special-case it.
TEST(DnsResolverTest, ResolvesNumericLiteralWithoutNetwork)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve("127.0.0.1", 8080);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(fixture.resolver().queryState(*handle), DnsQueryState::Pending);

    ASSERT_EQ(awaitQuery(fixture.resolver(), *handle), DnsQueryState::Resolved);

    const auto addresses = fixture.resolver().addresses(*handle);
    ASSERT_TRUE(addresses.has_value());
    ASSERT_FALSE(addresses->empty());
    EXPECT_EQ(addresses->front().address, Network::IpAddress::v4Loopback());
    EXPECT_EQ(addresses->front().port, 8080);
}

TEST(DnsResolverTest, ResolvesLoopbackName)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve("localhost", 443);
    ASSERT_TRUE(handle.has_value());

    const auto state = awaitQuery(fixture.resolver(), *handle);
    if (state != DnsQueryState::Resolved) {
        // A host with no localhost entry is an environment property, not a defect
        // in this code.
        GTEST_SKIP() << "localhost did not resolve on this host";
    }

    const auto addresses = fixture.resolver().addresses(*handle);
    ASSERT_TRUE(addresses.has_value());
    ASSERT_FALSE(addresses->empty());
    for (const auto& endpoint : *addresses) {
        EXPECT_EQ(endpoint.port, 443);
        EXPECT_TRUE(endpoint.address.isLoopback())
            << "localhost resolved to a non-loopback address";
    }
}

TEST(DnsResolverTest, FamilyFilterRestrictsResults)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve(
        "localhost",
        80,
        DnsAddressFamily::V4Only);
    ASSERT_TRUE(handle.has_value());

    const auto state = awaitQuery(fixture.resolver(), *handle);
    if (state != DnsQueryState::Resolved) {
        GTEST_SKIP() << "localhost did not resolve to IPv4 on this host";
    }

    const auto addresses = fixture.resolver().addresses(*handle);
    ASSERT_TRUE(addresses.has_value());
    for (const auto& endpoint : *addresses) {
        EXPECT_EQ(endpoint.address.family(), Network::IpFamily::V4);
    }
}

// A name that cannot resolve must fail cleanly rather than hang or crash. The
// .invalid TLD is reserved by RFC 2606 precisely so it never resolves.
TEST(DnsResolverTest, UnresolvableNameFailsCleanly)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve(
        "tina-does-not-exist.invalid",
        80);
    ASSERT_TRUE(handle.has_value());

    ASSERT_EQ(awaitQuery(fixture.resolver(), *handle), DnsQueryState::Failed);

    const auto addresses = fixture.resolver().addresses(*handle);
    ASSERT_FALSE(addresses.has_value());
    EXPECT_EQ(addresses.error().code, Network::NetworkErrorCode::DnsResolutionFailed);
    EXPECT_GT(fixture.resolver().statistics().failedQueryCount, 0U);
}

// Addresses are unavailable until the query finishes, and asking early must say
// "pending" rather than "failed" -- they need different handling.
TEST(DnsResolverTest, AddressesArePendingBeforeCompletion)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve("127.0.0.1", 80);
    ASSERT_TRUE(handle.has_value());

    const auto addresses = fixture.resolver().addresses(*handle);
    if (!addresses.has_value()) {
        EXPECT_EQ(addresses.error().code, Network::NetworkErrorCode::DnsQueryPending);
    }
    // If the worker already finished, the query is simply resolved; both are
    // correct, so only the error code is asserted when there is one.
}

TEST(DnsResolverTest, QueryCapacityIsAHardBound)
{
    ResolverFixture fixture{/*queryCapacity=*/2};
    ASSERT_TRUE(fixture.isValid());
    EXPECT_EQ(fixture.resolver().queryCapacity(), 2U);

    const auto first = fixture.resolver().resolve("127.0.0.1", 80);
    ASSERT_TRUE(first.has_value());
    const auto second = fixture.resolver().resolve("127.0.0.2", 80);
    ASSERT_TRUE(second.has_value());

    // Both slots are claimed until released, even once resolved.
    const auto third = fixture.resolver().resolve("127.0.0.3", 80);
    ASSERT_FALSE(third.has_value());
    EXPECT_EQ(third.error().code, Network::NetworkErrorCode::DnsQueryRejected);
    EXPECT_GT(fixture.resolver().statistics().rejectedQueryCount, 0U);

    // Releasing frees a slot for the next query.
    ASSERT_EQ(awaitQuery(fixture.resolver(), *first), DnsQueryState::Resolved);
    fixture.resolver().release(*first);

    const auto fourth = fixture.resolver().resolve("127.0.0.4", 80);
    EXPECT_TRUE(fourth.has_value());
}

// A resolved query holds its slot until released, so an answer cannot be
// overwritten before it is read.
TEST(DnsResolverTest, ResolvedQueryHoldsItsSlotUntilReleased)
{
    ResolverFixture fixture{/*queryCapacity=*/1};
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve("127.0.0.1", 1234);
    ASSERT_TRUE(handle.has_value());
    ASSERT_EQ(awaitQuery(fixture.resolver(), *handle), DnsQueryState::Resolved);

    // The only slot is still held.
    const auto blocked = fixture.resolver().resolve("127.0.0.1", 5678);
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, Network::NetworkErrorCode::DnsQueryRejected);

    // The answer is still intact.
    const auto addresses = fixture.resolver().addresses(*handle);
    ASSERT_TRUE(addresses.has_value());
    EXPECT_EQ(addresses->front().port, 1234);

    fixture.resolver().release(*handle);
    EXPECT_TRUE(fixture.resolver().resolve("127.0.0.1", 5678).has_value());
}

// A released handle must not read a later query's answer. This is what the
// generation exists for.
TEST(DnsResolverTest, StaleHandleDoesNotSeeAReusedSlot)
{
    ResolverFixture fixture{/*queryCapacity=*/1};
    ASSERT_TRUE(fixture.isValid());

    const auto first = fixture.resolver().resolve("127.0.0.1", 1111);
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(awaitQuery(fixture.resolver(), *first), DnsQueryState::Resolved);
    fixture.resolver().release(*first);

    const auto second = fixture.resolver().resolve("127.0.0.1", 2222);
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(awaitQuery(fixture.resolver(), *second), DnsQueryState::Resolved);

    // Same slot, different generation.
    EXPECT_EQ(first->slot, second->slot);
    EXPECT_NE(first->generation, second->generation);

    EXPECT_EQ(fixture.resolver().queryState(*first), DnsQueryState::Cancelled);
    const auto stale = fixture.resolver().addresses(*first);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Network::NetworkErrorCode::InvalidQuery);

    // The live handle is unaffected.
    const auto live = fixture.resolver().addresses(*second);
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(live->front().port, 2222);
}

// Cancelling cannot interrupt getaddrinfo, so the slot must stay claimed until the
// worker returns -- otherwise the worker would write into a slot a later query
// owns.
TEST(DnsResolverTest, CancelDoesNotDeliverAnAnswer)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve("127.0.0.1", 80);
    ASSERT_TRUE(handle.has_value());

    fixture.resolver().cancel(*handle);
    EXPECT_EQ(fixture.resolver().queryState(*handle), DnsQueryState::Cancelled);
    EXPECT_GT(fixture.resolver().statistics().cancelledQueryCount, 0U);

    // Pumping must never turn a cancelled query into a resolved one.
    for (int attempt = 0; attempt < 200; ++attempt) {
        ASSERT_TRUE(fixture.resolver().pump().has_value());
        EXPECT_EQ(fixture.resolver().queryState(*handle), DnsQueryState::Cancelled);
    }

    const auto addresses = fixture.resolver().addresses(*handle);
    EXPECT_FALSE(addresses.has_value());
}

// A cancelled slot becomes reusable once its worker finishes.
TEST(DnsResolverTest, CancelledSlotIsReclaimedAfterTheWorkerReturns)
{
    ResolverFixture fixture{/*queryCapacity=*/1};
    ASSERT_TRUE(fixture.isValid());

    const auto first = fixture.resolver().resolve("127.0.0.1", 80);
    ASSERT_TRUE(first.has_value());
    fixture.resolver().cancel(*first);

    // Retry until the worker has published and the slot frees up.
    bool reclaimed = false;
    for (int attempt = 0; attempt < 4000 && !reclaimed; ++attempt) {
        ASSERT_TRUE(fixture.resolver().pump().has_value());
        const auto second = fixture.resolver().resolve("127.0.0.1", 81);
        if (second.has_value()) {
            reclaimed = true;
            EXPECT_EQ(awaitQuery(fixture.resolver(), *second), DnsQueryState::Resolved);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    EXPECT_TRUE(reclaimed) << "cancelled slot was never reclaimed";
}

TEST(DnsResolverTest, CancelAndReleaseAreIdempotentAndIgnoreStaleHandles)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto handle = fixture.resolver().resolve("127.0.0.1", 80);
    ASSERT_TRUE(handle.has_value());
    ASSERT_EQ(awaitQuery(fixture.resolver(), *handle), DnsQueryState::Resolved);

    fixture.resolver().release(*handle);
    // None of these may corrupt the slot table.
    fixture.resolver().release(*handle);
    fixture.resolver().cancel(*handle);
    fixture.resolver().release(DnsQueryHandle{9999, 1});
    fixture.resolver().cancel(DnsQueryHandle{});

    EXPECT_TRUE(fixture.resolver().resolve("127.0.0.1", 80).has_value());
}

TEST(DnsResolverTest, StatisticsTrackQueryOutcomes)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    const auto ok = fixture.resolver().resolve("127.0.0.1", 80);
    ASSERT_TRUE(ok.has_value());
    ASSERT_EQ(awaitQuery(fixture.resolver(), *ok), DnsQueryState::Resolved);

    const auto stats = fixture.resolver().statistics();
    EXPECT_GE(stats.startedQueryCount, 1U);
    EXPECT_GE(stats.resolvedQueryCount, 1U);
    EXPECT_GT(stats.pumpCallCount, 0U);
}

TEST(DnsResolverTest, RejectsUseFromNonOwnerThread)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    Core::ErrorCode resolveCode{};
    Core::ErrorCode pumpCode{};
    Core::ErrorCode addressCode{};

    std::thread other{[&]() {
        if (const auto handle = fixture.resolver().resolve("127.0.0.1", 80); !handle) {
            resolveCode = handle.error().code;
        }
        if (const auto pumped = fixture.resolver().pump(); !pumped) {
            pumpCode = pumped.error().code;
        }
        if (const auto addresses = fixture.resolver().addresses(DnsQueryHandle{0, 1});
            !addresses) {
            addressCode = addresses.error().code;
        }
    }};
    other.join();

    EXPECT_EQ(resolveCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(pumpCode, Network::NetworkErrorCode::WrongOwnerThread);
    EXPECT_EQ(addressCode, Network::NetworkErrorCode::WrongOwnerThread);
}

TEST(DnsResolverTest, MovedFromResolverAnswersQueriesInertly)
{
    ResolverFixture fixture;
    ASSERT_TRUE(fixture.isValid());

    DnsResolver moved{std::move(fixture.resolver())};

    EXPECT_FALSE(static_cast<bool>(fixture.resolver()));
    EXPECT_EQ(fixture.resolver().queryCapacity(), 0U);
    EXPECT_EQ(fixture.resolver().statistics().pumpCallCount, 0U);

    const auto pumped = fixture.resolver().pump();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error().code, Network::NetworkErrorCode::SocketClosed);

    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_GT(moved.queryCapacity(), 0U);
}

} // namespace Tina::Tests
