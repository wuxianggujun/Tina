#pragma once

#include <tina/platform/Input.hpp>
#include <tina/ui/UILayout.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::UI {

// On-screen thumbstick geometry and drag resolution.
//
// This is pure math with no UIContext dependency, no new widget kind and no
// behavior registration: a product composes two Elements (a base ring and a knob)
// and drives them from routed pointer listeners, which is the sanctioned path for
// a custom drag control (ADR 0023 -- UI-BEHAVIOR-SPI-001 is deferred, so the drag
// state machine belongs to the product). What is shared here is the part every
// implementation gets wrong the same way.
//
// The failure modes below were taken from cocos2d-x's circular controls
// (extensions/GUI/CCControlExtension), which are the closest thing in that engine
// to a thumbstick -- it ships no virtual joystick at all. Each is avoided here on
// purpose:
//
//  1. Its ControlSaturationBrightnessPicker freezes the knob the instant the
//     finger leaves the base circle, because its move handler early-returns when
//     the distance exceeds the radius. The fix is commented out in its source.
//     Here the knob is always clamped to the ring, so a finger that keeps sliding
//     keeps steering at full deflection -- which is what a player expects.
//  2. Its ControlHuePicker hit-tests a hardcoded 59..80 annulus while the knob
//     actually travels at radius 61, and adds a bare "+ 10" to the x coordinate
//     with no matching y term. Every radius here is derived from the authored
//     config, never from a literal tuned against one screen size.
//  3. cocos2d-x has no deadzone anywhere in the engine; raw values go straight to
//     the game. A stick that cannot report true zero makes a character drift, so
//     one is applied here, with the same outer-rescale shape the gamepad backend
//     already uses so both input paths feel identical.
//  4. Its EventListenerTouchOneByOne claims every touch that hits the control, so
//     two fingers on one stick fight over the knob. State here records the pointer
//     that engaged it and ignores the rest.
//  5. Its pickers have no touch-ended handler at all, so the knob never returns to
//     centre. release()/cancel() below always recentre.
//
// Sizing follows one relationship worth keeping from that engine
// (CCControlHuePicker.cpp:107, `limit = width*0.5f - 15.0f`): travel is clamped so
// the knob stays inside the base rather than letting the knob *centre* reach the
// base edge. Absolute sizes there come from a colour picker, so they are not
// carried over; defaults below are chosen for a thumb.

struct UIVirtualStickConfig final {
    // Logical pixels. The base is the ring the thumb moves within; the knob is the
    // moving disc. Both are radii, not diameters.
    float baseRadius = 56.0F;
    float knobRadius = 24.0F;
    // Fraction of travel, [0, 0.95). Inside it the stick reports exactly zero;
    // outside it the remaining range is rescaled so the ring still reads 1.0.
    // Matches Platform::Detail::DefaultGamepadStickDeadzone so a virtual stick and
    // a physical one behave the same.
    float deadzone = 0.18F;

    auto operator<=>(const UIVirtualStickConfig&) const = default;
};

// Resolved stick output plus the knob placement that produced it.
struct UIVirtualStickState final {
    // False until a pointer engages the base. A disengaged stick reads zero.
    bool engaged = false;
    // The pointer that engaged it. A second pointer is ignored rather than
    // allowed to fight over the knob.
    Platform::PointerId pointer = Platform::PrimaryPointerId;
    // Knob centre offset from the base centre, in logical pixels, already clamped
    // to the travel radius. This is what the knob Element's overlay offset gets.
    UILogicalPoint knobOffset{};
    // Normalized deflection. x is right-positive; y is **up-negative**, matching
    // both UI logical coordinates (y grows downward) and the gamepad convention
    // (GLFW reports stick Y negative-up), so gameplay code reads one axis sign.
    float x = 0.0F;
    float y = 0.0F;
    // Length of (x, y), in [0, 1]. Reported separately because clamping the vector
    // to the unit circle loses it, and a stick pushed diagonally must not be
    // faster than one pushed straight.
    float magnitude = 0.0F;

    auto operator<=>(const UIVirtualStickState&) const = default;
};

[[nodiscard]] constexpr bool isValidVirtualStickConfig(const UIVirtualStickConfig& config) noexcept
{
    return config.baseRadius > 0.0F && config.knobRadius > 0.0F
        && config.knobRadius < config.baseRadius && config.deadzone >= 0.0F
        && config.deadzone < 0.95F;
}

// How far the knob centre may travel. The knob stays fully inside the base, which
// is the relationship cocos2d-x's hue picker uses and the reason its ring never
// visually overflows its own artwork.
[[nodiscard]] constexpr float virtualStickTravelRadius(const UIVirtualStickConfig& config) noexcept
{
    const float travel = config.baseRadius - config.knobRadius;
    return travel > 0.0F ? travel : 0.0F;
}

// Centre of a base Element from its committed layout rect. Kept as a function so
// no caller re-derives it and gets the half-extent wrong.
[[nodiscard]] constexpr UILogicalPoint virtualStickCenter(UILogicalRect baseRect) noexcept
{
    return UILogicalPoint{
        .x = baseRect.x + baseRect.width * 0.5F,
        .y = baseRect.y + baseRect.height * 0.5F,
    };
}

