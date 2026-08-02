#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>

#include "detail/UIBehaviorStateStorage.hpp"

#include <cstddef>
#include <memory_resource>

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

constexpr UI::UIElementBehavior AllStoredBehaviors =
    UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput |
    UI::UIElementBehavior::TextInput | UI::UIElementBehavior::Scroll | UI::UIElementBehavior::Select;

constexpr UI::Detail::UIBehaviorStateSlotCounts OneOfEachSlot{
    .activate = 1,
    .toggle = 1,
    .range = 1,
    .textInput = 1,
    .scroll = 1,
    .selection = 1,
};

TEST(UIBehaviorStateStorageTests, PublishesCapabilitiesAtomicallyAndReusesReleasedSlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(3, 1, 1, 1, 1, 1, 1, resource);

    ASSERT_TRUE(storage.publish(0, AllStoredBehaviors));
    ASSERT_TRUE(storage.hasActivate(0));
    ASSERT_TRUE(storage.hasToggle(0));
    ASSERT_NE(storage.tryToggleValue(0), nullptr);
    *storage.tryToggleValue(0) = 1;
    ASSERT_NE(storage.tryRangeInputState(0), nullptr);
    EXPECT_FLOAT_EQ(storage.tryRangeInputState(0)->minValue, 0.0F);
    EXPECT_FLOAT_EQ(storage.tryRangeInputState(0)->maxValue, 1.0F);
    storage.tryRangeInputState(0)->value = 0.75F;
    ASSERT_NE(storage.tryTextInputState(0), nullptr);
    storage.tryTextInputState(0)->selection = {.anchorCodepoint = 2, .caretCodepoint = 3};
    ASSERT_NE(storage.tryScrollState(0), nullptr);
    storage.tryScrollState(0)->style.axes = UI::UIScrollAxes::Horizontal;
    storage.tryScrollState(0)->requestedOffset = {.x = 4.0F, .y = 8.0F};
    storage.tryScrollState(0)->committedMetrics.offset = {.x = 2.0F, .y = 3.0F};
    storage.tryScrollState(0)->committedViewportRect = {.x = 1.0F, .y = 2.0F, .width = 3.0F, .height = 4.0F};
    ASSERT_NE(storage.trySelectState(0), nullptr);
    EXPECT_EQ(storage.trySelectState(0)->selectedOption, (UI::UINodeId{}));

    const Core::Status rejected = storage.publish(1, AllStoredBehaviors);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(1));
    EXPECT_FALSE(storage.hasToggle(1));
    EXPECT_EQ(storage.tryRangeInputState(1), nullptr);
    EXPECT_EQ(storage.tryTextInputState(1), nullptr);
    EXPECT_EQ(storage.tryScrollState(1), nullptr);
    EXPECT_EQ(storage.trySelectState(1), nullptr);
    EXPECT_EQ(storage.activeActivateCount(), 1U);
    EXPECT_EQ(storage.activeToggleCount(), 1U);
    EXPECT_EQ(storage.activeRangeInputCount(), 1U);
    EXPECT_EQ(storage.activeTextInputCount(), 1U);
    EXPECT_EQ(storage.activeScrollCount(), 1U);
    EXPECT_EQ(storage.activeSelectCount(), 1U);

    storage.release(0);
    ASSERT_TRUE(storage.publish(1, AllStoredBehaviors));
    ASSERT_NE(storage.tryToggleValue(1), nullptr);
    EXPECT_EQ(*storage.tryToggleValue(1), 0U);
    ASSERT_NE(storage.tryRangeInputState(1), nullptr);
    EXPECT_FLOAT_EQ(storage.tryRangeInputState(1)->value, 0.0F);
    ASSERT_NE(storage.tryTextInputState(1), nullptr);
    EXPECT_EQ(storage.tryTextInputState(1)->selection, (UI::UITextSelection{}));
    ASSERT_NE(storage.tryScrollState(1), nullptr);
    EXPECT_EQ(storage.tryScrollState(1)->style, (UI::UIScrollViewStyle{}));
    EXPECT_EQ(storage.tryScrollState(1)->requestedOffset, (UI::UIScrollOffset{}));
    EXPECT_EQ(storage.tryScrollState(1)->committedMetrics, (UI::UIScrollViewMetrics{}));
    EXPECT_EQ(storage.tryScrollState(1)->committedViewportRect, (UI::UILogicalRect{}));
    ASSERT_NE(storage.trySelectState(1), nullptr);
    EXPECT_EQ(storage.trySelectState(1)->selectedOption, (UI::UINodeId{}));
    EXPECT_EQ(storage.activateCapacity(), 1U);
    EXPECT_EQ(storage.toggleCapacity(), 1U);
    EXPECT_EQ(storage.rangeInputCapacity(), 1U);
    EXPECT_EQ(storage.textInputCapacity(), 1U);
    EXPECT_EQ(storage.scrollCapacity(), 1U);
    EXPECT_EQ(storage.selectCapacity(), 1U);
    EXPECT_EQ(storage.activateHighWater(), 1U);
    EXPECT_EQ(storage.toggleHighWater(), 1U);
    EXPECT_EQ(storage.rangeInputHighWater(), 1U);
    EXPECT_EQ(storage.textInputHighWater(), 1U);
    EXPECT_EQ(storage.scrollHighWater(), 1U);
    EXPECT_EQ(storage.selectHighWater(), 1U);

    storage.release(1);
    storage.release(1);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
    EXPECT_EQ(storage.activeRangeInputCount(), 0U);
    EXPECT_EQ(storage.activeTextInputCount(), 0U);
    EXPECT_EQ(storage.activeScrollCount(), 0U);
    EXPECT_EQ(storage.activeSelectCount(), 0U);
}

