#include <gtest/gtest.h>

#include "detail/UITextStorage.hpp"

#include <tina/ui/UIErrors.hpp>

namespace Tina::Tests {
namespace {

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocationCount_;
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

    usize allocationCount_ = 0;
};

} // namespace

TEST(UITextStorageTests, AllocatesWritesAndTracksUsage)
{
    UI::Detail::UITextStorage storage(16, 4, *std::pmr::get_default_resource());

    auto allocation = storage.allocate(5);
    ASSERT_TRUE(allocation.has_value()) << (allocation ? "" : allocation.error().message);
    EXPECT_EQ(allocation->offset, 0U);
    EXPECT_EQ(allocation->capacity, 5U);

    storage.write(*allocation, "hello");
    EXPECT_EQ(storage.view(*allocation, 5), "hello");
    EXPECT_EQ(storage.capacity(), 16U);
    EXPECT_EQ(storage.used(), 5U);
    EXPECT_EQ(storage.highWater(), 5U);
}

TEST(UITextStorageTests, ZeroByteAllocationDoesNotConsumeCapacity)
{
    UI::Detail::UITextStorage storage(8, 1, *std::pmr::get_default_resource());

    auto allocation = storage.allocate(0);
    ASSERT_TRUE(allocation.has_value());
    EXPECT_EQ(*allocation, UI::Detail::UITextStorage::Allocation{});
    EXPECT_EQ(storage.used(), 0U);
    EXPECT_EQ(storage.highWater(), 0U);
}

TEST(UITextStorageTests, ReusesAndSplitsReleasedBlocks)
{
    UI::Detail::UITextStorage storage(16, 4, *std::pmr::get_default_resource());
    auto first = storage.allocate(8);
    auto second = storage.allocate(8);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    storage.release(*first);
    auto prefix = storage.allocate(3);
    auto suffix = storage.allocate(5);
    ASSERT_TRUE(prefix.has_value());
    ASSERT_TRUE(suffix.has_value());
    EXPECT_EQ(prefix->offset, 0U);
    EXPECT_EQ(suffix->offset, 3U);
    EXPECT_EQ(storage.used(), 16U);
    EXPECT_EQ(storage.highWater(), 16U);
}

TEST(UITextStorageTests, CoalescesReleasedBlocksAndRewindsBumpOffset)
{
    UI::Detail::UITextStorage storage(12, 3, *std::pmr::get_default_resource());
    auto first = storage.allocate(4);
    auto second = storage.allocate(4);
    auto third = storage.allocate(4);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());

    storage.release(*second);
    storage.release(*first);
    storage.release(*third);
    EXPECT_EQ(storage.used(), 0U);
    EXPECT_EQ(storage.highWater(), 12U);

    auto wholeArena = storage.allocate(12);
    ASSERT_TRUE(wholeArena.has_value()) << (wholeArena ? "" : wholeArena.error().message);
    EXPECT_EQ(wholeArena->offset, 0U);
    EXPECT_EQ(wholeArena->capacity, 12U);
}

TEST(UITextStorageTests, CapacityFailurePreservesStatistics)
{
    UI::Detail::UITextStorage storage(8, 2, *std::pmr::get_default_resource());
    auto allocated = storage.allocate(6);
    ASSERT_TRUE(allocated.has_value());

    auto overflow = storage.allocate(3);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(storage.used(), 6U);
    EXPECT_EQ(storage.highWater(), 6U);

    storage.release(*allocated);
    EXPECT_EQ(storage.used(), 0U);
    EXPECT_EQ(storage.highWater(), 6U);
}

TEST(UITextStorageTests, RuntimeOperationsStayWithinReservedPmrStorage)
{
    ObservingMemoryResource resource;
    UI::Detail::UITextStorage storage(32, 4, resource);
    const usize allocationCountAfterConstruction = resource.allocationCount();

    auto first = storage.allocate(8);
    auto second = storage.allocate(8);
    auto third = storage.allocate(8);
    auto fourth = storage.allocate(8);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());
    ASSERT_TRUE(fourth.has_value());

    storage.release(*first);
    storage.release(*third);
    auto reusedFirst = storage.allocate(4);
    auto reusedThird = storage.allocate(4);
    ASSERT_TRUE(reusedFirst.has_value());
    ASSERT_TRUE(reusedThird.has_value());
    storage.write(*reusedFirst, "Tina");
    EXPECT_EQ(storage.view(*reusedFirst, 4), "Tina");
    EXPECT_EQ(resource.allocationCount(), allocationCountAfterConstruction);
}

} // namespace Tina::Tests
