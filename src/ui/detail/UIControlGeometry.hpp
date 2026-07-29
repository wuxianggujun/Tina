#pragma once

#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UITreeView.hpp>

namespace Tina::UI::Detail {

struct ScrollBarGeometry final {
    UILogicalRect track{};
    UILogicalRect thumb{};
    bool visible = false;
};

struct ScrollBarPointerHit final {
    UINodeId scrollView{};
    UIScrollAxes axis = UIScrollAxes::None;
    ScrollBarGeometry geometry{};
    bool thumb = false;

    [[nodiscard]] bool hasValue() const noexcept;
};

struct SliderTrackGeometry final {
    float verticalInset = 0.0F;
    float thumbWidth = 0.0F;
    float startCenterX = 0.0F;
    float endCenterX = 0.0F;
};

struct SliderPaintGeometry final {
    UILogicalRect filledTrack{};
    UILogicalRect thumb{};
    float fraction = 0.0F;
};

[[nodiscard]] ScrollBarGeometry makeScrollBarGeometry(
    const UIScrollViewMetrics& metrics, UILogicalRect viewportRect,
    const UIScrollViewPaint& paint, UIScrollAxes axis) noexcept;
[[nodiscard]] ScrollBarGeometry makeListViewScrollBarGeometry(
    const UIListViewMetrics& metrics, UILogicalRect viewportRect,
    const UIScrollViewPaint& paint) noexcept;
[[nodiscard]] ScrollBarGeometry makeTreeViewScrollBarGeometry(
    const UITreeViewMetrics& metrics, UILogicalRect viewportRect,
    const UIScrollViewPaint& paint) noexcept;
[[nodiscard]] UILogicalRect makeTreeViewDisclosureRect(
    UILogicalRect rowRect, const UITreeViewStyle& style, u32 level) noexcept;

[[nodiscard]] float normalizedRangeFraction(float value, float minValue,
                                            float maxValue) noexcept;
[[nodiscard]] SliderTrackGeometry sliderTrackGeometry(
    UILogicalRect worldRect, const UISliderPaint& paint) noexcept;
[[nodiscard]] SliderPaintGeometry sliderPaintGeometry(
    UILogicalRect worldRect, float minValue, float maxValue, float value,
    const UISliderPaint& paint) noexcept;

} // namespace Tina::UI::Detail
