#include "UIKeyframeTimelineStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <string_view>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] u64 nextTimelineOwnerToken() noexcept
{
    static std::atomic<u64> next{1};
    u64 token = next.fetch_add(1, std::memory_order_relaxed);
    // Exhausting a 64-bit process-local identity space would require creating
    // more contexts than the process can represent. Keep zero reserved for an
    // invalid public handle if the counter ever wraps.
    if (token == 0)
    {
        token = next.fetch_add(1, std::memory_order_relaxed);
    }
    return token;
}

[[nodiscard]] Core::Status invalidTimeline(std::string_view message)
{
    return Core::failure(UIErrorCode::InvalidStyle, message);
}

[[nodiscard]] Core::Status timelineCapacityExceeded(std::string_view message)
{
    return Core::failure(UIErrorCode::CapacityExceeded, message);
}

[[nodiscard]] bool validValueForProperty(
    UIAnimatableProperty property, const UIKeyframeValue& value) noexcept
{
    if (value.kind != valueKindForAnimatableProperty(property))
    {
        return false;
    }
    switch (value.kind)
    {
    case UIKeyframeValueKind::Color:
        return true;
    case UIKeyframeValueKind::Scalar:
        if (!std::isfinite(value.scalar))
        {
            return false;
        }
        if (property == UIAnimatableProperty::Opacity)
        {
            return value.scalar >= 0.0F && value.scalar <= 1.0F;
        }
        return (property == UIAnimatableProperty::CornerRadius ||
                property == UIAnimatableProperty::LayoutWidth ||
                property == UIAnimatableProperty::LayoutHeight) &&
               value.scalar >= 0.0F;
    case UIKeyframeValueKind::Offset:
        return (property == UIAnimatableProperty::VisualOffset ||
                property == UIAnimatableProperty::LayoutOffset) &&
               std::isfinite(value.offset.x) && std::isfinite(value.offset.y);
    }
    return false;
}

[[nodiscard]] UIKeyframeValue interpolate(
    const UIKeyframeValue& from, const UIKeyframeValue& to, float progress) noexcept
{
    switch (from.kind)
    {
    case UIKeyframeValueKind::Color:
        return UIKeyframeValue::Color(lerpStraightSrgba8(from.color, to.color, progress));
    case UIKeyframeValueKind::Scalar:
        return UIKeyframeValue::Scalar(from.scalar + (to.scalar - from.scalar) * progress);
    case UIKeyframeValueKind::Offset:
        return UIKeyframeValue::Offset(
            from.offset.x + (to.offset.x - from.offset.x) * progress,
            from.offset.y + (to.offset.y - from.offset.y) * progress);
    }
    return from;
}

} // namespace

UIKeyframeTimelineStorage::UIKeyframeTimelineStorage(
    usize nodeCapacity, usize timelineCapacity, usize trackCapacity,
    usize keyframeCapacity, usize activeTimelineCapacity,
    std::pmr::memory_resource& resource)
    : ownerToken_(nextTimelineOwnerToken()), nodeCapacity_(nodeCapacity),
      activeCapacity_(activeTimelineCapacity),
      timelines_(&resource), tracks_(&resource), keyframes_(&resource),
      activeTimelineIndices_(&resource), ownerTrackByNodeProperty_(&resource),
      lastTargets_(&resource), candidateTrackIndices_(&resource),
      candidateCompletedTimelineIndices_(&resource), candidateLayoutNodes_(&resource)
{
    timelines_.resize(timelineCapacity);
    tracks_.resize(trackCapacity);
    keyframes_.resize(keyframeCapacity);
    activeTimelineIndices_.reserve(activeTimelineCapacity);
    ownerTrackByNodeProperty_.resize(nodeCapacity * PropertyCount, InvalidSlot);
    lastTargets_.reserve(trackCapacity);
    candidateTrackIndices_.reserve(trackCapacity);
    candidateCompletedTimelineIndices_.reserve(activeTimelineCapacity);
    candidateLayoutNodes_.reserve(trackCapacity);

    for (usize index = 0; index < timelines_.size(); ++index)
    {
        timelines_[index].nextFree = index + 1U < timelines_.size()
                                         ? static_cast<u32>(index + 1U)
                                         : InvalidSlot;
    }
    for (usize index = 0; index < tracks_.size(); ++index)
    {
        tracks_[index].nextFree = index + 1U < tracks_.size()
                                      ? static_cast<u32>(index + 1U)
                                      : InvalidSlot;
    }
    for (usize index = 0; index < keyframes_.size(); ++index)
    {
        keyframes_[index].nextFree = index + 1U < keyframes_.size()
                                         ? static_cast<u32>(index + 1U)
                                         : InvalidSlot;
    }
    freeTimelineHead_ = timelines_.empty() ? InvalidSlot : 0U;
    freeTrackHead_ = tracks_.empty() ? InvalidSlot : 0U;
    freeKeyframeHead_ = keyframes_.empty() ? InvalidSlot : 0U;
}

