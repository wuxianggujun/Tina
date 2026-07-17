#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/platform/Window.hpp>

#include <array>
#include <bitset>
#include <compare>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace Tina::Platform {

struct GamepadRegistryTag;
using GamepadId = Core::GenerationId<GamepadRegistryTag>;

using PointerId = u32;
inline constexpr PointerId PrimaryPointerId = 0;

// Backend-neutral physical keys. Platform adapters must normalize native key
// values before they enter Tina Platform public headers.
enum class Key : u16 {
    Unknown = 0,
    Space,
    Apostrophe,
    Comma,
    Minus,
    Period,
    Slash,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Semicolon,
    Equal,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    LeftBracket,
    Backslash,
    RightBracket,
    GraveAccent,
    Escape,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    KeypadEqual,
    LeftShift,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,
    Menu,
    Count,
};

enum class PointerButton : u8 {
    Primary = 0,
    Secondary,
    Middle,
    Button4,
    Button5,
    Button6,
    Button7,
    Button8,
    Count,
};

enum class GamepadButton : u8 {
    South = 0,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftStick,
    RightStick,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
    Count,
};

enum class GamepadAxis : u8 {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

enum class DigitalTransition : u8 {
    Down,
    Up,
};

enum class InputCancelReason : u8 {
    FocusLost,
    DeviceDisconnected,
    WindowClosing,
    BackendRecovery,
};

enum class InputResetReason : u8 {
    CapacityExceeded,
    TextByteCapacityExceeded,
    BackendRecovery,
};

inline constexpr usize KeyCount = static_cast<usize>(Key::Count);
inline constexpr usize PointerButtonCount = static_cast<usize>(PointerButton::Count);
inline constexpr usize GamepadButtonCount = static_cast<usize>(GamepadButton::Count);
inline constexpr usize GamepadAxisCount = static_cast<usize>(GamepadAxis::Count);

struct PointerSnapshot final {
    PointerId pointer = PrimaryPointerId;
    double logicalX = 0.0;
    double logicalY = 0.0;
    double accumulatedDeltaX = 0.0;
    double accumulatedDeltaY = 0.0;
    std::bitset<PointerButtonCount> heldButtons{};

    [[nodiscard]] bool isHeld(PointerButton button) const noexcept
    {
        const auto index = static_cast<usize>(button);
        return index < heldButtons.size() && heldButtons.test(index);
    }
};

struct WindowInputSnapshot final {
    WindowId window{};
    u64 sourceMetricsRevision = 0;
    std::bitset<KeyCount> heldKeys{};
    PointerSnapshot pointer{};

    [[nodiscard]] bool isHeld(Key key) const noexcept
    {
        const auto index = static_cast<usize>(key);
        return index < heldKeys.size() && heldKeys.test(index);
    }
};

struct GamepadSnapshot final {
    GamepadId gamepad{};
    u64 revision = 0;
    std::bitset<GamepadButtonCount> heldButtons{};
    std::array<float, GamepadAxisCount> axes{};

    [[nodiscard]] bool isHeld(GamepadButton button) const noexcept
    {
        const auto index = static_cast<usize>(button);
        return index < heldButtons.size() && heldButtons.test(index);
    }

