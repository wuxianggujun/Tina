#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UITheme.hpp>

#include <memory>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Platform::WindowId makeTestWindow()
{
    static auto windows = [] {
        auto pool = WindowPool::Create(8);
        EXPECT_TRUE(pool.has_value());
        return std::make_unique<WindowPool>(std::move(*pool));
    }();
    auto id = windows->tryEmplace(1);
    EXPECT_TRUE(id.has_value());
    return id ? *id : Platform::WindowId{};
}

// Product default: applyDefaultProductChrome remains true.
[[nodiscard]] std::unique_ptr<UI::UIContext> createProductContext(Platform::WindowId window)
{
    auto contextResult = UI::UIContext::Create(window);
    EXPECT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);
    return contextResult ? std::move(*contextResult) : nullptr;
}

TEST(UIThemeTest, DefaultTokensAreDistinctSurfacesAndTextTiers)
{
    constexpr UI::UITheme theme = UI::makeDefaultProductTheme();
    EXPECT_NE(theme.surface0, theme.surface1);
    EXPECT_NE(theme.surface1, theme.surface2);
    EXPECT_NE(theme.textPrimary, theme.textSecondary);
    EXPECT_NE(theme.textTitle, theme.textAccent);
    EXPECT_GT(theme.panelBorderWidth, 0.0F);
}

TEST(UIThemeTest, PanelBoxPaintIncludesBorderAndOptionalShadow)
{
    constexpr UI::UITheme theme = UI::makeDefaultProductTheme();
    constexpr UI::UIBoxPaint flat =
        UI::makePanelBoxPaint(theme, theme.surface1, UI::UIElevation::None);
    ASSERT_TRUE(flat.solidFill.has_value());
    EXPECT_EQ(flat.solidFill->color, theme.surface1);
    EXPECT_EQ(flat.borderLight, theme.borderLight);
    EXPECT_EQ(flat.borderDark, theme.borderDark);
    EXPECT_EQ(flat.borderWidth, theme.panelBorderWidth);
    EXPECT_EQ(flat.shadow.alpha, 0);

    constexpr UI::UIBoxPaint elevated =
        UI::makePanelBoxPaint(theme, theme.surface0, UI::UIElevation::Low);
    EXPECT_EQ(elevated.shadow, theme.shadow);
    EXPECT_EQ(elevated.shadowOffsetX, theme.panelShadowOffsetX);
    EXPECT_EQ(elevated.shadowOffsetY, theme.panelShadowOffsetY);
}

TEST(UIThemeTest, PressedAndDisabledHelpersAreDeterministic)
{
    constexpr UI::UIStraightSrgba8Color base = UI::rgb(0x40A070, 230);
    constexpr UI::UIStraightSrgba8Color pressed = UI::darkenChannel(base, 36);
    constexpr UI::UIStraightSrgba8Color hovered = UI::lightenChannel(base, 28);
    EXPECT_LT(pressed.red, base.red);
    EXPECT_GT(hovered.red, base.red);
    EXPECT_EQ(pressed.alpha, base.alpha);
    EXPECT_EQ(UI::scaleColorAlpha(base, 100).alpha, 100);
}

TEST(UIThemeTest, TextStyleHelpersUseTokenColors)
{
    constexpr UI::UITheme theme = UI::makeDefaultProductTheme();
    EXPECT_EQ(UI::makeTitleTextStyle(theme).color, theme.textTitle);
    EXPECT_EQ(UI::makeBodyTextStyle(theme).color, theme.textPrimary);
    EXPECT_EQ(UI::makeSecondaryTextStyle(theme).color, theme.textSecondary);
    EXPECT_EQ(UI::makeAccentTextStyle(theme).color, theme.textAccent);
}

TEST(UIThemeTest, ButtonChromeUsesThemeTokens)
{
    constexpr UI::UITheme theme = UI::makeDefaultProductTheme();
    constexpr UI::UIButtonChrome chrome = UI::makeButtonChrome(theme);
    ASSERT_TRUE(chrome.box.solidFill.has_value());
    EXPECT_EQ(chrome.box.solidFill->color.alpha, 230);
    EXPECT_EQ(chrome.states.focusedBackgroundColor, theme.focusRing);
    EXPECT_EQ(chrome.states.disabledBackgroundColor, theme.buttonDisabled);
    EXPECT_EQ(chrome.label.color, theme.textPrimary);
}

