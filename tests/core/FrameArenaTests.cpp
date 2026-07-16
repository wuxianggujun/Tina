#include <gtest/gtest.h>

#include <tina/core/memory/FrameArena.hpp>

#include <limits>
#include <memory_resource>
#include <new>
#include <utility>

namespace Tina::Tests {
namespace {

class ArenaUpstreamResource final : public std::pmr::memory_resource {
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

TEST(FrameArenaTest, RejectsInvalidConfigurationAndReportsBackingAllocationFailure)
{
    ArenaUpstreamResource upstream;

    const auto empty = Core::FrameArena::Create({.capacityBytes = 0}, upstream);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, Core::CoreErrorCode::InvalidArgument);

    const auto invalidAlignment = Core::FrameArena::Create(
        {.capacityBytes = 64, .maximumAlignment = 3},
        upstream);
    ASSERT_FALSE(invalidAlignment);
    EXPECT_EQ(invalidAlignment.error().code, Core::CoreErrorCode::InvalidArgument);

    upstream.failAllocations = true;
    const auto exhausted = Core::FrameArena::Create({.capacityBytes = 64}, upstream);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, Core::CoreErrorCode::OutOfMemory);
}

TEST(FrameArenaTest, AllocatesOneBackingBlockAndHonorsRequestedAlignment)
{
    ArenaUpstreamResource upstream;
    auto result = Core::FrameArena::Create(
        {.capacityBytes = 512, .maximumAlignment = 64},
        upstream);
    ASSERT_TRUE(result);
    Core::FrameArena arena = std::move(*result);

    EXPECT_EQ(upstream.allocationCount, 1U);
    for (const usize alignment : {1U, 2U, 4U, 8U, 16U, 32U, 64U}) {
        void* allocation = arena.tryAllocate(3, alignment);
        ASSERT_NE(allocation, nullptr);
        EXPECT_TRUE(arena.owns(allocation));
        EXPECT_EQ(reinterpret_cast<uintptr>(allocation) % alignment, 0U);
    }
    EXPECT_EQ(upstream.allocationCount, 1U);
    EXPECT_EQ(upstream.deallocationCount, 0U);
}

TEST(FrameArenaTest, NeverFallsBackAndPreservesStateAfterFailedAllocation)
{
    ArenaUpstreamResource upstream;
    auto result = Core::FrameArena::Create({.capacityBytes = 32}, upstream);
    ASSERT_TRUE(result);
    Core::FrameArena arena = std::move(*result);

    ASSERT_NE(arena.tryAllocate(24, 8), nullptr);
    const usize usedBeforeFailure = arena.usedBytes();
    EXPECT_EQ(arena.tryAllocate(32, 8), nullptr);
    EXPECT_EQ(arena.tryAllocate(1, 128), nullptr);
    EXPECT_EQ(arena.usedBytes(), usedBeforeFailure);
    EXPECT_EQ(arena.statistics().failedAllocationCount, 2U);
    EXPECT_EQ(upstream.allocationCount, 1U);
    EXPECT_THROW(static_cast<void>(arena.allocate(32, 8)), std::bad_alloc);
    EXPECT_EQ(arena.statistics().failedAllocationCount, 3U);
    EXPECT_EQ(upstream.allocationCount, 1U);
}

TEST(FrameArenaTest, DetectsArraySizeOverflowBeforePointerArithmetic)
{
    auto result = Core::FrameArena::Create({.capacityBytes = 128});
    ASSERT_TRUE(result);
    Core::FrameArena arena = std::move(*result);

    constexpr usize OverflowingCount = (std::numeric_limits<usize>::max)() / sizeof(u64) + 1;
    EXPECT_EQ(arena.tryAllocateUninitializedArray<u64>(OverflowingCount), nullptr);
    EXPECT_EQ(arena.usedBytes(), 0U);
    EXPECT_EQ(arena.statistics().failedAllocationCount, 1U);
}

TEST(FrameArenaTest, DefinesZeroSizeSemanticsForRawAndTypedAllocation)
{
    auto result = Core::FrameArena::Create({.capacityBytes = 16});
    ASSERT_TRUE(result);
    Core::FrameArena arena = std::move(*result);

    void* zeroByteAllocation = arena.tryAllocate(0, 1);
    ASSERT_NE(zeroByteAllocation, nullptr);
    EXPECT_TRUE(arena.owns(zeroByteAllocation));
    EXPECT_EQ(arena.usedBytes(), 1U);
    EXPECT_EQ(arena.statistics().allocationCount, 1U);

    EXPECT_EQ(arena.tryAllocateUninitializedArray<u32>(0), nullptr);
    EXPECT_EQ(arena.usedBytes(), 1U);
    EXPECT_EQ(arena.statistics().failedAllocationCount, 0U);
}

TEST(FrameArenaTest, ResetReusesStorageAndAdvancesEpochWithoutDiscardingPeak)
{
    auto result = Core::FrameArena::Create({.capacityBytes = 128});
    ASSERT_TRUE(result);
    Core::FrameArena arena = std::move(*result);

    void* first = arena.tryAllocate(32, 16);
    ASSERT_NE(first, nullptr);
    const usize peak = arena.statistics().peakBytes;
    const u64 epoch = arena.epoch();

    arena.reset();
    EXPECT_EQ(arena.usedBytes(), 0U);
    EXPECT_EQ(arena.epoch(), epoch + 1);
    EXPECT_EQ(arena.statistics().peakBytes, peak);
    EXPECT_EQ(arena.tryAllocate(32, 16), first);
}

TEST(FrameArenaTest, MoveTransfersTheSingleBackingAllocationExactlyOnce)
{
    ArenaUpstreamResource upstream;
    {
        auto result = Core::FrameArena::Create({.capacityBytes = 64}, upstream);
        ASSERT_TRUE(result);
        Core::FrameArena source = std::move(*result);
        Core::FrameArena destination = std::move(source);
        EXPECT_NE(destination.tryAllocate(8, 8), nullptr);
        EXPECT_EQ(source.capacityBytes(), 0U);
    }
    EXPECT_EQ(upstream.allocationCount, 1U);
    EXPECT_EQ(upstream.deallocationCount, 1U);
}

} // namespace
} // namespace Tina::Tests
