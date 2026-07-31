#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UIRangeInput.hpp>

#include <array>
#include <optional>

namespace Tina::UI::Detail {

// Fixed-capacity control-identity latch. Multiple keys and gamepads may map to
// the same capability command without releasing one another's press.
class UIRangeInputPressLatch final {
  public:
    explicit UIRangeInputPressLatch(Platform::WindowId ownerWindow) noexcept;

    [[nodiscard]] Core::Status validateControl(const Platform::DigitalControlIdentity& control) const;
    [[nodiscard]] bool isLatched(const Platform::DigitalControlIdentity& control,
                                 UIRangeInputCommand command) const noexcept;
    void latch(const Platform::DigitalControlIdentity& control, UIRangeInputCommand command) noexcept;
    [[nodiscard]] bool release(const Platform::DigitalControlIdentity& control,
                               UIRangeInputCommand command) noexcept;
    void clear() noexcept;
    void clearGamepad(Platform::GamepadId gamepad) noexcept;

  private:
    struct Press final {
        UIRangeInputCommand command = UIRangeInputCommand::Decrease;
        bool latched = false;
    };

    struct GamepadPresses final {
        Platform::GamepadId gamepad{};
        std::array<Press, 4> buttons{};
    };

    [[nodiscard]] static std::optional<usize> keySlot(Platform::Key key) noexcept;
    [[nodiscard]] static std::optional<usize> gamepadButtonSlot(Platform::GamepadButton button) noexcept;

    Platform::WindowId ownerWindow_{};
    std::array<Press, 4> keys_{};
    std::array<GamepadPresses, Platform::PlatformFrameBuilder::MaximumGamepadSlots> gamepads_{};
};

} // namespace Tina::UI::Detail
