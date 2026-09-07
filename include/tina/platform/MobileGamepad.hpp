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
    // Sticks: [-1, 1], positive Y is down. Triggers: [0, 1].
    // Hats: negative X/Y is left/up, zero is neutral.
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

    // Consumer-only lookahead for bounded frame assembly. The producer cannot
    // reuse this slot until tryPop advances the consumer index.
    [[nodiscard]] bool tryPeek(MobileGamepadEvent& event) const noexcept
    {
        const u64 read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire)) { return false; }
        event = slots_[read];
        return true;
    }

    [[nodiscard]] u64 droppedEventCount() const noexcept
    {
        return droppedEventCount_.load(std::memory_order_relaxed);
    }

    // Consumer-only. A recovery discards one bounded prefix, never chases a producer.
    void discard() noexcept
    {
        readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
    }

    // The consumer invalidates its registry after loss. Before delivering more input,
    // the producer must enumerate live devices again when this request is consumed.
    void requestResync() noexcept { resyncRequested_.store(true, std::memory_order_release); }
    [[nodiscard]] bool takeResyncRequest() noexcept
    {
        return resyncRequested_.exchange(false, std::memory_order_acq_rel);
    }

private:
    static constexpr usize SlotCount = MobileGamepadEventCapacity + 1U;
    std::array<MobileGamepadEvent, SlotCount> slots_{};
    alignas(64) std::atomic<u64> writeIndex_{0};
    alignas(64) std::atomic<u64> readIndex_{0};
    alignas(64) std::atomic<u64> droppedEventCount_{0};
    std::atomic<bool> resyncRequested_{false};
};

} // namespace Tina::Platform
