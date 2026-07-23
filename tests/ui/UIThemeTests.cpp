#include <gtest/gtest.h>

#include <tina/ui/UITheme.hpp>

namespace Tina::Tests {
namespace {

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

} // namespace
} // namespace Tina::Tests
