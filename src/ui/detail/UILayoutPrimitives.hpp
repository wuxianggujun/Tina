#pragma once

#include <tina/ui/UILayout.hpp>

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

[[nodiscard]] constexpr bool isCrossAxisAuto(
    const UILayoutStyle& style, UIFlexDirection direction) noexcept
{
    return direction == UIFlexDirection::Row ? style.size.height.isAuto()
                                             : style.size.width.isAuto();
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
