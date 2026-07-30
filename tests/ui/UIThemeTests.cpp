#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UITheme.hpp>

#include <limits>
#include <memory>
#include <thread>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] Platform::WindowId makeTestWindow()
{
    static auto windows = [] {
        auto pool = WindowPool::Create(32);
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
    EXPECT_NE(chrome.states.focusedBackgroundColor.alpha, 0);
    EXPECT_EQ(chrome.states.disabledBackgroundColor, theme.buttonDisabled);
    EXPECT_EQ(chrome.states.focusedBorderColor, theme.focusRing);
    EXPECT_EQ(chrome.label.color, theme.textPrimary);
    EXPECT_EQ(chrome.box.shadow, theme.shadow);
    EXPECT_GT(chrome.box.borderWidth, 0.0F);
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
    auto button = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
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

TEST(UIThemeTest, SetProductThemeUpdatesExistingAndSubsequentControls)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);

    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto darkButton = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(darkButton.has_value());

    ASSERT_TRUE(context->setProductTheme(UI::makeLightProductTheme()).has_value());
    EXPECT_TRUE(context->productTheme() == UI::makeLightProductTheme());

    auto lightButton = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(lightButton.has_value());

    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    auto darkPaint = updater->buttonPaint(*darkButton);
    auto lightPaint = updater->buttonPaint(*lightButton);
    ASSERT_TRUE(darkPaint.has_value());
    ASSERT_TRUE(lightPaint.has_value());
    EXPECT_EQ(*darkPaint, UI::makeButtonChrome(UI::makeLightProductTheme()).states);
    EXPECT_EQ(*lightPaint, UI::makeButtonChrome(UI::makeLightProductTheme()).states);
    EXPECT_EQ(*darkPaint, *lightPaint);
}

TEST(UIThemeTest, LocalPaintOverrideDoesNotClearButtonStateChrome)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto button = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());

    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    const UI::UIBoxPaint overridePaint = UI::makeSolidBox(UI::rgb(0xFF0000, 255));
    ASSERT_TRUE(updater->setBoxPaint(*button, overridePaint).has_value());

    auto buttonPaint = updater->buttonPaint(*button);
    ASSERT_TRUE(buttonPaint.has_value());
    EXPECT_EQ(*buttonPaint, UI::makeButtonChrome(UI::makeDefaultProductTheme()).states);
}

TEST(UIThemeTest, LocalOverridesDetachOnlyTheirCorrespondingThemeProperties)
{
    const Platform::WindowId window = makeTestWindow();
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto button = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());

    const UI::UIButtonPaint localStates{
        .hoveredBackgroundColor = UI::rgb(0xAA2200),
        .pressedBackgroundColor = UI::rgb(0x771100),
        .focusedBackgroundColor = UI::rgb(0xCC4400),
        .disabledBackgroundColor = UI::rgb(0x555555),
        .focusedBorderColor = UI::rgb(0xFFFFFF),
    };
    ASSERT_TRUE(updater->setButtonPaint(*button, localStates).has_value());
    ASSERT_TRUE(context->setProductTheme(UI::makeLightProductTheme()).has_value());

    auto states = updater->buttonPaint(*button);
    auto textStyle = updater->textStyle(*button);
    ASSERT_TRUE(states.has_value());
    ASSERT_TRUE(textStyle.has_value());
    EXPECT_EQ(*states, localStates);
    EXPECT_EQ(*textStyle, UI::makeButtonChrome(UI::makeLightProductTheme()).label);
}

TEST(UIThemeTest, ExplicitSameValueSetterStillDetachesThatProperty)
{
    const Platform::WindowId window = makeTestWindow();
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto button = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());

    const UI::UIButtonPaint darkStates = updater->buttonPaint(*button).value();
    ASSERT_TRUE(updater->setButtonPaint(*button, darkStates).has_value());
    ASSERT_TRUE(context->setProductTheme(UI::makeLightProductTheme()).has_value());
    EXPECT_EQ(updater->buttonPaint(*button).value(), darkStates);
}

