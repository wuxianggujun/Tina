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
    EXPECT_TRUE(isValidStyleRole(UI::UIStyleRoleId::Switch));
    EXPECT_TRUE(isValidStyleRole(UI::UIStyleRoleId::Splitter));
    EXPECT_TRUE(isValidStyleRole(UI::UIStyleRoleId::IconOnError));
    EXPECT_TRUE(isValidStyleRole(UI::UIStyleRoleId::FloatingSurface));
    EXPECT_FALSE(isValidStyleRole(static_cast<UI::UIStyleRoleId>(255)));
    EXPECT_EQ(defaultThemeBindingsFor(static_cast<UI::UIStyleRoleId>(255)), 0U);

    const std::array expected{
        std::pair{UI::UIStyleRoleId::PanelSurface, ThemeBindingBoxPaint},
        std::pair{UI::UIStyleRoleId::ModalScrim, ThemeBindingBoxPaint},
        std::pair{UI::UIStyleRoleId::TextBody, ThemeBindingTextStyle},
        std::pair{UI::UIStyleRoleId::TextError, ThemeBindingTextStyle},
        std::pair{UI::UIStyleRoleId::ButtonPrimary,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                   ThemeBindingTextStyle | ThemeBindingImageTint)},
        std::pair{UI::UIStyleRoleId::ButtonTonal,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                   ThemeBindingTextStyle | ThemeBindingImageTint)},
        std::pair{UI::UIStyleRoleId::ButtonOutlined,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                   ThemeBindingTextStyle | ThemeBindingImageTint)},
        std::pair{UI::UIStyleRoleId::ButtonText,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                                   ThemeBindingTextStyle | ThemeBindingImageTint)},
        std::pair{UI::UIStyleRoleId::Checkbox,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingCheckboxPaint)},
        std::pair{UI::UIStyleRoleId::Slider,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingSliderPaint)},
        std::pair{UI::UIStyleRoleId::TextInput,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingTextStyle | ThemeBindingTextEditPaint)},
        std::pair{UI::UIStyleRoleId::TextInputInvalid,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingTextStyle |
                                   ThemeBindingTextEditPaint)},
        std::pair{UI::UIStyleRoleId::ProgressBar,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingProgressBarPaint)},
        std::pair{UI::UIStyleRoleId::RadioButton,
                  static_cast<u16>(ThemeBindingRadioButtonPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::SegmentedButton,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingRadioButtonPaint |
                                   ThemeBindingTextStyle | ThemeBindingImageTint)},
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
        std::pair{UI::UIStyleRoleId::DividerAccent, ThemeBindingBoxPaint},
        std::pair{UI::UIStyleRoleId::BadgeDanger,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingTextStyle)},
        std::pair{UI::UIStyleRoleId::Switch,
                  static_cast<u16>(ThemeBindingBoxPaint | ThemeBindingCheckboxPaint)},
        std::pair{UI::UIStyleRoleId::Splitter, ThemeBindingSplitterPaint},
        std::pair{UI::UIStyleRoleId::IconOnPrimary, ThemeBindingImageTint},
        std::pair{UI::UIStyleRoleId::FloatingSurface, ThemeBindingBoxPaint},
    };
    for (const auto& [role, bindings] : expected)
    {
        EXPECT_EQ(defaultThemeBindingsFor(role), bindings);
    }
}

