#include <gtest/gtest.h>

#include "detail/UILayoutPrimitives.hpp"

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

TEST(UILayoutPrimitivesTests, NormalizesNegativeZeroAndChecksFiniteExtent)
{
    const float normalized = UI::Detail::normalizeFloat(-0.0F);

    EXPECT_FLOAT_EQ(normalized, 0.0F);
    EXPECT_FALSE(std::signbit(normalized));
    EXPECT_TRUE(UI::Detail::isFiniteNonNegative(0.0F));
    EXPECT_FALSE(UI::Detail::isFiniteNonNegative(-1.0F));
    EXPECT_FALSE(UI::Detail::isFiniteNonNegative(
        (std::numeric_limits<float>::infinity)()));
}

TEST(UILayoutPrimitivesTests, LayoutRectRequiresFiniteOriginExtentAndEdges)
{
    EXPECT_TRUE(UI::Detail::isFiniteLayoutRect(
        {.x = 1.0F, .y = 2.0F, .width = 3.0F, .height = 4.0F}));
    EXPECT_FALSE(UI::Detail::isFiniteLayoutRect(
        {.x = 1.0F, .y = 2.0F, .width = -1.0F, .height = 4.0F}));
    EXPECT_FALSE(UI::Detail::isFiniteLayoutRect(
        {.x = (std::numeric_limits<float>::infinity)(),
         .y = 2.0F,
         .width = 3.0F,
         .height = 4.0F}));
    EXPECT_FALSE(UI::Detail::isFiniteLayoutRect(
        {.x = (std::numeric_limits<float>::max)(),
         .y = 2.0F,
         .width = (std::numeric_limits<float>::max)(),
         .height = 4.0F}));
}

TEST(UILayoutPrimitivesTests, LayoutStateModelsStartStableAndComparePreparedInputs)
{
    const UI::Detail::LayoutScratchState scratch{};
    const UI::Detail::LayoutPreparedInputs initial{};
    UI::Detail::LayoutPreparedInputs changed{};
    changed.contentWidthDefinite = true;

    EXPECT_EQ(scratch.effectiveVisibility, UI::UIVisibility::Visible);
    EXPECT_EQ(scratch.paintLayer, UI::Detail::UIPaintLayer::Content);
    EXPECT_EQ(scratch.preparedInputs, initial);
    EXPECT_NE(initial, changed);
}

TEST(UILayoutPrimitivesTest, PaintLayerOrdersContentBelowModalPopupAndTooltip)
{
    using Layer = UI::Detail::UIPaintLayer;
    using Kind = UI::Detail::BuiltinElementKind;

    EXPECT_LT(Layer::Content, Layer::Modal);
    EXPECT_LT(Layer::Modal, Layer::Popup);
    EXPECT_LT(Layer::Popup, Layer::Tooltip);

    EXPECT_EQ(UI::Detail::paintLayerForKind(Kind::Panel), Layer::Content);
    EXPECT_EQ(UI::Detail::paintLayerForKind(Kind::Modal), Layer::Modal);
    EXPECT_EQ(UI::Detail::paintLayerForKind(Kind::Popup), Layer::Popup);
    EXPECT_EQ(UI::Detail::paintLayerForKind(Kind::Menu), Layer::Popup);
    EXPECT_EQ(UI::Detail::paintLayerForKind(Kind::Tooltip), Layer::Tooltip);

    // Promotion moves whole subtrees and never demotes: a child cannot outrank
    // its own parent, which is what the hit and semantics builders rely on.
    EXPECT_EQ(UI::Detail::combinePaintLayer(Layer::Popup, Layer::Content), Layer::Popup);
    EXPECT_EQ(UI::Detail::combinePaintLayer(Layer::Content, Layer::Popup), Layer::Popup);
    EXPECT_EQ(UI::Detail::combinePaintLayer(Layer::Popup, Layer::Tooltip), Layer::Tooltip);
    EXPECT_EQ(UI::Detail::combinePaintLayer(Layer::Tooltip, Layer::Popup), Layer::Tooltip);
}

