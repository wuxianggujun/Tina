#include "UIVirtualGridViewStateStorage.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace Tina::UI::Detail {

UIVirtualGridViewStateStorage::UIVirtualGridViewStateStorage(
    usize nodeCapacity, std::pmr::memory_resource& resource)
    : viewsByNodeIndex_(&resource), itemsByNodeIndex_(&resource),
      layoutScratchByNodeIndex_(&resource),
      linkValidationEpochByNodeIndex_(&resource)
{
    viewsByNodeIndex_.resize(nodeCapacity);
    itemsByNodeIndex_.resize(nodeCapacity);
    layoutScratchByNodeIndex_.resize(nodeCapacity);
    linkValidationEpochByNodeIndex_.resize(nodeCapacity);
}

usize UIVirtualGridViewStateStorage::capacity() const noexcept
{
    return viewsByNodeIndex_.size();
}

bool UIVirtualGridViewStateStorage::containsView(
    UINodeId virtualGridView) const noexcept
{
    return virtualGridView.hasValue() &&
           virtualGridView.index() < viewsByNodeIndex_.size() &&
           viewsByNodeIndex_[virtualGridView.index()].node == virtualGridView;
}

bool UIVirtualGridViewStateStorage::containsItem(UINodeId item) const noexcept
{
    return item.hasValue() && item.index() < itemsByNodeIndex_.size() &&
           itemsByNodeIndex_[item.index()].node == item;
}

VirtualGridViewState* UIVirtualGridViewStateStorage::tryView(
    UINodeId virtualGridView) noexcept
{
    return const_cast<VirtualGridViewState*>(
        std::as_const(*this).tryView(virtualGridView));
}

const VirtualGridViewState* UIVirtualGridViewStateStorage::tryView(
    UINodeId virtualGridView) const noexcept
{
    return containsView(virtualGridView)
               ? &viewsByNodeIndex_[virtualGridView.index()]
               : nullptr;
}

VirtualGridViewItemState* UIVirtualGridViewStateStorage::tryItem(
    UINodeId item) noexcept
{
    return const_cast<VirtualGridViewItemState*>(
        std::as_const(*this).tryItem(item));
}

const VirtualGridViewItemState* UIVirtualGridViewStateStorage::tryItem(
    UINodeId item) const noexcept
{
    return containsItem(item) ? &itemsByNodeIndex_[item.index()] : nullptr;
}

VirtualGridViewLayoutScratch*
UIVirtualGridViewStateStorage::tryLayoutScratch(
    UINodeId virtualGridView) noexcept
{
    return const_cast<VirtualGridViewLayoutScratch*>(
        std::as_const(*this).tryLayoutScratch(virtualGridView));
}

const VirtualGridViewLayoutScratch*
UIVirtualGridViewStateStorage::tryLayoutScratch(
    UINodeId virtualGridView) const noexcept
{
    return containsView(virtualGridView)
               ? &layoutScratchByNodeIndex_[virtualGridView.index()]
               : nullptr;
}

void UIVirtualGridViewStateStorage::initializeView(
    UINodeId virtualGridView,
    const UIVirtualGridViewCreateConfig& config) noexcept
{
    if (!virtualGridView.hasValue() ||
        virtualGridView.index() >= viewsByNodeIndex_.size())
    {
        return;
    }
    resetNode(virtualGridView.index());
    viewsByNodeIndex_[virtualGridView.index()] = VirtualGridViewState{
        .node = virtualGridView,
        .materializedItemCapacity = config.materializedItemCapacity,
    };
}

bool UIVirtualGridViewStateStorage::beginLinkValidation() noexcept
{
    if (linkValidationEpochByNodeIndex_.empty())
    {
        return false;
    }
    if (linkValidationEpoch_ == (std::numeric_limits<u32>::max)())
    {
        std::ranges::fill(linkValidationEpochByNodeIndex_, 0U);
        linkValidationEpoch_ = 1U;
    }
    else
    {
        ++linkValidationEpoch_;
    }
    return true;
}

bool UIVirtualGridViewStateStorage::markLinkNode(UINodeId node) noexcept
{
    if (!node.hasValue() || node.index() >= itemsByNodeIndex_.size() ||
        viewsByNodeIndex_[node.index()].node.hasValue() ||
        itemsByNodeIndex_[node.index()].node.hasValue() ||
        linkValidationEpochByNodeIndex_[node.index()] == linkValidationEpoch_)
    {
        return false;
    }
    linkValidationEpochByNodeIndex_[node.index()] = linkValidationEpoch_;
    return true;
}