// True when a point is within the base disc. Used to decide whether a press
// engages the stick; a circular test rather than the Element's rectangle, or the
// corners of the bounding box would steer the character.
[[nodiscard]] inline bool virtualStickContains(const UIVirtualStickConfig& config,
                                              UILogicalPoint center,
                                              UILogicalPoint position) noexcept
{
    const float dx = position.x - center.x;
    const float dy = position.y - center.y;
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return false;
    }
    return (dx * dx + dy * dy) <= (config.baseRadius * config.baseRadius);
}

// Resolves a pointer position into knob placement and normalized output.
//
// Outside the travel radius the knob is clamped to the ring and the output stays
// at full deflection: sliding further must not freeze or release the stick.
[[nodiscard]] inline UIVirtualStickState resolveVirtualStick(const UIVirtualStickConfig& config,
                                                            UILogicalPoint center,
                                                            UILogicalPoint position) noexcept
{
    UIVirtualStickState state{};
    const float travel = virtualStickTravelRadius(config);
    const float dx = position.x - center.x;
    const float dy = position.y - center.y;
    if (!(travel > 0.0F) || !std::isfinite(dx) || !std::isfinite(dy)) {
        return state;
    }

    const float distance = std::sqrt(dx * dx + dy * dy);
    // A press exactly on the centre has no direction; reporting zero is correct
    // and avoids dividing by it.
    if (!(distance > 0.0F)) {
        return state;
    }

    const float clampedDistance = std::min(distance, travel);
    const float unitX = dx / distance;
    const float unitY = dy / distance;
    state.knobOffset = UILogicalPoint{
        .x = unitX * clampedDistance,
        .y = unitY * clampedDistance,
    };

    // Deadzone is radial, then the remaining range is rescaled to [0, 1]. Applying
    // it per axis instead would let a stick held straight up still report sideways
    // drift, and would make the deadzone square rather than round.
    const float normalized = clampedDistance / travel;
    const float deadzone = std::clamp(config.deadzone, 0.0F, 0.95F);
    if (normalized <= deadzone) {
        return state;
    }
    const float magnitude = std::clamp((normalized - deadzone) / (1.0F - deadzone), 0.0F, 1.0F);
    state.magnitude = magnitude;
    state.x = unitX * magnitude;
    state.y = unitY * magnitude;
    return state;
}

// Engages the stick if `position` is inside the base. Returns false without
// touching `state` otherwise, so a press elsewhere cannot steal an active stick.
[[nodiscard]] inline bool pressVirtualStick(UIVirtualStickState& state,
                                           const UIVirtualStickConfig& config,
                                           UILogicalPoint center, Platform::PointerId pointer,
                                           UILogicalPoint position) noexcept
{
    if (state.engaged || !isValidVirtualStickConfig(config)
        || !virtualStickContains(config, center, position)) {
        return false;
    }
    state = resolveVirtualStick(config, center, position);
    state.engaged = true;
    state.pointer = pointer;
    return true;
}

// Tracks a drag. Ignores any pointer other than the one that engaged the stick,
// which is the multi-touch fight cocos2d-x's one-by-one listener does not prevent.
[[nodiscard]] inline bool dragVirtualStick(UIVirtualStickState& state,
                                          const UIVirtualStickConfig& config,
                                          UILogicalPoint center, Platform::PointerId pointer,
                                          UILogicalPoint position) noexcept
{
    if (!state.engaged || state.pointer != pointer || !isValidVirtualStickConfig(config)) {
        return false;
    }
    const Platform::PointerId owner = state.pointer;
    state = resolveVirtualStick(config, center, position);
    state.engaged = true;
    state.pointer = owner;
    return true;
}

// Releases and recentres. Ignores other pointers for the same reason drag does.
// A cancel (focus loss, device loss) uses this same path: there is no half-released
// stick, because a knob left deflected keeps steering a character that nobody is
// touching.
[[nodiscard]] inline bool releaseVirtualStick(UIVirtualStickState& state,
                                             Platform::PointerId pointer) noexcept
{
    if (!state.engaged || state.pointer != pointer) {
        return false;
    }
    state = UIVirtualStickState{};
    return true;
}

// Unconditional recentre, for a cancel whose pointer identity is unknown.
inline void cancelVirtualStick(UIVirtualStickState& state) noexcept
{
    state = UIVirtualStickState{};
}

// Digital keys driving the same stick, so WASD and a finger produce identical
// downstream values. Opposite keys cancel rather than one winning, and the
// diagonal is normalized so keyboard movement is not faster than pointer movement
// -- the classic bug where holding W+D gives sqrt(2) speed.
//
// The deadzone is deliberately *not* applied: a key is already an unambiguous full
// deflection, and rescaling it would make the keyboard reach only (1 - deadzone).
[[nodiscard]] inline UIVirtualStickState virtualStickFromDigital(const UIVirtualStickConfig& config,
                                                                bool left, bool right, bool up,
                                                                bool down) noexcept
{
    UIVirtualStickState state{};
    const float x = (right ? 1.0F : 0.0F) - (left ? 1.0F : 0.0F);
    // Up is negative to match the pointer path and the gamepad convention.
    const float y = (down ? 1.0F : 0.0F) - (up ? 1.0F : 0.0F);
    if (x == 0.0F && y == 0.0F) {
        return state;
    }
    const float length = std::sqrt(x * x + y * y);
    state.engaged = true;
    state.magnitude = 1.0F;
    state.x = x / length;
    state.y = y / length;
    const float travel = virtualStickTravelRadius(config);
    state.knobOffset = UILogicalPoint{.x = state.x * travel, .y = state.y * travel};
    return state;
}

} // namespace Tina::UI
