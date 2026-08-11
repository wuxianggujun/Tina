#include <gtest/gtest.h>

#include "detail/UIThemeTransitionResolver.hpp"

namespace Tina::Tests {
namespace {

[[nodiscard]] UI::Detail::ProductChromeStorage storageFor(UI::Detail::ProductChrome& chrome) noexcept
{
    return {
        .box = chrome.box,
        .text = chrome.text,
        .button = chrome.button,
        .checkbox = chrome.checkbox,
        .slider = chrome.slider,
        .progressBar = chrome.progressBar,
        .radioButton = chrome.radioButton,
        .scrollView = chrome.scrollView,
        .dropdown = chrome.dropdown,
        .listView = chrome.listView,
        .treeView = chrome.treeView,
        .textEdit = chrome.textEdit,
    };
}

TEST(UIThemeTransitionResolverTests, TracksTextEditPaintAsPaintOnlyBinding)
{
    using namespace UI::Detail;

    const UI::UITheme theme = UI::makeLightProductTheme();
    ProductChrome current = productChromeFor(UI::UIStyleRoleId::TextInput, UI::makeDefaultProductTheme());

    const ProductChromeTransition transition = resolveProductChromeTransition(
        storageFor(current), UI::UIStyleRoleId::TextInput, theme, ThemeBindingTextEditPaint,
        ThemeBindingTextEditPaint);

    EXPECT_EQ(transition.changedBindings, ThemeBindingTextEditPaint);
    EXPECT_EQ(transition.layoutAffectingBindings, 0U);
    applyProductChromeTransition(storageFor(current), transition, ThemeBindingTextEditPaint);
    EXPECT_EQ(current.textEdit, UI::makeTextEditChrome(theme).paint);
}

TEST(UIThemeTransitionResolverTests, IdenticalChromeHasNoChanges)
{
    using namespace UI::Detail;

    const UI::UITheme theme = UI::makeLightProductTheme();
    ProductChrome current = productChromeFor(UI::UIStyleRoleId::Dropdown, theme);
    const u16 bindings = defaultThemeBindingsFor(UI::UIStyleRoleId::Dropdown);

    const ProductChromeTransition transition =
        resolveProductChromeTransition(storageFor(current), UI::UIStyleRoleId::Dropdown, theme, bindings, bindings);

    EXPECT_EQ(transition.changedBindings, 0U);
    EXPECT_EQ(transition.layoutAffectingBindings, 0U);
}

TEST(UIThemeTransitionResolverTests, ClassifiesEveryLayoutAffectingChromeInput)
{
    using namespace UI::Detail;

    const UI::UITheme theme = UI::makeDefaultProductTheme();
    ProductChrome current = productChromeFor(UI::UIStyleRoleId::None, theme);
    current.text.logicalSize = 1.0F;
    current.radioButton.labelGap = 1.0F;
    current.scrollView.thickness = 1.0F;
    current.dropdown.indicatorWidth = 1.0F;
    current.listView.scrollBar.minThumbExtent = 1.0F;
    current.treeView.scrollBar.thickness = 1.0F;
    constexpr u16 bindings = ThemeBindingTextStyle | ThemeBindingRadioButtonPaint | ThemeBindingScrollViewPaint |
                             ThemeBindingDropdownPaint | ThemeBindingListViewPaint | ThemeBindingTreeViewPaint;

    const ProductChromeTransition transition =
        resolveProductChromeTransition(storageFor(current), UI::UIStyleRoleId::None, theme, bindings, bindings);

    EXPECT_EQ(transition.changedBindings, bindings);
    EXPECT_EQ(transition.layoutAffectingBindings, bindings);
}

TEST(UIThemeTransitionResolverTests, IndicatorVisibilityChangeRequiresLayout)
{
    using namespace UI::Detail;

    const UI::UITheme theme = UI::makeDefaultProductTheme();
    ProductChrome current = productChromeFor(UI::UIStyleRoleId::RadioButton, theme);

    const ProductChromeTransition transition = resolveProductChromeTransition(
        storageFor(current), UI::UIStyleRoleId::SegmentedButton, theme,
        ThemeBindingRadioButtonPaint, ThemeBindingRadioButtonPaint);

    EXPECT_EQ(transition.changedBindings, ThemeBindingRadioButtonPaint);
    EXPECT_EQ(transition.layoutAffectingBindings, ThemeBindingRadioButtonPaint);
    EXPECT_FALSE(transition.target.radioButton.indicatorVisible);
}

TEST(UIThemeTransitionResolverTests, ClearsDetachedBindingsAndAppliesOnlyAffectedStorage)
{
    using namespace UI::Detail;

    const UI::UITheme theme = UI::makeLightProductTheme();
    ProductChrome current = productChromeFor(UI::UIStyleRoleId::ButtonPrimary, theme);
    const UI::UITreeViewPaint retainedTreeView = UI::makeTreeViewPaint(theme);
    current.treeView = retainedTreeView;
    constexpr u16 affected = ThemeBindingBoxPaint | ThemeBindingTextStyle | ThemeBindingButtonPaint;

    const ProductChromeTransition transition = resolveProductChromeTransition(
        storageFor(current), UI::UIStyleRoleId::ButtonPrimary, theme, affected, ThemeBindingTextStyle);

    EXPECT_EQ(transition.changedBindings, static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint));
    applyProductChromeTransition(storageFor(current), transition, affected);
    EXPECT_EQ(current.box, UI::UIBoxPaint{});
    EXPECT_EQ(current.button, UI::UIButtonPaint{});
    EXPECT_EQ(current.text, productChromeFor(UI::UIStyleRoleId::ButtonPrimary, theme).text);
    EXPECT_EQ(current.treeView, retainedTreeView);
}

} // namespace
} // namespace Tina::Tests
