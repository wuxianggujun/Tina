#include "UIControlGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

} // namespace

bool ScrollBarPointerHit::hasValue() const noexcept
{
    return scrollView.hasValue() && axis != UIScrollAxes::None;
}

ScrollBarGeometry makeScrollBarGeometry(const UIScrollViewMetrics& metrics,
                                        UILogicalRect viewportRect,
                                        const UIScrollViewPaint& paint,
                                        UIScrollAxes axis) noexcept
{
    const bool horizontal = axis == UIScrollAxes::Horizontal;
    const bool visible = horizontal ? metrics.horizontalScrollBarVisible
                                    : metrics.verticalScrollBarVisible;
    if (!visible)
    {
        return {};
    }

    const float viewportExtent =
        horizontal ? metrics.viewportSize.width : metrics.viewportSize.height;
    const float contentExtent =
        horizontal ? metrics.contentSize.width : metrics.contentSize.height;
    const float offset = horizontal ? metrics.offset.x : metrics.offset.y;
    const UILogicalRect track = horizontal
                                    ? UILogicalRect{
                                          .x = viewportRect.x,
                                          .y = normalizeFloat(viewportRect.bottom()),
                                          .width = viewportRect.width,
                                          .height = paint.thickness,
                                      }
                                    : UILogicalRect{
                                          .x = normalizeFloat(viewportRect.right()),
                                          .y = viewportRect.y,
                                          .width = paint.thickness,
                                          .height = viewportRect.height,
                                      };
    const float trackExtent = horizontal ? track.width : track.height;
    if (!(trackExtent > 0.0F))
    {
        return ScrollBarGeometry{.track = track, .visible = true};
    }
    const float proportionalExtent = contentExtent > 0.0F
                                         ? trackExtent * viewportExtent /
                                               contentExtent
                                         : trackExtent;
    const float thumbExtent = normalizeFloat((std::clamp)(
        proportionalExtent, (std::min)(paint.minThumbExtent, trackExtent),
        trackExtent));
    const float maxOffset =
        (std::max)(0.0F, contentExtent - viewportExtent);
    const float travel = (std::max)(0.0F, trackExtent - thumbExtent);
    const float thumbStart = maxOffset > 0.0F
                                 ? travel * ((std::clamp)(offset, 0.0F, maxOffset) /
                                             maxOffset)
                                 : 0.0F;
    const UILogicalRect thumb = horizontal
                                    ? UILogicalRect{
                                          .x = normalizeFloat(track.x + thumbStart),
                                          .y = track.y,
                                          .width = thumbExtent,
                                          .height = track.height,
                                      }
                                    : UILogicalRect{
                                          .x = track.x,
                                          .y = normalizeFloat(track.y + thumbStart),
                                          .width = track.width,
                                          .height = thumbExtent,
                                      };
    return ScrollBarGeometry{.track = track, .thumb = thumb, .visible = true};
}

ScrollBarGeometry makeListViewScrollBarGeometry(
    const UIListViewMetrics& metrics, UILogicalRect viewportRect,
    const UIScrollViewPaint& paint) noexcept
{
    return makeScrollBarGeometry(
        UIScrollViewMetrics{
            .offset = {.x = 0.0F, .y = metrics.scrollOffset},
            .viewportSize = metrics.viewportSize,
            .contentSize = metrics.contentSize,
            .horizontalScrollBarVisible = false,
            .verticalScrollBarVisible = metrics.verticalScrollBarVisible,
        },
        viewportRect, paint, UIScrollAxes::Vertical);
}

ScrollBarGeometry makeTreeViewScrollBarGeometry(
    const UITreeViewMetrics& metrics, UILogicalRect viewportRect,
    const UIScrollViewPaint& paint) noexcept
{
    return makeScrollBarGeometry(
        UIScrollViewMetrics{
            .offset = {.x = 0.0F, .y = metrics.scrollOffset},
            .viewportSize = metrics.viewportSize,
            .contentSize = metrics.contentSize,
            .horizontalScrollBarVisible = false,
            .verticalScrollBarVisible = metrics.verticalScrollBarVisible,
        },
        viewportRect, paint, UIScrollAxes::Vertical);
}

