#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UITheme.hpp>

#include "detail/UIElementContractResolver.hpp"

#include <memory>
#include <ranges>
#include <vector>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Platform::WindowId makeWindow()
{
    static auto windows = [] {
        auto result = WindowPool::Create(32);
        EXPECT_TRUE(result.has_value());
        return std::make_unique<WindowPool>(std::move(*result));
    }();
    auto window = windows->tryEmplace(1);
    EXPECT_TRUE(window.has_value());
    return window ? *window : Platform::WindowId{};
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(Platform::WindowId window)
{
    auto result = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .routePathCapacity = 8,
            .buttonActionCapacity = 4,
        });
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle layout;
    layout.size.width = UI::UILayoutLength::Px(width);
    layout.size.height = UI::UILayoutLength::Px(height);
    return layout;
}

[[nodiscard]] std::vector<UI::UICommittedPaintEntry> paintsFor(
    UI::UICommittedPaintView paint, UI::UINodeId node)
{
    std::vector<UI::UICommittedPaintEntry> result;
    for (const UI::UICommittedPaintEntry& entry : paint.entries())
    {
        if (entry.node == node && entry.kind == UI::UICommittedPaintKind::SolidQuad)
        {
            result.push_back(entry);
        }
    }
    return result;
}

[[nodiscard]] const UI::UISemanticsEntry* semanticsFor(
    UI::UICommittedSemanticsView semantics, UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        semantics.entries(), [node](const UI::UISemanticsEntry& entry) {
            return entry.node == node;
        });
    return found == semantics.entries().end() ? nullptr : &*found;
}

[[nodiscard]] UI::UIPointerInputEvent pointerEvent(
    Platform::WindowId window, UI::UIRoutedPointerEventKind kind,
    u64 sequence) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1U),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = {.x = 10.0F, .y = 10.0F},
        .button = Platform::PointerButton::Primary,
    };
}

TEST(UIToggleSwitchTests, RecipeUsesExistingToggleContractWithSwitchSemantics)
{
    constexpr auto defaults = UI::makeToggleSwitchElement();
    constexpr auto standard = UI::makeToggleSwitchElement({
        .accessibleName = "Snap to grid",
    });
    constexpr auto compact = UI::makeToggleSwitchElement({
        .accessibleName = "Compact",
        .size = UI::UIToggleSwitchSize::Compact,
    });
    static_assert(defaults.layout.size.width == UI::UILayoutLength::Px(44.0F));
    static_assert(defaults.layout.size.height == UI::UILayoutLength::Px(24.0F));
    static_assert(standard.layout.size.width == UI::UILayoutLength::Px(44.0F));
    static_assert(standard.layout.size.height == UI::UILayoutLength::Px(24.0F));
    static_assert(compact.layout.size.width == UI::UILayoutLength::Px(36.0F));
    static_assert(compact.layout.size.height == UI::UILayoutLength::Px(20.0F));
    static_assert(standard.visual.styleRole == UI::UIStyleRoleId::ToggleSwitch);
    static_assert(standard.semantics.role == UI::UISemanticsRole::Switch);
    static_assert(standard.semantics.name == std::string_view{"Snap to grid"});
    static_assert(UI::hasBehavior(standard.behaviors, UI::UIElementBehavior::Toggle));
    static_assert(UI::hasSemanticsAction(
        standard.semantics.actions, UI::UISemanticsAction::Toggle));

    const auto kind = UI::Detail::resolveElementBuiltinKind(standard);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, UI::Detail::BuiltinElementKind::Checkbox);
}

