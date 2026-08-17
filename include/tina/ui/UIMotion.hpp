#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>
#include <limits>
#include <span>

namespace Tina::UI::Detail {

class UIKeyframeTimelineStorage;

} // namespace Tina::UI::Detail

namespace Tina::UI {

// UI-MOTION-001 direct transitions remain fixed-capacity and paint-only.
// UI-MOTION-002 timelines additionally admit a bounded layout-property set and
// publish its Layout/Hit/Paint sample transactionally. Reduced-motion snaps to
// the target without occupying the active list after the snap frame.

enum class UIEasing : u8 {
    Linear = 0,
    EaseOut,
    EaseInOut,
};

enum class UIAnimatableProperty : u16 {
    BackgroundColor = 0,
    BorderColor,
    TextColor,
    Opacity,
    CornerRadius,
    VisualOffset,
    // UI-MOTION-002 layout slice. Values are finite logical pixels. Width and
    // height are non-negative Scalar tracks; offset is an Offset track and is
    // consumed by Overlay placement.
    LayoutWidth,
    LayoutHeight,
    LayoutOffset,
};

struct UITransitionSpec final {
    UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
    Core::Duration duration{0.0};
    Core::Duration delay{0.0};
    UIEasing easing = UIEasing::Linear;
};

// UI-MOTION-002 retained timeline identity. The storage owner token is kept
// private so an ID from another live/recreated UIContext cannot alias a local
// definition even when its slot and generation happen to match.
class UITimelineId final {
  public:
    constexpr UITimelineId() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return ownerToken_ != 0 && index_ != InvalidIndex && generation_ != 0;
    }

    [[nodiscard]] constexpr u32 index() const noexcept { return index_; }
    [[nodiscard]] constexpr u32 generation() const noexcept { return generation_; }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    auto operator<=>(const UITimelineId&) const = default;

  private:
    friend class Detail::UIKeyframeTimelineStorage;

    inline static constexpr u32 InvalidIndex = (std::numeric_limits<u32>::max)();

    [[nodiscard]] static constexpr UITimelineId create(
        u64 ownerToken, u32 index, u32 generation) noexcept
    {
        return UITimelineId(ownerToken, index, generation);
    }

    constexpr UITimelineId(u64 ownerToken, u32 index, u32 generation) noexcept
        : ownerToken_(ownerToken), index_(index), generation_(generation)
    {
    }

    u64 ownerToken_ = 0;
    u32 index_ = InvalidIndex;
    u32 generation_ = 0;
};

enum class UIKeyframeValueKind : u8 {
    Color = 0,
    Scalar,
    Offset,
};

struct UIAnimatableOffset final {
    float x = 0.0F;
    float y = 0.0F;

    auto operator<=>(const UIAnimatableOffset&) const = default;
};

// Explicit tagged storage keeps the public ABI backend-neutral and avoids a
// heap-owning/exception-heavy authoring variant. Constructors below are the
// canonical way to create a value; play/create validation still rejects a tag
// that does not match the selected property.
struct UIKeyframeValue final {
    UIKeyframeValueKind kind = UIKeyframeValueKind::Color;
    UIStraightSrgba8Color color{};
    float scalar = 0.0F;
    UIAnimatableOffset offset{};

    [[nodiscard]] static constexpr UIKeyframeValue
    Color(UIStraightSrgba8Color value) noexcept
    {
        return UIKeyframeValue{.kind = UIKeyframeValueKind::Color, .color = value};
    }

    [[nodiscard]] static constexpr UIKeyframeValue Scalar(float value) noexcept
    {
        return UIKeyframeValue{.kind = UIKeyframeValueKind::Scalar, .scalar = value};
    }

    [[nodiscard]] static constexpr UIKeyframeValue Offset(float x, float y) noexcept
    {
        return UIKeyframeValue{
            .kind = UIKeyframeValueKind::Offset,
            .offset = {.x = x, .y = y},
        };
    }
};

struct UIKeyframe final {
    // Strictly increasing in [0,1]. Every track starts at exactly 0 and ends at
    // exactly 1. easingToNext is ignored for the final keyframe.
    float normalizedTime = 0.0F;
    UIKeyframeValue value{};
    UIEasing easingToNext = UIEasing::Linear;
};

struct UITimelineTrackDesc final {
    UINodeId node{};
    UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
    std::span<const UIKeyframe> keyframes{};
};

struct UITimelineDesc final {
    Core::Duration duration{0.0};
    Core::Duration delay{0.0};
    std::span<const UITimelineTrackDesc> tracks{};
};

