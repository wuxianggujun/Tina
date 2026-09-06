#pragma once

#include <tina/platform/Input.hpp>

#include <array>
#include <atomic>

namespace Tina::Platform {

// Raw controller events crossing an Android/UIKit producer into the engine
// owner thread. deviceId is an opaque native connection identity; it is never
// exposed as a persistent Tina handle.
enum class MobileGamepadEventKind : u8 {
    Connected,
    Disconnected,
    Button,
    Axis,
    HatX,
    HatY,
};

struct MobileGamepadEvent final {
    MobileGamepadEventKind kind = MobileGamepadEventKind::Button;
    u64 deviceId = 0;
    GamepadDeviceInfo device{};
    GamepadButton button = GamepadButton::South;
    GamepadAxis axis = GamepadAxis::LeftX;
    DigitalTransition state = DigitalTransition::Up;
    float value = 0.0F;
};

inline constexpr usize MobileGamepadEventCapacity = 512;

// Single-producer/single-consumer fixed ring. Android's UI thread and UIKit's
// main thread are producers; the platform backend drains it from its owner
// thread. A full queue drops the newest event and exposes a monotonic counter.
class MobileGamepadEventQueue final {
public:
    MobileGamepadEventQueue() noexcept = default;
    MobileGamepadEventQueue(const MobileGamepadEventQueue&) = delete;
    MobileGamepadEventQueue& operator=(const MobileGamepadEventQueue&) = delete;
    MobileGamepadEventQueue(MobileGamepadEventQueue&&) = delete;
    MobileGamepadEventQueue& operator=(MobileGamepadEventQueue&&) = delete;

    [[nodiscard]] bool tryPush(const MobileGamepadEvent& event) noexcept
    {
        const u64 write = writeIndex_.load(std::memory_order_relaxed);
        const u64 read = readIndex_.load(std::memory_order_acquire);
        const u64 next = (write + 1U) % SlotCount;
        if (next == read) {
            droppedEventCount_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        slots_[write] = event;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(MobileGamepadEvent& event) noexcept
    {
        const u64 read = readIndex_.load(std::memory_order_relaxed);
        const u64 write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) { return false; }
        event = slots_[read];
        readIndex_.store((read + 1U) % SlotCount, std::memory_order_release);
        return true;
    }

    [[nodiscard]] u64 droppedEventCount() const noexcept
    {
        return droppedEventCount_.load(std::memory_order_relaxed);
    }

private:
    static constexpr usize SlotCount = MobileGamepadEventCapacity + 1U;
    std::array<MobileGamepadEvent, SlotCount> slots_{};
    alignas(64) std::atomic<u64> writeIndex_{0};
    alignas(64) std::atomic<u64> readIndex_{0};
    alignas(64) std::atomic<u64> droppedEventCount_{0};
};

} // namespace Tina::Platform