Core::Result<UIKeyframeTimelineStorage::DefinitionCounts>
UIKeyframeTimelineStorage::validateDefinition(
    const UITimelineDesc& desc, usize reclaimTracks, usize reclaimKeyframes) const
{
    if (!std::isfinite(desc.duration.count()) || desc.duration.count() <= 0.0 ||
        !std::isfinite(desc.delay.count()) || desc.delay.count() < 0.0)
    {
        return Core::failure(
            UIErrorCode::InvalidStyle,
            "UI timeline duration must be finite and positive and delay finite and non-negative");
    }
    if (desc.tracks.empty())
    {
        return Core::failure(UIErrorCode::InvalidStyle,
                             "UI timeline must contain at least one track");
    }

    DefinitionCounts counts{.tracks = desc.tracks.size()};
    for (usize trackIndex = 0; trackIndex < desc.tracks.size(); ++trackIndex)
    {
        const UITimelineTrackDesc& track = desc.tracks[trackIndex];
        if (!track.node.hasValue() || !isTimelineAnimatableProperty(track.property))
        {
            return Core::failure(UIErrorCode::InvalidStyle,
                                 "UI timeline track node and animatable property must be valid");
        }
        if (track.keyframes.size() < 2U)
        {
            return Core::failure(UIErrorCode::InvalidStyle,
                                 "UI timeline track requires at least two keyframes");
        }
        if (track.keyframes.size() > (std::numeric_limits<usize>::max)() - counts.keyframes)
        {
            return Core::failure(UIErrorCode::CapacityExceeded,
                                 "UI timeline keyframe count overflowed");
        }
        counts.keyframes += track.keyframes.size();

        if (track.keyframes.front().normalizedTime != 0.0F ||
            track.keyframes.back().normalizedTime != 1.0F)
        {
            return Core::failure(UIErrorCode::InvalidStyle,
                                 "UI timeline keyframes must begin at 0 and end at 1");
        }
        float previousTime = -1.0F;
        for (const UIKeyframe& keyframe : track.keyframes)
        {
            if (!std::isfinite(keyframe.normalizedTime) || keyframe.normalizedTime < 0.0F ||
                keyframe.normalizedTime > 1.0F || keyframe.normalizedTime <= previousTime ||
                !isValidUIEasing(keyframe.easingToNext) ||
                !validValueForProperty(track.property, keyframe.value))
            {
                return Core::failure(
                    UIErrorCode::InvalidStyle,
                    "UI timeline keyframe time, easing, property, and typed value must be valid");
            }
            previousTime = keyframe.normalizedTime;
        }

        for (usize previous = 0; previous < trackIndex; ++previous)
        {
            if (desc.tracks[previous].node == track.node &&
                desc.tracks[previous].property == track.property)
            {
                return Core::failure(
                    UIErrorCode::InvalidStyle,
                    "UI timeline cannot bind the same node property more than once");
            }
        }
    }

    const usize retainedTracks = trackCount_ - reclaimTracks;
    const usize retainedKeyframes = keyframeCount_ - reclaimKeyframes;
    if (counts.tracks > tracks_.size() - retainedTracks)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI timeline track capacity has been exhausted");
    }
    if (counts.keyframes > keyframes_.size() - retainedKeyframes)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI timeline keyframe capacity has been exhausted");
    }
    return counts;
}

UIKeyframeTimelineStorage::TimelineSlot*
UIKeyframeTimelineStorage::resolve(UITimelineId timeline) noexcept
{
    return const_cast<TimelineSlot*>(
        static_cast<const UIKeyframeTimelineStorage*>(this)->resolve(timeline));
}

const UIKeyframeTimelineStorage::TimelineSlot*
UIKeyframeTimelineStorage::resolve(UITimelineId timeline) const noexcept
{
    if (!timeline.hasValue() || timeline.ownerToken_ != ownerToken_ ||
        timeline.index_ >= timelines_.size())
    {
        return nullptr;
    }
    const TimelineSlot& slot = timelines_[timeline.index_];
    if (!slot.occupied || slot.generation != timeline.generation_)
    {
        return nullptr;
    }
    return &slot;
}

UITimelineId UIKeyframeTimelineStorage::idFor(u32 timelineIndex) const noexcept
{
    if (timelineIndex >= timelines_.size() || !timelines_[timelineIndex].occupied)
    {
        return {};
    }
    return UITimelineId::create(
        ownerToken_, timelineIndex, timelines_[timelineIndex].generation);
}

u32 UIKeyframeTimelineStorage::allocateTimeline() noexcept
{
    if (freeTimelineHead_ == InvalidSlot)
    {
        return InvalidSlot;
    }
    const u32 index = freeTimelineHead_;
    TimelineSlot& slot = timelines_[index];
    freeTimelineHead_ = slot.nextFree;
    const u32 generation = slot.generation;
    slot = TimelineSlot{};
    slot.generation = generation;
    slot.occupied = true;
    ++timelineCount_;
    timelineHighWater_ = (std::max)(timelineHighWater_, timelineCount_);
    return index;
}

