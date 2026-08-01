#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>

#include "detail/UIBehaviorStateStorage.hpp"

#include <memory_resource>

namespace Tina::Tests {
namespace {

TEST(UIBehaviorStateStorageTests, PublishesCapabilitiesAtomicallyAndReusesReleasedSlots)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(3, 1, 1, resource);

    ASSERT_TRUE(storage.publish(0, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle));
    ASSERT_TRUE(storage.hasActivate(0));
    ASSERT_TRUE(storage.hasToggle(0));
    ASSERT_NE(storage.tryToggleValue(0), nullptr);
    *storage.tryToggleValue(0) = 1;

    const Core::Status rejected = storage.publish(1, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(1));
    EXPECT_FALSE(storage.hasToggle(1));
    EXPECT_EQ(storage.activeActivateCount(), 1U);
    EXPECT_EQ(storage.activeToggleCount(), 1U);

    storage.release(0);
    ASSERT_TRUE(storage.publish(1, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle));
    ASSERT_NE(storage.tryToggleValue(1), nullptr);
    EXPECT_EQ(*storage.tryToggleValue(1), 0U);
    EXPECT_EQ(storage.activateCapacity(), 1U);
    EXPECT_EQ(storage.toggleCapacity(), 1U);
    EXPECT_EQ(storage.activateHighWater(), 1U);
    EXPECT_EQ(storage.toggleHighWater(), 1U);

    storage.release(1);
    storage.release(1);
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activeToggleCount(), 0U);
}

TEST(UIBehaviorStateStorageTests, TogglePreflightFailureDoesNotPublishActivateSlot)
{
    std::pmr::monotonic_buffer_resource resource;
    UI::Detail::UIBehaviorStateStorage storage(2, 1, 0, resource);

    const Core::Status rejected = storage.publish(0, UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(storage.hasActivate(0));
    EXPECT_FALSE(storage.hasToggle(0));
    EXPECT_EQ(storage.activeActivateCount(), 0U);
    EXPECT_EQ(storage.activateHighWater(), 0U);
}

} // namespace
} // namespace Tina::Tests
