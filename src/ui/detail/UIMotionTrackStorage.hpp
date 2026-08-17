#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIMotion.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

// Fixed-capacity active transition store. One track per (node, property).
// Sampling only walks the compact active list (O(M)).

class UIMotionTrackStorage final {
  public:
    static constexpr u32 InvalidSlot = (std::numeric_limits<u32>::max)();

    enum class ValueKind : u8 {
        Color = 0,
        Scalar,
        Offset,
    };

    enum class CompletionMode : u8 {
        CommitProperty = 0,
        StylePresentationOnly,
    };

    struct Scalar2 final {
        float x = 0.0F;
        float y = 0.0F;
    };

    struct Track final {
        UINodeId node{};
        UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
        ValueKind valueKind = ValueKind::Color;
        UIStraightSrgba8Color startColor{};
        UIStraightSrgba8Color targetColor{};
        UIStraightSrgba8Color presentationColor{};
        UIStraightSrgba8Color candidatePresentationColor{};
        UIStraightSrgba8Color candidateStyleStartColor{};
        UIStraightSrgba8Color candidateStyleTargetColor{};
        float startScalar = 0.0F;
        float targetScalar = 0.0F;
        float presentationScalar = 0.0F;
        float candidatePresentationScalar = 0.0F;
        Scalar2 startOffset{};
        Scalar2 targetOffset{};
        Scalar2 presentationOffset{};
        Scalar2 candidatePresentationOffset{};
        Core::MonotonicTimePoint startTime{};
        Core::Duration duration{0.0};
        Core::Duration delay{0.0};
        Core::MonotonicTimePoint candidateStyleStartTime{};
        Core::Duration candidateStyleDuration{0.0};
        Core::Duration candidateStyleDelay{0.0};
        UIEasing easing = UIEasing::Linear;
        UIEasing candidateStyleEasing = UIEasing::Linear;
        CompletionMode completionMode = CompletionMode::CommitProperty;
        bool occupied = false;
        bool persistentReservation = false;
        bool active = false;
        bool hasCandidatePresentation = false;
        bool candidateCompleted = false;
        bool hasCandidateStyleActivation = false;
        u32 nextActive = InvalidSlot;
        u32 prevActive = InvalidSlot;
    };

    struct Completed final {
        UINodeId node{};
        UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
        ValueKind valueKind = ValueKind::Color;
        UIStraightSrgba8Color color{};
        float scalar = 0.0F;
        Scalar2 offset{};
        CompletionMode completionMode = CompletionMode::CommitProperty;
    };