void UIKeyframeTimelineStorage::releaseTimeline(u32 timelineIndex) noexcept
{
    TimelineSlot& slot = timelines_[timelineIndex];
    const u32 generation = slot.generation;
    slot = TimelineSlot{};
    --timelineCount_;
    if (generation == (std::numeric_limits<u32>::max)())
    {
        slot.generation = generation;
        slot.retired = true;
        return;
    }
    slot.generation = generation + 1U;
    slot.nextFree = freeTimelineHead_;
    freeTimelineHead_ = timelineIndex;
}

u32 UIKeyframeTimelineStorage::allocateTrack() noexcept
{
    if (freeTrackHead_ == InvalidSlot)
    {
        return InvalidSlot;
    }
    const u32 index = freeTrackHead_;
    TrackSlot& slot = tracks_[index];
    freeTrackHead_ = slot.nextFree;
    slot = TrackSlot{};
    slot.occupied = true;
    ++trackCount_;
    trackHighWater_ = (std::max)(trackHighWater_, trackCount_);
    return index;
}

void UIKeyframeTimelineStorage::releaseTrack(u32 trackIndex) noexcept
{
    TrackSlot& slot = tracks_[trackIndex];
    slot = TrackSlot{};
    slot.nextFree = freeTrackHead_;
    freeTrackHead_ = trackIndex;
    --trackCount_;
}

u32 UIKeyframeTimelineStorage::allocateKeyframe() noexcept
{
    if (freeKeyframeHead_ == InvalidSlot)
    {
        return InvalidSlot;
    }
    const u32 index = freeKeyframeHead_;
    KeyframeSlot& slot = keyframes_[index];
    freeKeyframeHead_ = slot.nextFree;
    slot = KeyframeSlot{};
    slot.occupied = true;
    ++keyframeCount_;
    keyframeHighWater_ = (std::max)(keyframeHighWater_, keyframeCount_);
    return index;
}

void UIKeyframeTimelineStorage::releaseKeyframe(u32 keyframeIndex) noexcept
{
    KeyframeSlot& slot = keyframes_[keyframeIndex];
    slot = KeyframeSlot{};
    slot.nextFree = freeKeyframeHead_;
    freeKeyframeHead_ = keyframeIndex;
    --keyframeCount_;
}

void UIKeyframeTimelineStorage::copyDefinition(
    u32 timelineIndex, const UITimelineDesc& desc) noexcept
{
    TimelineSlot& timeline = timelines_[timelineIndex];
    timeline.duration = desc.duration;
    timeline.delay = desc.delay;
    for (const UITimelineTrackDesc& sourceTrack : desc.tracks)
    {
        const u32 trackIndex = allocateTrack();
        TrackSlot& track = tracks_[trackIndex];
        track.node = sourceTrack.node;
        track.property = sourceTrack.property;
        track.valueKind = valueKindForAnimatableProperty(sourceTrack.property);
        track.timelineIndex = timelineIndex;
        if (timeline.firstTrack == InvalidSlot)
        {
            timeline.firstTrack = trackIndex;
        }
        else
        {
            tracks_[timeline.lastTrack].nextInTimeline = trackIndex;
        }
        timeline.lastTrack = trackIndex;
        ++timeline.trackCount;

        for (const UIKeyframe& sourceKeyframe : sourceTrack.keyframes)
        {
            const u32 keyframeIndex = allocateKeyframe();
            KeyframeSlot& keyframe = keyframes_[keyframeIndex];
            keyframe.keyframe = sourceKeyframe;
            if (track.firstKeyframe == InvalidSlot)
            {
                track.firstKeyframe = keyframeIndex;
            }
            else
            {
                keyframes_[track.lastKeyframe].nextInTrack = keyframeIndex;
            }
            track.lastKeyframe = keyframeIndex;
            ++track.keyframeCount;
            ++timeline.keyframeCount;
        }
        track.presentation = firstValue(track);
    }
}

void UIKeyframeTimelineStorage::freeDefinition(TimelineSlot& timeline) noexcept
{
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;)
    {
        TrackSlot& track = tracks_[trackIndex];
        const u32 nextTrack = track.nextInTimeline;
        for (u32 keyframeIndex = track.firstKeyframe; keyframeIndex != InvalidSlot;)
        {
            const u32 nextKeyframe = keyframes_[keyframeIndex].nextInTrack;
            releaseKeyframe(keyframeIndex);
            keyframeIndex = nextKeyframe;
        }
        releaseTrack(trackIndex);
        trackIndex = nextTrack;
    }
    timeline.firstTrack = InvalidSlot;
    timeline.lastTrack = InvalidSlot;
    timeline.trackCount = 0;
    timeline.keyframeCount = 0;
}