TEST(UIStyleRoleResolverTests, ResolvesControlChromeFromTheRequestedTheme)
{
    const UI::UITheme theme = UI::makeModernDesktopTheme(UI::UIColorScheme::Light);

    const auto primary = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonPrimary, theme);
    const UI::UIButtonChrome expectedPrimary = UI::makeButtonChrome(theme);
    EXPECT_EQ(primary.box, expectedPrimary.box);
    EXPECT_EQ(primary.button, expectedPrimary.states);
    EXPECT_EQ(primary.text, expectedPrimary.label);
    EXPECT_EQ(primary.imageTint, theme.colors.onPrimary);

    const auto danger = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonDanger, theme);
    const UI::UIButtonChrome expectedDanger = UI::makeButtonChrome(theme, theme.colors.error);
    EXPECT_EQ(danger.box, expectedDanger.box);
    EXPECT_EQ(danger.button, expectedDanger.states);
    EXPECT_EQ(danger.text, expectedDanger.label);
    EXPECT_EQ(danger.imageTint, theme.colors.onError);

    const auto tonal = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonTonal, theme);
    const UI::UIButtonChrome expectedTonal = UI::makeTonalButtonChrome(theme);
    EXPECT_EQ(tonal.box, expectedTonal.box);
    EXPECT_EQ(tonal.button, expectedTonal.states);
    EXPECT_EQ(tonal.text, expectedTonal.label);
    EXPECT_EQ(tonal.imageTint, theme.colors.onSurface);

    const auto outlined = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonOutlined, theme);
    const UI::UIButtonChrome expectedOutlined = UI::makeOutlinedButtonChrome(theme);
    EXPECT_EQ(outlined.box, expectedOutlined.box);
    EXPECT_EQ(outlined.button, expectedOutlined.states);
    EXPECT_EQ(outlined.text, expectedOutlined.label);
    EXPECT_EQ(outlined.imageTint, theme.colors.onSurface);

    const auto textButton = UI::Detail::productChromeFor(UI::UIStyleRoleId::ButtonText, theme);
    const UI::UIButtonChrome expectedTextButton = UI::makeTextButtonChrome(theme);
    EXPECT_EQ(textButton.box, expectedTextButton.box);
    EXPECT_EQ(textButton.button, expectedTextButton.states);
    EXPECT_EQ(textButton.text, expectedTextButton.label);
    EXPECT_EQ(textButton.imageTint, theme.colors.onSurfaceVariant);

    const auto segmented = UI::Detail::productChromeFor(UI::UIStyleRoleId::SegmentedButton, theme);
    const UI::UISegmentedButtonChrome expectedSegmented = UI::makeSegmentedButtonChrome(theme);
    EXPECT_EQ(segmented.box, expectedSegmented.box);
    EXPECT_EQ(segmented.radioButton, expectedSegmented.radio);
    EXPECT_EQ(segmented.text, expectedSegmented.label);
    EXPECT_EQ(segmented.imageTint, theme.colors.onSurfaceVariant);

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

    const auto invalidTextEdit =
        UI::Detail::productChromeFor(UI::UIStyleRoleId::TextInputInvalid, theme);
    const UI::UITextEditChrome expectedInvalidTextEdit =
        UI::makeInvalidTextEditChrome(theme);
    EXPECT_EQ(invalidTextEdit.box, expectedInvalidTextEdit.box);
    EXPECT_EQ(invalidTextEdit.textEdit, expectedInvalidTextEdit.paint);
    EXPECT_EQ(invalidTextEdit.text, expectedInvalidTextEdit.text);

    const auto errorText =
        UI::Detail::productChromeFor(UI::UIStyleRoleId::TextError, theme);
    EXPECT_EQ(errorText.text, UI::makeErrorTextStyle(theme));

    const auto scrim =
        UI::Detail::productChromeFor(UI::UIStyleRoleId::ModalScrim, theme);
    ASSERT_TRUE(scrim.box.solidFill.has_value());
    EXPECT_EQ(scrim.box.solidFill->color, theme.colors.scrim);

    EXPECT_EQ(UI::Detail::productChromeFor(
                  UI::UIStyleRoleId::IconOnSurface, theme).imageTint,
              theme.colors.onSurface);
    EXPECT_EQ(UI::Detail::productChromeFor(
                  UI::UIStyleRoleId::IconOnPrimary, theme).imageTint,
              theme.colors.onPrimary);
    EXPECT_EQ(UI::Detail::productChromeFor(
                  UI::UIStyleRoleId::IconOnError, theme).imageTint,
              theme.colors.onError);

    const auto tree = UI::Detail::productChromeFor(UI::UIStyleRoleId::TreeView, theme);
    EXPECT_EQ(tree.box, UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.colors.surfaceContainerLow, 245)));
    EXPECT_EQ(tree.treeView, UI::makeTreeViewPaint(theme));

    const auto divider = UI::Detail::productChromeFor(UI::UIStyleRoleId::DividerAccent, theme);
    EXPECT_EQ(divider.box, UI::makeDividerChrome(theme, UI::UIDividerTone::Accent).line);

    const auto badge = UI::Detail::productChromeFor(UI::UIStyleRoleId::BadgeDanger, theme);
    const UI::UIBadgeChrome expectedBadge = UI::makeBadgeChrome(theme, UI::UIBadgeTone::Danger);
    EXPECT_EQ(badge.box, expectedBadge.box);
    EXPECT_EQ(badge.text, expectedBadge.label);

    const auto switchChrome = UI::Detail::productChromeFor(UI::UIStyleRoleId::Switch, theme);
    const UI::UICheckboxChrome expectedSwitch = UI::makeSwitchChrome(theme);
    EXPECT_EQ(switchChrome.box, expectedSwitch.box);
    EXPECT_EQ(switchChrome.checkbox, expectedSwitch.indicator);

    const auto splitter = UI::Detail::productChromeFor(UI::UIStyleRoleId::Splitter, theme);
    EXPECT_EQ(splitter.splitter, UI::makeSplitterChrome(theme).splitter);

    const auto floating =
        UI::Detail::productChromeFor(UI::UIStyleRoleId::FloatingSurface, theme);
    EXPECT_EQ(floating.box,
              UI::makePanelBoxPaint(
                  theme,
                  UI::scaleColorAlpha(theme.colors.surfaceContainerHigh, 252),
                  UI::UIElevation::Floating));
}

TEST(UIStyleRoleResolverTests, NoneAndInvalidRolesResolveEmptyChrome)
{
    const UI::UITheme theme = UI::makeModernDesktopTheme();
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
