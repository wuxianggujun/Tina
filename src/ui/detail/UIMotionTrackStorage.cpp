#include "UIMotionTrackStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] float progressAt(const UIMotionTrackStorage::Track& track,
                               Core::MonotonicTimePoint now) noexcept
{
    const Core::Duration elapsed = Core::durationBetween(track.startTime, now);
    const double delaySeconds = track.delay.count();
    const double durationSeconds = track.duration.count();
    if (elapsed.count() <= delaySeconds) {
        return 0.0F;
    }
    if (durationSeconds <= 0.0) {
        return 1.0F;
    }
    const double t = (elapsed.count() - delaySeconds) / durationSeconds;
    if (t <= 0.0) {
        return 0.0F;
    }
    if (t >= 1.0) {
        return 1.0F;
    }
    return static_cast<float>(t);
}

} // namespace

UIMotionTrackStorage::UIMotionTrackStorage(usize trackCapacity, std::pmr::memory_resource& resource)
    : tracks_(&resource), freeList_(&resource), lastCompleted_(&resource)
{
    tracks_.resize(trackCapacity);
    freeList_.resize(trackCapacity, InvalidSlot);
    lastCompleted_.reserve(trackCapacity);
    if (trackCapacity == 0) {
        return;
    }
    for (usize index = 0; index + 1U < trackCapacity; ++index) {
        freeList_[index] = static_cast<u32>(index + 1U);
    }
    freeList_[trackCapacity - 1U] = InvalidSlot;
    freeHead_ = 0;
}

usize UIMotionTrackStorage::capacity() const noexcept
{
    return tracks_.size();
}

usize UIMotionTrackStorage::activeCount() const noexcept
{
    return activeCount_;
}

usize UIMotionTrackStorage::highWater() const noexcept
{
    return highWater_;
}

usize UIMotionTrackStorage::lastSampledCount() const noexcept
{
    return lastSampledCount_;
}

u32 UIMotionTrackStorage::allocateSlot() noexcept
{
    if (freeHead_ == InvalidSlot) {
        return InvalidSlot;
    }
    const u32 slot = freeHead_;
    freeHead_ = freeList_[slot];
    freeList_[slot] = InvalidSlot;
    return slot;
}

void UIMotionTrackStorage::freeSlot(u32 slotIndex) noexcept
{
    if (slotIndex >= tracks_.size()) {
        return;
    }
    tracks_[slotIndex] = {};
    freeList_[slotIndex] = freeHead_;
    freeHead_ = slotIndex;
}

void UIMotionTrackStorage::unlinkActive(u32 slotIndex) noexcept
{
    if (slotIndex >= tracks_.size() || !tracks_[slotIndex].active) {
        return;
    }
    Track& track = tracks_[slotIndex];
    if (track.prevActive == InvalidSlot) {
        activeHead_ = track.nextActive;
    } else {
        tracks_[track.prevActive].nextActive = track.nextActive;
    }
    if (track.nextActive != InvalidSlot) {
        tracks_[track.nextActive].prevActive = track.prevActive;
    }
    track.nextActive = InvalidSlot;
    track.prevActive = InvalidSlot;
    track.active = false;
    if (activeCount_ > 0) {
        --activeCount_;
    }
}

void UIMotionTrackStorage::linkActive(u32 slotIndex) noexcept
{
    if (slotIndex >= tracks_.size()) {
        return;
    }
    Track& track = tracks_[slotIndex];
    if (track.active) {
        return;
    }
    track.prevActive = InvalidSlot;
    track.nextActive = activeHead_;
    if (activeHead_ != InvalidSlot) {
        tracks_[activeHead_].prevActive = slotIndex;
    }
    activeHead_ = slotIndex;
    track.active = true;
    ++activeCount_;
    highWater_ = (std::max)(highWater_, activeCount_);
}

const UIMotionTrackStorage::Track*
UIMotionTrackStorage::findActiveBackgroundColor(UINodeId node) const noexcept
{
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        const Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        if (track.node == node && track.property == UIAnimatableProperty::BackgroundColor) {
            return &track;
        }
        slot = next;
    }
    return nullptr;
}