Core::Result<UITimelineId> UIKeyframeTimelineStorage::create(const UITimelineDesc& desc)
{
    if (timelineCount_ >= timelines_.size() || freeTimelineHead_ == InvalidSlot)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI timeline definition capacity has been exhausted");
    }
    auto counts = validateDefinition(desc);
    if (!counts)
    {
        return Core::failure(counts.error());
    }
    const u32 timelineIndex = allocateTimeline();
    copyDefinition(timelineIndex, desc);
    return idFor(timelineIndex);
}

Core::Status UIKeyframeTimelineStorage::preflightReplace(
    UITimelineId timelineId, const UITimelineDesc& desc) const
{
    const TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    auto counts = validateDefinition(desc, timeline->trackCount, timeline->keyframeCount);
    return counts ? Core::success() : Core::failure(counts.error());
}

Core::Status UIKeyframeTimelineStorage::replace(
    UITimelineId timelineId, const UITimelineDesc& desc)
{
    TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    auto counts = validateDefinition(desc, timeline->trackCount, timeline->keyframeCount);
    if (!counts)
    {
        return Core::failure(counts.error());
    }

    discardCandidateSample();
    lastTargets_.clear();
    const u32 timelineIndex = timelineId.index_;
    if (timeline->active)
    {
        appendFinalTargets(*timeline);
        deactivate(timelineIndex);
        ++cancelCount_;
    }
    freeDefinition(*timeline);
    copyDefinition(timelineIndex, desc);
    return Core::success();
}

Core::Status UIKeyframeTimelineStorage::preflightPlay(u32 timelineIndex) const
{
    const TimelineSlot& timeline = timelines_[timelineIndex];
    if (!timeline.active && activeTimelineIndices_.size() >= activeCapacity_)
    {
        return timelineCapacityExceeded("UI active timeline index capacity has been exhausted");
    }
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        const TrackSlot& track = tracks_[trackIndex];
        const usize lookup = ownerLookupIndex(track.node, track.property);
        if (lookup == (std::numeric_limits<usize>::max)())
        {
            return invalidTimeline("UI timeline track references a node outside this context capacity");
        }
        const u32 ownerTrack = ownerTrackByNodeProperty_[lookup];
        if (ownerTrack != InvalidSlot && tracks_[ownerTrack].occupied &&
            tracks_[ownerTrack].timelineIndex != timelineIndex)
        {
            return invalidTimeline("UI timeline property already has another presentation owner");
        }
    }
    return Core::success();
}

Core::Status UIKeyframeTimelineStorage::preflightPlay(UITimelineId timelineId) const
{
    const TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    return preflightPlay(timelineId.index_);
}

Core::Status UIKeyframeTimelineStorage::play(
    UITimelineId timelineId, Core::MonotonicTimePoint now)
{
    TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    const u32 timelineIndex = timelineId.index_;
    if (Core::Status status = preflightPlay(timelineIndex); !status)
    {
        return status;
    }
    discardCandidateSample();
    lastTargets_.clear();
    // Publish the authored final values as the underlying targets in the same
    // infallible mutation that acquires presentation ownership. UIContext reads
    // this fixed-capacity scratch immediately after play(); there is no second
    // fallible lookup after the active index has changed.
    appendFinalTargets(*timeline);
    activate(timelineIndex, now);
    return Core::success();
}

void UIKeyframeTimelineStorage::activate(
    u32 timelineIndex, Core::MonotonicTimePoint now) noexcept
{
    TimelineSlot& timeline = timelines_[timelineIndex];
    if (timeline.active)
    {
        const double elapsed = Core::durationBetween(timeline.startTime, now).count();
        const double afterDelay = elapsed - timeline.delay.count();
        const float normalized = afterDelay <= 0.0
                                     ? 0.0F
                                     : static_cast<float>(std::clamp(
                                           afterDelay / timeline.duration.count(), 0.0, 1.0));
        for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
             trackIndex = tracks_[trackIndex].nextInTimeline)
        {
            TrackSlot& track = tracks_[trackIndex];
            // Retarget starts from the absolute-time sample without publishing
            // it. The next UI snapshot transaction decides whether that value
            // becomes the new committed presentation.
            track.effectiveStart = sampleTrackValue(track, normalized).value;
            track.hasEffectiveStart = true;
        }
        ++retargetCount_;
    }
    else
    {
        for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
             trackIndex = tracks_[trackIndex].nextInTimeline)
        {
            TrackSlot& track = tracks_[trackIndex];
            track.presentation = firstValue(track);
            track.hasEffectiveStart = false;
        }
        timeline.active = true;
        timeline.activeIndex = static_cast<u32>(activeTimelineIndices_.size());
        activeTimelineIndices_.push_back(timelineIndex);
        activeHighWater_ = (std::max)(activeHighWater_, activeTimelineIndices_.size());
    }
    timeline.startTime = now;
    publishOwners(timeline);
}

