#include <gtest/gtest.h>

#include "detail/UIStyleRoleResolver.hpp"

#include <array>
#include <utility>

namespace Tina::Tests {
namespace {

TEST(UIStyleRoleResolverTests, ValidatesRolesAndPublishesExpectedBindingMasks)
{
    using namespace UI::Detail;

    EXPECT_TRUE(isValidStyleRole(UI::UIStyleRoleId::None));
    EXPECT_TRUE(isValidStyleRole(UI::UIStyleRoleId::TreeView));
    EXPECT_FALSE(isValidStyleRole(static_cast<UI::UIStyleRoleId>(255)));
    EXPECT_EQ(defaultThemeBindingsFor(static_cast<UI::UIStyleRoleId>(255)), 0U);

    const std::array expected{
        std::pair{UI::UIStyleRoleId::PanelSurface, ThemeBindingBoxPaint},
        std::pair{UI::UIStyleRoleId::TextBody, ThemeBindingTextStyle},
        std::pair{UI::UIStyleRoleId::ButtonPrimary,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::ButtonTonal,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::ButtonOutlined,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::ButtonText,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::Checkbox,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingCheckboxPaint)},
        std::pair{UI::UIStyleRoleId::Slider,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingSliderPaint)},
        std::pair{UI::UIStyleRoleId::TextInput,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingTextStyle | ThemeBindingTextEditPaint)},
        std::pair{UI::UIStyleRoleId::ProgressBar,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingProgressBarPaint)},
        std::pair{UI::UIStyleRoleId::RadioButton,
                  static_cast<u16>(ThemeBindingRadioButtonPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::SegmentedButton,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingRadioButtonPaint |
                                   ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::Tab,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingTabPaint |
                                   ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::ScrollView, ThemeBindingScrollViewPaint},
        std::pair{UI::UIStyleRoleId::Dropdown,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle |
                                   ThemeBindingDropdownPaint)},
        std::pair{UI::UIStyleRoleId::ListView,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingListViewPaint)},
        std::pair{UI::UIStyleRoleId::TreeView,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingTreeViewPaint)},
    };
    for (const auto& [role, bindings] : expected)
    {
        EXPECT_EQ(defaultThemeBindingsFor(role), bindings);
    }
}

TEST(UIStyleRoleResolverTests, ResolvesControlChromeFromTheRequestedTheme)
{
    const UI::UITheme theme = UI::makeLightProductTheme();

    const auto primary = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonPrimary, theme);
    const UI::UIButtonChrome expectedPrimary = UI::makeButtonChrome(theme);
    EXPECT_EQ(primary.box, expectedPrimary.box);
    EXPECT_EQ(primary.button, expectedPrimary.states);
    EXPECT_EQ(primary.text, expectedPrimary.label);

    const auto danger = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonDanger, theme);
    const UI::UIButtonChrome expectedDanger = UI::makeButtonChrome(theme, theme.danger);
    EXPECT_EQ(danger.box, expectedDanger.box);
    EXPECT_EQ(danger.button, expectedDanger.states);
    EXPECT_EQ(danger.text, expectedDanger.label);

    const auto tonal = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonTonal, theme);
    const UI::UIButtonChrome expectedTonal = UI::makeTonalButtonChrome(theme);
    EXPECT_EQ(tonal.box, expectedTonal.box);
    EXPECT_EQ(tonal.button, expectedTonal.states);
    EXPECT_EQ(tonal.text, expectedTonal.label);

    const auto outlined = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonOutlined, theme);
    const UI::UIButtonChrome expectedOutlined = UI::makeOutlinedButtonChrome(theme);
    EXPECT_EQ(outlined.box, expectedOutlined.box);
    EXPECT_EQ(outlined.button, expectedOutlined.states);
    EXPECT_EQ(outlined.text, expectedOutlined.label);

    const auto textButton = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonText, theme);
    const UI::UIButtonChrome expectedTextButton = UI::makeTextButtonChrome(theme);
    EXPECT_EQ(textButton.box, expectedTextButton.box);
    EXPECT_EQ(textButton.button, expectedTextButton.states);
    EXPECT_EQ(textButton.text, expectedTextButton.label);

    const auto segmented = UI::Detail::productChromeFor(UI::UIStyleRoleId::SegmentedButton, theme);
    const UI::UISegmentedButtonChrome expectedSegmented = UI::makeSegmentedButtonChrome(theme);
    EXPECT_EQ(segmented.box, expectedSegmented.box);
    EXPECT_EQ(segmented.radioButton, expectedSegmented.radio);
    EXPECT_EQ(segmented.text, expectedSegmented.label);

    const auto tab = UI::Detail::productChromeFor(UI::UIStyleRoleId::Tab, theme);
    const UI::UITabChrome expectedTab = UI::makeTabChrome(theme);
    EXPECT_EQ(tab.box, expectedTab.box);
    EXPECT_EQ(tab.tab, expectedTab.tab);
    EXPECT_EQ(tab.text, expectedTab.label);

    const auto dropdown = UI::Detail::productChromeFor(UI::UIStyleRoleId::Dropdown, theme);
    const UI::UIDropdownChrome expectedDropdown = UI::makeDropdownChrome(theme);
    EXPECT_EQ(dropdown.box, expectedDropdown.box);
    EXPECT_EQ(dropdown.button, expectedDropdown.states);
    EXPECT_EQ(dropdown.text, expectedDropdown.label);
    EXPECT_EQ(dropdown.dropdown, expectedDropdown.dropdown);

    const auto textEdit = UI::Detail::productChromeFor(UI::UIStyleRoleId::TextInput, theme);
    const UI::UITextEditChrome expectedTextEdit = UI::makeTextEditChrome(theme);
    EXPECT_EQ(textEdit.box, expectedTextEdit.box);
    EXPECT_EQ(textEdit.textEdit, expectedTextEdit.paint);
    EXPECT_EQ(textEdit.text, expectedTextEdit.text);

    const auto tree = UI::Detail::productChromeFor(UI::UIStyleRoleId::TreeView, theme);
    EXPECT_EQ(tree.box, UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface1, 245)));
    EXPECT_EQ(tree.treeView, UI::makeTreeViewPaint(theme));
}

TEST(UIStyleRoleResolverTests, NoneAndInvalidRolesResolveEmptyChrome)
{
    const UI::UITheme theme = UI::makeDefaultProductTheme();
    const UI::Detail::ProductChrome empty{};

    const auto none = UI::Detail::productChromeFor(UI::UIStyleRoleId::None, theme);
    EXPECT_EQ(none.box, empty.box);
    EXPECT_EQ(none.text, empty.text);
    EXPECT_EQ(none.button, empty.button);

    const auto invalid = UI::Detail::productChromeFor(static_cast<UI::UIStyleRoleId>(255), theme);
    EXPECT_EQ(invalid.box, empty.box);
    EXPECT_EQ(invalid.text, empty.text);
    EXPECT_EQ(invalid.treeView, empty.treeView);
    EXPECT_EQ(invalid.textEdit, empty.textEdit);
}

} // namespace
} // namespace Tina::Tests