TEST(UILayoutPrimitivesTests, ResolvesAndClampsAxisSpecificOuterSizes)
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(50.0F);
    style.size.height = UI::UILayoutLength::Px(24.0F);
    style.minMax.minWidth = UI::UILayoutLength::Px(60.0F);
    style.minMax.maxHeight = UI::UILayoutLength::Px(20.0F);
    UI::Detail::LayoutScratchState scratch{};
    scratch.parentContentWidthDefinite = true;
    scratch.parentContentHeightDefinite = true;
    scratch.parentContentWidth = 100.0F;
    scratch.parentContentHeight = 80.0F;
    UI::Detail::LayoutPassStatistics statistics{};

    EXPECT_FLOAT_EQ(UI::Detail::resolvedWidth(style, scratch, statistics), 50.0F);
    EXPECT_FLOAT_EQ(UI::Detail::resolvedHeight(style, scratch, statistics), 24.0F);
    EXPECT_FLOAT_EQ(UI::Detail::clampWidth(50.0F, style, scratch, statistics), 60.0F);
    EXPECT_FLOAT_EQ(UI::Detail::clampHeight(24.0F, style, scratch, statistics), 20.0F);
    EXPECT_FLOAT_EQ(UI::Detail::resolveInset(
                        UI::UILayoutLength::Percent(25.0F), 80.0F, statistics),
                    20.0F);
    EXPECT_FALSE(UI::Detail::isCrossAxisAuto(style, UI::UIFlexDirection::Column));
    EXPECT_FALSE(UI::Detail::isCrossAxisAuto(style, UI::UIFlexDirection::Row));
    const UI::UILayoutStyle autoStyle{};
    EXPECT_TRUE(UI::Detail::isCrossAxisAuto(autoStyle, UI::UIFlexDirection::Column));
    EXPECT_TRUE(UI::Detail::isCrossAxisAuto(autoStyle, UI::UIFlexDirection::Row));
    EXPECT_EQ(statistics.percentMeasureFallbackCount, 0U);
}

TEST(UILayoutPrimitivesTests, ResolvesPixelsAndPercentWithExplicitFallbackCount)
{
    UI::Detail::LayoutPassStatistics statistics{};

    const auto pixels = UI::Detail::resolveLength(
        UI::UILayoutLength::Px(12.0F), false, 0.0F, statistics);
    const auto percent = UI::Detail::resolveLength(
        UI::UILayoutLength::Percent(25.0F), true, 200.0F, statistics);
    const auto fallback = UI::Detail::resolveLength(
        UI::UILayoutLength::Percent(25.0F), false, 200.0F, statistics);

    EXPECT_TRUE(pixels.hasValue);
    EXPECT_FLOAT_EQ(pixels.value, 12.0F);
    EXPECT_TRUE(percent.hasValue);
    EXPECT_FLOAT_EQ(percent.value, 50.0F);
    EXPECT_FALSE(fallback.hasValue);
    EXPECT_EQ(statistics.percentMeasureFallbackCount, 1U);
}

TEST(UILayoutPrimitivesTests, NoFallbackResolverRejectsUnresolvedLengths)
{
    const auto unresolved = UI::Detail::resolveLengthNoFallbackCount(
        UI::UILayoutLength::Percent(50.0F), false, 100.0F);
    const auto automatic = UI::Detail::resolveLengthNoFallbackCount(
        UI::UILayoutLength::Auto(), true, 100.0F);

    EXPECT_FALSE(unresolved.hasValue);
    EXPECT_FALSE(automatic.hasValue);
}

TEST(UILayoutPrimitivesTests, MinimumWinsConflictingRangeAndOutputIsNonNegative)
{
    UI::Detail::LayoutPassStatistics statistics{};

    EXPECT_FLOAT_EQ(UI::Detail::clampWithMinMax(
                        15.0F, UI::UILayoutLength::Px(20.0F),
                        UI::UILayoutLength::Px(10.0F), false, 0.0F, statistics),
                    20.0F);
    EXPECT_FLOAT_EQ(UI::Detail::clampWithMinMax(
                        -10.0F, UI::UILayoutLength::Auto(),
                        UI::UILayoutLength::Auto(), false, 0.0F, statistics),
                    0.0F);
}


