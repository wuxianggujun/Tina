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

[[nodiscard]] Core::Status validateSpec(const UITransitionSpec& spec,
                                        UIAnimatableProperty expectedProperty) noexcept
{
    if (spec.property != expectedProperty) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion transition property does not match API");
    }
    if (!isPaintOnlyAnimatableProperty(spec.property)) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion property is not paint-only");
    }
    if (!isValidUIEasing(spec.easing)) {
        return Core::failure(UIErrorCode::InvalidStyle, "UI motion easing is not recognized");
    }
    if (spec.duration.count() < 0.0 || spec.delay.count() < 0.0 ||
        !std::isfinite(spec.duration.count()) || !std::isfinite(spec.delay.count())) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion duration/delay must be finite and non-negative");
    }
    return Core::success();
}

} // namespace

UIMotionTrackStorage::UIMotionTrackStorage(usize trackCapacity, std::pmr::memory_resource& resource)
    : tracks_(&resource), freeList_(&resource), availableCount_(trackCapacity),
      lastCompleted_(&resource), candidateStyleActivationSlots_(&resource)
{
    tracks_.resize(trackCapacity);
    freeList_.resize(trackCapacity, InvalidSlot);
    lastCompleted_.reserve(trackCapacity);
    candidateStyleActivationSlots_.reserve(trackCapacity);
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

usize UIMotionTrackStorage::availableCount() const noexcept
{
    return availableCount_;
}

usize UIMotionTrackStorage::reservedCount() const noexcept
{
    return reservedCount_;
}

usize UIMotionTrackStorage::reservedHighWater() const noexcept
{
    return reservedHighWater_;
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
    --availableCount_;
    return slot;
}

void UIMotionTrackStorage::freeSlot(u32 slotIndex) noexcept
{
    if (slotIndex >= tracks_.size() || !tracks_[slotIndex].occupied)
    {
        return;
    }
    if (tracks_[slotIndex].active)
    {
        unlinkActive(slotIndex);
    }
    if (tracks_[slotIndex].persistentReservation && reservedCount_ > 0)
    {
        --reservedCount_;
    }
    tracks_[slotIndex] = {};
    freeList_[slotIndex] = freeHead_;
    freeHead_ = slotIndex;
    ++availableCount_;
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
UIMotionTrackStorage::findActive(UINodeId node, UIAnimatableProperty property) const noexcept
{
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        const Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        if (track.node == node && track.property == property) {
            return &track;
        }
        slot = next;
    }
    return nullptr;
}

UIMotionTrackStorage::Track*
UIMotionTrackStorage::findActive(UINodeId node, UIAnimatableProperty property) noexcept
{
    return const_cast<Track*>(
        static_cast<const UIMotionTrackStorage*>(this)->findActive(node, property));
}

const UIMotionTrackStorage::Track* UIMotionTrackStorage::findOccupied(UINodeId node,
                                                                      UIAnimatableProperty property) const noexcept
{
    const auto iterator = std::find_if(tracks_.begin(), tracks_.end(), [node, property](const Track& track) {
        return track.occupied && track.node == node && track.property == property;
    });
    return iterator == tracks_.end() ? nullptr : &*iterator;
}

UIMotionTrackStorage::Track* UIMotionTrackStorage::findOccupied(UINodeId node, UIAnimatableProperty property) noexcept
{
    return const_cast<Track*>(static_cast<const UIMotionTrackStorage*>(this)->findOccupied(node, property));
}

Core::Status UIMotionTrackStorage::beginOrRetargetCommon(
    UINodeId node, UIAnimatableProperty property, ValueKind kind, const UITransitionSpec& spec,
    Core::MonotonicTimePoint now, Track*& outTrack)
{
    outTrack = nullptr;
    if (!node.hasValue()) {
        return Core::failure(UIErrorCode::InvalidNode, "UI motion track requires a live node");
    }
    if (Core::Status status = validateSpec(spec, property); !status) {
        return status;
    }
    if (valueKindForProperty(property) != kind) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion property value kind mismatch");
    }

    if (Track* existing = findOccupied(node, property); existing != nullptr) {
        discardCandidateSample();
        existing = findOccupied(node, property);
        existing->startTime = now;
        existing->duration = spec.duration;
        existing->delay = spec.delay;
        existing->easing = spec.easing;
        existing->completionMode = CompletionMode::CommitProperty;
        linkActive(static_cast<u32>(existing - tracks_.data()));
        outTrack = existing;
        return Core::success();
    }

    if (freeHead_ == InvalidSlot) {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI motion track capacity has been exhausted");
    }
    discardCandidateSample();
    const u32 slot = allocateSlot();
    Track& track = tracks_[slot];
    track = {};
    track.occupied = true;
    track.node = node;
    track.property = property;
    track.valueKind = kind;
    track.startTime = now;
    track.duration = spec.duration;
    track.delay = spec.delay;
    track.easing = spec.easing;
    linkActive(slot);
    outTrack = &track;
    return Core::success();
}

