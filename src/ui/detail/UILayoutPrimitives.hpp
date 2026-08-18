#pragma once

#include <tina/ui/UIContent.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPopup.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {

struct LayoutPassStatistics final {
    usize passCount = 0;
    usize measuredNodeCount = 0;
    usize arrangedNodeCount = 0;
    usize percentMeasureFallbackCount = 0;
};

struct LayoutPreparedInputs final {
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;

    bool operator==(const LayoutPreparedInputs&) const = default;
};

struct LayoutScratchState final {
    UILogicalSize measuredSize{};
    UILogicalRect localRect{};
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    // Clip inherited from the nearest clipping container. Ordinary containers
    // pass it through; ScrollView replaces it with its content viewport.
    UILogicalRect descendantClip{};
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    bool inPopupSubtree = false;
    bool inTooltipSubtree = false;
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;
    u32 layoutOrdinal = 0;
    u32 paintOrdinal = 0;
    // Prepare inputs remain stable after Arrange. The corresponding working
    // fields above are intentionally updated to final geometry during Arrange.
    LayoutPreparedInputs preparedInputs{};
};

inline constexpr u8 LayoutWorkMeasure = 1U << 0U;
inline constexpr u8 LayoutWorkArrange = 1U << 1U;
inline constexpr u8 LayoutWorkMeasureComplete = 1U << 2U;
inline constexpr u8 LayoutWorkArrangeComplete = 1U << 3U;

[[nodiscard]] constexpr bool hasLayoutWork(u8 work, u8 flag) noexcept
{
    return (work & flag) != 0;
}

[[nodiscard]] constexpr u8 layoutSubtreeCompletionMask(u8 work) noexcept
{
    u8 mask = 0;
    if ((work & LayoutWorkMeasure) != 0)
    {
        mask |= LayoutWorkMeasureComplete;
    }
    if ((work & LayoutWorkArrange) != 0)
    {
        mask |= LayoutWorkArrangeComplete;
    }
    return mask;
}

struct ResolvedLength final {
    bool hasValue = false;
    float value = 0.0F;
};