void UIKeyframeTimelineStorage::deactivate(u32 timelineIndex) noexcept
{
    TimelineSlot& timeline = timelines_[timelineIndex];
    if (!timeline.active)
    {
        return;
    }
    clearOwners(timeline);
    const u32 activeIndex = timeline.activeIndex;
    const u32 movedTimeline = activeTimelineIndices_.back();
    activeTimelineIndices_[activeIndex] = movedTimeline;
    activeTimelineIndices_.pop_back();
    if (movedTimeline != timelineIndex)
    {
        timelines_[movedTimeline].activeIndex = activeIndex;
    }
    timeline.active = false;
    timeline.activeIndex = InvalidSlot;
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        tracks_[trackIndex].hasEffectiveStart = false;
    }
}

Core::Status UIKeyframeTimelineStorage::snapToFinal(UITimelineId timelineId) noexcept
{
    TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    discardCandidateSample();
    lastTargets_.clear();
    appendFinalTargets(*timeline);
    if (timeline->active)
    {
        ++retargetCount_;
        deactivate(timelineId.index_);
    }
    return Core::success();
}

Core::Status UIKeyframeTimelineStorage::cancel(UITimelineId timelineId) noexcept
{
    TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    discardCandidateSample();
    lastTargets_.clear();
    if (!timeline->active)
    {
        return Core::success();
    }
    appendFinalTargets(*timeline);
    deactivate(timelineId.index_);
    ++cancelCount_;
    return Core::success();
}

Core::Status UIKeyframeTimelineStorage::destroy(UITimelineId timelineId) noexcept
{
    TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    discardCandidateSample();
    lastTargets_.clear();
    const u32 timelineIndex = timelineId.index_;
    if (timeline->active)
    {
        appendFinalTargets(*timeline);
        deactivate(timelineIndex);
        ++cancelCount_;
    }
    freeDefinition(*timeline);
    releaseTimeline(timelineIndex);
    return Core::success();
}

bool UIKeyframeTimelineStorage::contains(UITimelineId timeline) const noexcept
{
    return resolve(timeline) != nullptr;
}

Core::Result<bool> UIKeyframeTimelineStorage::isActive(UITimelineId timeline) const
{
    const TimelineSlot* slot = resolve(timeline);
    if (slot == nullptr)
    {
        return Core::failure(
            UIErrorCode::InvalidStyle,
            "UI timeline ID is invalid, stale, or belongs to another context");
    }
    return slot->active;
}

