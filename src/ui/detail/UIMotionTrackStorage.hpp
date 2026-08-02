#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIMotion.hpp>
#include <tina/ui/UINodeId.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

// Fixed-capacity active transition store. One track per (node, property) for
// the first BackgroundColor slice. Sampling only walks the compact active list.

class UIMotionTrackStorage final {
  public:
    static constexpr u32 InvalidSlot = (std::numeric_limits<u32>::max)();

    struct Track final {
        UINodeId node{};
        UIAnimatableProperty property = UIAnimatableProperty::BackgroundColor;
        UIStraightSrgba8Color startColor{};
        UIStraightSrgba8Color targetColor{};
        UIStraightSrgba8Color presentationColor{};
        Core::MonotonicTimePoint startTime{};
        Core::Duration duration{0.0};
        Core::Duration delay{0.0};
        UIEasing easing = UIEasing::Linear;
        bool active = false;
        u32 nextActive = InvalidSlot;
        u32 prevActive = InvalidSlot;
    };

    UIMotionTrackStorage(usize trackCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;
    [[nodiscard]] usize lastSampledCount() const noexcept;

    // Retargets an existing track for the same node/property, or allocates a free
    // slot. Capacity failure leaves the store unchanged.
    [[nodiscard]] Core::Status beginOrRetargetBackgroundColor(
        UINodeId node, UIStraightSrgba8Color start, UIStraightSrgba8Color target,
        const UITransitionSpec& spec, Core::MonotonicTimePoint now);

    // Snaps presentation to target and deactivates without leaving a free hole
    // in the active list (used by reduced-motion and completion).
    void snapAndDeactivate(u32 slotIndex) noexcept;
    // Snaps every active track to target, stages lastCompleted, and frees slots.
    void snapAllActive() noexcept;
    void releaseNode(UINodeId node) noexcept;

    // Walks only the active list. Returns number of tracks still active after
    // sampling. Completed tracks are unlinked; their final colors are staged in
    // lastCompleted() for the Context to publish into paint storage.
    [[nodiscard]] usize sample(Core::MonotonicTimePoint now) noexcept;

    struct Completed final {
        UINodeId node{};
        UIStraightSrgba8Color color{};
    };

    [[nodiscard]] std::span<const Completed> lastCompleted() const noexcept;

    [[nodiscard]] const Track* findActiveBackgroundColor(UINodeId node) const noexcept;
    [[nodiscard]] Track* findActiveBackgroundColor(UINodeId node) noexcept;

  private:
    void unlinkActive(u32 slotIndex) noexcept;
    void linkActive(u32 slotIndex) noexcept;
    [[nodiscard]] u32 allocateSlot() noexcept;
    void freeSlot(u32 slotIndex) noexcept;

    std::pmr::vector<Track> tracks_;
    std::pmr::vector<u32> freeList_;
    u32 freeHead_ = InvalidSlot;
    u32 activeHead_ = InvalidSlot;
    usize activeCount_ = 0;
    usize highWater_ = 0;
    usize lastSampledCount_ = 0;
    std::pmr::vector<Completed> lastCompleted_;
};

} // namespace Tina::UI::Detail