TEST(UILayoutPrimitivesTests, FlexBasisOverridesMeasuredMainSizeAndStillAppliesConstraints)
{
    UI::UILayoutStyle style{};
    style.flexItem.basis = UI::UILayoutLength::Px(80.0F);
    style.minMax.maxWidth = UI::UILayoutLength::Px(70.0F);
    UI::Detail::LayoutScratchState scratch{};
    scratch.measuredSize = {.width = 25.0F, .height = 30.0F};
    UI::Detail::LayoutPassStatistics statistics{};

    EXPECT_FLOAT_EQ(
        UI::Detail::flexBaseMainSize(style, scratch, true, 200.0F, statistics),
        70.0F);
    EXPECT_FLOAT_EQ(
        UI::Detail::flexBaseMainSize(style, scratch, false, 200.0F, statistics),
        80.0F);
}

TEST(UILayoutPrimitivesTests, OverlayPlacementResolvesAlignmentOffsetsAndStretch)
{
    UI::UILayoutStyle style{};
    style.margin = {
        .left = 5.0F,
        .top = 7.0F,
        .right = 15.0F,
        .bottom = 13.0F,
    };
    style.overlay.horizontal = UI::UIAxisAlignment::Center;
    style.overlay.vertical = UI::UIAxisAlignment::End;
    style.overlay.offset.x = UI::UILayoutLength::Percent(10.0F);
    style.overlay.offset.y = UI::UILayoutLength::Px(3.0F);
    UI::Detail::LayoutScratchState scratch{};
    scratch.measuredSize = {.width = 40.0F, .height = 20.0F};
    UI::Detail::LayoutPassStatistics statistics{};
    constexpr UI::UILogicalRect Parent{
        .x = 10.0F,
        .y = 20.0F,
        .width = 200.0F,
        .height = 100.0F,
    };

    EXPECT_EQ(
        UI::Detail::resolveOverlayRect(style, scratch, Parent, statistics),
        (UI::UILogicalRect{
            .x = 105.0F,
            .y = 90.0F,
            .width = 40.0F,
            .height = 20.0F,
        }));

    style.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    style.overlay.vertical = UI::UIAxisAlignment::Stretch;
    style.overlay.offset = {};
    EXPECT_EQ(
        UI::Detail::resolveOverlayRect(style, scratch, Parent, statistics),
        (UI::UILogicalRect{
            .x = 15.0F,
            .y = 27.0F,
            .width = 180.0F,
            .height = 80.0F,
        }));
}

TEST(UILayoutPrimitivesTests, PopupPlacementMatchesAnchorFlipsAndClampsToViewport)
{
    UI::UILayoutStyle layout{};
    UI::Detail::LayoutScratchState scratch{};
    scratch.measuredSize = {.width = 100.0F, .height = 60.0F};
    UI::UIPopupStyle popup{};
    popup.placement = UI::UIPopupPlacement::Auto;
    popup.anchorGap = 4.0F;
    popup.matchAnchorWidth = true;
    UI::Detail::LayoutPassStatistics statistics{};
    constexpr UI::UILogicalRect Viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = 300.0F,
        .height = 200.0F,
    };

    const auto above = UI::Detail::resolvePopupPlacement(
        layout, scratch, popup,
        {.x = 250.0F, .y = 160.0F, .width = 80.0F, .height = 30.0F},
        Viewport, statistics);
    EXPECT_EQ(above.placement, UI::UIPopupPlacement::Above);
    EXPECT_EQ(
        above.rect,
        (UI::UILogicalRect{
            .x = 220.0F,
            .y = 96.0F,
            .width = 80.0F,
            .height = 60.0F,
        }));

    popup.placement = UI::UIPopupPlacement::Above;
    const auto below = UI::Detail::resolvePopupPlacement(
        layout, scratch, popup,
        {.x = 20.0F, .y = 5.0F, .width = 80.0F, .height = 30.0F},
        Viewport, statistics);
    EXPECT_EQ(below.placement, UI::UIPopupPlacement::Below);
    EXPECT_FLOAT_EQ(below.rect.y, 39.0F);
}