struct UIMotionStatistics final {
    usize trackCapacity = 0;
    usize reservedTrackCount = 0;
    usize reservedTrackHighWater = 0;
    usize activeTrackCount = 0;
    usize trackHighWater = 0;
    usize lastSampledTrackCount = 0;
    usize timelineCapacity = 0;
    usize timelineCount = 0;
    usize timelineHighWater = 0;
    usize timelineTrackCapacity = 0;
    usize timelineTrackCount = 0;
    usize timelineTrackHighWater = 0;
    usize keyframeCapacity = 0;
    usize keyframeCount = 0;
    usize keyframeHighWater = 0;
    usize activeTimelineCapacity = 0;
    usize activeTimelineCount = 0;
    usize activeTimelineHighWater = 0;
    usize lastSampledTimelineCount = 0;
    usize lastSampledTimelineTrackCount = 0;
    usize lastSampledTimelineLayoutTrackCount = 0;
    usize lastSampledKeyframeSegmentCount = 0;
    usize timelineCancelCount = 0;
    usize timelineRetargetCount = 0;
    usize layoutTimelineCommitFailureCount = 0;
    bool reducedMotion = false;
};

[[nodiscard]] constexpr UIKeyframeValueKind
valueKindForAnimatableProperty(UIAnimatableProperty property) noexcept
{
    switch (property) {
    case UIAnimatableProperty::BackgroundColor:
    case UIAnimatableProperty::BorderColor:
    case UIAnimatableProperty::TextColor:
        return UIKeyframeValueKind::Color;
    case UIAnimatableProperty::Opacity:
    case UIAnimatableProperty::CornerRadius:
    case UIAnimatableProperty::LayoutWidth:
    case UIAnimatableProperty::LayoutHeight:
        return UIKeyframeValueKind::Scalar;
    case UIAnimatableProperty::VisualOffset:
    case UIAnimatableProperty::LayoutOffset:
        return UIKeyframeValueKind::Offset;
    }
    return UIKeyframeValueKind::Color;
}

[[nodiscard]] constexpr bool isValidUIEasing(UIEasing easing) noexcept
{
    return easing == UIEasing::Linear || easing == UIEasing::EaseOut ||
           easing == UIEasing::EaseInOut;
}

[[nodiscard]] constexpr bool isPaintOnlyAnimatableProperty(UIAnimatableProperty property) noexcept
{
    switch (property) {
    case UIAnimatableProperty::BackgroundColor:
    case UIAnimatableProperty::BorderColor:
    case UIAnimatableProperty::TextColor:
    case UIAnimatableProperty::Opacity:
    case UIAnimatableProperty::CornerRadius:
    case UIAnimatableProperty::VisualOffset:
        return true;
    case UIAnimatableProperty::LayoutWidth:
    case UIAnimatableProperty::LayoutHeight:
    case UIAnimatableProperty::LayoutOffset:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr bool isLayoutAnimatableProperty(UIAnimatableProperty property) noexcept
{
    switch (property) {
    case UIAnimatableProperty::LayoutWidth:
    case UIAnimatableProperty::LayoutHeight:
    case UIAnimatableProperty::LayoutOffset:
        return true;
    case UIAnimatableProperty::BackgroundColor:
    case UIAnimatableProperty::BorderColor:
    case UIAnimatableProperty::TextColor:
    case UIAnimatableProperty::Opacity:
    case UIAnimatableProperty::CornerRadius:
    case UIAnimatableProperty::VisualOffset:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr bool isTimelineAnimatableProperty(UIAnimatableProperty property) noexcept
{
    return isPaintOnlyAnimatableProperty(property) || isLayoutAnimatableProperty(property);
}

// t in [0,1] after delay. Returns eased progress in [0,1].
[[nodiscard]] constexpr float evaluateUIEasing(UIEasing easing, float t) noexcept
{
    if (t <= 0.0F) {
        return 0.0F;
    }
    if (t >= 1.0F) {
        return 1.0F;
    }
    switch (easing) {
    case UIEasing::Linear:
        return t;
    case UIEasing::EaseOut: {
        const float u = 1.0F - t;
        return 1.0F - u * u;
    }
    case UIEasing::EaseInOut:
        return t < 0.5F ? (2.0F * t * t) : (1.0F - ((-2.0F * t + 2.0F) * (-2.0F * t + 2.0F) / 2.0F));
    }
    return t;
}

[[nodiscard]] constexpr UIStraightSrgba8Color lerpStraightSrgba8(UIStraightSrgba8Color a,
                                                                  UIStraightSrgba8Color b,
                                                                  float t) noexcept
{
    const auto channel = [t](u8 from, u8 to) noexcept -> u8 {
        const float mixed = static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t;
        if (mixed <= 0.0F) {
            return 0;
        }
        if (mixed >= 255.0F) {
            return 255;
        }
        return static_cast<u8>(mixed + 0.5F);
    };
    return UIStraightSrgba8Color{
        .red = channel(a.red, b.red),
        .green = channel(a.green, b.green),
        .blue = channel(a.blue, b.blue),
        .alpha = channel(a.alpha, b.alpha),
    };
}

} // namespace Tina::UI