TEST(UIThemeTest, LightThemeDiffersFromDefault)
{
    constexpr UI::UITheme dark = UI::makeDefaultProductTheme();
    constexpr UI::UITheme light = UI::makeLightProductTheme();
    EXPECT_NE(dark.surface0, light.surface0);
    EXPECT_NE(dark.textPrimary, light.textPrimary);
    const auto darkChrome = UI::makeButtonChrome(dark);
    const auto lightChrome = UI::makeButtonChrome(light);
    ASSERT_TRUE(darkChrome.box.solidFill.has_value());
    ASSERT_TRUE(lightChrome.box.solidFill.has_value());
    EXPECT_NE(darkChrome.box.solidFill->color, lightChrome.box.solidFill->color);
}

TEST(UIThemeTest, CreateAppliesDefaultButtonChrome)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    EXPECT_TRUE(context->productTheme() == UI::makeDefaultProductTheme());

    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value()) << (root ? "" : root.error().message);
    auto button = context->rootBuilder().createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value()) << (button ? "" : button.error().message);

    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value()) << (updater ? "" : updater.error().message);
    auto buttonPaint = updater->buttonPaint(*button);
    ASSERT_TRUE(buttonPaint.has_value()) << (buttonPaint ? "" : buttonPaint.error().message);

    const UI::UIButtonChrome expected = UI::makeButtonChrome(context->productTheme());
    EXPECT_EQ(*buttonPaint, expected.states);

    auto textStyle = updater->textStyle(*button);
    ASSERT_TRUE(textStyle.has_value()) << (textStyle ? "" : textStyle.error().message);
    EXPECT_EQ(*textStyle, expected.label);
}

TEST(UIThemeTest, SetProductThemeAffectsSubsequentCreatesOnly)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);

    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto darkButton = context->rootBuilder().createButton(root->rootNodeId());
    ASSERT_TRUE(darkButton.has_value());

    context->setProductTheme(UI::makeLightProductTheme());
    EXPECT_TRUE(context->productTheme() == UI::makeLightProductTheme());

    auto lightButton = context->rootBuilder().createButton(root->rootNodeId());
    ASSERT_TRUE(lightButton.has_value());

    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    auto darkPaint = updater->buttonPaint(*darkButton);
    auto lightPaint = updater->buttonPaint(*lightButton);
    ASSERT_TRUE(darkPaint.has_value());
    ASSERT_TRUE(lightPaint.has_value());
    EXPECT_EQ(*darkPaint, UI::makeButtonChrome(UI::makeDefaultProductTheme()).states);
    EXPECT_EQ(*lightPaint, UI::makeButtonChrome(UI::makeLightProductTheme()).states);
    EXPECT_NE(*darkPaint, *lightPaint);
}

TEST(UIThemeTest, LocalPaintOverrideDoesNotClearButtonStateChrome)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto button = context->rootBuilder().createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value());

    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    const UI::UIBoxPaint overridePaint = UI::makeSolidBox(UI::rgb(0xFF0000, 255));
    ASSERT_TRUE(updater->setBoxPaint(*button, overridePaint).has_value());

    auto buttonPaint = updater->buttonPaint(*button);
    ASSERT_TRUE(buttonPaint.has_value());
    EXPECT_EQ(*buttonPaint, UI::makeButtonChrome(UI::makeDefaultProductTheme()).states);
}

TEST(UIThemeTest, CreateAppliesDefaultSliderAndProgressChrome)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());

    auto slider = context->rootBuilder().createSlider(root->rootNodeId());
    auto progress = context->rootBuilder().createProgressBar(root->rootNodeId());
    ASSERT_TRUE(slider.has_value());
    ASSERT_TRUE(progress.has_value());

    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    auto sliderPaint = updater->sliderPaint(*slider);
    auto progressPaint = updater->progressBarPaint(*progress);
    ASSERT_TRUE(sliderPaint.has_value());
    ASSERT_TRUE(progressPaint.has_value());
    EXPECT_EQ(*sliderPaint, UI::makeSliderChrome(UI::makeDefaultProductTheme()).slider);
    EXPECT_EQ(*progressPaint, UI::makeProgressBarChrome(UI::makeDefaultProductTheme()).bar);
}

TEST(UIThemeTest, ConfigCanDisableDefaultChromeForTests)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    UI::UIContextCapacityConfig capacities{};
    capacities.applyDefaultProductChrome = false;
    auto contextResult = UI::UIContext::Create(window, capacities);
    ASSERT_TRUE(contextResult.has_value());
    auto& context = *contextResult;
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto button = context->rootBuilder().createButton(root->rootNodeId());
    ASSERT_TRUE(button.has_value());
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    auto buttonPaint = updater->buttonPaint(*button);
    ASSERT_TRUE(buttonPaint.has_value());
    EXPECT_EQ(*buttonPaint, UI::UIButtonPaint{});
}

} // namespace
} // namespace Tina::Tests