TEST(UIBehaviorStateStorageTests, TogglePreflightFailureDoesNotPublishActivateSlot)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 0, 0, 0, 0, 0, resource);

    const Core::Status rejected = storage.publish(0, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activateHighWater(), 0U);
}

TEST(UIBehaviorStateStorageTests, RangePreflightFailureDoesNotPublishEarlierCapabilitySlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 0, 0, 0, 0, resource);

    const Core::Status rejected = storage.publish(0, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle |
                                                         UI::UIElementBehavior::RangeInput);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.tryRangeInputState(0), nullptr);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
    EXPECT_EQ(storage.activeRangeInputCount(), 0U);
    EXPECT_EQ(storage.activateHighWater(), 0U);
    EXPECT_EQ(storage.toggleHighWater(), 0U);
    EXPECT_EQ(storage.rangeInputHighWater(), 0U);
}

TEST(UIBehaviorStateStorageTests, TextInputPreflightFailureDoesNotPublishEarlierCapabilitySlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 1, 0, 0, 0, resource);

    constexpr UI::UIElementBehavior Behaviors =
        UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput |
        UI::UIElementBehavior::TextInput;
    const Core::Status rejected = storage.publish(0, Behaviors);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.tryRangeInputState(0), nullptr);
    EXPECT_EQ(storage.tryTextInputState(0), nullptr);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
    EXPECT_EQ(storage.activeRangeInputCount(), 0U);
    EXPECT_EQ(storage.activeTextInputCount(), 0U);
    EXPECT_EQ(storage.activateHighWater(), 0U);
    EXPECT_EQ(storage.toggleHighWater(), 0U);
    EXPECT_EQ(storage.rangeInputHighWater(), 0U);
    EXPECT_EQ(storage.textInputHighWater(), 0U);
}

TEST(UIBehaviorStateStorageTests, ScrollPreflightFailureDoesNotPublishEarlierCapabilitySlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 1, 1, 0, 0, resource);

    constexpr UI::UIElementBehavior Behaviors =
        UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput |
        UI::UIElementBehavior::TextInput | UI::UIElementBehavior::Scroll;
    const Core::Status rejected = storage.publish(0, Behaviors);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.tryRangeInputState(0), nullptr);
    EXPECT_EQ(storage.tryTextInputState(0), nullptr);
    EXPECT_EQ(storage.tryScrollState(0), nullptr);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
    EXPECT_EQ(storage.activeRangeInputCount(), 0U);
    EXPECT_EQ(storage.activeTextInputCount(), 0U);
    EXPECT_EQ(storage.activeScrollCount(), 0U);
    EXPECT_EQ(storage.activateHighWater(), 0U);
    EXPECT_EQ(storage.toggleHighWater(), 0U);
    EXPECT_EQ(storage.rangeInputHighWater(), 0U);
    EXPECT_EQ(storage.textInputHighWater(), 0U);
    EXPECT_EQ(storage.scrollHighWater(), 0U);
}