[[nodiscard]] constexpr float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] inline bool isFiniteNonNegative(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] inline bool isFiniteLayoutRect(UILogicalRect rect) noexcept
{
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           isFiniteNonNegative(rect.width) && isFiniteNonNegative(rect.height) &&
           std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

[[nodiscard]] inline ResolvedLength resolveLength(
    UILayoutLength length, bool basisDefinite, float basis,
    LayoutPassStatistics& statistics) noexcept
{
    if (length.unit == UILayoutLengthUnit::Px)
    {
        return ResolvedLength{.hasValue = true, .value = length.value};
    }
    if (length.unit == UILayoutLengthUnit::Percent)
    {
        if (basisDefinite && isFiniteNonNegative(basis))
        {
            return ResolvedLength{
                .hasValue = true,
                .value = normalizeFloat(basis * (length.value * 0.01F)),
            };
        }
        ++statistics.percentMeasureFallbackCount;
    }
    return {};
}

[[nodiscard]] inline ResolvedLength resolveLengthNoFallbackCount(
    UILayoutLength length, bool basisDefinite, float basis) noexcept
{
    if (length.unit == UILayoutLengthUnit::Px)
    {
        return ResolvedLength{.hasValue = true, .value = length.value};
    }
    if (length.unit == UILayoutLengthUnit::Percent && basisDefinite &&
        isFiniteNonNegative(basis))
    {
        return ResolvedLength{
            .hasValue = true,
            .value = normalizeFloat(basis * (length.value * 0.01F)),
        };
    }
    return {};
}

[[nodiscard]] inline float clampWithMinMax(
    float value, UILayoutLength minLength, UILayoutLength maxLength,
    bool basisDefinite, float basis,
    LayoutPassStatistics& statistics) noexcept
{
    const ResolvedLength minValue =
        resolveLength(minLength, basisDefinite, basis, statistics);
    ResolvedLength maxValue =
        resolveLength(maxLength, basisDefinite, basis, statistics);
    if (minValue.hasValue && maxValue.hasValue && maxValue.value < minValue.value)
    {
        maxValue.value = minValue.value;
    }
    if (maxValue.hasValue)
    {
        value = (std::min)(value, maxValue.value);
    }
    if (minValue.hasValue)
    {
        value = (std::max)(value, minValue.value);
    }
    return normalizeFloat((std::max)(0.0F, value));
}

[[nodiscard]] inline float resolvedWidth(
    const UILayoutStyle& style, const LayoutScratchState& scratch,
    LayoutPassStatistics& statistics) noexcept
{
    const ResolvedLength value = resolveLength(
        style.size.width, scratch.parentContentWidthDefinite,
        scratch.parentContentWidth, statistics);
    return value.hasValue ? value.value : -1.0F;
}

[[nodiscard]] inline float resolvedHeight(
    const UILayoutStyle& style, const LayoutScratchState& scratch,
    LayoutPassStatistics& statistics) noexcept
{
    const ResolvedLength value = resolveLength(
        style.size.height, scratch.parentContentHeightDefinite,
        scratch.parentContentHeight, statistics);
    return value.hasValue ? value.value : -1.0F;
}

[[nodiscard]] inline float clampWidth(
    float value, const UILayoutStyle& style, const LayoutScratchState& scratch,
    LayoutPassStatistics& statistics) noexcept
{
    return clampWithMinMax(
        value, style.minMax.minWidth, style.minMax.maxWidth,
        scratch.parentContentWidthDefinite, scratch.parentContentWidth,
        statistics);
}

[[nodiscard]] inline float clampHeight(
    float value, const UILayoutStyle& style, const LayoutScratchState& scratch,
    LayoutPassStatistics& statistics) noexcept
{
    return clampWithMinMax(
        value, style.minMax.minHeight, style.minMax.maxHeight,
        scratch.parentContentHeightDefinite, scratch.parentContentHeight,
        statistics);
}


[[nodiscard]] inline float flexBaseMainSize(
    const UILayoutStyle& style, const LayoutScratchState& scratch,
    bool row, float contentMain,
    LayoutPassStatistics& statistics) noexcept
{
    float base = row ? scratch.measuredSize.width : scratch.measuredSize.height;
    const ResolvedLength basis =
        resolveLength(style.flexItem.basis, true, contentMain, statistics);
    if (basis.hasValue)
    {
        base = basis.value;
    }
    return row ? clampWidth(base, style, scratch, statistics)
               : clampHeight(base, style, scratch, statistics);
}

[[nodiscard]] constexpr bool isCrossAxisAuto(
    const UILayoutStyle& style, UIFlexDirection direction) noexcept
{
    return direction == UIFlexDirection::Row ? style.size.height.isAuto()
                                             : style.size.width.isAuto();
}

[[nodiscard]] constexpr UIAxisAlignment resolvedItemAlignment(
    UIAxisAlignment parentAlignment, UIAlignSelf alignSelf) noexcept
{
    switch (alignSelf)
    {
    case UIAlignSelf::Start:
        return UIAxisAlignment::Start;
    case UIAlignSelf::Center:
        return UIAxisAlignment::Center;
    case UIAlignSelf::End:
        return UIAxisAlignment::End;
    case UIAlignSelf::Stretch:
        return UIAxisAlignment::Stretch;
    case UIAlignSelf::Auto:
        return parentAlignment;
    }
    return parentAlignment;
}

[[nodiscard]] inline float resolveInset(
    UILayoutLength length, float basis,
    LayoutPassStatistics& statistics) noexcept
{
    const ResolvedLength resolved = resolveLength(length, true, basis, statistics);
    return resolved.hasValue ? resolved.value : -1.0F;
}

[[nodiscard]] constexpr float horizontalMargin(
    const UIEdgeSpacing& margin) noexcept
{
    return margin.left + margin.right;
}

[[nodiscard]] constexpr float verticalMargin(
    const UIEdgeSpacing& margin) noexcept
{
    return margin.top + margin.bottom;
}


[[nodiscard]] inline UILogicalRect resolveOverlayRect(
    const UILayoutStyle& style, const LayoutScratchState& scratch,
    UILogicalRect parentContentRect,
    LayoutPassStatistics& statistics) noexcept
{
    float width = scratch.measuredSize.width;
    float height = scratch.measuredSize.height;

    if (style.overlay.horizontal == UIAxisAlignment::Stretch)
    {
        width = (std::max)(0.0F, parentContentRect.width -
                                    horizontalMargin(style.margin));
    }
    if (style.overlay.vertical == UIAxisAlignment::Stretch)
    {
        height = (std::max)(0.0F, parentContentRect.height -
                                     verticalMargin(style.margin));
    }
    width = clampWidth(width, style, scratch, statistics);
    height = clampHeight(height, style, scratch, statistics);

    const float availableWidth =
        (std::max)(0.0F, parentContentRect.width - horizontalMargin(style.margin));
    const float availableHeight =
        (std::max)(0.0F, parentContentRect.height - verticalMargin(style.margin));
    const ResolvedLength horizontalOffset =
        resolveLength(style.overlay.offset.x, true, parentContentRect.width,
                      statistics);
    const ResolvedLength verticalOffset =
        resolveLength(style.overlay.offset.y, true, parentContentRect.height,
                      statistics);
    float x = parentContentRect.x + style.margin.left;
    float y = parentContentRect.y + style.margin.top;
    switch (style.overlay.horizontal)
    {
    case UIAxisAlignment::Center:
        x += (availableWidth - width) * 0.5F;
        break;
    case UIAxisAlignment::End:
        x += availableWidth - width;
        break;
    case UIAxisAlignment::Start:
    case UIAxisAlignment::Stretch:
        break;
    }
    switch (style.overlay.vertical)
    {
    case UIAxisAlignment::Center:
        y += (availableHeight - height) * 0.5F;
        break;
    case UIAxisAlignment::End:
        y += availableHeight - height;
        break;
    case UIAxisAlignment::Start:
    case UIAxisAlignment::Stretch:
        break;
    }
    x += horizontalOffset.hasValue ? horizontalOffset.value : 0.0F;
    y += verticalOffset.hasValue ? verticalOffset.value : 0.0F;

    return UILogicalRect{
        .x = normalizeFloat(x),
        .y = normalizeFloat(y),
        .width = normalizeFloat((std::max)(0.0F, width)),
        .height = normalizeFloat((std::max)(0.0F, height)),
    };
}

struct ResolvedPopupPlacement final {
    UILogicalRect rect{};
    UIPopupPlacement placement = UIPopupPlacement::Below;
};

[[nodiscard]] inline ResolvedPopupPlacement resolvePopupPlacement(
    const UILayoutStyle& layoutStyle, const LayoutScratchState& scratch,
    const UIPopupStyle& popupStyle, UILogicalRect anchorRect,
    UILogicalRect viewportRect,
    LayoutPassStatistics& statistics) noexcept
{
    float width = scratch.measuredSize.width;
    float height = scratch.measuredSize.height;
    if (popupStyle.matchAnchorWidth)
    {
        width = anchorRect.width;
    }
    width = (std::min)(clampWidth(width, layoutStyle, scratch, statistics),
                       viewportRect.width);
    height = (std::min)(clampHeight(height, layoutStyle, scratch, statistics),
                        viewportRect.height);

    const float belowY = anchorRect.bottom() + popupStyle.anchorGap;
    const float aboveY = anchorRect.y - popupStyle.anchorGap - height;
    const bool fitsBelow = belowY + height <= viewportRect.bottom();
    const bool fitsAbove = aboveY >= viewportRect.y;
    const float availableBelow =
        (std::max)(0.0F, viewportRect.bottom() - belowY);
    const float availableAbove =
        (std::max)(0.0F, anchorRect.y - popupStyle.anchorGap - viewportRect.y);

    UIPopupPlacement resolved = popupStyle.placement;
    if (resolved == UIPopupPlacement::Auto)
    {
        resolved = fitsBelow || (!fitsAbove && availableBelow >= availableAbove)
                       ? UIPopupPlacement::Below
                       : UIPopupPlacement::Above;
    } else if (resolved == UIPopupPlacement::Below && !fitsBelow && fitsAbove)
    {
        resolved = UIPopupPlacement::Above;
    } else if (resolved == UIPopupPlacement::Above && !fitsAbove && fitsBelow)
    {
        resolved = UIPopupPlacement::Below;
    }

    const float maximumX =
        (std::max)(viewportRect.x, viewportRect.right() - width);
    const float maximumY =
        (std::max)(viewportRect.y, viewportRect.bottom() - height);
    const float x = (std::clamp)(anchorRect.x, viewportRect.x, maximumX);
    const float requestedY =
        resolved == UIPopupPlacement::Above ? aboveY : belowY;
    const float y = (std::clamp)(requestedY, viewportRect.y, maximumY);

    return ResolvedPopupPlacement{
        .rect = UILogicalRect{
            .x = normalizeFloat(x),
            .y = normalizeFloat(y),
            .width = normalizeFloat((std::max)(0.0F, width)),
            .height = normalizeFloat((std::max)(0.0F, height)),
        },
        .placement = resolved,
    };
}

[[nodiscard]] inline UICommittedContentPlacement resolveContentPlacement(
    UILogicalRect worldRect, const UIEdgeSpacing& padding,
    float leadingReservedWidth, float trailingReservedWidth,
    UIContentAlignment alignment,
    const UILogicalSize* intrinsicSize) noexcept
{
    UILogicalRect contentBox{
        .x = normalizeFloat(worldRect.x + padding.left),
        .y = normalizeFloat(worldRect.y + padding.top),
        .width = normalizeFloat((std::max)(
            0.0F, worldRect.width - horizontalMargin(padding))),
        .height = normalizeFloat((std::max)(
            0.0F, worldRect.height - verticalMargin(padding))),
    };

    contentBox.width = normalizeFloat((std::max)(
        0.0F, contentBox.width - trailingReservedWidth));
    contentBox.x = normalizeFloat(contentBox.x + leadingReservedWidth);
    contentBox.width = normalizeFloat((std::max)(
        0.0F, contentBox.width - leadingReservedWidth));

    UICommittedContentPlacement placement{
        .contentBox = contentBox,
        .origin = contentBox.origin(),
    };
    if (intrinsicSize == nullptr)
    {
        return placement;
    }

    const float horizontalFree =
        (std::max)(0.0F, contentBox.width - intrinsicSize->width);
    const float verticalFree =
        (std::max)(0.0F, contentBox.height - intrinsicSize->height);
    const auto alignedOffset = [](UIAxisAlignment axis,
                                  float freeSpace) noexcept {
        switch (axis)
        {
        case UIAxisAlignment::Center:
            return freeSpace * 0.5F;
        case UIAxisAlignment::End:
            return freeSpace;
        case UIAxisAlignment::Start:
        case UIAxisAlignment::Stretch:
            return 0.0F;
        }
        return 0.0F;
    };
    placement.origin = UILogicalPoint{
        .x = normalizeFloat(contentBox.x +
                            alignedOffset(alignment.horizontal, horizontalFree)),
        .y = normalizeFloat(contentBox.y +
                            alignedOffset(alignment.vertical, verticalFree)),
    };
    placement.intrinsicSize = *intrinsicSize;
    placement.hasIntrinsicContent = true;
    return placement;
}

[[nodiscard]] constexpr UILogicalRect intersectRects(
    UILogicalRect first, UILogicalRect second) noexcept
{
    const float left = (std::max)(first.x, second.x);
    const float top = (std::max)(first.y, second.y);
    const float right = (std::min)(first.right(), second.right());
    const float bottom = (std::min)(first.bottom(), second.bottom());
    return UILogicalRect{
        .x = normalizeFloat(left),
        .y = normalizeFloat(top),
        .width = normalizeFloat((std::max)(0.0F, right - left)),
        .height = normalizeFloat((std::max)(0.0F, bottom - top)),
    };
}

[[nodiscard]] constexpr UIVisibility combineVisibility(
    UIVisibility parent, UIVisibility local) noexcept
{
    if (parent == UIVisibility::Collapsed || local == UIVisibility::Collapsed)
    {
        return UIVisibility::Collapsed;
    }
    if (parent == UIVisibility::Hidden || local == UIVisibility::Hidden)
    {
        return UIVisibility::Hidden;
    }
    return UIVisibility::Visible;
}

[[nodiscard]] constexpr bool containsPointHalfOpen(
    UILogicalRect rect, UILogicalPoint point) noexcept
{
    return rect.width > 0.0F && rect.height > 0.0F && point.x >= rect.x &&
           point.y >= rect.y && point.x < rect.right() && point.y < rect.bottom();
}

} // namespace Tina::UI::Detail
