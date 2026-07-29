#include <gtest/gtest.h>

#include "detail/UIThemeBindingResolver.hpp"

#include <array>

namespace Tina::Tests {
namespace {

using UI::Detail::ThemeBindingBoxPaint;
using UI::Detail::ThemeBindingButtonPaint;
using UI::Detail::ThemeBindingCheckboxPaint;
using UI::Detail::ThemeBindingDropdownPaint;
using UI::Detail::ThemeBindingListViewPaint;
using UI::Detail::ThemeBindingProgressBarPaint;
using UI::Detail::ThemeBindingRadioButtonPaint;
using UI::Detail::ThemeBindingScrollViewPaint;
using UI::Detail::ThemeBindingSliderPaint;
using UI::Detail::ThemeBindingTextStyle;
using UI::Detail::ThemeBindingTreeViewPaint;
using UI::Detail::UIThemeBindingMask;

struct ExpectedThemeBindings final {
    UI::UIWidgetKind kind = UI::UIWidgetKind::Root;
    UIThemeBindingMask bindings = 0;
};

TEST(UIThemeBindingResolverTest, EveryWidgetKindHasOneCanonicalBindingSet)
{
    constexpr std::array expected{
        ExpectedThemeBindings{UI::UIWidgetKind::Root, 0},
        ExpectedThemeBindings{UI::UIWidgetKind::Panel, 0},
        ExpectedThemeBindings{UI::UIWidgetKind::Label, ThemeBindingTextStyle},
        ExpectedThemeBindings{UI::UIWidgetKind::Button,
                              ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                  ThemeBindingTextStyle},
        ExpectedThemeBindings{UI::UIWidgetKind::Checkbox,
                              ThemeBindingBoxPaint | ThemeBindingCheckboxPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::Slider,
                              ThemeBindingBoxPaint | ThemeBindingSliderPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::TextEdit,
                              ThemeBindingBoxPaint | ThemeBindingTextStyle},
        ExpectedThemeBindings{UI::UIWidgetKind::ProgressBar,
                              ThemeBindingBoxPaint |
                                  ThemeBindingProgressBarPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::RadioButton,
                              ThemeBindingRadioButtonPaint |
                                  ThemeBindingTextStyle},
        ExpectedThemeBindings{UI::UIWidgetKind::Modal, ThemeBindingBoxPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::ScrollView,
                              ThemeBindingScrollViewPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::Dropdown,
                              ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                  ThemeBindingTextStyle |
                                  ThemeBindingDropdownPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::Popup, ThemeBindingBoxPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::DropdownItem,
                              ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                  ThemeBindingTextStyle},
        ExpectedThemeBindings{UI::UIWidgetKind::ListView,
                              ThemeBindingBoxPaint | ThemeBindingListViewPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::ListViewItem,
                              ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                  ThemeBindingTextStyle},
        ExpectedThemeBindings{UI::UIWidgetKind::TreeView,
                              ThemeBindingBoxPaint | ThemeBindingTreeViewPaint},
        ExpectedThemeBindings{UI::UIWidgetKind::TreeViewItem,
                              ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                  ThemeBindingTextStyle},
    };

    const UI::UITheme theme = UI::makeDefaultProductTheme();
    for (const ExpectedThemeBindings entry : expected)
    {
        EXPECT_EQ(UI::Detail::resolveProductThemeChrome(entry.kind, theme).bindings,
                  entry.bindings);
    }
}

TEST(UIThemeBindingResolverTest, ResolvedChromeUsesPublicThemeFactories)
{
    const UI::UITheme theme = UI::makeLightProductTheme();

    const auto button = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::Button, theme);
    const UI::UIButtonChrome expectedButton = UI::makeButtonChrome(theme);
    EXPECT_EQ(button.boxPaint, expectedButton.box);
    EXPECT_EQ(button.buttonPaint, expectedButton.states);
    EXPECT_EQ(button.textStyle, expectedButton.label);

    const auto dropdown = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::Dropdown, theme);
    const UI::UIDropdownChrome expectedDropdown = UI::makeDropdownChrome(theme);
    EXPECT_EQ(dropdown.boxPaint, expectedDropdown.box);
    EXPECT_EQ(dropdown.buttonPaint, expectedDropdown.states);
    EXPECT_EQ(dropdown.textStyle, expectedDropdown.label);
    EXPECT_EQ(dropdown.dropdownPaint, expectedDropdown.dropdown);

    const auto tree = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::TreeView, theme);
    EXPECT_EQ(tree.listViewPaint, UI::UIListViewPaint{});
    EXPECT_EQ(tree.treeViewPaint, UI::makeTreeViewPaint(theme));
}

TEST(UIThemeBindingResolverTest, ChangeClassificationHonorsDetachedBindings)
{
    const auto dark = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::Button, UI::makeDefaultProductTheme());
    const auto light = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::Button, UI::makeLightProductTheme());

    const auto allChanges = UI::Detail::classifyBoundThemeChromeChanges(
        dark.bindings, dark, light);
    EXPECT_TRUE(allChanges.paint);
    EXPECT_FALSE(allChanges.layout);

    const auto detachedChanges = UI::Detail::classifyBoundThemeChromeChanges(
        UIThemeBindingMask{0}, dark, light);
    EXPECT_FALSE(detachedChanges.paint);
    EXPECT_FALSE(detachedChanges.layout);
}

TEST(UIThemeBindingResolverTest, LayoutSensitiveControlMetricsAreClassified)
{
    auto current = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::ScrollView, UI::makeDefaultProductTheme());
    auto next = current;
    next.scrollViewPaint.thickness += 1.0F;

    auto changes = UI::Detail::classifyBoundThemeChromeChanges(
        ThemeBindingScrollViewPaint, current, next);
    EXPECT_TRUE(changes.paint);
    EXPECT_TRUE(changes.layout);

    current = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::Dropdown, UI::makeDefaultProductTheme());
    next = current;
    next.dropdownPaint.indicatorInset += 1.0F;
    changes = UI::Detail::classifyBoundThemeChromeChanges(
        ThemeBindingDropdownPaint, current, next);
    EXPECT_TRUE(changes.paint);
    EXPECT_TRUE(changes.layout);

    current = UI::Detail::resolveProductThemeChrome(
        UI::UIWidgetKind::RadioButton, UI::makeDefaultProductTheme());
    next = current;
    next.radioButtonPaint.labelGap += 1.0F;
    changes = UI::Detail::classifyBoundThemeChromeChanges(
        ThemeBindingRadioButtonPaint, current, next);
    EXPECT_TRUE(changes.paint);
    EXPECT_TRUE(changes.layout);
}

} // namespace
} // namespace Tina::Tests