TEST(UILayoutPrimitivesTests, ContentPlacementAppliesPaddingReservedChromeAndAlignment)
{
    constexpr UI::UILogicalRect WorldRect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 200.0F,
        .height = 100.0F,
    };
    constexpr UI::UIEdgeSpacing Padding{
        .left = 10.0F,
        .top = 5.0F,
        .right = 20.0F,
        .bottom = 15.0F,
    };
    constexpr UI::UILogicalSize Intrinsic{.width = 60.0F, .height = 20.0F};
    constexpr UI::UIContentAlignment Alignment{
        .horizontal = UI::UIAxisAlignment::End,
        .vertical = UI::UIAxisAlignment::Center,
    };

    const auto placement = UI::Detail::resolveContentPlacement(
        WorldRect, Padding, 40.0F, 30.0F, Alignment, &Intrinsic);
    EXPECT_EQ(
        placement.contentBox,
        (UI::UILogicalRect{
            .x = 60.0F,
            .y = 25.0F,
            .width = 100.0F,
            .height = 80.0F,
        }));
    EXPECT_EQ(
        placement.origin,
        (UI::UILogicalPoint{.x = 100.0F, .y = 55.0F}));
    EXPECT_EQ(placement.intrinsicSize, Intrinsic);
    EXPECT_TRUE(placement.hasIntrinsicContent);

    const auto empty = UI::Detail::resolveContentPlacement(
        WorldRect, Padding, 0.0F, 0.0F, {}, nullptr);
    EXPECT_EQ(empty.origin, empty.contentBox.origin());
    EXPECT_FALSE(empty.hasIntrinsicContent);
}

TEST(UILayoutPrimitivesTests, CombinesSpacingVisibilityAndIntersection)
{
    const UI::UIEdgeSpacing spacing{
        .left = 2.0F,
        .top = 3.0F,
        .right = 5.0F,
        .bottom = 7.0F,
    };
    const auto intersection = UI::Detail::intersectRects(
        UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 20.0F, .height = 10.0F},
        UI::UILogicalRect{.x = 5.0F, .y = 4.0F, .width = 20.0F, .height = 10.0F});

    EXPECT_FLOAT_EQ(UI::Detail::horizontalMargin(spacing), 7.0F);
    EXPECT_FLOAT_EQ(UI::Detail::verticalMargin(spacing), 10.0F);
    EXPECT_EQ(intersection,
              (UI::UILogicalRect{.x = 5.0F, .y = 4.0F, .width = 15.0F, .height = 6.0F}));
    EXPECT_EQ(UI::Detail::combineVisibility(UI::UIVisibility::Visible,
                                            UI::UIVisibility::Hidden),
              UI::UIVisibility::Hidden);
    EXPECT_EQ(UI::Detail::combineVisibility(UI::UIVisibility::Hidden,
                                            UI::UIVisibility::Collapsed),
              UI::UIVisibility::Collapsed);
}

TEST(UILayoutPrimitivesTests, HalfOpenPointContainmentExcludesRightAndBottom)
{
    constexpr UI::UILogicalRect Rect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 30.0F,
        .height = 40.0F,
    };

    EXPECT_TRUE(UI::Detail::containsPointHalfOpen(Rect, {.x = 10.0F, .y = 20.0F}));
    EXPECT_TRUE(UI::Detail::containsPointHalfOpen(Rect, {.x = 39.0F, .y = 59.0F}));
    EXPECT_FALSE(UI::Detail::containsPointHalfOpen(Rect, {.x = 40.0F, .y = 20.0F}));
    EXPECT_FALSE(UI::Detail::containsPointHalfOpen(Rect, {.x = 10.0F, .y = 60.0F}));
}

TEST(UILayoutPrimitivesTests, WorkMaskPublishesOnlyRequestedCompletionBits)
{
    constexpr u8 Work = UI::Detail::LayoutWorkMeasure |
                        UI::Detail::LayoutWorkArrange;
    constexpr u8 Completion = UI::Detail::LayoutWorkMeasureComplete |
                              UI::Detail::LayoutWorkArrangeComplete;

    EXPECT_TRUE(UI::Detail::hasLayoutWork(Work, UI::Detail::LayoutWorkMeasure));
    EXPECT_TRUE(UI::Detail::hasLayoutWork(Work, UI::Detail::LayoutWorkArrange));
    EXPECT_EQ(UI::Detail::layoutSubtreeCompletionMask(Work), Completion);
    EXPECT_EQ(UI::Detail::layoutSubtreeCompletionMask(0), 0U);
}

} // namespace
} // namespace Tina::Tests
