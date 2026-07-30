#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include "detail/UIPointerRoutePath.hpp"

#include <array>
#include <memory>
#include <memory_resource>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class UIPointerRoutePathTests : public ::testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());

        auto contextResult = UI::UIContext::Create(
            *windowResult,
            {.nodeCapacity = 3, .rootCapacity = 1,
             .applyDefaultProductChrome = false});
        ASSERT_TRUE(contextResult.has_value());
        context = std::move(*contextResult);
        auto rootResult = context->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value());
        root = std::move(*rootResult);
        auto panelResult = context->rootBuilder().createElement(
            root.rootNodeId(), UI::makePanelElement());
        ASSERT_TRUE(panelResult.has_value());
        panel = *panelResult;
        auto buttonResult = context->rootBuilder().createElement(
            panel, UI::makeButtonElement());
        ASSERT_TRUE(buttonResult.has_value());
        button = *buttonResult;
    }

    [[nodiscard]] std::array<UI::UICommittedHitEntry, 3> entries() const
    {
        return {
            UI::UICommittedHitEntry{
                .node = root.rootNodeId(),
                .rootEntryIndex = 0,
                .worldRect = {.width = 100.0F, .height = 100.0F},
                .effectiveClip = {.width = 100.0F, .height = 100.0F},
                .paintOrdinal = 1,
            },
            UI::UICommittedHitEntry{
                .node = panel,
                .parentEntryIndex = 0,
                .rootEntryIndex = 0,
                .paintOrdinal = 2,
            },
            UI::UICommittedHitEntry{
                .node = button,
                .parentEntryIndex = 1,
                .rootEntryIndex = 0,
                .worldRect = {.x = 10.0F, .y = 20.0F,
                              .width = 30.0F, .height = 40.0F},
                .effectiveClip = {.width = 80.0F, .height = 90.0F},
                .paintOrdinal = 3,
            },
        };
    }

    std::unique_ptr<WindowPool> windows;
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UINodeId panel{};
    UI::UINodeId button{};
};

TEST_F(UIPointerRoutePathTests, ProjectsCommittedEntryIntoPointerTarget)
{
    auto hitEntries = entries();

    const auto target = UI::Detail::pointerHitTargetForEntry(hitEntries, 2);

    EXPECT_EQ(target.node, button);
    EXPECT_EQ(target.rootNode, root.rootNodeId());
    EXPECT_EQ(target.hitEntryIndex, 2U);
    EXPECT_EQ(target.rootEntryIndex, 0U);
    EXPECT_EQ(target.worldRect, hitEntries[2].worldRect);
    EXPECT_EQ(target.effectiveClip, hitEntries[2].effectiveClip);
    EXPECT_EQ(target.paintOrdinal, 3U);
    EXPECT_FALSE(UI::Detail::pointerHitTargetForEntry(hitEntries, 3).hasValue());
    hitEntries[2].rootEntryIndex = 9;
    EXPECT_FALSE(UI::Detail::pointerHitTargetForEntry(hitEntries, 2).hasValue());
}

TEST_F(UIPointerRoutePathTests, BuildsTargetToRootPathWithinFixedCapacity)
{
    const auto hitEntries = entries();
    const auto target = UI::Detail::pointerHitTargetForEntry(hitEntries, 2);
    std::pmr::vector<u32> path;

    const auto result = UI::Detail::buildPointerRoutePath(
        target, hitEntries, 3, path);

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.depth, 3U);
    ASSERT_EQ(path.size(), 3U);
    EXPECT_EQ(path[0], 2U);
    EXPECT_EQ(path[1], 1U);
    EXPECT_EQ(path[2], 0U);
}

TEST_F(UIPointerRoutePathTests, ClearsPartialPathWhenCapacityIsExhausted)
{
    const auto hitEntries = entries();
    const auto target = UI::Detail::pointerHitTargetForEntry(hitEntries, 2);
    std::pmr::vector<u32> path;

    const auto result = UI::Detail::buildPointerRoutePath(
        target, hitEntries, 2, path);

    EXPECT_EQ(result.error,
              UI::Detail::UIPointerRoutePathError::CapacityExceeded);
    EXPECT_TRUE(path.empty());
}

TEST_F(UIPointerRoutePathTests, RejectsCyclesAndMismatchedRoots)
{
    auto hitEntries = entries();
    auto target = UI::Detail::pointerHitTargetForEntry(hitEntries, 2);
    std::pmr::vector<u32> path;
    hitEntries[1].parentEntryIndex = 2;

    const auto cycle = UI::Detail::buildPointerRoutePath(
        target, hitEntries, 8, path);

    EXPECT_EQ(cycle.error, UI::Detail::UIPointerRoutePathError::AncestryCycle);
    EXPECT_TRUE(path.empty());

    hitEntries = entries();
    target = UI::Detail::pointerHitTargetForEntry(hitEntries, 2);
    hitEntries[1].parentEntryIndex = UI::InvalidUIHitEntryIndex;
    const auto disconnected = UI::Detail::buildPointerRoutePath(
        target, hitEntries, 8, path);
    EXPECT_EQ(disconnected.error,
              UI::Detail::UIPointerRoutePathError::InvalidRoot);
    EXPECT_TRUE(path.empty());

    hitEntries = entries();
    target = UI::Detail::pointerHitTargetForEntry(hitEntries, 2);
    target.rootNode = panel;
    const auto mismatched = UI::Detail::buildPointerRoutePath(
        target, hitEntries, 8, path);
    EXPECT_EQ(mismatched.error,
              UI::Detail::UIPointerRoutePathError::InvalidRoot);
    EXPECT_TRUE(path.empty());
}

} // namespace
} // namespace Tina::Tests
