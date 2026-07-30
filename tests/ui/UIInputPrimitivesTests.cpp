#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include "detail/UIInputPrimitives.hpp"

#include <array>
#include <limits>
#include <memory>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

TEST(UIInputPrimitivesTests, ValidatesInputEnumsAndMapsEventPhases)
{
    EXPECT_TRUE(UI::Detail::isValidPointerHitPolicy(
        UI::UIPointerHitPolicy::Ignore));
    EXPECT_TRUE(UI::Detail::isValidPointerHitPolicy(
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_FALSE(UI::Detail::isValidPointerHitPolicy(
        static_cast<UI::UIPointerHitPolicy>(255)));

    EXPECT_TRUE(UI::Detail::isValidFocusScopeMode(UI::UIFocusScopeMode::None));
    EXPECT_TRUE(UI::Detail::isValidFocusScopeMode(UI::UIFocusScopeMode::Contain));
    EXPECT_FALSE(UI::Detail::isValidFocusScopeMode(
        static_cast<UI::UIFocusScopeMode>(255)));

    EXPECT_TRUE(UI::Detail::isValidRoutedPointerEventKind(
        UI::UIRoutedPointerEventKind::Move));
    EXPECT_TRUE(UI::Detail::isValidRoutedPointerEventKind(
        UI::UIRoutedPointerEventKind::PointerCancel));
    EXPECT_FALSE(UI::Detail::isValidRoutedPointerEventKind(
        static_cast<UI::UIRoutedPointerEventKind>(255)));

    EXPECT_FALSE(UI::Detail::isValidEventPhaseMask(UI::UIEventPhaseMask::None));
    EXPECT_TRUE(UI::Detail::isValidEventPhaseMask(UI::UIEventPhaseMask::All));
    EXPECT_FALSE(UI::Detail::isValidEventPhaseMask(
        static_cast<UI::UIEventPhaseMask>(1U << 7U)));
    EXPECT_EQ(UI::Detail::phaseMaskFor(UI::UIEventPhase::Capture),
              UI::UIEventPhaseMask::Capture);
    EXPECT_EQ(UI::Detail::phaseMaskFor(UI::UIEventPhase::Target),
              UI::UIEventPhaseMask::Target);
    EXPECT_EQ(UI::Detail::phaseMaskFor(UI::UIEventPhase::Bubble),
              UI::UIEventPhaseMask::Bubble);
    EXPECT_EQ(UI::Detail::phaseMaskFor(static_cast<UI::UIEventPhase>(255)),
              UI::UIEventPhaseMask::None);
}

TEST(UIInputPrimitivesTests, QueriesHitIdentityAncestryCyclesAndModalScope)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    auto windows = std::make_unique<WindowPool>(std::move(*windowsResult));
    auto windowResult = windows->tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto contextResult = UI::UIContext::Create(
        *windowResult,
        {.nodeCapacity = 3, .rootCapacity = 1,
         .applyDefaultProductChrome = false});
    ASSERT_TRUE(contextResult.has_value());
    auto context = std::move(*contextResult);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    auto root = std::move(*rootResult);
    auto panelResult = context->rootBuilder().createElement(
        root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panelResult.has_value());
    auto buttonResult = context->rootBuilder().createElement(
        *panelResult, UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value());

    const std::array entries{
        UI::UICommittedHitEntry{
            .node = root.rootNodeId(),
            .parentEntryIndex = UI::InvalidUIHitEntryIndex,
        },
        UI::UICommittedHitEntry{
            .node = *panelResult,
            .parentEntryIndex = 0,
        },
        UI::UICommittedHitEntry{
            .node = *buttonResult,
            .parentEntryIndex = 1,
            .modalScopeEntryIndex = 0,
            .policy = UI::UIPointerHitPolicy::Targetable,
            .behaviors = UI::UIElementBehavior::Focusable |
                         UI::UIElementBehavior::Activate,
        },
    };

    EXPECT_EQ(UI::Detail::findHitEntryIndex(root.rootNodeId(), entries), 0U);
    EXPECT_EQ(UI::Detail::findHitEntryIndex(*buttonResult, entries), 2U);
    EXPECT_EQ(UI::Detail::findHitEntryIndex({}, entries),
              UI::InvalidUIHitEntryIndex);
    EXPECT_TRUE(UI::Detail::hitEntryIsWithinScope(2, 0, entries));
    EXPECT_TRUE(UI::Detail::hitEntryIsWithinScope(
        2, UI::InvalidUIHitEntryIndex, entries));
    EXPECT_FALSE(UI::Detail::hitEntryIsWithinScope(1, 2, entries));
    EXPECT_FALSE(UI::Detail::hitEntryIsWithinScope(9, 0, entries));
    EXPECT_TRUE(UI::Detail::hitEntryAllowedByModal(entries[2], 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowedByModal(entries[2], 1));
    EXPECT_TRUE(UI::Detail::hitEntryAllowedByModal(
        entries[2], UI::InvalidUIHitEntryIndex));
    EXPECT_TRUE(UI::Detail::hitEntryAllowsPointerInteraction(2, entries, 0));
    EXPECT_TRUE(UI::Detail::hitEntryAllowsPointerCapture(2, entries, 0));
    EXPECT_TRUE(UI::Detail::hitEntryAllowsKeyboardFocus(2, entries, 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsPointerInteraction(1, entries, 0));
    EXPECT_TRUE(UI::Detail::hitEntryAllowsPointerCapture(
        1, entries, UI::InvalidUIHitEntryIndex));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsPointerCapture(1, entries, 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsKeyboardFocus(1, entries, 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsPointerInteraction(3, entries, 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsPointerCapture(3, entries, 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsKeyboardFocus(3, entries, 0));
    EXPECT_FALSE(UI::Detail::hitEntryAllowsPointerInteraction(2, entries, 1));

    std::array cycleEntries{
        UI::UICommittedHitEntry{.parentEntryIndex = 1},
        UI::UICommittedHitEntry{.parentEntryIndex = 0},
        UI::UICommittedHitEntry{},
    };
    EXPECT_FALSE(UI::Detail::hitEntryIsWithinScope(0, 2, cycleEntries));
}

TEST(UIInputPrimitivesTests, QueriesCommittedHitInFrontToBackOrderWithClipModalAndRevisionEvidence)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    auto windows = std::make_unique<WindowPool>(std::move(*windowsResult));
    auto windowResult = windows->tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto contextResult = UI::UIContext::Create(
        *windowResult,
        {.nodeCapacity = 3, .rootCapacity = 1,
         .applyDefaultProductChrome = false});
    ASSERT_TRUE(contextResult.has_value());
    auto context = std::move(*contextResult);
    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value());
    auto root = std::move(*rootResult);
    auto panelResult = context->rootBuilder().createElement(
        root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panelResult.has_value());
    auto buttonResult = context->rootBuilder().createElement(
        *panelResult, UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value());

    const UI::UILogicalRect fullRect{.x = 0.0F, .y = 0.0F,
                                     .width = 100.0F, .height = 100.0F};
    std::array entries{
        UI::UICommittedHitEntry{
            .node = root.rootNodeId(),
            .rootEntryIndex = 0,
            .worldRect = fullRect,
            .effectiveClip = fullRect,
            .paintOrdinal = 0,
            .policy = UI::UIPointerHitPolicy::Targetable,
            .behaviors = UI::UIElementBehavior::None,
        },
        UI::UICommittedHitEntry{
            .node = *panelResult,
            .parentEntryIndex = 0,
            .rootEntryIndex = 0,
            .modalScopeEntryIndex = 1,
            .worldRect = {.x = 10.0F, .y = 10.0F,
                          .width = 80.0F, .height = 80.0F},
            .effectiveClip = fullRect,
            .paintOrdinal = 1,
            .policy = UI::UIPointerHitPolicy::Ignore,
            .behaviors = UI::UIElementBehavior::ModalBarrier,
        },
        UI::UICommittedHitEntry{
            .node = *buttonResult,
            .parentEntryIndex = 1,
            .rootEntryIndex = 0,
            .modalScopeEntryIndex = 1,
            .worldRect = {.x = 20.0F, .y = 20.0F,
                          .width = 40.0F, .height = 40.0F},
            .effectiveClip = {.x = 20.0F, .y = 20.0F,
                              .width = 20.0F, .height = 20.0F},
            .paintOrdinal = 2,
            .policy = UI::UIPointerHitPolicy::Targetable,
            .behaviors = UI::UIElementBehavior::Focusable |
                         UI::UIElementBehavior::Activate,
        },
    };

    const UI::UICommittedHitView hit(entries, 11, 12, 13, 14);
    const auto front = UI::Detail::queryCommittedPointerHit(
        hit, {.x = 25.0F, .y = 25.0F});
    ASSERT_TRUE(front.hasTarget());
    EXPECT_EQ(front.target.node, *buttonResult);
    EXPECT_EQ(front.target.rootNode, root.rootNodeId());
    EXPECT_EQ(front.target.hitEntryIndex, 2U);
    EXPECT_EQ(front.visitedEntryCount, 1U);
    EXPECT_EQ(front.structureRevision, 11U);
    EXPECT_EQ(front.layoutRevision, 12U);
    EXPECT_EQ(front.paintOrderRevision, 13U);
    EXPECT_EQ(front.hitRevision, 14U);
    EXPECT_FALSE(front.modalBarrierActive);

    const auto clipped = UI::Detail::queryCommittedPointerHit(
        hit, {.x = 45.0F, .y = 45.0F});
    ASSERT_TRUE(clipped.hasTarget());
    EXPECT_EQ(clipped.target.node, root.rootNodeId());
    EXPECT_EQ(clipped.visitedEntryCount, 3U);

    const auto halfOpenMiss = UI::Detail::queryCommittedPointerHit(
        hit, {.x = 100.0F, .y = 50.0F});
    EXPECT_FALSE(halfOpenMiss.hasTarget());
    EXPECT_EQ(halfOpenMiss.visitedEntryCount, 3U);

    const UI::UICommittedHitView modalHit(entries, 11, 12, 13, 14, 1);
    const auto modalTarget = UI::Detail::queryCommittedPointerHit(
        modalHit, {.x = 25.0F, .y = 25.0F});
    ASSERT_TRUE(modalTarget.hasTarget());
    EXPECT_EQ(modalTarget.target.node, *buttonResult);
    EXPECT_TRUE(modalTarget.modalBarrierActive);
    const auto modalMiss = UI::Detail::queryCommittedPointerHit(
        modalHit, {.x = 45.0F, .y = 45.0F});
    EXPECT_FALSE(modalMiss.hasTarget());
    EXPECT_EQ(modalMiss.visitedEntryCount, 3U);

    const UI::UICommittedHitView invalidModalHit(entries, 11, 12, 13, 14, 99);
    const auto invalidModal = UI::Detail::queryCommittedPointerHit(
        invalidModalHit, {.x = 45.0F, .y = 45.0F});
    ASSERT_TRUE(invalidModal.hasTarget());
    EXPECT_EQ(invalidModal.target.node, root.rootNodeId());
    EXPECT_FALSE(invalidModal.modalBarrierActive);

    entries[2].rootEntryIndex = 99;
    entries[0].policy = UI::UIPointerHitPolicy::Ignore;
    const auto invalidRoot = UI::Detail::queryCommittedPointerHit(
        UI::UICommittedHitView(entries, 11, 12, 13, 14),
        {.x = 25.0F, .y = 25.0F});
    EXPECT_FALSE(invalidRoot.hasTarget());
    EXPECT_EQ(invalidRoot.visitedEntryCount, 3U);

    const auto nonFinite = UI::Detail::queryCommittedPointerHit(
        hit, {.x = (std::numeric_limits<float>::quiet_NaN)(), .y = 25.0F});
    EXPECT_FALSE(nonFinite.hasTarget());
    EXPECT_EQ(nonFinite.visitedEntryCount, 0U);
    EXPECT_EQ(nonFinite.hitRevision, 14U);
    const auto infinite = UI::Detail::queryCommittedPointerHit(
        hit, {.x = 25.0F, .y = (std::numeric_limits<float>::infinity)()});
    EXPECT_FALSE(infinite.hasTarget());
    EXPECT_EQ(infinite.visitedEntryCount, 0U);
}

} // namespace
} // namespace Tina::Tests
