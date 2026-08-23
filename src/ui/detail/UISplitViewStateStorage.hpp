#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UISplitView.hpp>

#include "UIBoundedNodeStateTable.hpp"

#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

struct SplitViewLayoutScratch final {
    UISplitViewMetrics metrics{};
};

struct SplitViewState final {
    UINodeId node{};
    UISplitViewConfig config{};
    UISplitViewParts parts{};
    UISplitViewMetrics committedMetrics{};
    SplitViewLayoutScratch layoutScratch{};
    float requestedFraction = 0.5F;
};

struct SplitterState final {
    UINodeId node{};
    UISplitterConfig config{};
    UISplitterPaint paint{};
    UINodeId splitView{};
};

// Bounded sparse state owned by one UIContext. UIContext keeps tree validation
// and dirty coordination; this store owns relationship cleanup across
// destruction and generation reuse.
class UISplitViewStateStorage final {
  public:
    UISplitViewStateStorage(
        usize splitViewCapacity, usize splitterCapacity,
        std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize availableSplitViewCount() const noexcept;
    [[nodiscard]] usize availableSplitterCount() const noexcept;
    [[nodiscard]] bool containsSplitView(UINodeId splitView) const noexcept;
    [[nodiscard]] bool containsSplitter(UINodeId splitter) const noexcept;
    [[nodiscard]] SplitViewState* trySplitView(UINodeId splitView) noexcept;
    [[nodiscard]] const SplitViewState* trySplitView(UINodeId splitView) const noexcept;
    [[nodiscard]] SplitterState* trySplitter(UINodeId splitter) noexcept;
    [[nodiscard]] const SplitterState* trySplitter(UINodeId splitter) const noexcept;
    [[nodiscard]] UISplitterPaint& splitterPaintByIndex(u32 nodeIndex) noexcept;
    [[nodiscard]] const UISplitterPaint& splitterPaintByIndex(u32 nodeIndex) const noexcept;
    [[nodiscard]] SplitViewLayoutScratch& layoutScratchByIndex(u32 nodeIndex) noexcept;

    [[nodiscard]] bool initializeSplitView(
        UINodeId splitView, const UISplitViewConfig& config) noexcept;
    [[nodiscard]] bool initializeSplitter(
        UINodeId splitter, const UISplitterConfig& config) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node) noexcept;

    [[nodiscard]] UISplitViewParts parts(UINodeId splitView) const noexcept;
    [[nodiscard]] UINodeId splitViewForPart(UINodeId part) const noexcept;
    [[nodiscard]] UINodeId splitViewForSplitter(UINodeId splitter) const noexcept;
    void linkValidated(UINodeId splitView, UISplitViewParts parts) noexcept;
    [[nodiscard]] UISplitViewParts unlinkSplitView(UINodeId splitView) noexcept;

    void setRequestedFraction(UINodeId splitView, float fraction) noexcept;
    [[nodiscard]] float requestedFraction(UINodeId splitView) const noexcept;
    void publishMetrics(u32 splitViewIndex) noexcept;
    [[nodiscard]] UISplitViewMetrics committedMetrics(UINodeId splitView) const noexcept;

  private:
    [[nodiscard]] bool relationshipMatches(UINodeId splitView,
                                           const UISplitViewParts& parts) const noexcept;

    UIBoundedNodeStateTable<SplitViewState> splitViews_;
    UIBoundedNodeStateTable<SplitterState> splitters_;
};

} // namespace Tina::UI::Detail
