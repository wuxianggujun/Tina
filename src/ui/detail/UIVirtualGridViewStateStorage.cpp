#include "UIVirtualGridViewStateStorage.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace Tina::UI::Detail {

UIVirtualGridViewStateStorage::UIVirtualGridViewStateStorage(
    usize viewCapacity, usize itemCapacity,
    std::pmr::memory_resource& resource)
    : views_(viewCapacity, resource), items_(itemCapacity, resource),
      linkValidationNodeIndices_(&resource)
{
    linkValidationNodeIndices_.reserve(itemCapacity);
}

usize UIVirtualGridViewStateStorage::capacity() const noexcept
{
    return views_.capacity();
}

usize UIVirtualGridViewStateStorage::availableViewCount() const noexcept
{
    return views_.availableCount();
}

usize UIVirtualGridViewStateStorage::availableItemCount() const noexcept
{
    return items_.availableCount();
}

bool UIVirtualGridViewStateStorage::containsView(
    UINodeId virtualGridView) const noexcept
{
    return views_.contains(virtualGridView);
}

bool UIVirtualGridViewStateStorage::containsItem(UINodeId item) const noexcept
{
    return items_.contains(item);
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
    return views_.tryGet(virtualGridView);
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
    return items_.tryGet(item);
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
    const VirtualGridViewState* state = tryView(virtualGridView);
    return state != nullptr ? &state->layoutScratch : nullptr;
}

bool UIVirtualGridViewStateStorage::initializeView(
    UINodeId virtualGridView,
    const UIVirtualGridViewCreateConfig& config) noexcept
{
    if (!virtualGridView.hasValue())
    {
        return false;
    }
    resetNode(virtualGridView.index());
    return views_.insertOrAssign(VirtualGridViewState{
        .node = virtualGridView,
        .materializedItemCapacity = config.materializedItemCapacity,
    });
}

bool UIVirtualGridViewStateStorage::beginLinkValidation() noexcept
{
    linkValidationNodeIndices_.clear();
    return true;
}

bool UIVirtualGridViewStateStorage::markLinkNode(UINodeId node) noexcept
{
    if (!node.hasValue() || views_.tryGetByIndex(node.index()) != nullptr ||
        items_.tryGetByIndex(node.index()) != nullptr)
    {
        return false;
    }
    const auto found = std::lower_bound(
        linkValidationNodeIndices_.begin(), linkValidationNodeIndices_.end(),
        node.index());
    if (found != linkValidationNodeIndices_.end() && *found == node.index())
    {
        return false;
    }
    linkValidationNodeIndices_.insert(found, node.index());
    return true;
}

bool UIVirtualGridViewStateStorage::linkMaterializedItems(
    UINodeId virtualGridView, std::span<const UINodeId> items) noexcept
{
    VirtualGridViewState* view = tryView(virtualGridView);
    if (view == nullptr || view->linkedMaterializedItemCount != 0 ||
        items.size() != view->materializedItemCapacity ||
        items.size() > items_.availableCount() || !beginLinkValidation())
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
        const bool inserted = items_.insertOrAssign(VirtualGridViewItemState{
            .node = item,
            .virtualGridView = virtualGridView,
            .previousItem = ordinal == 0 ? UINodeId{} : items[ordinal - 1],
            .nextItem = ordinal + 1 == view->linkedMaterializedItemCount
                            ? UINodeId{}
                            : items[ordinal + 1],
            .poolOrdinal = ordinal,
        });
        if (!inserted)
        {
            std::terminate();
        }
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
        static_cast<void>(items_.erase(item));
        item = next;
    }
    view->firstMaterializedItem = {};
    view->lastMaterializedItem = {};
    view->linkedMaterializedItemCount = 0;
}

void UIVirtualGridViewStateStorage::resetNode(u32 nodeIndex) noexcept
{
    if (VirtualGridViewState* view = views_.tryGetByIndex(nodeIndex);
        view != nullptr)
    {
        const UINodeId viewNode = view->node;
        unlinkMaterializedItems(viewNode);
        static_cast<void>(views_.eraseByIndex(nodeIndex));
    }
    if (VirtualGridViewItemState* item = items_.tryGetByIndex(nodeIndex);
        item != nullptr)
    {
        const UINodeId owner = item->virtualGridView;
        if (containsView(owner))
        {
            unlinkMaterializedItems(owner);
        }
    }
    static_cast<void>(views_.eraseByIndex(nodeIndex));
    static_cast<void>(items_.eraseByIndex(nodeIndex));
}

bool UIVirtualGridViewStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue())
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