Core::Status UIMotionTrackStorage::beginOrRetargetColor(
    UINodeId node, UIAnimatableProperty property, UIStraightSrgba8Color start,
    UIStraightSrgba8Color target, const UITransitionSpec& spec, Core::MonotonicTimePoint now)
{
    Track* track = nullptr;
    if (Core::Status status =
            beginOrRetargetCommon(node, property, ValueKind::Color, spec, now, track);
        !status) {
        return status;
    }
    track->startColor = start;
    track->targetColor = target;
    track->presentationColor = start;
    return Core::success();
}

Core::Status UIMotionTrackStorage::beginOrRetargetScalar(
    UINodeId node, UIAnimatableProperty property, float start, float target,
    const UITransitionSpec& spec, Core::MonotonicTimePoint now)
{
    if (!std::isfinite(start) || !std::isfinite(target)) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion scalar values must be finite");
    }
    Track* track = nullptr;
    if (Core::Status status =
            beginOrRetargetCommon(node, property, ValueKind::Scalar, spec, now, track);
        !status) {
        return status;
    }
    track->startScalar = start;
    track->targetScalar = target;
    track->presentationScalar = start;
    return Core::success();
}

Core::Status UIMotionTrackStorage::beginOrRetargetOffset(
    UINodeId node, UIAnimatableProperty property, Scalar2 start, Scalar2 target,
    const UITransitionSpec& spec, Core::MonotonicTimePoint now)
{
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(target.x) ||
        !std::isfinite(target.y)) {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI motion offset values must be finite");
    }
    Track* track = nullptr;
    if (Core::Status status =
            beginOrRetargetCommon(node, property, ValueKind::Offset, spec, now, track);
        !status) {
        return status;
    }
    track->startOffset = start;
    track->targetOffset = target;
    track->presentationOffset = start;
    return Core::success();
}

Core::Status UIMotionTrackStorage::reservePersistent(UINodeId node, UIAnimatableProperty property)
{
    if (!node.hasValue())
    {
        return Core::failure(UIErrorCode::InvalidNode, "UI motion reservation requires a live node");
    }
    if (!isPaintOnlyAnimatableProperty(property))
    {
        return Core::failure(UIErrorCode::InvalidStyle, "UI motion reservation requires a paint-only property");
    }
    if (Track* existing = findOccupied(node, property); existing != nullptr)
    {
        discardCandidateSample();
        existing = findOccupied(node, property);
        if (!existing->persistentReservation)
        {
            existing->persistentReservation = true;
            ++reservedCount_;
            reservedHighWater_ = (std::max)(reservedHighWater_, reservedCount_);
        }
        return Core::success();
    }

    if (freeHead_ == InvalidSlot)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI motion track capacity has been exhausted");
    }
    discardCandidateSample();
    const u32 slot = allocateSlot();
    Track& track = tracks_[slot];
    track = {};
    track.node = node;
    track.property = property;
    track.valueKind = valueKindForProperty(property);
    track.occupied = true;
    track.persistentReservation = true;
    ++reservedCount_;
    reservedHighWater_ = (std::max)(reservedHighWater_, reservedCount_);
    return Core::success();
}