TEST(UIBehaviorStateStorageTests, SelectPreflightFailureDoesNotPublishEarlierCapabilitySlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 1, 1, 1, 0, resource);

    constexpr UI::UIElementBehavior Behaviors =
        UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput |
        UI::UIElementBehavior::TextInput | UI::UIElementBehavior::Scroll | UI::UIElementBehavior::Select;
    const Core::Status rejected = storage.publish(0, Behaviors);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.tryRangeInputState(0), nullptr);
    EXPECT_EQ(storage.tryTextInputState(0), nullptr);
    EXPECT_EQ(storage.tryScrollState(0), nullptr);
    EXPECT_EQ(storage.trySelectState(0), nullptr);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
    EXPECT_EQ(storage.activeRangeInputCount(), 0U);
    EXPECT_EQ(storage.activeTextInputCount(), 0U);
    EXPECT_EQ(storage.activeScrollCount(), 0U);
    EXPECT_EQ(storage.activeSelectCount(), 0U);
    EXPECT_EQ(storage.activateHighWater(), 0U);
    EXPECT_EQ(storage.toggleHighWater(), 0U);
    EXPECT_EQ(storage.rangeInputHighWater(), 0U);
    EXPECT_EQ(storage.textInputHighWater(), 0U);
    EXPECT_EQ(storage.scrollHighWater(), 0U);
    EXPECT_EQ(storage.selectHighWater(), 0U);
}

