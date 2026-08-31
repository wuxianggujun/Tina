#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>

#include <cmath>

namespace Tina::Gameplay {

// Interpolation curves for tweens and gameplay timing.
//
// Deliberately a separate enum from UI::UIEasing rather than a shared one. That
// enum has three values because a UI transition that overshoots or bounces is a
// motion-design decision the UI theme owns; gameplay wants Back/Elastic/Bounce as
// a matter of course. Merging them would either force UI to publish curves its
// theme contract does not admit, or force gameplay to live with three.
//
// Values are append-only: a curve is authored into content and a renumbering
// would silently retarget every existing tween.
enum class Easing : u8 {
    Linear = 0,

    QuadraticIn = 1,
    QuadraticOut = 2,
    QuadraticInOut = 3,

    CubicIn = 4,
    CubicOut = 5,
    CubicInOut = 6,

    QuarticIn = 7,
    QuarticOut = 8,
    QuarticInOut = 9,

    SineIn = 10,
    SineOut = 11,
    SineInOut = 12,

    ExponentialIn = 13,
    ExponentialOut = 14,
    ExponentialInOut = 15,

    CircularIn = 16,
    CircularOut = 17,
    CircularInOut = 18,

    // Overshoots past the target before settling. The overshoot is the point, so
    // a tween using these can leave [0,1] in the middle of its run even though it
    // still starts at exactly 0 and ends at exactly 1.
    BackIn = 19,
    BackOut = 20,
    BackInOut = 21,

    ElasticIn = 22,
    ElasticOut = 23,
    ElasticInOut = 24,

    BounceIn = 25,
    BounceOut = 26,
    BounceInOut = 27,

