#include <tina/ui/UILayout.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UILayoutLength>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UILayoutStyle>);
static_assert(Tina::UI::UILayoutLength::Auto().isAuto());
static_assert(Tina::UI::UILayoutLength::Px(24.0F).isPx());
static_assert(Tina::UI::UILayoutLength::Percent(50.0F).isPercent());

constexpr Tina::UI::UIEdgeSpacing Spacing =
    Tina::UI::UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
static_assert(Spacing.left == 8.0F);
static_assert(Spacing.top == 4.0F);

constexpr Tina::UI::UILayoutInsets Insets =
    Tina::UI::UILayoutInsets::HorizontalVertical(
        Tina::UI::UILayoutLength::Px(8.0F),
        Tina::UI::UILayoutLength::Percent(100.0F));
static_assert(Insets.left == Tina::UI::UILayoutLength::Px(8.0F));
static_assert(Insets.top == Tina::UI::UILayoutLength::Percent(100.0F));

constexpr Tina::UI::UILayoutStyle OverlayStyle{
    .size = {.width = Tina::UI::UILayoutLength::Percent(100.0F),
             .height = Tina::UI::UILayoutLength::Auto()},
    .margin = Tina::UI::UIEdgeSpacing::All(0.0F),
    .padding = Tina::UI::UIEdgeSpacing::HorizontalVertical(12.0F, 8.0F),
    .absoluteInset = Tina::UI::UILayoutInsets::All(Tina::UI::UILayoutLength::Px(0.0F)),
    .flex = {.direction = Tina::UI::UIFlexDirection::Row,
             .justify = Tina::UI::UIJustifyContent::SpaceBetween,
             .alignItems = Tina::UI::UIAlignItems::Center,
             .grow = 1.0F},
    .position = Tina::UI::UILayoutPositionMode::AbsoluteOverlay,
    .visibility = Tina::UI::UIVisibility::Visible,
};
static_assert(OverlayStyle.position == Tina::UI::UILayoutPositionMode::AbsoluteOverlay);
static_assert(OverlayStyle.flex.direction == Tina::UI::UIFlexDirection::Row);