bool UIVirtualGridViewStateStorage::linkMaterializedItems(
    UINodeId virtualGridView, std::span<const UINodeId> items) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr || view->linkedMaterializedItemCount != 0 ||
        items.size() != view->materializedItemCapacity ||
        !beginLinkValidation())
    {
        return false;
    }
    for (const UINodeId item : items)
    {
        if (!markLinkNode(item))
        {
            return false;
        }
    }

    view->firstMaterializedItem = items.empty() ? UINodeId{} : items.front();
    view->lastMaterializedItem = items.empty() ? UINodeId{} : items.back();
    view->linkedMaterializedItemCount = static_cast<u32>(items.size());
    for (u32 ordinal = 0; ordinal < view->linkedMaterializedItemCount; ++ordinal)
    {
        const UINodeId item = items[ordinal];
        itemsByNodeIndex_[item.index()] = VirtualGridViewItemState{
            .node = item,
            .virtualGridView = virtualGridView,
            .previousItem = ordinal == 0 ? UINodeId{} : items[ordinal - 1],
            .nextItem = ordinal + 1 == view->linkedMaterializedItemCount
                            ? UINodeId{}
                            : items[ordinal + 1],
            .poolOrdinal = ordinal,
        };
    }
    return true;
}

void UIVirtualGridViewStateStorage::unlinkMaterializedItems(
    UINodeId virtualGridView) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr)
    {
        return;
    }
    UINodeId item = view->firstMaterializedItem;
    for (u32 visited = 0;
         item.hasValue() && visited < view->linkedMaterializedItemCount;
         ++visited)
    {
        VirtualGridViewItemState* itemState = tryItem(item);
        if (itemState == nullptr || itemState->virtualGridView != virtualGridView)
        {
            break;
        }
        const UINodeId next = itemState->nextItem;
        *itemState = {};
        item = next;
    }
    view->firstMaterializedItem = {};
    view->lastMaterializedItem = {};
    view->linkedMaterializedItemCount = 0;
}

void UIVirtualGridViewStateStorage::resetNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= viewsByNodeIndex_.size())
    {
        return;
    }
    if (viewsByNodeIndex_[nodeIndex].node.hasValue())
    {
        unlinkMaterializedItems(viewsByNodeIndex_[nodeIndex].node);
    }
    if (itemsByNodeIndex_[nodeIndex].node.hasValue())
    {
        const UINodeId owner = itemsByNodeIndex_[nodeIndex].virtualGridView;
        if (containsView(owner))
        {
            unlinkMaterializedItems(owner);
        }
    }
    viewsByNodeIndex_[nodeIndex] = {};
    itemsByNodeIndex_[nodeIndex] = {};
    layoutScratchByNodeIndex_[nodeIndex] = {};
}

bool UIVirtualGridViewStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue() || node.index() >= viewsByNodeIndex_.size())
    {
        return false;
    }
    const bool related = containsView(node) || containsItem(node);
    resetNode(node.index());
    return related;
}

bool UIVirtualGridViewStateStorage::relationshipValid(
    UINodeId virtualGridView) const noexcept
{
    const VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr ||
        view->linkedMaterializedItemCount != view->materializedItemCapacity)
    {
        return false;
    }
    if (view->linkedMaterializedItemCount == 0)
    {
        return !view->firstMaterializedItem.hasValue() &&
               !view->lastMaterializedItem.hasValue();
    }

    UINodeId previous{};
    UINodeId item = view->firstMaterializedItem;
    for (u32 ordinal = 0; ordinal < view->linkedMaterializedItemCount; ++ordinal)
    {
        const VirtualGridViewItemState* itemState = tryItem(item);
        if (itemState == nullptr || itemState->virtualGridView != virtualGridView ||
            itemState->previousItem != previous ||
            itemState->poolOrdinal != ordinal)
        {
            return false;
        }
        previous = item;
        item = itemState->nextItem;
    }
    return previous == view->lastMaterializedItem && !item.hasValue();
}