    // Presentation snapshot for one node after active-track sampling.
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
        Scalar2 visualOffset{};
    };

    UIMotionTrackStorage(usize trackCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize availableCount() const noexcept;
    [[nodiscard]] usize reservedCount() const noexcept;
    [[nodiscard]] usize reservedHighWater() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;
    [[nodiscard]] usize lastSampledCount() const noexcept;

    [[nodiscard]] Core::Status beginOrRetargetColor(
        UINodeId node, UIAnimatableProperty property, UIStraightSrgba8Color start,
        UIStraightSrgba8Color target, const UITransitionSpec& spec,
        Core::MonotonicTimePoint now);
    [[nodiscard]] Core::Status beginOrRetargetScalar(
        UINodeId node, UIAnimatableProperty property, float start, float target,
        const UITransitionSpec& spec, Core::MonotonicTimePoint now);
    [[nodiscard]] Core::Status beginOrRetargetOffset(
        UINodeId node, UIAnimatableProperty property, Scalar2 start, Scalar2 target,
        const UITransitionSpec& spec, Core::MonotonicTimePoint now);
    [[nodiscard]] Core::Status reservePersistent(UINodeId node, UIAnimatableProperty property);
    [[nodiscard]] Core::Status activateReservedStyleColor(UINodeId node, UIStraightSrgba8Color start,
                                                          UIStraightSrgba8Color target, const UITransitionSpec& spec,
                                                          Core::MonotonicTimePoint now);
    [[nodiscard]] bool hasPersistentReservation(UINodeId node, UIAnimatableProperty property) const noexcept;
    [[nodiscard]] bool hasTrack(UINodeId node, UIAnimatableProperty property) const noexcept;
    void releasePersistentReservation(UINodeId node, UIAnimatableProperty property) noexcept;
    void releaseAllPersistentReservations(UIAnimatableProperty property) noexcept;
    void cancelActiveProperty(UINodeId node, UIAnimatableProperty property) noexcept;

    void snapAllActive() noexcept;
    void releaseNode(UINodeId node) noexcept;
    void releaseProperty(UINodeId node, UIAnimatableProperty property) noexcept;

    // Candidate sampling lets UIContext include direct transitions in the same
    // publication transaction as a layout timeline. The ordinary sample()
    // entry point preserves its immediate-commit behavior.
    void beginCandidateSample(Core::MonotonicTimePoint now) noexcept;
    void ensureCandidateTransaction() noexcept;
    void commitCandidateSample() noexcept;
    // Rollback keeps builder-time Style activations pending so a failed layout
    // publication can retry them without mutating the active list early.
    void rollbackCandidateSample() noexcept;
    void discardCandidateSample() noexcept;
    [[nodiscard]] bool hasCandidateSample() const noexcept;
    [[nodiscard]] usize sample(Core::MonotonicTimePoint now) noexcept;
    [[nodiscard]] std::span<const Completed> lastCompleted() const noexcept;

    [[nodiscard]] const Track* findActive(UINodeId node, UIAnimatableProperty property) const noexcept;
    [[nodiscard]] Track* findActive(UINodeId node, UIAnimatableProperty property) noexcept;
    [[nodiscard]] NodePresentation presentationFor(UINodeId node) const noexcept;

  private:
    [[nodiscard]] const Track* findOccupied(UINodeId node, UIAnimatableProperty property) const noexcept;
    [[nodiscard]] Track* findOccupied(UINodeId node, UIAnimatableProperty property) noexcept;
    void unlinkActive(u32 slotIndex) noexcept;
    void linkActive(u32 slotIndex) noexcept;
    [[nodiscard]] u32 allocateSlot() noexcept;
    void freeSlot(u32 slotIndex) noexcept;
    [[nodiscard]] Core::Status beginOrRetargetCommon(
        UINodeId node, UIAnimatableProperty property, ValueKind kind,
        const UITransitionSpec& spec, Core::MonotonicTimePoint now,
        Track*& outTrack);

    std::pmr::vector<Track> tracks_;
    std::pmr::vector<u32> freeList_;
    u32 freeHead_ = InvalidSlot;
    usize availableCount_ = 0;
    u32 activeHead_ = InvalidSlot;
    usize activeCount_ = 0;
    usize highWater_ = 0;
    usize reservedCount_ = 0;
    usize reservedHighWater_ = 0;
    usize lastSampledCount_ = 0;
    std::pmr::vector<Completed> lastCompleted_;
    std::pmr::vector<u32> candidateStyleActivationSlots_;
    bool candidateSamplePending_ = false;
};

[[nodiscard]] constexpr UIMotionTrackStorage::ValueKind
valueKindForProperty(UIAnimatableProperty property) noexcept
{
    switch (property) {
    case UIAnimatableProperty::BackgroundColor:
    case UIAnimatableProperty::BorderColor:
    case UIAnimatableProperty::TextColor:
        return UIMotionTrackStorage::ValueKind::Color;
    case UIAnimatableProperty::Opacity:
    case UIAnimatableProperty::CornerRadius:
    case UIAnimatableProperty::LayoutWidth:
    case UIAnimatableProperty::LayoutHeight:
        return UIMotionTrackStorage::ValueKind::Scalar;
    case UIAnimatableProperty::VisualOffset:
    case UIAnimatableProperty::LayoutOffset:
        return UIMotionTrackStorage::ValueKind::Offset;
    }
    return UIMotionTrackStorage::ValueKind::Color;
}

[[nodiscard]] constexpr float lerpFloat(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

} // namespace Tina::UI::Detail