UIMotionTrackStorage::Track* UIMotionTrackStorage::findActiveBackgroundColor(UINodeId node) noexcept
{
    return const_cast<Track*>(
        static_cast<const UIMotionTrackStorage*>(this)->findActiveBackgroundColor(node));
}

Core::Status UIMotionTrackStorage::beginOrRetargetBackgroundColor(
    UINodeId node, UIStraightSrgba8Color start, UIStraightSrgba8Color target,
    const UITransitionSpec& spec, Core::MonotonicTimePoint now)
{
    if (!node.hasValue()) {
        return Core::failure(UIErrorCode::InvalidNode, "UI motion track requires a live node");
    }
    if (spec.property != UIAnimatableProperty::BackgroundColor) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion first slice only supports BackgroundColor");
    }
    if (!isValidUIEasing(spec.easing)) {
        return Core::failure(UIErrorCode::InvalidStyle, "UI motion easing is not recognized");
    }
    if (spec.duration.count() < 0.0 || spec.delay.count() < 0.0 ||
        !std::isfinite(spec.duration.count()) || !std::isfinite(spec.delay.count())) {
        return Core::failure(UIErrorCode::InvalidStyle, "UI motion duration/delay must be finite and non-negative");
    }

    if (Track* existing = findActiveBackgroundColor(node); existing != nullptr) {
        existing->startColor = start;
        existing->targetColor = target;
        existing->presentationColor = start;
        existing->startTime = now;
        existing->duration = spec.duration;
        existing->delay = spec.delay;
        existing->easing = spec.easing;
        return Core::success();
    }

    const u32 slot = allocateSlot();
    if (slot == InvalidSlot) {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI motion track capacity has been exhausted");
    }
    Track& track = tracks_[slot];
    track = {};
    track.node = node;
    track.property = UIAnimatableProperty::BackgroundColor;
    track.startColor = start;
    track.targetColor = target;
    track.presentationColor = start;
    track.startTime = now;
    track.duration = spec.duration;
    track.delay = spec.delay;
    track.easing = spec.easing;
    linkActive(slot);
    return Core::success();
}

void UIMotionTrackStorage::snapAndDeactivate(u32 slotIndex) noexcept
{
    if (slotIndex >= tracks_.size()) {
        return;
    }
    Track& track = tracks_[slotIndex];
    track.presentationColor = track.targetColor;
    unlinkActive(slotIndex);
    freeSlot(slotIndex);
}

void UIMotionTrackStorage::snapAllActive() noexcept
{
    lastCompleted_.clear();
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        track.presentationColor = track.targetColor;
        if (lastCompleted_.size() < lastCompleted_.capacity()) {
            lastCompleted_.push_back(
                Completed{.node = track.node, .color = track.targetColor});
        }
        unlinkActive(slot);
        freeSlot(slot);
        slot = next;
    }
    lastSampledCount_ = 0;
}

void UIMotionTrackStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue()) {
        return;
    }
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        if (track.node == node) {
            unlinkActive(slot);
            freeSlot(slot);
        }
        slot = next;
    }
}

std::span<const UIMotionTrackStorage::Completed>
UIMotionTrackStorage::lastCompleted() const noexcept
{
    return lastCompleted_;
}

usize UIMotionTrackStorage::sample(Core::MonotonicTimePoint now) noexcept
{
    lastCompleted_.clear();
    usize sampled = 0;
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        ++sampled;
        const float linear = progressAt(track, now);
        const float eased = evaluateUIEasing(track.easing, linear);
        track.presentationColor = lerpStraightSrgba8(track.startColor, track.targetColor, eased);
        if (linear >= 1.0F) {
            track.presentationColor = track.targetColor;
            if (lastCompleted_.size() < lastCompleted_.capacity()) {
                lastCompleted_.push_back(
                    Completed{.node = track.node, .color = track.targetColor});
            }
            unlinkActive(slot);
            freeSlot(slot);
        }
        slot = next;
    }
    lastSampledCount_ = sampled;
    return activeCount_;
}

} // namespace Tina::UI::Detail