TEST(UIToggleSwitchTests, TrackThumbCheckedStateAndSemanticsCommitTogether)
{
    const Platform::WindowId window = makeWindow();
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root);
    auto toggle = context->rootBuilder().createElement(
        root->rootNodeId(), UI::makeToggleSwitchElement({
                                .accessibleName = "Snap to grid",
                            }));
    ASSERT_TRUE(toggle);
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater);
    ASSERT_TRUE(updater->setLayoutStyle(root->rootNodeId(), fixedSize(100.0F, 60.0F)));

    int activations = 0;
    ASSERT_TRUE(updater->setCheckboxAction(
        *toggle,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}));

    const UI::UITheme dark = UI::makeModernDesktopTheme();
    const UI::UICheckboxChrome expected = UI::makeToggleSwitchChrome(dark);
    EXPECT_EQ(updater->checkboxPaint(*toggle).value(), expected.indicator);
    EXPECT_FALSE(updater->isChecked(*toggle).value());

    auto paint = paintsFor(context->committedPaint(), *toggle);
    ASSERT_EQ(paint.size(), 2U);
    EXPECT_EQ(paint[0].solidFill,
              UI::premultiply(UI::scaleColorAlpha(dark.colors.surfaceContainer, 245)));
    EXPECT_EQ(paint[0].cornerRadii,
              UI::UILogicalCornerRadii::uniform(dark.controls.toggleSwitchCornerRadius));
    EXPECT_EQ(paint[1].worldRect,
              (UI::UILogicalRect{.x = 3.0F, .y = 3.0F,
                                 .width = 18.0F, .height = 18.0F}));
    EXPECT_EQ(paint[1].cornerRadii, UI::UILogicalCornerRadii::uniform(9.0F));
    EXPECT_EQ(paint[1].solidFill, UI::premultiply(dark.colors.onSurfaceVariant));

    const UI::UISemanticsEntry* semantics =
        semanticsFor(context->committedSemantics(), *toggle);
    ASSERT_NE(semantics, nullptr);
    EXPECT_EQ(semantics->role, UI::UISemanticsRole::Switch);
    EXPECT_EQ(semantics->name, "Snap to grid");
    EXPECT_FALSE(semantics->checked);

    ASSERT_TRUE(updater->setChecked(*toggle, true));
    ASSERT_TRUE(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    paint = paintsFor(context->committedPaint(), *toggle);
    ASSERT_EQ(paint.size(), 2U);
    EXPECT_EQ(paint[0].solidFill, UI::premultiply(dark.colors.primary));
    EXPECT_EQ(paint[1].worldRect,
              (UI::UILogicalRect{.x = 23.0F, .y = 3.0F,
                                 .width = 18.0F, .height = 18.0F}));
    EXPECT_EQ(paint[1].solidFill, UI::premultiply(dark.colors.onPrimary));
    semantics = semanticsFor(context->committedSemantics(), *toggle);
    ASSERT_NE(semantics, nullptr);
    EXPECT_TRUE(semantics->checked);

    auto down = context->routePointerInput(pointerEvent(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    auto up = context->routePointerInput(pointerEvent(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    ASSERT_TRUE(down && up);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(activations, 1);
    EXPECT_FALSE(updater->isChecked(*toggle).value());
}

TEST(UIToggleSwitchTests, ThemeAndInvalidChromeUpdatesRemainTransactional)
{
    auto context = createContext(makeWindow());
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root);
    auto toggle = context->rootBuilder().createElement(
        root->rootNodeId(), UI::makeToggleSwitchElement({
                                .accessibleName = "Live preview",
                            }));
    ASSERT_TRUE(toggle);
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater);

    const UI::UITheme light = UI::makeModernDesktopTheme(UI::UIColorScheme::Light);
    ASSERT_TRUE(context->setProductTheme(light));
    EXPECT_EQ(updater->checkboxPaint(*toggle).value(),
              UI::makeToggleSwitchChrome(light).indicator);

    const UI::UICheckboxPaint before = updater->checkboxPaint(*toggle).value();
    UI::UICheckboxPaint invalid = before;
    invalid.presentation = static_cast<UI::UIToggleIndicatorPresentation>(255);
    const Core::Status rejected = updater->setCheckboxPaint(*toggle, invalid);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(updater->checkboxPaint(*toggle).value(), before);

    const auto invalidRecipe = context->rootBuilder().createElement(
        root->rootNodeId(), UI::makeToggleSwitchElement({
                                .accessibleName = "Invalid",
                                .size = static_cast<UI::UIToggleSwitchSize>(255),
                            }));
    EXPECT_FALSE(invalidRecipe);
    EXPECT_EQ(invalidRecipe.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

} // namespace
} // namespace Tina::Tests
