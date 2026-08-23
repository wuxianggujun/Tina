#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UITooltip.hpp>

#include "UIBoundedNodeStateTable.hpp"

#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

struct TooltipLayoutScratch final {
    UITooltipMetrics metrics{};
};

struct TooltipState final {
    UINodeId node{};
    UITooltipConfig config{};
    UINodeId anchor{};
    UITooltipMetrics committedMetrics{};
    TooltipLayoutScratch layoutScratch{};
    bool open = false;
    bool manualRequested = false;
    bool suppressedUntilTriggerReset = false;
};

struct UITooltipAdvanceCandidate final {
    UINodeId tooltip{};
    bool eligible = false;
};

struct UITooltipAdvanceInput final {
    Core::MonotonicTimePoint now{};
    UINodeId focusedAnchor{};
    UITooltipAdvanceCandidate active{};
    UITooltipAdvanceCandidate manual{};
    UITooltipAdvanceCandidate hover{};
    UITooltipAdvanceCandidate focus{};
};

// Fixed-capacity, owner-thread storage for one UIContext. The Context remains
// the lifetime owner and validates tree/root/modal constraints before calling
// relationship or presentation operations.
class UITooltipStateStorage final {
  public:
    UITooltipStateStorage(usize capacity, std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize availableCount() const noexcept;
    [[nodiscard]] bool containsTooltip(UINodeId tooltip) const noexcept;
    [[nodiscard]] TooltipState* tryState(UINodeId tooltip) noexcept;
    [[nodiscard]] const TooltipState* tryState(UINodeId tooltip) const noexcept;
    [[nodiscard]] TooltipState& stateByIndex(u32 nodeIndex) noexcept;
    [[nodiscard]] const TooltipState& stateByIndex(u32 nodeIndex) const noexcept;
    [[nodiscard]] TooltipLayoutScratch& layoutScratchByIndex(u32 nodeIndex) noexcept;
    [[nodiscard]] const TooltipLayoutScratch& layoutScratchByIndex(u32 nodeIndex) const noexcept;

    [[nodiscard]] bool initializeTooltip(
        UINodeId tooltip, const UITooltipConfig& config) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node, Core::MonotonicTimePoint now) noexcept;

    [[nodiscard]] bool hasRelationship(UINodeId tooltip, UINodeId anchor) const noexcept;
    [[nodiscard]] UINodeId tooltipForAnchor(UINodeId anchor) const noexcept;
    [[nodiscard]] UINodeId anchorForTooltip(UINodeId tooltip) const noexcept;
    void linkValidated(UINodeId tooltip, UINodeId anchor) noexcept;
    [[nodiscard]] UINodeId unlinkTooltip(UINodeId tooltip) noexcept;
    [[nodiscard]] UINodeId unlinkAnchor(UINodeId anchor) noexcept;

    void setHoveredAnchor(UINodeId anchor) noexcept;
    [[nodiscard]] UINodeId hoveredAnchor() const noexcept;
    [[nodiscard]] UINodeId activeTooltip() const noexcept;
    [[nodiscard]] UINodeId pendingTooltip() const noexcept;
    [[nodiscard]] UINodeId manualTooltip() const noexcept;

    [[nodiscard]] bool requestManual(UINodeId tooltip, bool eligible) noexcept;
    [[nodiscard]] bool dismiss(UINodeId tooltip, bool suppress,
                               Core::MonotonicTimePoint now) noexcept;
    [[nodiscard]] bool hardDismiss(UINodeId focusedAnchor, bool suppress) noexcept;
    [[nodiscard]] bool advance(const UITooltipAdvanceInput& input) noexcept;

    void publishMetrics(u32 tooltipIndex) noexcept;
    [[nodiscard]] UITooltipMetrics committedMetrics(UINodeId tooltip) const noexcept;

    // The rollback copy is allocated at construction and reused for every
    // layout transaction. rollbackCommitTransaction() performs no allocation.
    void beginCommitTransaction() noexcept;
    void rollbackCommitTransaction() noexcept;

  private:
    struct PresentationState final {
        UINodeId active{};
        UINodeId pending{};
        UINodeId hoveredAnchor{};
        UINodeId manual{};
        Core::MonotonicTimePoint openDeadline{};
        Core::MonotonicTimePoint closeDeadline{};
        Core::MonotonicTimePoint reshowExpiry{};
        bool openPending = false;
        bool closePending = false;
        bool reshowAvailable = false;
    };

    [[nodiscard]] static Core::MonotonicTimePoint
    deadlineAfter(Core::MonotonicTimePoint now, Core::Duration delay) noexcept;
    [[nodiscard]] bool rawTriggerActive(const TooltipState& state,
                                        UINodeId focusedAnchor) const noexcept;
    void clearResettableSuppression(UINodeId focusedAnchor) noexcept;
    void cancelPendingOpen() noexcept;
    [[nodiscard]] bool activate(UINodeId tooltip) noexcept;
    [[nodiscard]] bool deactivate(UINodeId tooltip, bool suppress, bool allowReshow,
                                  Core::MonotonicTimePoint now) noexcept;
    void suppress(UINodeId tooltip) noexcept;
    [[nodiscard]] UINodeId desiredTooltip(const UITooltipAdvanceInput& input) const noexcept;

    UIBoundedNodeStateTable<TooltipState> states_;
    std::pmr::vector<TooltipState> rollbackStates_;
    PresentationState presentation_{};
    PresentationState rollbackPresentation_{};
};

} // namespace Tina::UI::Detail
