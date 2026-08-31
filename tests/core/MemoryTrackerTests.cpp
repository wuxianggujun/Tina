#include <gtest/gtest.h>

#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>

#include <atomic>
#include <limits>
#include <memory_resource>
#include <new>
#include <thread>
#include <vector>

namespace Tina::Tests {
namespace {

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    bool failAllocations = false;
    usize allocationCount = 0;
    usize deallocationCount = 0;

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (failAllocations) {
            throw std::bad_alloc{};
        }
        ++allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocationCount;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

TEST(MemoryTagTest, UsesStableExplicitDiagnosticValues)
{
    static_assert(std::to_underlying(Core::MemoryTag::Invalid) == 0U);
    static_assert(std::to_underlying(Core::MemoryTag::Core) == 1U);
    static_assert(std::to_underlying(Core::MemoryTag::RuntimeFrame) == 5U);
    static_assert(std::to_underlying(Core::MemoryTag::Cooker) == 12U);
    static_assert(std::to_underlying(Core::MemoryTag::Network) == 13U);
    static_assert(std::to_underlying(Core::MemoryTag::Gameplay) == 14U);
    static_assert(std::to_underlying(Core::MemoryTag::Count) == 15U);

    EXPECT_TRUE(Core::isValidMemoryTag(Core::MemoryTag::UI));
    EXPECT_TRUE(Core::isValidMemoryTag(Core::MemoryTag::Network));
    EXPECT_FALSE(Core::isValidMemoryTag(Core::MemoryTag::Invalid));
    EXPECT_EQ(Core::memoryTagName(Core::MemoryTag::Physics2D), "Physics2D");
    EXPECT_EQ(Core::memoryTagName(Core::MemoryTag::Network), "Network");
}

TEST(MemoryTrackerTest, TracksIndependentCurrentPeakFailureAndInvalidDeallocationCounters)
{
    Core::MemoryTracker tracker;

    EXPECT_TRUE(tracker.tryRecordAllocation(Core::MemoryTag::Scene, 64));
    EXPECT_TRUE(tracker.tryRecordAllocation(Core::MemoryTag::Scene, 32));
    EXPECT_TRUE(tracker.tryRecordDeallocation(Core::MemoryTag::Scene, 64));
    EXPECT_FALSE(tracker.tryRecordDeallocation(Core::MemoryTag::Scene, 64));
    tracker.recordAllocationFailure(Core::MemoryTag::Scene);
    EXPECT_TRUE(tracker.tryRecordAllocation(Core::MemoryTag::UI, 7));

    const Core::MemoryStatistics scene = tracker.snapshot(Core::MemoryTag::Scene);
    EXPECT_EQ(scene.currentBytes, 32U);
    EXPECT_EQ(scene.peakBytes, 96U);
    EXPECT_EQ(scene.allocationCount, 2U);
    EXPECT_EQ(scene.deallocationCount, 1U);
    EXPECT_EQ(scene.failedAllocationCount, 1U);
    EXPECT_EQ(scene.invalidDeallocationCount, 1U);

    const Core::MemoryStatistics ui = tracker.snapshot(Core::MemoryTag::UI);
    EXPECT_EQ(ui.currentBytes, 7U);
    EXPECT_EQ(ui.peakBytes, 7U);
    EXPECT_EQ(tracker.snapshot(Core::MemoryTag::Invalid).currentBytes, 0U);
    EXPECT_EQ(
        tracker.snapshot()[Core::memoryTagIndex(Core::MemoryTag::Scene)].peakBytes,
        96U);
}

TEST(MemoryTrackerTest, RejectsByteCounterOverflowWithoutMutatingCurrentBytes)
{
    Core::MemoryTracker tracker;
    constexpr usize Maximum = (std::numeric_limits<usize>::max)();

    ASSERT_TRUE(tracker.tryRecordAllocation(Core::MemoryTag::Core, Maximum));
    EXPECT_FALSE(tracker.tryRecordAllocation(Core::MemoryTag::Core, 1));

    const Core::MemoryStatistics statistics = tracker.snapshot(Core::MemoryTag::Core);
    EXPECT_EQ(statistics.currentBytes, Maximum);
    EXPECT_EQ(statistics.peakBytes, Maximum);
    EXPECT_EQ(statistics.allocationCount, 1U);
}

TEST(MemoryTrackerTest, SupportsConcurrentDiagnosticUpdates)
{
    Core::MemoryTracker tracker;
    constexpr usize ThreadCount = 4;
    constexpr usize IterationCount = 2'000;
    std::vector<std::thread> workers;
    std::atomic<bool> allUpdatesSucceeded{true};

    for (usize threadIndex = 0; threadIndex < ThreadCount; ++threadIndex) {
        workers.emplace_back([&tracker, &allUpdatesSucceeded] {
            for (usize iteration = 0; iteration < IterationCount; ++iteration) {
                if (!tracker.tryRecordAllocation(Core::MemoryTag::Task, 8)
                    || !tracker.tryRecordDeallocation(Core::MemoryTag::Task, 8)) {
                    allUpdatesSucceeded.store(false, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    const Core::MemoryStatistics statistics = tracker.snapshot(Core::MemoryTag::Task);
    EXPECT_TRUE(allUpdatesSucceeded.load(std::memory_order_relaxed));
    EXPECT_EQ(statistics.currentBytes, 0U);
    EXPECT_EQ(statistics.allocationCount, ThreadCount * IterationCount);
    EXPECT_EQ(statistics.deallocationCount, ThreadCount * IterationCount);
    EXPECT_GE(statistics.peakBytes, 8U);
}

TEST(CountingMemoryResourceTest, RoutesAndCountsOnlyItsOwnAllocations)
{
    Core::MemoryTracker tracker;
    ObservingMemoryResource upstream;
    Core::CountingMemoryResource resource(tracker, Core::MemoryTag::Asset, upstream);

    void* allocation = resource.allocate(128, 32);
    ASSERT_NE(allocation, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr>(allocation) % 32U, 0U);

    Core::MemoryStatistics statistics = tracker.snapshot(Core::MemoryTag::Asset);
    EXPECT_EQ(statistics.currentBytes, 128U);
    EXPECT_EQ(statistics.peakBytes, 128U);
    EXPECT_EQ(statistics.allocationCount, 1U);
    EXPECT_EQ(upstream.allocationCount, 1U);

    resource.deallocate(allocation, 128, 32);
    statistics = tracker.snapshot(Core::MemoryTag::Asset);
    EXPECT_EQ(statistics.currentBytes, 0U);
    EXPECT_EQ(statistics.deallocationCount, 1U);
    EXPECT_EQ(upstream.deallocationCount, 1U);
}

TEST(CountingMemoryResourceTest, CountsUpstreamAndTrackerFailuresWithoutLeakingBackingMemory)
{
    Core::MemoryTracker tracker;
    ObservingMemoryResource upstream;
    Core::CountingMemoryResource resource(tracker, Core::MemoryTag::RenderCpu, upstream);

    upstream.failAllocations = true;
    EXPECT_THROW(static_cast<void>(resource.allocate(16, 8)), std::bad_alloc);
    EXPECT_EQ(tracker.snapshot(Core::MemoryTag::RenderCpu).failedAllocationCount, 1U);

    upstream.failAllocations = false;
    ASSERT_TRUE(tracker.tryRecordAllocation(
        Core::MemoryTag::RenderCpu,
        (std::numeric_limits<usize>::max)()));
    EXPECT_THROW(static_cast<void>(resource.allocate(1, 1)), std::bad_alloc);
    EXPECT_EQ(upstream.allocationCount, 1U);
    EXPECT_EQ(upstream.deallocationCount, 1U);
    EXPECT_EQ(tracker.snapshot(Core::MemoryTag::RenderCpu).failedAllocationCount, 2U);
}

} // namespace
} // namespace Tina::Tests