Core::Status UIKeyframeTimelineStorage::visitTracks(
    UITimelineId timelineId, void* user, TrackVisitor visitor) const
{
    const TimelineSlot* timeline = resolve(timelineId);
    if (timeline == nullptr)
    {
        return invalidTimeline("UI timeline ID is invalid, stale, or belongs to another context");
    }
    if (visitor == nullptr)
    {
        return invalidTimeline("UI timeline track visitor must be valid");
    }
    for (u32 trackIndex = timeline->firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        const TrackSlot& track = tracks_[trackIndex];
        if (Core::Status status = visitor(
                user,
                TrackView{
                    .node = track.node,
                    .property = track.property,
                    .valueKind = track.valueKind,
                    .finalValue = finalValue(track),
                });
            !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status UIKeyframeTimelineStorage::visitActiveTracks(
    void* user, TrackVisitor visitor) const
{
    if (visitor == nullptr)
    {
        return invalidTimeline("UI active timeline track visitor must be valid");
    }
    for (const u32 timelineIndex : activeTimelineIndices_)
    {
        const TimelineSlot& timeline = timelines_[timelineIndex];
        for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
             trackIndex = tracks_[trackIndex].nextInTimeline)
        {
            const TrackSlot& track = tracks_[trackIndex];
            if (Core::Status status = visitor(
                    user,
                    TrackView{
                        .node = track.node,
                        .property = track.property,
                        .valueKind = track.valueKind,
                        .finalValue = finalValue(track),
                    });
                !status)
            {
                return status;
            }
        }
    }
    return Core::success();
}

UIKeyframeValue UIKeyframeTimelineStorage::firstValue(const TrackSlot& track) const noexcept
{
    return keyframes_[track.firstKeyframe].keyframe.value;
}

UIKeyframeValue UIKeyframeTimelineStorage::finalValue(const TrackSlot& track) const noexcept
{
    return keyframes_[track.lastKeyframe].keyframe.value;
}

UIKeyframeTimelineStorage::SampledTrack UIKeyframeTimelineStorage::sampleTrackValue(
    const TrackSlot& track, float normalizedTime) const noexcept
{
    if (normalizedTime >= 1.0F)
    {
        // The final keyframe is copied directly; no keyframe segment was
        // traversed for this sample.
        return SampledTrack{.value = finalValue(track), .segmentCount = 0};
    }

    u32 lowerIndex = track.firstKeyframe;
    u32 upperIndex = keyframes_[lowerIndex].nextInTrack;
    usize segmentCount = 1;
    while (upperIndex != InvalidSlot &&
           normalizedTime > keyframes_[upperIndex].keyframe.normalizedTime)
    {
        lowerIndex = upperIndex;
        upperIndex = keyframes_[upperIndex].nextInTrack;
        ++segmentCount;
    }
    if (upperIndex == InvalidSlot)
    {
        return SampledTrack{.value = finalValue(track), .segmentCount = segmentCount};
    }

    const UIKeyframe& lower = keyframes_[lowerIndex].keyframe;
    const UIKeyframe& upper = keyframes_[upperIndex].keyframe;
    const float segmentDuration = upper.normalizedTime - lower.normalizedTime;
    const float linear = std::clamp(
        (normalizedTime - lower.normalizedTime) / segmentDuration, 0.0F, 1.0F);
    const float eased = evaluateUIEasing(lower.easingToNext, linear);
    const UIKeyframeValue& from =
        lowerIndex == track.firstKeyframe && track.hasEffectiveStart
            ? track.effectiveStart
            : lower.value;
    return SampledTrack{
        .value = interpolate(from, upper.value, eased),
        .segmentCount = segmentCount,
    };
}

usize UIKeyframeTimelineStorage::sampleTrack(
    TrackSlot& track, float normalizedTime) noexcept
{
    const SampledTrack sampled = sampleTrackValue(track, normalizedTime);
    track.presentation = sampled.value;
    return sampled.segmentCount;
}

usize UIKeyframeTimelineStorage::sampleTimeline(
    u32 timelineIndex, Core::MonotonicTimePoint now) noexcept
{
    TimelineSlot& timeline = timelines_[timelineIndex];
    const double elapsed = Core::durationBetween(timeline.startTime, now).count();
    const double afterDelay = elapsed - timeline.delay.count();
    const float normalized = afterDelay <= 0.0
                                 ? 0.0F
                                 : static_cast<float>(std::clamp(
                                       afterDelay / timeline.duration.count(), 0.0, 1.0));
    usize segmentCount = 0;
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        segmentCount += sampleTrack(tracks_[trackIndex], normalized);
    }
    return segmentCount;
}

void UIKeyframeTimelineStorage::beginCandidateSample(Core::MonotonicTimePoint now) noexcept
{
    discardCandidateSample();
    lastTargets_.clear();
    lastSampledTimelineCount_ = 0;
    lastSampledTrackCount_ = 0;
    lastSampledLayoutTrackCount_ = 0;
    lastSampledSegmentCount_ = 0;

    if (activeTimelineIndices_.empty())
    {
        return;
    }
    candidateSamplePending_ = true;
    for (const u32 timelineIndex : activeTimelineIndices_)
    {
        TimelineSlot& timeline = timelines_[timelineIndex];
        ++lastSampledTimelineCount_;
        lastSampledTrackCount_ += timeline.trackCount;

        const double elapsed = Core::durationBetween(timeline.startTime, now).count();
        const double afterDelay = elapsed - timeline.delay.count();
        const float normalized = afterDelay <= 0.0
                                     ? 0.0F
                                     : static_cast<float>(std::clamp(
                                           afterDelay / timeline.duration.count(), 0.0, 1.0));
        for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
             trackIndex = tracks_[trackIndex].nextInTimeline)
        {
            TrackSlot& track = tracks_[trackIndex];
            const SampledTrack sampled = sampleTrackValue(track, normalized);
            track.candidatePresentation = sampled.value;
            track.hasCandidatePresentation = true;
            candidateTrackIndices_.push_back(trackIndex);
            lastSampledSegmentCount_ += sampled.segmentCount;
            if (isLayoutAnimatableProperty(track.property))
            {
                candidateLayoutNodes_.push_back(track.node);
                ++lastSampledLayoutTrackCount_;
            }
        }

        if (elapsed - timeline.delay.count() >= timeline.duration.count())
        {
            candidateCompletedTimelineIndices_.push_back(timelineIndex);
        }
    }
}

void UIKeyframeTimelineStorage::commitCandidateSample() noexcept
{
    if (!candidateSamplePending_)
    {
        return;
    }
    for (const u32 trackIndex : candidateTrackIndices_)
    {
        TrackSlot& track = tracks_[trackIndex];
        if (!track.occupied || !track.hasCandidatePresentation)
        {
            continue;
        }
        track.presentation = track.candidatePresentation;
        track.hasCandidatePresentation = false;
    }
    for (const u32 timelineIndex : candidateCompletedTimelineIndices_)
    {
        if (timelineIndex >= timelines_.size() || !timelines_[timelineIndex].active)
        {
            continue;
        }
        appendFinalTargets(timelines_[timelineIndex]);
        deactivate(timelineIndex);
    }
    candidateTrackIndices_.clear();
    candidateCompletedTimelineIndices_.clear();
    candidateLayoutNodes_.clear();
    candidateSamplePending_ = false;
}