Core::Status UIMotionTrackStorage::activateReservedStyleColor(UINodeId node, UIStraightSrgba8Color start,
                                                              UIStraightSrgba8Color target,
                                                              const UITransitionSpec& spec,
                                                              Core::MonotonicTimePoint now)
{
    if (Core::Status valid = validateSpec(spec, UIAnimatableProperty::BackgroundColor); !valid)
    {
        return valid;
    }
    Track* track = findOccupied(node, UIAnimatableProperty::BackgroundColor);
    if (track == nullptr || !track->persistentReservation || track->valueKind != ValueKind::Color)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI style motion track was not reserved during style binding");
    }
    if (candidateSamplePending_)
    {
        const u32 slot = static_cast<u32>(track - tracks_.data());
        if (!track->hasCandidateStyleActivation)
        {
            if (candidateStyleActivationSlots_.size() >=
                candidateStyleActivationSlots_.capacity())
            {
                return Core::failure(
                    UIErrorCode::CapacityExceeded,
                    "UI style motion candidate activation capacity has been exhausted");
            }
            candidateStyleActivationSlots_.push_back(slot);
            track->hasCandidateStyleActivation = true;
        }
        track->candidateStyleStartColor = start;
        track->candidateStyleTargetColor = target;
        track->candidateStyleStartTime = now;
        track->candidateStyleDuration = spec.duration;
        track->candidateStyleDelay = spec.delay;
        track->candidateStyleEasing = spec.easing;
        return Core::success();
    }
    discardCandidateSample();
    track = findOccupied(node, UIAnimatableProperty::BackgroundColor);
    track->startColor = start;
    track->targetColor = target;
    track->presentationColor = start;
    track->startTime = now;
    track->duration = spec.duration;
    track->delay = spec.delay;
    track->easing = spec.easing;
    track->completionMode = CompletionMode::StylePresentationOnly;
    linkActive(static_cast<u32>(track - tracks_.data()));
    return Core::success();
}

bool UIMotionTrackStorage::hasPersistentReservation(UINodeId node, UIAnimatableProperty property) const noexcept
{
    const Track* track = findOccupied(node, property);
    return track != nullptr && track->persistentReservation;
}

bool UIMotionTrackStorage::hasTrack(UINodeId node, UIAnimatableProperty property) const noexcept
{
    return findOccupied(node, property) != nullptr;
}

void UIMotionTrackStorage::releasePersistentReservation(UINodeId node, UIAnimatableProperty property) noexcept
{
    Track* track = findOccupied(node, property);
    if (track == nullptr || !track->persistentReservation)
    {
        return;
    }
    discardCandidateSample();
    track = findOccupied(node, property);
    track->persistentReservation = false;
    if (reservedCount_ > 0)
    {
        --reservedCount_;
    }
    if (!track->active)
    {
        freeSlot(static_cast<u32>(track - tracks_.data()));
    }
}

void UIMotionTrackStorage::releaseAllPersistentReservations(UIAnimatableProperty property) noexcept
{
    discardCandidateSample();
    for (u32 slot = 0; slot < tracks_.size(); ++slot)
    {
        Track& track = tracks_[slot];
        if (!track.occupied || !track.persistentReservation || track.property != property)
        {
            continue;
        }
        track.persistentReservation = false;
        if (reservedCount_ > 0)
        {
            --reservedCount_;
        }
        if (!track.active)
        {
            freeSlot(slot);
        }
    }
}

void UIMotionTrackStorage::cancelActiveProperty(UINodeId node, UIAnimatableProperty property) noexcept
{
    Track* track = findOccupied(node, property);
    if (track == nullptr)
    {
        return;
    }
    discardCandidateSample();
    track = findOccupied(node, property);
    const u32 slot = static_cast<u32>(track - tracks_.data());
    if (track->active)
    {
        unlinkActive(slot);
    }
    if (!track->persistentReservation)
    {
        freeSlot(slot);
    }
}

void UIMotionTrackStorage::snapAllActive() noexcept
{
    discardCandidateSample();
    lastCompleted_.clear();
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        Completed completed{
            .node = track.node,
            .property = track.property,
            .valueKind = track.valueKind,
            .completionMode = track.completionMode,
        };
        switch (track.valueKind) {
        case ValueKind::Color:
            track.presentationColor = track.targetColor;
            completed.color = track.targetColor;
            break;
        case ValueKind::Scalar:
            track.presentationScalar = track.targetScalar;
            completed.scalar = track.targetScalar;
            break;
        case ValueKind::Offset:
            track.presentationOffset = track.targetOffset;
            completed.offset = track.targetOffset;
            break;
        }
        if (lastCompleted_.size() < lastCompleted_.capacity()) {
            lastCompleted_.push_back(completed);
        }
        unlinkActive(slot);
        if (!track.persistentReservation)
        {
            freeSlot(slot);
        }
        slot = next;
    }
    lastSampledCount_ = 0;
}

void UIMotionTrackStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue()) {
        return;
    }
    discardCandidateSample();
    for (u32 slot = 0; slot < tracks_.size(); ++slot)
    {
        Track& track = tracks_[slot];
        if (track.occupied && track.node == node)
        {
            freeSlot(slot);
        }
    }
}