UINodeId UIVirtualGridViewStateStorage::itemAt(
    UINodeId virtualGridView, u32 poolOrdinal) const noexcept
{
    const VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr || poolOrdinal >= view->linkedMaterializedItemCount)
    {
        return {};
    }
    UINodeId item = view->firstMaterializedItem;
    for (u32 ordinal = 0; ordinal < poolOrdinal && item.hasValue(); ++ordinal)
    {
        const VirtualGridViewItemState* state = tryItem(item);
        item = state != nullptr ? state->nextItem : UINodeId{};
    }
    const VirtualGridViewItemState* state = tryItem(item);
    return state != nullptr && state->virtualGridView == virtualGridView &&
                   state->poolOrdinal == poolOrdinal
               ? item
               : UINodeId{};
}

UINodeId UIVirtualGridViewStateStorage::viewForItem(UINodeId item) const noexcept
{
    const VirtualGridViewItemState* state = tryItem(item);
    return state != nullptr && containsView(state->virtualGridView)
               ? state->virtualGridView
               : UINodeId{};
}

void UIVirtualGridViewStateStorage::setDataSource(
    UINodeId virtualGridView,
    UIVirtualGridViewDataSource dataSource) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr)
    {
        return;
    }
    view->dataSource = dataSource;
    view->selection = {};
    view->requestedScrollOffset = 0.0F;
    clearItemBindings(virtualGridView);
}

void UIVirtualGridViewStateStorage::clearDataSource(
    UINodeId virtualGridView) noexcept
{
    setDataSource(virtualGridView, {});
}

bool UIVirtualGridViewStateStorage::setSelection(
    UINodeId virtualGridView,
    UIVirtualGridViewSelection selection) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr || view->selection == selection)
    {
        return false;
    }
    view->selection = selection;
    return true;
}

bool UIVirtualGridViewStateStorage::clearSelection(
    UINodeId virtualGridView) noexcept
{
    return setSelection(virtualGridView, {});
}

bool UIVirtualGridViewStateStorage::bindItem(
    UINodeId item, UIVirtualGridViewItemKey key, u64 logicalIndex,
    bool enabled) noexcept
{
    VirtualGridViewItemState* state = tryItem(item);
    if (state == nullptr || key == InvalidUIVirtualGridViewItemKey)
    {
        return false;
    }
    state->key = key;
    state->logicalIndex = logicalIndex;
    state->bound = true;
    state->enabled = enabled;
    return true;
}

void UIVirtualGridViewStateStorage::clearItemBinding(UINodeId item) noexcept
{
    VirtualGridViewItemState* state = tryItem(item);
    if (state == nullptr)
    {
        return;
    }
    state->key = InvalidUIVirtualGridViewItemKey;
    state->logicalIndex = 0;
    state->bound = false;
    state->enabled = true;
}

void UIVirtualGridViewStateStorage::clearItemBindings(
    UINodeId virtualGridView) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr)
    {
        return;
    }
    UINodeId item = view->firstMaterializedItem;
    for (u32 visited = 0;
         item.hasValue() && visited < view->linkedMaterializedItemCount;
         ++visited)
    {
        VirtualGridViewItemState* state = tryItem(item);
        if (state == nullptr || state->virtualGridView != virtualGridView)
        {
            break;
        }
        const UINodeId next = state->nextItem;
        clearItemBinding(item);
        item = next;
    }
}

void UIVirtualGridViewStateStorage::publishItemBindings(
    UINodeId virtualGridView) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr)
    {
        return;
    }
    UINodeId item = view->firstMaterializedItem;
    for (u32 visited = 0;
         item.hasValue() && visited < view->linkedMaterializedItemCount;
         ++visited)
    {
        VirtualGridViewItemState* state = tryItem(item);
        if (state == nullptr || state->virtualGridView != virtualGridView)
        {
            break;
        }
        state->committedKey = state->key;
        state->committedLogicalIndex = state->logicalIndex;
        state->committedBound = state->bound;
        state->committedEnabled = state->enabled;
        item = state->nextItem;
    }
}

void UIVirtualGridViewStateStorage::publishMetrics(
    UINodeId virtualGridView) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    VirtualGridViewLayoutScratch* scratch =
        tryLayoutScratch(virtualGridView);
    if (view == nullptr || scratch == nullptr)
    {
        return;
    }
    view->committedMetrics = scratch->metrics;
    view->committedViewportRect = scratch->viewportRect;
}

} // namespace Tina::UI::Detail
