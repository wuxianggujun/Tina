#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include "detail/UIInputPrimitives.hpp"

#include <array>
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
    auto panelResult = context->rootBuilder().createPanel(root.rootNodeId());
    ASSERT_TRUE(panelResult.has_value());
    auto buttonResult = context->rootBuilder().createButton(*panelResult);
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

    std::array cycleEntries{
        UI::UICommittedHitEntry{.parentEntryIndex = 1},
        UI::UICommittedHitEntry{.parentEntryIndex = 0},
        UI::UICommittedHitEntry{},
    };
    EXPECT_FALSE(UI::Detail::hitEntryIsWithinScope(0, 2, cycleEntries));
}

} // namespace
} // namespace Tina::Tests