void UIKeyframeTimelineStorage::discardCandidateSample() noexcept
{
    for (const u32 trackIndex : candidateTrackIndices_)
    {
        if (trackIndex < tracks_.size() && tracks_[trackIndex].occupied)
        {
            tracks_[trackIndex].hasCandidatePresentation = false;
        }
    }
    candidateTrackIndices_.clear();
    candidateCompletedTimelineIndices_.clear();
    candidateLayoutNodes_.clear();
    candidateSamplePending_ = false;
}

bool UIKeyframeTimelineStorage::hasCandidateSample() const noexcept
{
    return candidateSamplePending_;
}

std::span<const UINodeId> UIKeyframeTimelineStorage::candidateLayoutNodes() const noexcept
{
    return std::span<const UINodeId>(candidateLayoutNodes_.data(), candidateLayoutNodes_.size());
}

usize UIKeyframeTimelineStorage::sample(Core::MonotonicTimePoint now) noexcept
{
    beginCandidateSample(now);
    commitCandidateSample();
    return activeTimelineIndices_.size();
}

void UIKeyframeTimelineStorage::appendFinalTargets(
    const TimelineSlot& timeline, UINodeId excludedNode) noexcept
{
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        const TrackSlot& track = tracks_[trackIndex];
        if (excludedNode.hasValue() && track.node == excludedNode)
        {
            continue;
        }
        lastTargets_.push_back(Target{
            .node = track.node,
            .property = track.property,
            .value = finalValue(track),
        });
    }
}

void UIKeyframeTimelineStorage::snapAllActive() noexcept
{
    discardCandidateSample();
    lastTargets_.clear();
    while (!activeTimelineIndices_.empty())
    {
        const u32 timelineIndex = activeTimelineIndices_.back();
        appendFinalTargets(timelines_[timelineIndex]);
        deactivate(timelineIndex);
    }
    lastSampledTimelineCount_ = 0;
    lastSampledTrackCount_ = 0;
    lastSampledLayoutTrackCount_ = 0;
    lastSampledSegmentCount_ = 0;
}

void UIKeyframeTimelineStorage::releaseNode(UINodeId node) noexcept
{
    discardCandidateSample();
    lastTargets_.clear();
    if (!node.hasValue())
    {
        return;
    }
    usize activeIndex = 0;
    while (activeIndex < activeTimelineIndices_.size())
    {
        const u32 timelineIndex = activeTimelineIndices_[activeIndex];
        TimelineSlot& timeline = timelines_[timelineIndex];
        bool bindsNode = false;
        for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
             trackIndex = tracks_[trackIndex].nextInTimeline)
        {
            if (tracks_[trackIndex].node == node)
            {
                bindsNode = true;
                break;
            }
        }
        if (!bindsNode)
        {
            ++activeIndex;
            continue;
        }
        appendFinalTargets(timeline, node);
        deactivate(timelineIndex);
        ++cancelCount_;
    }
}

usize UIKeyframeTimelineStorage::ownerLookupIndex(
    UINodeId node, UIAnimatableProperty property) const noexcept
{
    const usize propertyIndex = static_cast<usize>(property);
    if (!node.hasValue() || node.index() >= nodeCapacity_ || propertyIndex >= PropertyCount)
    {
        return (std::numeric_limits<usize>::max)();
    }
    return static_cast<usize>(node.index()) * PropertyCount + propertyIndex;
}

void UIKeyframeTimelineStorage::publishOwners(const TimelineSlot& timeline) noexcept
{
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        const TrackSlot& track = tracks_[trackIndex];
        ownerTrackByNodeProperty_[ownerLookupIndex(track.node, track.property)] = trackIndex;
    }
}

void UIKeyframeTimelineStorage::clearOwners(const TimelineSlot& timeline) noexcept
{
    for (u32 trackIndex = timeline.firstTrack; trackIndex != InvalidSlot;
         trackIndex = tracks_[trackIndex].nextInTimeline)
    {
        const TrackSlot& track = tracks_[trackIndex];
        const usize lookup = ownerLookupIndex(track.node, track.property);
        if (lookup != (std::numeric_limits<usize>::max)() &&
            ownerTrackByNodeProperty_[lookup] == trackIndex)
        {
            ownerTrackByNodeProperty_[lookup] = InvalidSlot;
        }
    }
}

bool UIKeyframeTimelineStorage::hasPresentationOwner(
    UINodeId node, UIAnimatableProperty property, UITimelineId exceptTimeline) const noexcept
{
    const usize lookup = ownerLookupIndex(node, property);
    if (lookup == (std::numeric_limits<usize>::max)())
    {
        return false;
    }
    const u32 trackIndex = ownerTrackByNodeProperty_[lookup];
    if (trackIndex == InvalidSlot || !tracks_[trackIndex].occupied)
    {
        return false;
    }
    const TrackSlot& track = tracks_[trackIndex];
    if (!timelines_[track.timelineIndex].active)
    {
        return false;
    }
    return !exceptTimeline.hasValue() || idFor(track.timelineIndex) != exceptTimeline;
}

