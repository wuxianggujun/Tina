#pragma once

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/MobileGamepad.hpp>
#include <tina/platform/PlatformFrame.hpp>

#include <array>
#include <bitset>
#include <span>

namespace Tina::Platform::Detail {

[[nodiscard]] std::optional<GamepadButton> mapAndroidGamepadButton(i32 keyCode) noexcept;
[[nodiscard]] std::optional<GamepadAxis> mapAndroidGamepadAxis(i32 axisCode) noexcept;
[[nodiscard]] bool isAndroidGamepadHatX(i32 axisCode) noexcept;
[[nodiscard]] bool isAndroidGamepadHatY(i32 axisCode) noexcept;
[[nodiscard]] GamepadName makeMobileGamepadName(std::string_view value) noexcept;
[[nodiscard]] GamepadGuid makeMobileGamepadGuid(std::string_view value) noexcept;
[[nodiscard]] GamepadLayout classifyMobileGamepadLayout(std::string_view name,
                                                        std::string_view guid) noexcept;
[[nodiscard]] float filterMobileGamepadAxis(GamepadAxis axis, float value) noexcept;
[[nodiscard]] bool mobileGamepadAxisChanged(float previous, float current) noexcept;

class MobileGamepadState final {
public:
    MobileGamepadState();

    [[nodiscard]] Core::Status drain(MobileGamepadEventQueue& queue,
                                     PlatformFrameBuilder& frame,
                                     WindowId window) noexcept;
    [[nodiscard]] Core::Status cancelAll(PlatformFrameBuilder& frame,
                                         WindowId window) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::span<const GamepadSnapshot> snapshots() const noexcept
    {
        return {snapshots_.data(), snapshotCount_};
    }

private:
    struct Slot final {
        bool active = false;
        u64 nativeDeviceId = 0;
        GamepadId id{};
        GamepadDeviceInfo device{};
        u64 revision = 0;
        std::bitset<GamepadButtonCount> heldButtons{};
        std::array<float, GamepadAxisCount> axes{};
        float hatX = 0.0F;
        float hatY = 0.0F;
    };

    using Pool = Core::GenerationPool<int, GamepadRegistryTag>;

    [[nodiscard]] Core::Status appendInput(PlatformFrameBuilder& frame,
                                            InputTransitionPayload payload) noexcept;
    [[nodiscard]] Core::Status appendDisconnect(Slot& slot,
                                                 PlatformFrameBuilder& frame,
                                                 WindowId window) noexcept;
    [[nodiscard]] Core::Status applyEvent(const MobileGamepadEvent& event,
                                          PlatformFrameBuilder& frame,
                                          WindowId window) noexcept;
    [[nodiscard]] Slot* find(u64 nativeDeviceId) noexcept;
    [[nodiscard]] const Slot* find(u64 nativeDeviceId) const noexcept;
    [[nodiscard]] Core::Status publishSnapshots() noexcept;
    [[nodiscard]] Core::Status applyHat(Slot& slot, PlatformFrameBuilder& frame,
                                        WindowId window, bool xAxis, float value) noexcept;
    [[nodiscard]] Core::Status applyButton(Slot& slot, PlatformFrameBuilder& frame,
                                           WindowId window, GamepadButton button,
                                           DigitalTransition state) noexcept;

    Pool pool_;
    std::array<Slot, PlatformFrameBuilder::MaximumGamepadSlots> slots_{};
    std::array<GamepadSnapshot, PlatformFrameBuilder::MaximumGamepadSlots> snapshots_{};
    usize snapshotCount_ = 0;
};

} // namespace Tina::Platform::Detail
