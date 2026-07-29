#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UIButton.hpp>

#include <array>
#include <optional>
#include <type_traits>

namespace Tina::UI::Detail {

class UIDefaultActionPressState final {
public:
    explicit UIDefaultActionPressState(Platform::WindowId ownerWindow) noexcept;

    [[nodiscard]] Core::Status
    validateControl(UIButtonActivationSource source,
                    const Platform::DigitalControlIdentity& control) const;

    [[nodiscard]] UINodeId
    pressedTarget(const Platform::DigitalControlIdentity& control) const noexcept;
    [[nodiscard]] UINodeId pressedTarget(Platform::GamepadId gamepad) const noexcept;

    void setPressedTarget(const Platform::DigitalControlIdentity& control,
                          UINodeId target) noexcept;
    void clearPressedTarget(const Platform::DigitalControlIdentity& control) noexcept;
    void clearAll() noexcept;
    void clearNode(UINodeId node) noexcept;
    void clearGamepad(Platform::GamepadId gamepad) noexcept;

    [[nodiscard]] bool isPressed(UINodeId node) const noexcept;

private:
    struct GamepadPress final {
        Platform::GamepadId gamepad{};
        UINodeId target{};
    };

    [[nodiscard]] static std::optional<usize> keySlot(Platform::Key key) noexcept;
    [[nodiscard]] bool isOwnedAcceptKey(const Platform::KeyControlIdentity& key) const noexcept;
    [[nodiscard]] bool
    isOwnedAcceptButton(const Platform::GamepadButtonControlIdentity& button) const noexcept;

    Platform::WindowId ownerWindow_{};
    std::array<UINodeId, 3> keyPressedTargets_{};
    std::array<GamepadPress, Platform::PlatformFrameBuilder::MaximumGamepadSlots>
        gamepadPressed_{};
};

static_assert(std::is_nothrow_copy_constructible_v<UIDefaultActionPressState>);
static_assert(std::is_nothrow_copy_assignable_v<UIDefaultActionPressState>);

} // namespace Tina::UI::Detail
