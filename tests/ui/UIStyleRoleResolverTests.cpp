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
        std::pair{UI::UIStyleRoleId::Checkbox,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingCheckboxPaint)},
        std::pair{UI::UIStyleRoleId::Slider,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingSliderPaint)},
        std::pair{UI::UIStyleRoleId::ProgressBar,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingProgressBarPaint)},
        std::pair{UI::UIStyleRoleId::RadioButton,
                  static_cast<u16>(ThemeBindingRadioButtonPaint | ThemeBindingTextStyle)},
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

    const auto danger = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonDanger, theme);
    const UI::UIButtonChrome expectedDanger = UI::makeButtonChrome(theme, theme.danger);
    EXPECT_EQ(danger.box, expectedDanger.box);
    EXPECT_EQ(danger.button, expectedDanger.states);
    EXPECT_EQ(danger.text, expectedDanger.label);

    const auto dropdown = UI::Detail::productChromeFor(UI::UIStyleRoleId::Dropdown, theme);
    const UI::UIDropdownChrome expectedDropdown = UI::makeDropdownChrome(theme);
    EXPECT_EQ(dropdown.box, expectedDropdown.box);
    EXPECT_EQ(dropdown.button, expectedDropdown.states);
    EXPECT_EQ(dropdown.text, expectedDropdown.label);
    EXPECT_EQ(dropdown.dropdown, expectedDropdown.dropdown);

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
}

} // namespace
} // namespace Tina::Tests
