#include <tina/ui/UILayout.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::UI::UILayoutLength>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UIGridTrackList>);
static_assert(std::is_trivially_copyable_v<Tina::UI::UILayoutStyle>);
static_assert(Tina::UI::UILayoutLength::Auto().isAuto());
static_assert(Tina::UI::UILayoutLength::Px(24.0F).isPx());
static_assert(Tina::UI::UILayoutLength::Percent(50.0F).isPercent());
static_assert(Tina::UI::UILayoutLength::MinContent().isMinContent());
static_assert(Tina::UI::UILayoutLength::MaxContent().isMaxContent());

constexpr Tina::UI::UIEdgeSpacing Spacing =
    Tina::UI::UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
static_assert(Spacing.left == 8.0F);
static_assert(Spacing.top == 4.0F);

constexpr Tina::UI::UIOverlayOffset Offset{
    .x = Tina::UI::UILayoutLength::Px(8.0F),
    .y = Tina::UI::UILayoutLength::Percent(100.0F),
};
static_assert(Offset.x == Tina::UI::UILayoutLength::Px(8.0F));
static_assert(Offset.y == Tina::UI::UILayoutLength::Percent(100.0F));

constexpr Tina::UI::UILayoutStyle OverlayStyle{
    .size = {.width = Tina::UI::UILayoutLength::Percent(100.0F),
             .height = Tina::UI::UILayoutLength::Auto()},
    .margin = Tina::UI::UIEdgeSpacing::All(0.0F),
    .padding = Tina::UI::UIEdgeSpacing::HorizontalVertical(12.0F, 8.0F),
    .flexContainer = {.direction = Tina::UI::UIFlexDirection::Row,
                      .justifyContent = Tina::UI::UIJustifyContent::SpaceBetween,
                      .alignContent = Tina::UI::UIAlignContent::Center,
                      .alignItems = Tina::UI::UIAxisAlignment::Center},
    .flexItem = {.grow = 1.0F,
                 .shrink = 1.0F,
                 .basis = Tina::UI::UILayoutLength::Auto(),
                 .alignSelf = Tina::UI::UIAlignSelf::Center},
    .overlay = {.horizontal = Tina::UI::UIAxisAlignment::Center,
                .vertical = Tina::UI::UIAxisAlignment::End,
                .offset = Offset},
    .placement = Tina::UI::UILayoutPlacement::Overlay,
    .visibility = Tina::UI::UIVisibility::Visible,
    .aspectRatio = 16.0F / 9.0F,
};
static_assert(OverlayStyle.placement == Tina::UI::UILayoutPlacement::Overlay);
static_assert(OverlayStyle.flexContainer.direction == Tina::UI::UIFlexDirection::Row);
static_assert(OverlayStyle.flexContainer.alignContent ==
              Tina::UI::UIAlignContent::Center);
static_assert(*OverlayStyle.aspectRatio == 16.0F / 9.0F);

constexpr Tina::UI::UIResponsiveLayoutOverrides ResponsiveOverrides{
    .gap = Tina::UI::UILayoutGap::All(6.0F),
    .padding = Tina::UI::UIEdgeSpacing::All(8.0F),
    .minMax = Tina::UI::UILayoutMinMaxSpec{
        .minWidth = Tina::UI::UILayoutLength::MinContent(),
        .maxWidth = Tina::UI::UILayoutLength::Px(640.0F),
    },
};
static_assert(ResponsiveOverrides.gap->row == 6.0F);
static_assert(ResponsiveOverrides.padding->left == 8.0F);
static_assert(ResponsiveOverrides.minMax->minWidth.isMinContent());

constexpr Tina::UI::UIGridTrackList GridColumns =
    Tina::UI::UIGridTrackList::Of({
        Tina::UI::UIGridTrack::Px(68.0F),
        Tina::UI::UIGridTrack::Fr(),
    });
static_assert(GridColumns.count == 2U);
static_assert(GridColumns.tracks[0] == Tina::UI::UIGridTrack::Px(68.0F));
static_assert(GridColumns.tracks[1] == Tina::UI::UIGridTrack::Fr());

constexpr Tina::UI::UILayoutStyle GridStyle{
    .gridContainer = {
        .columns = GridColumns,
        .rows = Tina::UI::UIGridTrackList::Of(
            {Tina::UI::UIGridTrack::Auto()}),
        .gap = {.row = 4.0F, .column = 8.0F},
    },
    .gridItem = {.row = 0U, .column = 1U, .columnSpan = 2U},
    .containerLayout = Tina::UI::UIContainerLayout::Grid,
};
static_assert(GridStyle.containerLayout == Tina::UI::UIContainerLayout::Grid);