UILogicalRect makeTreeViewDisclosureRect(UILogicalRect rowRect,
                                         const UITreeViewStyle& style,
                                         u32 level) noexcept
{
    const double logicalX = static_cast<double>(rowRect.x) + 8.0 +
                            static_cast<double>(level) *
                                static_cast<double>(style.indentation);
    const float extent =
        normalizeFloat((std::min)(style.disclosureExtent, rowRect.height));
    const float x = logicalX >=
                            static_cast<double>((std::numeric_limits<float>::max)())
                        ? (std::numeric_limits<float>::max)()
                        : normalizeFloat(static_cast<float>(logicalX));
    return UILogicalRect{
        .x = x,
        .y = normalizeFloat(rowRect.y + (rowRect.height - extent) * 0.5F),
        .width = extent,
        .height = extent,
    };
}

float normalizedRangeFraction(float value, float minValue,
                              float maxValue) noexcept
{
    if (!(std::isfinite(value) && std::isfinite(minValue) &&
          std::isfinite(maxValue) && maxValue > minValue))
    {
        return 0.0F;
    }
    const double numerator =
        static_cast<double>(value) - static_cast<double>(minValue);
    const double denominator =
        static_cast<double>(maxValue) - static_cast<double>(minValue);
    return static_cast<float>(std::clamp(numerator / denominator, 0.0, 1.0));
}

SliderTrackGeometry sliderTrackGeometry(UILogicalRect worldRect,
                                        const UISliderPaint& paint) noexcept
{
    const float width = (std::max)(0.0F, worldRect.width);
    const float height = (std::max)(0.0F, worldRect.height);
    const float horizontalInset = (std::min)(paint.contentInset, width * 0.5F);
    const float trackThickness = (std::min)(paint.trackThickness, height);
    const float thumbExtent = (std::min)({paint.thumbExtent, width, height});
    const float minimumCenterX = worldRect.x + thumbExtent * 0.5F;
    const float maximumCenterX = worldRect.x + width - thumbExtent * 0.5F;
    const float rawStartCenterX = worldRect.x + horizontalInset;
    const float rawEndCenterX = worldRect.x + width - horizontalInset;
    return SliderTrackGeometry{
        .trackThickness = trackThickness,
        .thumbExtent = thumbExtent,
        .startCenterX =
            std::clamp(rawStartCenterX, minimumCenterX, maximumCenterX),
        .endCenterX =
            std::clamp(rawEndCenterX, minimumCenterX, maximumCenterX),
    };
}

SliderPaintGeometry sliderPaintGeometry(UILogicalRect worldRect,
                                        float minValue, float maxValue,
                                        float value,
                                        const UISliderPaint& paint) noexcept
{
    const float fraction = normalizedRangeFraction(value, minValue, maxValue);
    const SliderTrackGeometry track = sliderTrackGeometry(worldRect, paint);
    const float centerSpan =
        (std::max)(0.0F, track.endCenterX - track.startCenterX);
    const float thumbCenterX = track.startCenterX + centerSpan * fraction;
    const float thumbX = thumbCenterX - track.thumbExtent * 0.5F;
    const float trackY = worldRect.y + (worldRect.height - track.trackThickness) * 0.5F;
    return SliderPaintGeometry{
        .track =
            UILogicalRect{
                .x = normalizeFloat(track.startCenterX),
                .y = normalizeFloat(trackY),
                .width = normalizeFloat(centerSpan),
                .height = normalizeFloat(track.trackThickness),
            },
        .filledTrack =
            UILogicalRect{
                .x = normalizeFloat(track.startCenterX),
                .y = normalizeFloat(trackY),
                .width = normalizeFloat(centerSpan * fraction),
                .height = normalizeFloat(track.trackThickness),
            },
        .thumb =
            UILogicalRect{
                .x = normalizeFloat(thumbX),
                .y = normalizeFloat(worldRect.y + (worldRect.height - track.thumbExtent) * 0.5F),
                .width = normalizeFloat(track.thumbExtent),
                .height = normalizeFloat(track.thumbExtent),
            },
        .fraction = fraction,
    };
}

} // namespace Tina::UI::Detail