void UIMotionTrackStorage::releaseProperty(UINodeId node, UIAnimatableProperty property) noexcept
{
    if (!node.hasValue())
    {
        return;
    }
    discardCandidateSample();
    for (u32 slot = 0; slot < tracks_.size(); ++slot)
    {
        Track& track = tracks_[slot];
        if (track.occupied && track.node == node && track.property == property)
        {
            freeSlot(slot);
        }
    }
}

std::span<const UIMotionTrackStorage::Completed> UIMotionTrackStorage::lastCompleted() const noexcept
{
    return lastCompleted_;
}

void UIMotionTrackStorage::beginCandidateSample(Core::MonotonicTimePoint now) noexcept
{
    rollbackCandidateSample();
    lastCompleted_.clear();
    usize sampled = 0;
    if (activeHead_ == InvalidSlot && candidateStyleActivationSlots_.empty())
    {
        lastSampledCount_ = 0;
        return;
    }
    candidateSamplePending_ = true;
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        ++sampled;
        const float linear = progressAt(track, now);
        const float eased = evaluateUIEasing(track.easing, linear);
        switch (track.valueKind) {
        case ValueKind::Color:
            track.candidatePresentationColor =
                lerpStraightSrgba8(track.startColor, track.targetColor, eased);
            if (linear >= 1.0F) {
                track.candidatePresentationColor = track.targetColor;
            }
            break;
        case ValueKind::Scalar:
            track.candidatePresentationScalar =
                lerpFloat(track.startScalar, track.targetScalar, eased);
            if (linear >= 1.0F) {
                track.candidatePresentationScalar = track.targetScalar;
            }
            break;
        case ValueKind::Offset:
            track.candidatePresentationOffset = {
                .x = lerpFloat(track.startOffset.x, track.targetOffset.x, eased),
                .y = lerpFloat(track.startOffset.y, track.targetOffset.y, eased),
            };
            if (linear >= 1.0F) {
                track.candidatePresentationOffset = track.targetOffset;
            }
            break;
        }
        track.hasCandidatePresentation = true;
        track.candidateCompleted = linear >= 1.0F;
        slot = next;
    }
    for (const u32 slot : candidateStyleActivationSlots_)
    {
        if (slot >= tracks_.size())
        {
            continue;
        }
        Track& track = tracks_[slot];
        if (!track.occupied || !track.hasCandidateStyleActivation)
        {
            continue;
        }
        if (track.hasCandidatePresentation)
        {
            track.candidateStyleStartColor = track.candidatePresentationColor;
        }
        track.candidateStyleStartTime = now;
    }
    lastSampledCount_ = sampled;
}

void UIMotionTrackStorage::ensureCandidateTransaction() noexcept
{
    candidateSamplePending_ = true;
}

void UIMotionTrackStorage::commitCandidateSample() noexcept
{
    if (!candidateSamplePending_)
    {
        return;
    }
    lastCompleted_.clear();
    for (u32 slot = activeHead_; slot != InvalidSlot;)
    {
        Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        if (!track.hasCandidatePresentation)
        {
            slot = next;
            continue;
        }
        Completed completed{
            .node = track.node,
            .property = track.property,
            .valueKind = track.valueKind,
            .completionMode = track.completionMode,
        };
        switch (track.valueKind)
        {
        case ValueKind::Color:
            track.presentationColor = track.candidatePresentationColor;
            completed.color = track.candidatePresentationColor;
            break;
        case ValueKind::Scalar:
            track.presentationScalar = track.candidatePresentationScalar;
            completed.scalar = track.candidatePresentationScalar;
            break;
        case ValueKind::Offset:
            track.presentationOffset = track.candidatePresentationOffset;
            completed.offset = track.candidatePresentationOffset;
            break;
        }
        const bool completedNow =
            track.candidateCompleted && !track.hasCandidateStyleActivation;
        track.hasCandidatePresentation = false;
        track.candidateCompleted = false;
        if (completedNow)
        {
            if (lastCompleted_.size() < lastCompleted_.capacity())
            {
                lastCompleted_.push_back(completed);
            }
            unlinkActive(slot);
            if (!track.persistentReservation)
            {
                freeSlot(slot);
            }
        }
        slot = next;
    }
    for (const u32 slot : candidateStyleActivationSlots_)
    {
        if (slot >= tracks_.size())
        {
            continue;
        }
        Track& track = tracks_[slot];
        if (!track.occupied || !track.hasCandidateStyleActivation)
        {
            continue;
        }
        track.startColor = track.candidateStyleStartColor;
        track.targetColor = track.candidateStyleTargetColor;
        track.presentationColor = track.candidateStyleStartColor;
        track.startTime = track.candidateStyleStartTime;
        track.duration = track.candidateStyleDuration;
        track.delay = track.candidateStyleDelay;
        track.easing = track.candidateStyleEasing;
        track.completionMode = CompletionMode::StylePresentationOnly;
        track.hasCandidateStyleActivation = false;
        linkActive(slot);
    }
    candidateStyleActivationSlots_.clear();
    candidateSamplePending_ = false;
}

