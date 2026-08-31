#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/math/Vec.hpp>

#include <cmath>

namespace Tina::Gameplay {

// How many times a timer fires, or how many times an action subtree runs.
//
// `count == 0` with `infinite == false` is rejected at schedule/commit rather
// than reinterpreted as "forever". Every engine that overloads zero this way
// makes "I computed a count and it came out empty" indistinguishable from "run
// until cancelled", and the two failures look nothing alike at runtime.
struct Repeat final {
    Core::u32 count = 1;
    bool infinite = false;

    [[nodiscard]] static constexpr Repeat once() noexcept { return Repeat{.count = 1}; }

    [[nodiscard]] static constexpr Repeat times(Core::u32 value) noexcept
    {
        return Repeat{.count = value};
    }

    [[nodiscard]] static constexpr Repeat forever() noexcept
    {
        return Repeat{.count = 0, .infinite = true};
    }

    [[nodiscard]] constexpr bool isValid() const noexcept { return infinite || count >= 1; }

    // True once `delivered` iterations have run. Infinite repeats never finish.
    [[nodiscard]] constexpr bool isComplete(Core::u32 delivered) const noexcept
    {
        return !infinite && delivered >= count;
    }

    friend constexpr bool operator==(const Repeat&, const Repeat&) noexcept = default;
};

// A time scale is a multiplier on the delta a runner distributes. Negative and
// non-finite values are rejected: running gameplay timers backwards would need a
// rewind contract that nothing here implements, and silently accepting -1 would
// simply stall every timer forever.
[[nodiscard]] inline bool isValidTimeScale(double scale) noexcept
{
    return std::isfinite(scale) && scale >= 0.0;
}

[[nodiscard]] inline bool isValidDuration(Core::Duration duration) noexcept
{
    return std::isfinite(duration.count()) && duration.count() >= 0.0;
}

// Value interpolation for tween setters. These take an already-eased alpha, so a
// caller composes them with evaluateEasing() rather than passing raw progress.
//
// Defined here rather than in Tina::Math because they are not new geometry: each
// one is `from + (to - from) * alpha` over the operators Math already publishes,
// and ADR 0035 keeps Math as the definition point for the *types* and geometric
// queries rather than for every gameplay-side convenience over them.
[[nodiscard]] constexpr float interpolate(float from, float to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}

[[nodiscard]] constexpr Math::Vec2 interpolate(Math::Vec2 from, Math::Vec2 to, float alpha) noexcept
{
    return Math::lerp(from, to, alpha);
}

[[nodiscard]] constexpr Math::Vec3 interpolate(Math::Vec3 from, Math::Vec3 to, float alpha) noexcept
{
    return Math::lerp(from, to, alpha);
}

[[nodiscard]] constexpr Math::Vec4 interpolate(Math::Vec4 from, Math::Vec4 to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}

} // namespace Tina::Gameplay
