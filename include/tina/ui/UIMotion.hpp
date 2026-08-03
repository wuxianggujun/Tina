#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIPaint.hpp>

namespace Tina::UI {

// First UI-MOTION-001 public slice. Tracks are fixed-capacity, paint-only, and
// never change hit/layout/callback timing. Reduced-motion snaps to the target
// without occupying the active list after the snap frame.

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
};

struct UITransitionSpec final {
    UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
    Core::Duration duration{0.0};
    Core::Duration delay{0.0};
    UIEasing easing = UIEasing::Linear;
};

struct UIMotionStatistics final {
    usize trackCapacity = 0;
    usize reservedTrackCount = 0;
    usize reservedTrackHighWater = 0;
    usize activeTrackCount = 0;
    usize trackHighWater = 0;
    usize lastSampledTrackCount = 0;
    bool reducedMotion = false;
};

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
    }
    return false;
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
