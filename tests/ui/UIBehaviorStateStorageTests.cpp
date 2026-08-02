#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>

#include "detail/UIBehaviorStateStorage.hpp"

#include <memory_resource>

namespace Tina::Tests {
namespace {

TEST(UIBehaviorStateStorageTests, PublishesCapabilitiesAtomicallyAndReusesReleasedSlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(3, 1, 1, 1, 1, resource);

    constexpr UI::UIElementBehavior Behaviors =
        UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput |
        UI::UIElementBehavior::TextInput;
    ASSERT_TRUE(storage.publish(0, Behaviors));
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

    const Core::Status rejected = storage.publish(1, Behaviors);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(1));
    EXPECT_FALSE(storage.hasToggle(1));
    EXPECT_EQ(storage.tryRangeInputState(1), nullptr);
    EXPECT_EQ(storage.tryTextInputState(1), nullptr);
    EXPECT_EQ(storage.activeActivateCount(), 1U);
    EXPECT_EQ(storage.activeToggleCount(), 1U);
    EXPECT_EQ(storage.activeRangeInputCount(), 1U);
    EXPECT_EQ(storage.activeTextInputCount(), 1U);

    storage.release(0);
    ASSERT_TRUE(storage.publish(1, Behaviors));
    ASSERT_NE(storage.tryToggleValue(1), nullptr);
    EXPECT_EQ(*storage.tryToggleValue(1), 0U);
    ASSERT_NE(storage.tryRangeInputState(1), nullptr);
    EXPECT_FLOAT_EQ(storage.tryRangeInputState(1)->value, 0.0F);
    ASSERT_NE(storage.tryTextInputState(1), nullptr);
    EXPECT_EQ(storage.tryTextInputState(1)->selection, (UI::UITextSelection{}));
    EXPECT_EQ(storage.activateCapacity(), 1U);
    EXPECT_EQ(storage.toggleCapacity(), 1U);
    EXPECT_EQ(storage.rangeInputCapacity(), 1U);
    EXPECT_EQ(storage.textInputCapacity(), 1U);
    EXPECT_EQ(storage.activateHighWater(), 1U);
    EXPECT_EQ(storage.toggleHighWater(), 1U);
    EXPECT_EQ(storage.rangeInputHighWater(), 1U);
    EXPECT_EQ(storage.textInputHighWater(), 1U);

    storage.release(1);
    storage.release(1);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
    EXPECT_EQ(storage.activeRangeInputCount(), 0U);
    EXPECT_EQ(storage.activeTextInputCount(), 0U);
}

TEST(UIBehaviorStateStorageTests, TogglePreflightFailureDoesNotPublishActivateSlot)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 0, 0, 0, resource);

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
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 0, 0, resource);

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
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 1, 1, 0, resource);

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

} // namespace
} // namespace Tina::Tests