UIKeyframeTimelineStorage::NodePresentation
UIKeyframeTimelineStorage::presentationFor(UINodeId node) const noexcept
{
    NodePresentation result{};
    for (usize propertyIndex = 0; propertyIndex < PropertyCount; ++propertyIndex)
    {
        const auto property = static_cast<UIAnimatableProperty>(propertyIndex);
        const usize lookup = ownerLookupIndex(node, property);
        if (lookup == (std::numeric_limits<usize>::max)())
        {
            continue;
        }
        const u32 trackIndex = ownerTrackByNodeProperty_[lookup];
        if (trackIndex == InvalidSlot || !tracks_[trackIndex].occupied)
        {
            continue;
        }
        const TrackSlot& track = tracks_[trackIndex];
        if (!timelines_[track.timelineIndex].active || track.node != node)
        {
            continue;
        }
        const UIKeyframeValue& presentation =
            candidateSamplePending_ && track.hasCandidatePresentation
                ? track.candidatePresentation
                : track.presentation;
        switch (property)
        {
        case UIAnimatableProperty::BackgroundColor:
            result.hasBackgroundColor = true;
            result.backgroundColor = presentation.color;
            break;
        case UIAnimatableProperty::BorderColor:
            result.hasBorderColor = true;
            result.borderColor = presentation.color;
            break;
        case UIAnimatableProperty::TextColor:
            result.hasTextColor = true;
            result.textColor = presentation.color;
            break;
        case UIAnimatableProperty::Opacity:
            result.hasOpacity = true;
            result.opacity = presentation.scalar;
            break;
        case UIAnimatableProperty::CornerRadius:
            result.hasCornerRadius = true;
            result.cornerRadius = presentation.scalar;
            break;
        case UIAnimatableProperty::VisualOffset:
            result.hasVisualOffset = true;
            result.visualOffset = presentation.offset;
            break;
        case UIAnimatableProperty::LayoutWidth:
            result.hasLayoutWidth = true;
            result.layoutWidth = presentation.scalar;
            break;
        case UIAnimatableProperty::LayoutHeight:
            result.hasLayoutHeight = true;
            result.layoutHeight = presentation.scalar;
            break;
        case UIAnimatableProperty::LayoutOffset:
            result.hasLayoutOffset = true;
            result.layoutOffset = presentation.offset;
            break;
        }
    }
    return result;
}

std::span<const UIKeyframeTimelineStorage::Target>
UIKeyframeTimelineStorage::lastTargets() const noexcept
{
    return lastTargets_;
}

usize UIKeyframeTimelineStorage::timelineCapacity() const noexcept { return timelines_.size(); }
usize UIKeyframeTimelineStorage::timelineCount() const noexcept { return timelineCount_; }
usize UIKeyframeTimelineStorage::timelineHighWater() const noexcept { return timelineHighWater_; }
usize UIKeyframeTimelineStorage::trackCapacity() const noexcept { return tracks_.size(); }
usize UIKeyframeTimelineStorage::trackCount() const noexcept { return trackCount_; }
usize UIKeyframeTimelineStorage::trackHighWater() const noexcept { return trackHighWater_; }
usize UIKeyframeTimelineStorage::keyframeCapacity() const noexcept { return keyframes_.size(); }
usize UIKeyframeTimelineStorage::keyframeCount() const noexcept { return keyframeCount_; }
usize UIKeyframeTimelineStorage::keyframeHighWater() const noexcept { return keyframeHighWater_; }
usize UIKeyframeTimelineStorage::activeCapacity() const noexcept
{
    return activeCapacity_;
}
usize UIKeyframeTimelineStorage::activeCount() const noexcept
{
    return activeTimelineIndices_.size();
}
usize UIKeyframeTimelineStorage::activeHighWater() const noexcept { return activeHighWater_; }
usize UIKeyframeTimelineStorage::lastSampledTimelineCount() const noexcept
{
    return lastSampledTimelineCount_;
}
usize UIKeyframeTimelineStorage::lastSampledTrackCount() const noexcept
{
    return lastSampledTrackCount_;
}
usize UIKeyframeTimelineStorage::lastSampledLayoutTrackCount() const noexcept
{
    return lastSampledLayoutTrackCount_;
}
usize UIKeyframeTimelineStorage::lastSampledSegmentCount() const noexcept
{
    return lastSampledSegmentCount_;
}
usize UIKeyframeTimelineStorage::cancelCount() const noexcept { return cancelCount_; }
usize UIKeyframeTimelineStorage::retargetCount() const noexcept { return retargetCount_; }

} // namespace Tina::UI::Detail