    [[nodiscard]] float axis(GamepadAxis axisValue) const noexcept
    {
        const auto index = static_cast<usize>(axisValue);
        return index < axes.size() ? axes[index] : 0.0F;
    }
};

struct KeyTransition final {
    WindowId window{};
    Key key = Key::Unknown;
    DigitalTransition state = DigitalTransition::Down;
    bool repeat = false;
};

struct PointerButtonTransition final {
    WindowId window{};
    PointerId pointer = PrimaryPointerId;
    PointerButton button = PointerButton::Primary;
    DigitalTransition state = DigitalTransition::Down;
    // Position captured for this exact transition. It must not be replaced
    // with the poll-ending PointerSnapshot position by downstream routing.
    double logicalX = 0.0;
    double logicalY = 0.0;
};

struct PointerMoveTransition final {
    WindowId window{};
    PointerId pointer = PrimaryPointerId;
    double logicalX = 0.0;
    double logicalY = 0.0;
    double deltaX = 0.0;
    double deltaY = 0.0;
};

struct PointerWheelTransition final {
    WindowId window{};
    PointerId pointer = PrimaryPointerId;
    double deltaX = 0.0;
    double deltaY = 0.0;
    // Appended after the original aggregate fields so existing positional
    // initializers keep their delta semantics. Position and delta are both
    // window-logical values captured for this exact transition.
    double logicalX = 0.0;
    double logicalY = 0.0;
};

struct GamepadButtonTransition final {
    WindowId routedWindow{};
    GamepadId gamepad{};
    GamepadButton button = GamepadButton::South;
    DigitalTransition state = DigitalTransition::Down;
};

struct GamepadAxisTransition final {
    WindowId routedWindow{};
    GamepadId gamepad{};
    GamepadAxis axis = GamepadAxis::LeftX;
    float value = 0.0F;
};

// UTF-8 storage is copied into the owning PlatformFrameBuilder and has the same
// borrowed lifetime as PlatformFrameView. No native key or text representation
// crosses this API.
struct TextInputTransition final {
    WindowId window{};
    std::string_view committedUtf8;
};

enum class TextCompositionStage : u8 {
    Started,
    Updated,
    Ended,
    Cancelled,
};

struct TextCompositionTransition final {
    WindowId window{};
    std::string_view preeditUtf8;
    u32 cursorCodepoint = 0;
    TextCompositionStage stage = TextCompositionStage::Updated;
};

struct InputCancelTransition final {
    WindowId routedWindow{};
    InputCancelReason reason = InputCancelReason::FocusLost;
    std::optional<GamepadId> gamepad{};
};

struct InputStreamReset final {
    // nullopt resets the complete engine input stream.
    std::optional<WindowId> routedWindow{};
    InputResetReason reason = InputResetReason::CapacityExceeded;
};

struct KeyControlIdentity final {
    WindowId window{};
    Key key = Key::Unknown;

    auto operator<=>(const KeyControlIdentity&) const = default;
};

struct PointerButtonControlIdentity final {
    WindowId window{};
    PointerId pointer = PrimaryPointerId;
    PointerButton button = PointerButton::Primary;

    auto operator<=>(const PointerButtonControlIdentity&) const = default;
};

struct GamepadButtonControlIdentity final {
    WindowId routedWindow{};
    GamepadId gamepad{};
    GamepadButton button = GamepadButton::South;

    auto operator<=>(const GamepadButtonControlIdentity&) const = default;
};

using DigitalControlIdentity =
    std::variant<KeyControlIdentity, PointerButtonControlIdentity, GamepadButtonControlIdentity>;

struct GamepadAxisControlIdentity final {
    WindowId routedWindow{};
    GamepadId gamepad{};
    GamepadAxis axis = GamepadAxis::LeftX;

    auto operator<=>(const GamepadAxisControlIdentity&) const = default;
};

enum class PointerContinuousControl : u8 {
    Delta,
    Wheel,
};

struct PointerContinuousControlIdentity final {
    WindowId window{};
    PointerId pointer = PrimaryPointerId;
    PointerContinuousControl control = PointerContinuousControl::Delta;

    auto operator<=>(const PointerContinuousControlIdentity&) const = default;
};

using ContinuousControlIdentity = std::variant<GamepadAxisControlIdentity, PointerContinuousControlIdentity>;

using InputTransitionPayload =
    std::variant<KeyTransition, PointerButtonTransition, PointerMoveTransition, PointerWheelTransition,
                 GamepadButtonTransition, GamepadAxisTransition, TextInputTransition, TextCompositionTransition,
                 InputCancelTransition, InputStreamReset>;

struct InputTransition final {
    u64 sequence = 0;
    InputTransitionPayload payload{};
};

using InputTransitionBatch = std::span<const InputTransition>;

} // namespace Tina::Platform