void UIMotionTrackStorage::rollbackCandidateSample() noexcept
{
    if (!candidateSamplePending_)
    {
        return;
    }
    for (u32 slot = activeHead_; slot != InvalidSlot; slot = tracks_[slot].nextActive)
    {
        tracks_[slot].hasCandidatePresentation = false;
        tracks_[slot].candidateCompleted = false;
    }
    candidateSamplePending_ = false;
}

void UIMotionTrackStorage::discardCandidateSample() noexcept
{
    rollbackCandidateSample();
    for (const u32 slot : candidateStyleActivationSlots_)
    {
        if (slot < tracks_.size())
        {
            tracks_[slot].hasCandidateStyleActivation = false;
        }
    }
    candidateStyleActivationSlots_.clear();
}

bool UIMotionTrackStorage::hasCandidateSample() const noexcept
{
    return candidateSamplePending_;
}

usize UIMotionTrackStorage::sample(Core::MonotonicTimePoint now) noexcept
{
    beginCandidateSample(now);
    commitCandidateSample();
    return activeCount_;
}

UIMotionTrackStorage::NodePresentation
UIMotionTrackStorage::presentationFor(UINodeId node) const noexcept
{
    NodePresentation presentation{};
    if (!node.hasValue()) {
        return presentation;
    }
    for (u32 slot = activeHead_; slot != InvalidSlot;) {
        const Track& track = tracks_[slot];
        const u32 next = track.nextActive;
        if (track.node != node) {
            slot = next;
            continue;
        }
        switch (track.property) {
        case UIAnimatableProperty::BackgroundColor:
            presentation.hasBackgroundColor = true;
            presentation.backgroundColor =
                candidateSamplePending_ && track.hasCandidateStyleActivation
                    ? track.candidateStyleStartColor
                : candidateSamplePending_ && track.hasCandidatePresentation
                    ? track.candidatePresentationColor
                    : track.presentationColor;
            break;
        case UIAnimatableProperty::BorderColor:
            presentation.hasBorderColor = true;
            presentation.borderColor = candidateSamplePending_ && track.hasCandidatePresentation
                                           ? track.candidatePresentationColor
                                           : track.presentationColor;
            break;
        case UIAnimatableProperty::TextColor:
            presentation.hasTextColor = true;
            presentation.textColor = candidateSamplePending_ && track.hasCandidatePresentation
                                         ? track.candidatePresentationColor
                                         : track.presentationColor;
            break;
        case UIAnimatableProperty::Opacity:
            presentation.hasOpacity = true;
            presentation.opacity = candidateSamplePending_ && track.hasCandidatePresentation
                                       ? track.candidatePresentationScalar
                                       : track.presentationScalar;
            break;
        case UIAnimatableProperty::CornerRadius:
            presentation.hasCornerRadius = true;
            presentation.cornerRadius = candidateSamplePending_ && track.hasCandidatePresentation
                                            ? track.candidatePresentationScalar
                                            : track.presentationScalar;
            break;
        case UIAnimatableProperty::VisualOffset:
            presentation.hasVisualOffset = true;
            presentation.visualOffset = candidateSamplePending_ && track.hasCandidatePresentation
                                            ? track.candidatePresentationOffset
                                            : track.presentationOffset;
            break;
        case UIAnimatableProperty::LayoutWidth:
        case UIAnimatableProperty::LayoutHeight:
        case UIAnimatableProperty::LayoutOffset:
            // Direct UI-MOTION-001 tracks remain paint-only. Layout properties
            // are owned exclusively by keyframe timelines.
            break;
        }
        slot = next;
    }
    if (candidateSamplePending_)
    {
        for (const u32 slot : candidateStyleActivationSlots_)
        {
            if (slot >= tracks_.size())
            {
                continue;
            }
            const Track& track = tracks_[slot];
            if (track.occupied && track.hasCandidateStyleActivation &&
                track.node == node)
            {
                presentation.hasBackgroundColor = true;
                presentation.backgroundColor = track.candidateStyleStartColor;
            }
        }
    }
    return presentation;
}

} // namespace Tina::UI::Detail
