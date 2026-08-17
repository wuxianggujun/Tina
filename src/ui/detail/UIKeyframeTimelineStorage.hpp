#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIMotion.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

// Fixed-capacity retained timeline definitions plus a compact active index.
// Definition mutation never allocates after UIContext::Create; sampling walks
// only active timelines/tracks and never scans retained nodes or definitions.
class UIKeyframeTimelineStorage final {
  public:
    inline static constexpr u32 InvalidSlot = (std::numeric_limits<u32>::max)();
    inline static constexpr usize PropertyCount = 9;

    struct TrackView final {
        UINodeId node{};
        UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
        UIKeyframeValueKind valueKind = UIKeyframeValueKind::Color;
        UIKeyframeValue finalValue{};
    };

    struct Target final {
        UINodeId node{};
        UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
        UIKeyframeValue value{};
    };

    struct NodePresentation final {
        bool hasBackgroundColor = false;
        UIStraightSrgba8Color backgroundColor{};
        bool hasBorderColor = false;
        UIStraightSrgba8Color borderColor{};
        bool hasTextColor = false;
        UIStraightSrgba8Color textColor{};
        bool hasOpacity = false;
        float opacity = 1.0F;
        bool hasCornerRadius = false;
        float cornerRadius = 0.0F;
        bool hasVisualOffset = false;
        UIAnimatableOffset visualOffset{};
        bool hasLayoutWidth = false;
        float layoutWidth = 0.0F;
        bool hasLayoutHeight = false;
        float layoutHeight = 0.0F;
        bool hasLayoutOffset = false;
        UIAnimatableOffset layoutOffset{};
    };

    using TrackVisitor = Core::Status (*)(void* user, const TrackView& track);

