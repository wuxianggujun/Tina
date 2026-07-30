#include <gtest/gtest.h>

#include "UIInputRouteTestSupport.hpp"
#include "detail/UIInputPrimitives.hpp"
#include "detail/UIPointerRouteInspection.hpp"
#include "detail/UIPointerRoutePath.hpp"

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Tests {
namespace {

using namespace UIInputRouteTestSupport;

TEST_F(UIInputRouteTest, InspectsNearestEnabledTargetsAcrossPhysicalAndCapturedRoutes)
{
    auto tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    const UI::UICommittedHitView hit = tree.context->committedHit();
    std::vector<UI::UICommittedHitEntry> entries(
        hit.entries().begin(), hit.entries().end());
    const auto physicalTarget = UI::Detail::pointerHitTargetForEntry(
        entries, UI::Detail::findHitEntryIndex(tree.target, entries));
    std::pmr::vector<u32> routePath;
    ASSERT_TRUE(UI::Detail::buildPointerRoutePath(
                    physicalTarget, entries, entries.size(), routePath)
                    .succeeded());
    const u32 panelIndex = UI::Detail::findHitEntryIndex(tree.panel, entries);
    ASSERT_LT(panelIndex, entries.size());
    entries[panelIndex].behaviors = UI::UIElementBehavior::RangeInput;

    const auto inspection = UI::Detail::inspectPointerRouteTargets(
        physicalTarget, routePath, entries, tree.target,
        [](UI::UINodeId) noexcept { return true; });

    EXPECT_EQ(inspection.error,
              UI::Detail::UIPointerRouteInspectionError::None);
    EXPECT_EQ(inspection.physicalNearestActivatable, tree.target);
    EXPECT_EQ(inspection.routedNearestActivatable, tree.target);
    EXPECT_EQ(inspection.routedNearestRangeInput, tree.panel);
    EXPECT_TRUE(inspection.pointWithinArmedActivatable);

    const auto targetDisabled = UI::Detail::inspectPointerRouteTargets(
        physicalTarget, routePath, entries, tree.target,
        [&](UI::UINodeId node) noexcept { return node != tree.target; });
    EXPECT_FALSE(targetDisabled.physicalNearestActivatable.hasValue());
    EXPECT_FALSE(targetDisabled.routedNearestActivatable.hasValue());
    EXPECT_EQ(targetDisabled.routedNearestRangeInput, tree.panel);
    EXPECT_TRUE(targetDisabled.pointWithinArmedActivatable);
}

TEST_F(UIInputRouteTest, ReportsPhysicalCyclesAndInvalidRouteIndices)
{
    auto tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    const UI::UICommittedHitView hit = tree.context->committedHit();
    std::vector<UI::UICommittedHitEntry> entries(
        hit.entries().begin(), hit.entries().end());
    const u32 targetIndex = UI::Detail::findHitEntryIndex(tree.target, entries);
    ASSERT_LT(targetIndex, entries.size());
    const auto physicalTarget = UI::Detail::pointerHitTargetForEntry(
        entries, targetIndex);
    entries[targetIndex].parentEntryIndex = targetIndex;
    const std::array validRoute{targetIndex};

    const auto cycle = UI::Detail::inspectPointerRouteTargets(
        physicalTarget, validRoute, entries, UI::UINodeId{},
        [](UI::UINodeId) noexcept { return true; });

    EXPECT_EQ(cycle.error,
              UI::Detail::UIPointerRouteInspectionError::PhysicalAncestryCycle);

    entries[targetIndex].parentEntryIndex = UI::InvalidUIHitEntryIndex;
    const std::array invalidRoute{static_cast<u32>(entries.size())};
    const auto invalid = UI::Detail::inspectPointerRouteTargets(
        physicalTarget, invalidRoute, entries, UI::UINodeId{},
        [](UI::UINodeId) noexcept { return true; });
    EXPECT_EQ(invalid.error,
              UI::Detail::UIPointerRouteInspectionError::InvalidRouteEntryIndex);
}

} // namespace
} // namespace Tina::Tests
