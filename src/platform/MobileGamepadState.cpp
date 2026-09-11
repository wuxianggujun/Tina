#include "MobileGamepadState.hpp"

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Platform::Detail {
namespace {

constexpr float StickDeadzone = 0.18F;
constexpr float AxisHysteresis = 0.02F;
constexpr float HatThreshold = 0.5F;

[[nodiscard]] std::array<float, GamepadAxisCount> neutralAxes() noexcept
{
    return GamepadNeutralAxes;
}

template <typename Text>
[[nodiscard]] Text copyIdentity(std::string_view value) noexcept
{
    Text result{};
    if (!Core::isStrictUtf8WithoutNul(value)) { return result; }
    usize size = (std::min)(value.size(), result.bytes.size() - 1U);
    while (size != 0 && !Core::isStrictUtf8WithoutNul(value.substr(0, size))) { --size; }
    std::copy_n(value.begin(), size, result.bytes.begin());
    result.length = static_cast<u8>(size);
    return result;
}

[[nodiscard]] bool hatHeld(GamepadButton button, float x, float y) noexcept
{
    switch (button) {
    case GamepadButton::DpadLeft: return x < -HatThreshold;
    case GamepadButton::DpadRight: return x > HatThreshold;
    case GamepadButton::DpadUp: return y < -HatThreshold;
    case GamepadButton::DpadDown: return y > HatThreshold;
    default: return false;
    }
}

[[nodiscard]] Core::Status appendStatus(FrameBatchAppendResult result) noexcept
{
    switch (result) {
    case FrameBatchAppendResult::Appended:
    case FrameBatchAppendResult::Coalesced:
    case FrameBatchAppendResult::ResetInserted:
    case FrameBatchAppendResult::IgnoredAfterReset:
        return Core::success();
    case FrameBatchAppendResult::SequenceExhausted:
        return Core::failure(PlatformErrorCode::PlatformSequenceExhausted,
                             "Mobile gamepad event sequence exhausted");
    default:
        return Core::failure(PlatformErrorCode::InvalidInputPayload,
                             "Mobile gamepad event could not be appended");
    }
}

} // namespace

std::optional<GamepadButton> mapAndroidGamepadButton(i32 keyCode) noexcept
{
    // Android KeyEvent constants, translated here so no Android SDK enters Platform.
    switch (keyCode) {
    case 96: return GamepadButton::South;
    case 97: return GamepadButton::East;
    case 99: return GamepadButton::West;
    case 100: return GamepadButton::North;
    case 102: return GamepadButton::LeftBumper;
    case 103: return GamepadButton::RightBumper;
    case 106: return GamepadButton::LeftStick;
    case 107: return GamepadButton::RightStick;
    case 108: return GamepadButton::Start;
    case 4:
    case 109: return GamepadButton::Back;
    case 110: return GamepadButton::Guide;
    case 19: return GamepadButton::DpadUp;
    case 20: return GamepadButton::DpadDown;
    case 21: return GamepadButton::DpadLeft;
    case 22: return GamepadButton::DpadRight;
    case 23: return GamepadButton::South; // DPAD_CENTER on remotes.
    default: return std::nullopt;
    }
}

std::optional<GamepadAxis> mapAndroidGamepadAxis(i32 axisCode) noexcept
{
    switch (axisCode) {
    case 0: return GamepadAxis::LeftX;
    case 1: return GamepadAxis::LeftY;
    case 11: return GamepadAxis::RightX;
    case 14: return GamepadAxis::RightY;
    case 17: return GamepadAxis::LeftTrigger;
    case 18: return GamepadAxis::RightTrigger;
    default: return std::nullopt;
    }
}

bool isAndroidGamepadHatX(i32 axisCode) noexcept { return axisCode == 15; }
bool isAndroidGamepadHatY(i32 axisCode) noexcept { return axisCode == 16; }

GamepadName makeMobileGamepadName(std::string_view value) noexcept { return copyIdentity<GamepadName>(value); }
GamepadGuid makeMobileGamepadGuid(std::string_view value) noexcept { return copyIdentity<GamepadGuid>(value); }

GamepadLayout classifyMobileGamepadLayout(std::string_view name) noexcept
{
    const auto contains = [name](std::string_view needle) noexcept {
        return std::search(name.begin(), name.end(), needle.begin(), needle.end(), [](char a, char b) {
            return (a >= 'A' && a <= 'Z' ? static_cast<char>(a + ('a' - 'A')) : a) == b;
        }) != name.end();
    };
    if (contains("xbox") || contains("xinput")) { return GamepadLayout::Xbox; }
    if (contains("playstation") || contains("dualshock") || contains("dualsense")) {
        return GamepadLayout::PlayStation;
    }
    if (contains("nintendo") || contains("joy-con") || contains("switch")) { return GamepadLayout::Nintendo; }
    return GamepadLayout::Generic;
}

float filterMobileGamepadAxis(GamepadAxis axis, float value) noexcept
{
    if (!std::isfinite(value)) { value = 0.0F; }
    if (axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger) {
        return std::clamp(value, 0.0F, 1.0F) * 2.0F - 1.0F;
    }
    value = std::clamp(value, -1.0F, 1.0F);
    const float magnitude = std::abs(value);
    return magnitude <= StickDeadzone ? 0.0F
        : std::copysign((magnitude - StickDeadzone) / (1.0F - StickDeadzone), value);
}

bool mobileGamepadAxisChanged(float previous, float current) noexcept
{
    if (previous == current) { return false; }
    return previous == 0.0F || current == 0.0F || current == -1.0F || current == 1.0F ||
           std::abs(current - previous) >= AxisHysteresis;
}

Core::Result<MobileGamepadState> MobileGamepadState::Create()
{
    auto pool = Pool::Create(PlatformFrameBuilder::MaximumGamepadSlots);
    if (!pool) { return std::unexpected(std::move(pool.error())); }
    return MobileGamepadState(std::move(*pool));
}

Core::Status MobileGamepadState::appendInput(PlatformFrameBuilder& frame, InputTransitionPayload payload) noexcept
{
    const auto appended = frame.appendInputTransition(std::move(payload));
    resyncPending_ |= appended == FrameBatchAppendResult::ResetInserted ||
                      appended == FrameBatchAppendResult::IgnoredAfterReset;
    return appendStatus(appended);
}

MobileGamepadState::Slot* MobileGamepadState::find(u64 nativeDeviceId) noexcept
{
    for (auto& slot : slots_) {
        if (slot.active && slot.nativeDeviceId == nativeDeviceId) { return &slot; }
    }
    return nullptr;
}

Core::Status MobileGamepadState::appendDisconnect(Slot& slot, PlatformFrameBuilder& frame, WindowId window) noexcept
{
    if (auto status = appendInput(frame, InputCancelTransition{
            .routedWindow = window, .reason = InputCancelReason::DeviceDisconnected, .gamepad = slot.id}); !status) {
        return status;
    }
    const auto appended = frame.appendPlatformEvent(GamepadDisconnectedEvent{slot.id});
    if (auto status = appendStatus(appended); !status) { return status; }
    resyncPending_ |= appended != FrameBatchAppendResult::Appended;
    (void)pool_.erase(slot.id);
    slot = {};
    return Core::success();
}

Core::Status MobileGamepadState::applyButton(Slot& slot, PlatformFrameBuilder& frame, WindowId window,
                                            GamepadButton button, DigitalTransition state) noexcept
{
    const usize index = static_cast<usize>(button);
    const bool held = state == DigitalTransition::Down;
    if (slot.heldButtons.test(index) == held) { return Core::success(); }
    if (slot.revision == (std::numeric_limits<u64>::max)()) {
        return Core::failure(PlatformErrorCode::FrameSequenceExhausted, "Mobile gamepad revision exhausted");
    }
    if (auto status = appendInput(frame, GamepadButtonTransition{window, slot.id, button, state}); !status) {
        return status;
    }
    if (resyncPending_) { return Core::success(); }
    slot.heldButtons.set(index, held);
    ++slot.revision;
    return Core::success();
}

Core::Status MobileGamepadState::applyHat(Slot& slot, PlatformFrameBuilder& frame, WindowId window,
                                         bool xAxis, float value) noexcept
{
    const float x = xAxis ? value : slot.hatX;
    const float y = xAxis ? slot.hatY : value;
    // Release before press. Even a one-transition frame can consume a reversal
    // over two polls without transiently holding both opposing directions.
    for (const bool press : {false, true}) {
        for (GamepadButton button : {GamepadButton::DpadUp, GamepadButton::DpadRight,
                                     GamepadButton::DpadDown, GamepadButton::DpadLeft}) {
            const usize index = static_cast<usize>(button);
            const bool held = slot.directButtons.test(index) || hatHeld(button, x, y);
            if (held != press || slot.heldButtons.test(index) == held) { continue; }
            if (frame.remainingInputTransitionCapacity() == 0) {
                eventConsumed_ = false;
                return Core::success();
            }
            if (auto status = applyButton(slot, frame, window, button,
                    held ? DigitalTransition::Down : DigitalTransition::Up); !status) { return status; }
        }
    }
    slot.hatX = x;
    slot.hatY = y;
    return Core::success();
}

Core::Status MobileGamepadState::applyEvent(const MobileGamepadEvent& event, PlatformFrameBuilder& frame,
                                           WindowId window, bool inputEnabled) noexcept
{
    Slot* slot = find(event.deviceId);
    if (event.kind == MobileGamepadEventKind::Connected) {
        if (slot != nullptr) {
            if (slot->device == event.device) { return Core::success(); }
            if (auto status = appendDisconnect(*slot, frame, window); !status) { return status; }
        }
        auto id = pool_.tryEmplace(0);
        if (!id) { return std::unexpected(std::move(id.error())); }
        const auto appended = frame.appendPlatformEvent(GamepadConnectedEvent{*id, event.device});
        if (appended != FrameBatchAppendResult::Appended) {
            (void)pool_.erase(*id);
            resyncPending_ = true;
            return appendStatus(appended);
        }
        slots_[id->index()] = Slot{.active = true, .nativeDeviceId = event.deviceId,
                                   .id = *id, .device = event.device, .revision = 1, .axes = neutralAxes()};
        return Core::success();
    }
    if (event.kind == MobileGamepadEventKind::Disconnected) {
        return slot == nullptr ? Core::success() : appendDisconnect(*slot, frame, window);
    }
    if (slot == nullptr || !inputEnabled) { return Core::success(); }
    switch (event.kind) {
    case MobileGamepadEventKind::Button: {
        if (event.button >= GamepadButton::Count ||
            (event.state != DigitalTransition::Down && event.state != DigitalTransition::Up)) {
            return Core::failure(PlatformErrorCode::InvalidInputPayload, "Invalid mobile gamepad button");
        }
        const bool held = event.state == DigitalTransition::Down || hatHeld(event.button, slot->hatX, slot->hatY);
        if (auto status = applyButton(*slot, frame, window, event.button,
                held ? DigitalTransition::Down : DigitalTransition::Up); !status) { return status; }
        slot->directButtons.set(static_cast<usize>(event.button), event.state == DigitalTransition::Down);
        return Core::success();
    }
    case MobileGamepadEventKind::Axis: {
        if (event.axis >= GamepadAxis::Count || !std::isfinite(event.value)) {
            return Core::failure(PlatformErrorCode::InvalidInputPayload, "Invalid mobile gamepad axis");
        }
        const float value = filterMobileGamepadAxis(event.axis, event.value);
        const usize index = static_cast<usize>(event.axis);
        if (!mobileGamepadAxisChanged(slot->axes[index], value)) { return Core::success(); }
        if (slot->revision == (std::numeric_limits<u64>::max)()) {
            return Core::failure(PlatformErrorCode::FrameSequenceExhausted, "Mobile gamepad revision exhausted");
        }
        if (auto status = appendInput(frame, GamepadAxisTransition{window, slot->id, event.axis, value}); !status) {
            return status;
        }
        if (resyncPending_) { return Core::success(); }
        slot->axes[index] = value;
        ++slot->revision;
        return Core::success();
    }
    case MobileGamepadEventKind::HatX:
    case MobileGamepadEventKind::HatY:
        if (!std::isfinite(event.value)) {
            return Core::failure(PlatformErrorCode::InvalidInputPayload, "Invalid mobile gamepad hat");
        }
        return applyHat(*slot, frame, window, event.kind == MobileGamepadEventKind::HatX, event.value);
    default:
        return Core::failure(PlatformErrorCode::InvalidInputPayload, "Unknown mobile gamepad event kind");
    }
}

Core::Status MobileGamepadState::drain(MobileGamepadEventQueue& queue, PlatformFrameBuilder& frame,
                                      WindowId window, bool inputEnabled) noexcept
{
    recoveredThisPoll_ = false;
    if (queue.droppedEventCount() != observedDrops_ || resyncPending_) { return recover(queue, frame, window); }
    MobileGamepadEvent event{};
    for (usize count = 0; count < MobileGamepadEventCapacity && queue.tryPeek(event); ++count) {
        Slot* slot = find(event.deviceId);
        if (event.kind == MobileGamepadEventKind::Connected) {
            if (event.device.name.length >= event.device.name.bytes.size() ||
                event.device.guid.length >= event.device.guid.bytes.size() ||
                event.device.layout > GamepadLayout::Nintendo ||
                !Core::isStrictUtf8WithoutNul(event.device.name.view()) ||
                !Core::isStrictUtf8WithoutNul(event.device.guid.view())) {
                resyncPending_ = true;
                return Core::failure(PlatformErrorCode::InvalidInputPayload, "Invalid mobile gamepad identity");
            }
            if (slot == nullptr && frame.remainingPlatformEventCapacity() == 0) { break; }
            if (slot != nullptr && slot->device != event.device) {
                if (frame.remainingPlatformEventCapacity() == 0 || frame.remainingInputTransitionCapacity() == 0) {
                    break;
                }
                if (frame.remainingPlatformEventCapacity() < 2) {
                    if (auto status = appendDisconnect(*slot, frame, window); !status) { return status; }
                    continue;
                }
            }
        } else if (event.kind == MobileGamepadEventKind::Disconnected && slot != nullptr) {
            if (frame.remainingInputTransitionCapacity() == 0 || frame.remainingPlatformEventCapacity() == 0) {
                break;
            }
        } else if (slot != nullptr && inputEnabled && frame.remainingInputTransitionCapacity() == 0) {
            break;
        }
        eventConsumed_ = true;
        if (auto status = applyEvent(event, frame, window, inputEnabled); !status) {
            resyncPending_ = true;
            return status;
        }
        if (!eventConsumed_) { break; }
        (void)queue.tryPop(event);
        if (resyncPending_) { break; }
    }
    if (queue.droppedEventCount() != observedDrops_ || resyncPending_) { return recover(queue, frame, window); }
    publishSnapshots();
    return Core::success();
}

Core::Status MobileGamepadState::recover(MobileGamepadEventQueue& queue, PlatformFrameBuilder& frame,
                                        WindowId window) noexcept
{
    // Reset both streams before retiring identities: retained earlier connect/input events
    // must remain valid even if the corresponding disconnect was the lost event.
    if (auto status = appendInput(frame, InputStreamReset{
            .routedWindow = window, .reason = InputResetReason::BackendRecovery}); !status) { return status; }
    if (auto status = appendStatus(frame.appendPlatformEvent(PlatformEventStreamReset{
            .reason = PlatformEventResetReason::BackendRecovery})); !status) { return status; }
    reset();
    recoveredThisPoll_ = true;
    const u64 dropsAtRecovery = queue.droppedEventCount();
    queue.discard();
    observedDrops_ = dropsAtRecovery;
    queue.requestResync();
    return Core::success();
}

void MobileGamepadState::clearInput() noexcept
{
    for (auto& slot : slots_) {
        if (!slot.active) { continue; }
        if (slot.heldButtons.any() || slot.axes != neutralAxes()) {
            if (slot.revision != (std::numeric_limits<u64>::max)()) { ++slot.revision; }
            else { resyncPending_ = true; }
        }
        slot.heldButtons.reset();
        slot.directButtons.reset();
        slot.axes = neutralAxes();
        slot.hatX = slot.hatY = 0.0F;
    }
    publishSnapshots();
}

void MobileGamepadState::reset() noexcept
{
    pool_.clear();
    slots_.fill(Slot{});
    snapshotCount_ = 0;
    resyncPending_ = false;
    recoveredThisPoll_ = false;
}

void MobileGamepadState::publishSnapshots() noexcept
{
    snapshotCount_ = 0;
    for (const auto& slot : slots_) {
        if (slot.active) {
            snapshots_[snapshotCount_++] = GamepadSnapshot{slot.id, slot.revision, slot.heldButtons, slot.axes};
        }
    }
}

} // namespace Tina::Platform::Detail