    Count = 28,
};

[[nodiscard]] constexpr bool isValidEasing(Easing easing) noexcept
{
    const auto value = static_cast<u8>(easing);
    return value < static_cast<u8>(Easing::Count);
}

namespace Detail {

// Shared by BounceIn/BounceInOut, which are defined in terms of the Out curve.
[[nodiscard]] inline float bounceOut(float t) noexcept
{
    constexpr float amplitude = 7.5625F;
    constexpr float period = 2.75F;

    if (t < 1.0F / period) {
        return amplitude * t * t;
    }
    if (t < 2.0F / period) {
        const float shifted = t - (1.5F / period);
        return amplitude * shifted * shifted + 0.75F;
    }
    if (t < 2.5F / period) {
        const float shifted = t - (2.25F / period);
        return amplitude * shifted * shifted + 0.9375F;
    }
    const float shifted = t - (2.625F / period);
    return amplitude * shifted * shifted + 0.984375F;
}

} // namespace Detail

// Maps normalized progress to an eased value. Endpoints are exact: 0 and 1 are
// returned unchanged rather than run through the curve, so a tween lands on its
// authored target instead of on whatever the formula rounds to. Non-finite input
// is treated as a finished tween rather than propagated as NaN into a setter.
[[nodiscard]] inline float evaluateEasing(Easing easing, float t) noexcept
{
    if (!std::isfinite(t) || t >= 1.0F) {
        return 1.0F;
    }
    if (t <= 0.0F) {
        return 0.0F;
    }

    switch (easing) {
    case Easing::Linear:
        return t;

    case Easing::QuadraticIn:
        return t * t;
    case Easing::QuadraticOut:
        return 1.0F - (1.0F - t) * (1.0F - t);
    case Easing::QuadraticInOut:
        return t < 0.5F ? (2.0F * t * t)
                        : (1.0F - ((-2.0F * t + 2.0F) * (-2.0F * t + 2.0F) * 0.5F));

    case Easing::CubicIn:
        return t * t * t;
    case Easing::CubicOut: {
        const float u = 1.0F - t;
        return 1.0F - u * u * u;
    }
    case Easing::CubicInOut: {
        if (t < 0.5F) {
            return 4.0F * t * t * t;
        }
        const float u = -2.0F * t + 2.0F;
        return 1.0F - (u * u * u * 0.5F);
    }

    case Easing::QuarticIn:
        return t * t * t * t;
    case Easing::QuarticOut: {
        const float u = 1.0F - t;
        return 1.0F - u * u * u * u;
    }
    case Easing::QuarticInOut: {
        if (t < 0.5F) {
            return 8.0F * t * t * t * t;
        }
        const float u = -2.0F * t + 2.0F;
        return 1.0F - (u * u * u * u * 0.5F);
    }

    case Easing::SineIn:
        return 1.0F - std::cos(t * Math::HalfPi);
    case Easing::SineOut:
        return std::sin(t * Math::HalfPi);
    case Easing::SineInOut:
        return -(std::cos(Math::Pi * t) - 1.0F) * 0.5F;

    case Easing::ExponentialIn:
        return std::pow(2.0F, 10.0F * t - 10.0F);
    case Easing::ExponentialOut:
        return 1.0F - std::pow(2.0F, -10.0F * t);
    case Easing::ExponentialInOut:
        return t < 0.5F ? (std::pow(2.0F, 20.0F * t - 10.0F) * 0.5F)
                        : ((2.0F - std::pow(2.0F, -20.0F * t + 10.0F)) * 0.5F);

    case Easing::CircularIn:
        return 1.0F - std::sqrt(1.0F - t * t);
    case Easing::CircularOut:
        return std::sqrt(1.0F - (t - 1.0F) * (t - 1.0F));
    case Easing::CircularInOut: {
        if (t < 0.5F) {
            return (1.0F - std::sqrt(1.0F - (2.0F * t) * (2.0F * t))) * 0.5F;
        }
        const float u = -2.0F * t + 2.0F;
        return (std::sqrt(1.0F - u * u) + 1.0F) * 0.5F;
    }

    case Easing::BackIn: {
        constexpr float overshoot = 1.70158F;
        return (overshoot + 1.0F) * t * t * t - overshoot * t * t;
    }
    case Easing::BackOut: {
        constexpr float overshoot = 1.70158F;
        const float u = t - 1.0F;
        return 1.0F + (overshoot + 1.0F) * u * u * u + overshoot * u * u;
    }
    case Easing::BackInOut: {
        constexpr float overshoot = 1.70158F * 1.525F;
        if (t < 0.5F) {
            const float u = 2.0F * t;
            return (u * u * ((overshoot + 1.0F) * u - overshoot)) * 0.5F;
        }
        const float u = 2.0F * t - 2.0F;
        return (u * u * ((overshoot + 1.0F) * u + overshoot) + 2.0F) * 0.5F;
    }

    case Easing::ElasticIn: {
        const float period = Math::TwoPi / 3.0F;
        return -std::pow(2.0F, 10.0F * t - 10.0F) * std::sin((10.0F * t - 10.75F) * period);
    }
    case Easing::ElasticOut: {
        const float period = Math::TwoPi / 3.0F;
        return std::pow(2.0F, -10.0F * t) * std::sin((10.0F * t - 0.75F) * period) + 1.0F;
    }
    case Easing::ElasticInOut: {
        const float period = Math::TwoPi / 4.5F;
        const float phase = std::sin((20.0F * t - 11.125F) * period);
        if (t < 0.5F) {
            return -(std::pow(2.0F, 20.0F * t - 10.0F) * phase) * 0.5F;
        }
        return (std::pow(2.0F, -20.0F * t + 10.0F) * phase) * 0.5F + 1.0F;
    }

    case Easing::BounceIn:
        return 1.0F - Detail::bounceOut(1.0F - t);
    case Easing::BounceOut:
        return Detail::bounceOut(t);
    case Easing::BounceInOut:
        return t < 0.5F ? ((1.0F - Detail::bounceOut(1.0F - 2.0F * t)) * 0.5F)
                        : ((1.0F + Detail::bounceOut(2.0F * t - 1.0F)) * 0.5F);

    case Easing::Count:
        return t;
    }
    return t;
}

} // namespace Tina::Gameplay