TEST(UIThemeTest, SetProductThemeUpdatesEveryManagedControlProperty)
{
    const Platform::WindowId window = makeTestWindow();
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto builder = context->rootBuilder();
    auto label = builder.createElement(root->rootNodeId(), UI::makeLabelElement());
    auto checkbox = builder.createElement(root->rootNodeId(), UI::makeCheckboxElement());
    auto slider = builder.createElement(root->rootNodeId(), UI::makeSliderElement());
    auto textEdit = builder.createElement(root->rootNodeId(), UI::makeTextEditElement());
    auto progress = builder.createElement(root->rootNodeId(), UI::makeProgressBarElement());
    auto radio = builder.createElement(root->rootNodeId(), UI::makeRadioButtonElement());
    ASSERT_TRUE(label && checkbox && slider && textEdit && progress && radio);
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    ASSERT_TRUE(context->setProductTheme(UI::makeLightProductTheme()).has_value());

    const UI::UITheme light = UI::makeLightProductTheme();
    EXPECT_EQ(updater->textStyle(*label).value(), UI::makeBodyTextStyle(light));
    EXPECT_EQ(updater->checkboxPaint(*checkbox).value(), UI::makeCheckboxChrome(light).indicator);
    EXPECT_EQ(updater->sliderPaint(*slider).value(), UI::makeSliderChrome(light).slider);
    EXPECT_EQ(updater->textStyle(*textEdit).value(), UI::makeTextEditChrome(light).text);
    EXPECT_EQ(updater->progressBarPaint(*progress).value(), UI::makeProgressBarChrome(light).bar);
    EXPECT_EQ(updater->radioButtonPaint(*radio).value(), UI::makeRadioButtonChrome(light).radio);
    EXPECT_EQ(updater->textStyle(*radio).value(), UI::makeRadioButtonChrome(light).label);
}

TEST(UIThemeTest, InvalidThemeAndWrongThreadAreRejectedWithoutMutation)
{
    const Platform::WindowId window = makeTestWindow();
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    const UI::UITheme original = context->productTheme();

    UI::UITheme invalid = UI::makeLightProductTheme();
    invalid.buttonTextSize = (std::numeric_limits<float>::quiet_NaN)();
    Core::Status invalidStatus = context->setProductTheme(invalid);
    ASSERT_FALSE(invalidStatus.has_value());
    EXPECT_EQ(invalidStatus.error().code, UI::UIErrorCode::InvalidTheme);
    EXPECT_EQ(context->productTheme(), original);

    Core::Status threadedStatus = Core::success();
    std::thread worker([&] {
        threadedStatus = context->setProductTheme(UI::makeLightProductTheme());
    });
    worker.join();
    ASSERT_FALSE(threadedStatus.has_value());
    EXPECT_EQ(threadedStatus.error().code, UI::UIErrorCode::WrongOwnerThread);
    EXPECT_EQ(context->productTheme(), original);
}

TEST(UIThemeTest, DirtyCapacityFailureLeavesThemeAndManagedPropertiesUntouched)
{
    const Platform::WindowId window = makeTestWindow();
    auto contextResult = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 16,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
            .paintSnapshotCapacity = 16,
        });
    ASSERT_TRUE(contextResult.has_value());
    auto& context = *contextResult;
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());
    auto first = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    auto second = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(first && second);
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    const UI::UIButtonPaint original = updater->buttonPaint(*first).value();

    Core::Status status = context->setProductTheme(UI::makeLightProductTheme());
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->productTheme(), UI::makeDefaultProductTheme());
    EXPECT_EQ(updater->buttonPaint(*first).value(), original);
    EXPECT_EQ(updater->buttonPaint(*second).value(), original);
}

TEST(UIThemeTest, ReapplyingCurrentThemeIsANoOp)
{
    const Platform::WindowId window = makeTestWindow();
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    const UI::UIContextStatistics before = context->statistics();
    ASSERT_TRUE(context->setProductTheme(context->productTheme()).has_value());
    const UI::UIContextStatistics after = context->statistics();
    EXPECT_EQ(after.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_EQ(after.paintRevision, before.paintRevision);
    EXPECT_EQ(after.layoutRevision, before.layoutRevision);
}

TEST(UIThemeTest, CreateAppliesDefaultSliderAndProgressChrome)
{
    const Platform::WindowId window = makeTestWindow();
    ASSERT_TRUE(window.hasValue());
    auto context = createProductContext(window);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value());

    auto slider = context->rootBuilder().createElement(root->rootNodeId(), UI::makeSliderElement());
    auto progress = context->rootBuilder().createElement(root->rootNodeId(), UI::makeProgressBarElement());
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
    auto button = context->rootBuilder().createElement(root->rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value());
    auto buttonPaint = updater->buttonPaint(*button);
    ASSERT_TRUE(buttonPaint.has_value());
    EXPECT_EQ(*buttonPaint, UI::UIButtonPaint{});
}

} // namespace
} // namespace Tina::Tests
