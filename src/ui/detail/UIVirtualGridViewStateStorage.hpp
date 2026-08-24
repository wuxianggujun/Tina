#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

#include "UIBoundedNodeStateTable.hpp"

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

struct VirtualGridViewLayoutScratch final {
    UIVirtualGridViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct VirtualGridViewState final {
    UINodeId node{};
    UIVirtualGridViewStyle style{};
    UIVirtualGridViewPaint paint{};
    UIVirtualGridViewDataSource dataSource{};
    UIVirtualGridViewSelection selection{};
    UIVirtualGridViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
    UINodeId firstMaterializedItem{};
    UINodeId lastMaterializedItem{};
    float requestedScrollOffset = 0.0F;
    u32 materializedItemCapacity = 0;
    u32 linkedMaterializedItemCount = 0;
    VirtualGridViewLayoutScratch layoutScratch{};
};

struct VirtualGridViewItemState final {
    UINodeId node{};
    UINodeId virtualGridView{};
    UINodeId previousItem{};
    UINodeId nextItem{};
    u32 poolOrdinal = 0;

    UIVirtualGridViewItemKey key = InvalidUIVirtualGridViewItemKey;
    u64 logicalIndex = 0;
    bool bound = false;
    bool enabled = true;
    UIVirtualGridViewItemPresentation presentation{};

    UIVirtualGridViewItemKey committedKey = InvalidUIVirtualGridViewItemKey;
    u64 committedLogicalIndex = 0;
    bool committedBound = false;
    bool committedEnabled = true;
    UIVirtualGridViewItemPresentation committedPresentation{};
};

// Independent fixed-capacity sparse state for VirtualGridView and its
// materialized item pool. Construction performs all allocation; binding,
// publication, and generation reuse do not grow storage. UIContext remains
// responsible for tree topology, text storage, dirty propagation, and
// descriptor validation.
class UIVirtualGridViewStateStorage final {
  public:
    UIVirtualGridViewStateStorage(
        usize viewCapacity, usize itemCapacity,
        std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize availableViewCount() const noexcept;
    [[nodiscard]] usize availableItemCount() const noexcept;
    [[nodiscard]] bool containsView(UINodeId virtualGridView) const noexcept;
    [[nodiscard]] bool containsItem(UINodeId item) const noexcept;
    [[nodiscard]] VirtualGridViewState* tryView(UINodeId virtualGridView) noexcept;
    [[nodiscard]] const VirtualGridViewState* tryView(
        UINodeId virtualGridView) const noexcept;
    [[nodiscard]] VirtualGridViewItemState* tryItem(UINodeId item) noexcept;
    [[nodiscard]] const VirtualGridViewItemState* tryItem(
        UINodeId item) const noexcept;
    [[nodiscard]] VirtualGridViewLayoutScratch* tryLayoutScratch(
        UINodeId virtualGridView) noexcept;
    [[nodiscard]] const VirtualGridViewLayoutScratch* tryLayoutScratch(
        UINodeId virtualGridView) const noexcept;

    [[nodiscard]] bool initializeView(
        UINodeId virtualGridView,
        const UIVirtualGridViewCreateConfig& config) noexcept;
    [[nodiscard]] bool linkMaterializedItems(
        UINodeId virtualGridView, std::span<const UINodeId> items) noexcept;
    void unlinkMaterializedItems(UINodeId virtualGridView) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node) noexcept;

    [[nodiscard]] bool relationshipValid(UINodeId virtualGridView) const noexcept;
    [[nodiscard]] UINodeId itemAt(
        UINodeId virtualGridView, u32 poolOrdinal) const noexcept;
    [[nodiscard]] UINodeId viewForItem(UINodeId item) const noexcept;

    void setDataSource(
        UINodeId virtualGridView,
        UIVirtualGridViewDataSource dataSource) noexcept;
    void clearDataSource(UINodeId virtualGridView) noexcept;
    [[nodiscard]] bool setSelection(
        UINodeId virtualGridView,
        UIVirtualGridViewSelection selection) noexcept;
    [[nodiscard]] bool clearSelection(UINodeId virtualGridView) noexcept;

    [[nodiscard]] bool bindItem(
        UINodeId item, UIVirtualGridViewItemKey key, u64 logicalIndex,
        bool enabled, UIVirtualGridViewItemPresentation presentation = {}) noexcept;
    void clearItemBinding(UINodeId item) noexcept;
    void clearItemBindings(UINodeId virtualGridView) noexcept;
    void publishItemBindings(UINodeId virtualGridView) noexcept;
    void publishMetrics(UINodeId virtualGridView) noexcept;

  private:
    [[nodiscard]] bool beginLinkValidation() noexcept;
    [[nodiscard]] bool markLinkNode(UINodeId node) noexcept;

    UIBoundedNodeStateTable<VirtualGridViewState> views_;
    UIBoundedNodeStateTable<VirtualGridViewItemState> items_;
    std::pmr::vector<u32> linkValidationNodeIndices_;
};

} // namespace Tina::UI::Detail