TEST(UIBehaviorStateStorageTests, ReservationPreflightFailureIsAtomicAcrossAllPoolsAndReportsEachFailure)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(4, 2, 1, 2, 1, 2, 1, resource);
    constexpr UI::UIElementBehavior ExhaustedBehaviors = UI::UIElementBehavior::Toggle |
                                                         UI::UIElementBehavior::TextInput |
                                                         UI::UIElementBehavior::Select;
    ASSERT_TRUE(storage.publish(0, ExhaustedBehaviors));

    const Core::Status rejected = storage.reserve(OneOfEachSlot);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);

    const UI::Detail::UIBehaviorStateStorageCounters counters = storage.counters();
    EXPECT_EQ(counters.requested, OneOfEachSlot);
    EXPECT_EQ(counters.reserved, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_EQ(counters.outstandingReservations, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_EQ(counters.capacityFailures.activate, 0U);
    EXPECT_EQ(counters.capacityFailures.toggle, 1U);
    EXPECT_EQ(counters.capacityFailures.range, 0U);
    EXPECT_EQ(counters.capacityFailures.textInput, 1U);
    EXPECT_EQ(counters.capacityFailures.scroll, 0U);
    EXPECT_EQ(counters.capacityFailures.selection, 1U);

    constexpr UI::UIElementBehavior AvailableBehaviors = UI::UIElementBehavior::Activate |
                                                         UI::UIElementBehavior::RangeInput |
                                                         UI::UIElementBehavior::Scroll;
    ASSERT_TRUE(storage.publish(1, AvailableBehaviors));
    EXPECT_TRUE(storage.hasActivate(1));
    EXPECT_NE(storage.tryRangeInputState(1), nullptr);
    EXPECT_NE(storage.tryScrollState(1), nullptr);
}

TEST(UIBehaviorStateStorageTests, ReservationPreventsOrdinaryPublishAndReservedPublishConsumesAllSixQuotas)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 1, 1, 1, 1, resource);
    UI::Detail::UIBehaviorStateSlotCounts reservation = OneOfEachSlot;
    ASSERT_TRUE(storage.reserve(reservation));

    const Core::Status ordinaryPublish = storage.publish(0, AllStoredBehaviors);
    ASSERT_FALSE(ordinaryPublish);
    EXPECT_EQ(ordinaryPublish.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.tryRangeInputState(0), nullptr);
    EXPECT_EQ(storage.tryTextInputState(0), nullptr);
    EXPECT_EQ(storage.tryScrollState(0), nullptr);
    EXPECT_EQ(storage.trySelectState(0), nullptr);
    EXPECT_EQ(reservation, OneOfEachSlot);

    ASSERT_TRUE(storage.publishReserved(1, AllStoredBehaviors, reservation));
    EXPECT_EQ(reservation, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_TRUE(storage.hasActivate(1));
    EXPECT_TRUE(storage.hasToggle(1));
    EXPECT_NE(storage.tryRangeInputState(1), nullptr);
    EXPECT_NE(storage.tryTextInputState(1), nullptr);
    EXPECT_NE(storage.tryScrollState(1), nullptr);
    EXPECT_NE(storage.trySelectState(1), nullptr);

    const UI::Detail::UIBehaviorStateStorageCounters counters = storage.counters();
    EXPECT_EQ(counters.requested, OneOfEachSlot);
    EXPECT_EQ(counters.reserved, OneOfEachSlot);
    EXPECT_EQ(counters.published, OneOfEachSlot);
    EXPECT_EQ(counters.outstandingReservations, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_EQ(counters.capacityFailures, OneOfEachSlot);
}

TEST(UIBehaviorStateStorageTests, ReservedPublishQuotaFailureDoesNotConsumeEarlierCapabilities)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 2, 2, 2, 2, 2, 2, resource);
    UI::Detail::UIBehaviorStateSlotCounts reservation{.activate = 1};
    ASSERT_TRUE(storage.reserve(reservation));

    const Core::Status rejected = storage.publishReserved(
        0, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle, reservation);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(reservation, (UI::Detail::UIBehaviorStateSlotCounts{.activate = 1}));
    EXPECT_EQ(storage.counters().outstandingReservations,
              (UI::Detail::UIBehaviorStateSlotCounts{.activate = 1}));
    EXPECT_EQ(storage.counters().published, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_EQ(storage.counters().capacityFailures.toggle, 1U);

    ASSERT_TRUE(storage.publishReserved(0, UI::UIElementBehavior::Activate, reservation));
    EXPECT_TRUE(storage.hasActivate(0));
    EXPECT_EQ(reservation, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_EQ(storage.counters().published.activate, 1U);
}

TEST(UIBehaviorStateStorageTests, PartialConsumptionReleaseIsIdempotentAndReturnsOnlyUnusedQuota)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(3, 3, 3, 3, 3, 3, 3, resource);
    UI::Detail::UIBehaviorStateSlotCounts reservation{
        .activate = 2,
        .toggle = 2,
        .range = 2,
        .textInput = 2,
        .scroll = 2,
        .selection = 2,
    };
    ASSERT_TRUE(storage.reserve(reservation));
    ASSERT_TRUE(storage.publishReserved(0, AllStoredBehaviors, reservation));
    EXPECT_EQ(reservation, OneOfEachSlot);
    EXPECT_EQ(storage.counters().outstandingReservations, OneOfEachSlot);

    storage.releaseReservation(reservation);
    EXPECT_EQ(reservation, (UI::Detail::UIBehaviorStateSlotCounts{}));
    EXPECT_EQ(storage.counters().outstandingReservations, (UI::Detail::UIBehaviorStateSlotCounts{}));
    storage.releaseReservation(reservation);
    EXPECT_EQ(storage.counters().outstandingReservations, (UI::Detail::UIBehaviorStateSlotCounts{}));

    ASSERT_TRUE(storage.publish(1, AllStoredBehaviors));
    ASSERT_TRUE(storage.publish(2, AllStoredBehaviors));
    EXPECT_EQ(storage.activeActivateCount(), 3U);
    EXPECT_EQ(storage.activeToggleCount(), 3U);
    EXPECT_EQ(storage.activeRangeInputCount(), 3U);
    EXPECT_EQ(storage.activeTextInputCount(), 3U);
    EXPECT_EQ(storage.activeScrollCount(), 3U);
    EXPECT_EQ(storage.activeSelectCount(), 3U);
    EXPECT_EQ(storage.counters().published.activate, 3U);
    EXPECT_EQ(storage.counters().published.toggle, 3U);
    EXPECT_EQ(storage.counters().published.range, 3U);
    EXPECT_EQ(storage.counters().published.textInput, 3U);
    EXPECT_EQ(storage.counters().published.scroll, 3U);
    EXPECT_EQ(storage.counters().published.selection, 3U);
}

TEST(UIBehaviorStateStorageTests, ReservationPublishAndReleaseDoNotGrowRuntimeStorage)
{
    ObservingMemoryResource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 2, 2, 2, 2, 2, 2, resource);
    const usize constructionAllocationCount = resource.allocationCount();
    ASSERT_GT(constructionAllocationCount, 0U);

    UI::Detail::UIBehaviorStateSlotCounts reservation = OneOfEachSlot;
    ASSERT_TRUE(storage.reserve(reservation));
    ASSERT_TRUE(storage.publishReserved(0, AllStoredBehaviors, reservation));
    storage.releaseReservation(reservation);
    storage.release(0);
    ASSERT_TRUE(storage.publish(1, AllStoredBehaviors));
    storage.release(1);

    EXPECT_EQ(resource.allocationCount(), constructionAllocationCount);
}

} // namespace
} // namespace Tina::Tests