    UIKeyframeTimelineStorage(
        usize nodeCapacity, usize timelineCapacity, usize trackCapacity,
        usize keyframeCapacity, usize activeTimelineCapacity,
        std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Result<UITimelineId> create(const UITimelineDesc& desc);
    [[nodiscard]] Core::Status preflightReplace(
        UITimelineId timeline, const UITimelineDesc& desc) const;
    [[nodiscard]] Core::Status replace(UITimelineId timeline, const UITimelineDesc& desc);
    [[nodiscard]] Core::Status preflightPlay(UITimelineId timeline) const;
    [[nodiscard]] Core::Status play(UITimelineId timeline, Core::MonotonicTimePoint now);
    [[nodiscard]] Core::Status snapToFinal(UITimelineId timeline) noexcept;
    [[nodiscard]] Core::Status cancel(UITimelineId timeline) noexcept;
    [[nodiscard]] Core::Status destroy(UITimelineId timeline) noexcept;

    [[nodiscard]] bool contains(UITimelineId timeline) const noexcept;
    [[nodiscard]] Core::Result<bool> isActive(UITimelineId timeline) const;
    [[nodiscard]] Core::Status visitTracks(
        UITimelineId timeline, void* user, TrackVisitor visitor) const;
    [[nodiscard]] Core::Status visitActiveTracks(void* user, TrackVisitor visitor) const;

    [[nodiscard]] bool hasPresentationOwner(
        UINodeId node, UIAnimatableProperty property,
        UITimelineId exceptTimeline = {}) const noexcept;
    [[nodiscard]] NodePresentation presentationFor(UINodeId node) const noexcept;

    // Candidate sampling is fixed-capacity and does not mutate the published
    // presentation or active index until commitCandidateSample(). UIContext
    // uses this transaction to publish layout/hit/paint from one timestamp.
    void beginCandidateSample(Core::MonotonicTimePoint now) noexcept;
    void commitCandidateSample() noexcept;
    void discardCandidateSample() noexcept;
    [[nodiscard]] bool hasCandidateSample() const noexcept;
    [[nodiscard]] std::span<const UINodeId> candidateLayoutNodes() const noexcept;
    [[nodiscard]] usize sample(Core::MonotonicTimePoint now) noexcept;
    void snapAllActive() noexcept;
    void releaseNode(UINodeId node) noexcept;
    [[nodiscard]] std::span<const Target> lastTargets() const noexcept;

    [[nodiscard]] usize timelineCapacity() const noexcept;
    [[nodiscard]] usize timelineCount() const noexcept;
    [[nodiscard]] usize timelineHighWater() const noexcept;
    [[nodiscard]] usize trackCapacity() const noexcept;
    [[nodiscard]] usize trackCount() const noexcept;
    [[nodiscard]] usize trackHighWater() const noexcept;
    [[nodiscard]] usize keyframeCapacity() const noexcept;
    [[nodiscard]] usize keyframeCount() const noexcept;
    [[nodiscard]] usize keyframeHighWater() const noexcept;
    [[nodiscard]] usize activeCapacity() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize activeHighWater() const noexcept;
    [[nodiscard]] usize lastSampledTimelineCount() const noexcept;
    [[nodiscard]] usize lastSampledTrackCount() const noexcept;
    [[nodiscard]] usize lastSampledLayoutTrackCount() const noexcept;
    [[nodiscard]] usize lastSampledSegmentCount() const noexcept;
    [[nodiscard]] usize cancelCount() const noexcept;
    [[nodiscard]] usize retargetCount() const noexcept;

  private:
    struct TimelineSlot final {
        Core::Duration duration{0.0};
        Core::Duration delay{0.0};
        Core::MonotonicTimePoint startTime{};
        u32 generation = 1;
        u32 nextFree = InvalidSlot;
        u32 firstTrack = InvalidSlot;
        u32 lastTrack = InvalidSlot;
        u32 activeIndex = InvalidSlot;
        usize trackCount = 0;
        usize keyframeCount = 0;
        bool occupied = false;
        bool retired = false;
        bool active = false;
    };

    struct TrackSlot final {
        UINodeId node{};
        UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
        UIKeyframeValueKind valueKind = UIKeyframeValueKind::Color;
        UIKeyframeValue presentation{};
        UIKeyframeValue candidatePresentation{};
        UIKeyframeValue effectiveStart{};
        u32 nextFree = InvalidSlot;
        u32 nextInTimeline = InvalidSlot;
        u32 firstKeyframe = InvalidSlot;
        u32 lastKeyframe = InvalidSlot;
        u32 timelineIndex = InvalidSlot;
        usize keyframeCount = 0;
        bool occupied = false;
        bool hasCandidatePresentation = false;
        bool hasEffectiveStart = false;
    };

    struct KeyframeSlot final {
        UIKeyframe keyframe{};
        u32 nextFree = InvalidSlot;
        u32 nextInTrack = InvalidSlot;
        bool occupied = false;
    };

    struct DefinitionCounts final {
        usize tracks = 0;
        usize keyframes = 0;
    };

    struct SampledTrack final {
        UIKeyframeValue value{};
        usize segmentCount = 0;
    };

    [[nodiscard]] Core::Result<DefinitionCounts> validateDefinition(
        const UITimelineDesc& desc, usize reclaimTracks = 0,
        usize reclaimKeyframes = 0) const;
    [[nodiscard]] TimelineSlot* resolve(UITimelineId timeline) noexcept;
    [[nodiscard]] const TimelineSlot* resolve(UITimelineId timeline) const noexcept;
    [[nodiscard]] UITimelineId idFor(u32 timelineIndex) const noexcept;

    [[nodiscard]] u32 allocateTimeline() noexcept;
    void releaseTimeline(u32 timelineIndex) noexcept;
    [[nodiscard]] u32 allocateTrack() noexcept;
    void releaseTrack(u32 trackIndex) noexcept;
    [[nodiscard]] u32 allocateKeyframe() noexcept;
    void releaseKeyframe(u32 keyframeIndex) noexcept;
    void copyDefinition(u32 timelineIndex, const UITimelineDesc& desc) noexcept;
    void freeDefinition(TimelineSlot& timeline) noexcept;

    [[nodiscard]] Core::Status preflightPlay(u32 timelineIndex) const;
    void activate(u32 timelineIndex, Core::MonotonicTimePoint now) noexcept;
    void deactivate(u32 timelineIndex) noexcept;
    [[nodiscard]] usize sampleTimeline(
        u32 timelineIndex, Core::MonotonicTimePoint now) noexcept;
    [[nodiscard]] SampledTrack sampleTrackValue(
        const TrackSlot& track, float normalizedTime) const noexcept;
    [[nodiscard]] usize sampleTrack(TrackSlot& track, float normalizedTime) noexcept;
    void appendFinalTargets(const TimelineSlot& timeline,
                            UINodeId excludedNode = {}) noexcept;

    [[nodiscard]] UIKeyframeValue firstValue(const TrackSlot& track) const noexcept;
    [[nodiscard]] UIKeyframeValue finalValue(const TrackSlot& track) const noexcept;
    [[nodiscard]] usize ownerLookupIndex(
        UINodeId node, UIAnimatableProperty property) const noexcept;
    void publishOwners(const TimelineSlot& timeline) noexcept;
    void clearOwners(const TimelineSlot& timeline) noexcept;

    u64 ownerToken_ = 0;
    usize nodeCapacity_ = 0;
    usize activeCapacity_ = 0;
    std::pmr::vector<TimelineSlot> timelines_;
    std::pmr::vector<TrackSlot> tracks_;
    std::pmr::vector<KeyframeSlot> keyframes_;
    std::pmr::vector<u32> activeTimelineIndices_;
    std::pmr::vector<u32> ownerTrackByNodeProperty_;
    std::pmr::vector<Target> lastTargets_;
    std::pmr::vector<u32> candidateTrackIndices_;
    std::pmr::vector<u32> candidateCompletedTimelineIndices_;
    std::pmr::vector<UINodeId> candidateLayoutNodes_;
    u32 freeTimelineHead_ = InvalidSlot;
    u32 freeTrackHead_ = InvalidSlot;
    u32 freeKeyframeHead_ = InvalidSlot;
    usize timelineCount_ = 0;
    usize timelineHighWater_ = 0;
    usize trackCount_ = 0;
    usize trackHighWater_ = 0;
    usize keyframeCount_ = 0;
    usize keyframeHighWater_ = 0;
    usize activeHighWater_ = 0;
    usize lastSampledTimelineCount_ = 0;
    usize lastSampledTrackCount_ = 0;
    usize lastSampledLayoutTrackCount_ = 0;
    usize lastSampledSegmentCount_ = 0;
    usize cancelCount_ = 0;
    usize retargetCount_ = 0;
    bool candidateSamplePending_ = false;
};

} // namespace Tina::UI::Detail
