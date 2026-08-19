#include "UISplitViewStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UISplitViewStateStorage::UISplitViewStateStorage(
    usize nodeCapacity, std::pmr::memory_resource& resource)
    : splitViewsByNodeIndex_(&resource), splittersByNodeIndex_(&resource),
      layoutScratchByNodeIndex_(&resource), splitViewForPartByNodeIndex_(&resource)
{
    splitViewsByNodeIndex_.resize(nodeCapacity);
    splittersByNodeIndex_.resize(nodeCapacity);
    layoutScratchByNodeIndex_.resize(nodeCapacity);
    splitViewForPartByNodeIndex_.resize(nodeCapacity);
}

usize UISplitViewStateStorage::capacity() const noexcept
{
    return splitViewsByNodeIndex_.size();
}

bool UISplitViewStateStorage::containsSplitView(UINodeId splitView) const noexcept
{
    return splitView.hasValue() && splitView.index() < splitViewsByNodeIndex_.size() &&
           splitViewsByNodeIndex_[splitView.index()].node == splitView;
}

bool UISplitViewStateStorage::containsSplitter(UINodeId splitter) const noexcept
{
    return splitter.hasValue() && splitter.index() < splittersByNodeIndex_.size() &&
           splittersByNodeIndex_[splitter.index()].node == splitter;
}

SplitViewState* UISplitViewStateStorage::trySplitView(UINodeId splitView) noexcept
{
    return const_cast<SplitViewState*>(std::as_const(*this).trySplitView(splitView));
}

const SplitViewState* UISplitViewStateStorage::trySplitView(UINodeId splitView) const noexcept
{
    return containsSplitView(splitView) ? &splitViewsByNodeIndex_[splitView.index()] : nullptr;
}

SplitterState* UISplitViewStateStorage::trySplitter(UINodeId splitter) noexcept
{
    return const_cast<SplitterState*>(std::as_const(*this).trySplitter(splitter));
}

const SplitterState* UISplitViewStateStorage::trySplitter(UINodeId splitter) const noexcept
{
    return containsSplitter(splitter) ? &splittersByNodeIndex_[splitter.index()] : nullptr;
}

UISplitterPaint& UISplitViewStateStorage::splitterPaintByIndex(u32 nodeIndex) noexcept
{
    assert(nodeIndex < splittersByNodeIndex_.size());
    return splittersByNodeIndex_[nodeIndex].paint;
}

const UISplitterPaint& UISplitViewStateStorage::splitterPaintByIndex(u32 nodeIndex) const noexcept
{
    assert(nodeIndex < splittersByNodeIndex_.size());
    return splittersByNodeIndex_[nodeIndex].paint;
}

SplitViewLayoutScratch& UISplitViewStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    assert(nodeIndex < layoutScratchByNodeIndex_.size());
    return layoutScratchByNodeIndex_[nodeIndex];
}

void UISplitViewStateStorage::initializeSplitView(
    UINodeId splitView, const UISplitViewConfig& config) noexcept
{
    assert(splitView.hasValue() && splitView.index() < splitViewsByNodeIndex_.size());
    resetNode(splitView.index());
    splitViewsByNodeIndex_[splitView.index()] = SplitViewState{
        .node = splitView,
        .config = config,
        .requestedFraction = config.initialFraction,
    };
}

void UISplitViewStateStorage::initializeSplitter(
    UINodeId splitter, const UISplitterConfig& config) noexcept
{
    assert(splitter.hasValue() && splitter.index() < splittersByNodeIndex_.size());
    resetNode(splitter.index());
    splittersByNodeIndex_[splitter.index()] = SplitterState{
        .node = splitter,
        .config = config,
    };
}

bool UISplitViewStateStorage::relationshipMatches(
    UINodeId splitView, const UISplitViewParts& parts) const noexcept
{
    if (!containsSplitView(splitView) || !parts.hasValue())
    {
        return false;
    }
    const auto owns = [this, splitView](UINodeId part) noexcept {
        return part.index() < splitViewForPartByNodeIndex_.size() &&
               splitViewForPartByNodeIndex_[part.index()] == splitView;
    };
    const SplitterState* splitter = trySplitter(parts.splitter);
    return owns(parts.primaryPane) && owns(parts.splitter) && owns(parts.secondaryPane) &&
           splitter != nullptr && splitter->splitView == splitView;
}

UISplitViewParts UISplitViewStateStorage::parts(UINodeId splitView) const noexcept
{
    const SplitViewState* state = trySplitView(splitView);
    return state != nullptr && relationshipMatches(splitView, state->parts)
               ? state->parts
               : UISplitViewParts{};
}

UINodeId UISplitViewStateStorage::splitViewForPart(UINodeId part) const noexcept
{
    if (!part.hasValue() || part.index() >= splitViewForPartByNodeIndex_.size())
    {
        return {};
    }
    const UINodeId splitView = splitViewForPartByNodeIndex_[part.index()];
    const SplitViewState* state = trySplitView(splitView);
    if (state == nullptr || !relationshipMatches(splitView, state->parts))
    {
        return {};
    }
    return state->parts.primaryPane == part || state->parts.splitter == part ||
                   state->parts.secondaryPane == part
               ? splitView
               : UINodeId{};
}

UINodeId UISplitViewStateStorage::splitViewForSplitter(UINodeId splitter) const noexcept
{
    const SplitterState* state = trySplitter(splitter);
    return state != nullptr && splitViewForPart(splitter) == state->splitView
               ? state->splitView
               : UINodeId{};
}

void UISplitViewStateStorage::linkValidated(
    UINodeId splitView, UISplitViewParts newParts) noexcept
{
    assert(containsSplitView(splitView));
    assert(containsSplitter(newParts.splitter));
    assert(newParts.hasValue());
    static_cast<void>(unlinkSplitView(splitView));

    SplitViewState& state = splitViewsByNodeIndex_[splitView.index()];
    state.parts = newParts;
    splitViewForPartByNodeIndex_[newParts.primaryPane.index()] = splitView;
    splitViewForPartByNodeIndex_[newParts.splitter.index()] = splitView;
    splitViewForPartByNodeIndex_[newParts.secondaryPane.index()] = splitView;
    splittersByNodeIndex_[newParts.splitter.index()].splitView = splitView;
}

UISplitViewParts UISplitViewStateStorage::unlinkSplitView(UINodeId splitView) noexcept
{
    SplitViewState* state = trySplitView(splitView);
    if (state == nullptr)
    {
        return {};
    }
    const UISplitViewParts previous = state->parts;
    for (const UINodeId part : {previous.primaryPane, previous.splitter, previous.secondaryPane})
    {
        if (part.hasValue() && part.index() < splitViewForPartByNodeIndex_.size() &&
            splitViewForPartByNodeIndex_[part.index()] == splitView)
        {
            splitViewForPartByNodeIndex_[part.index()] = {};
        }
    }
    if (SplitterState* splitter = trySplitter(previous.splitter);
        splitter != nullptr && splitter->splitView == splitView)
    {
        splitter->splitView = {};
    }
    state->parts = {};
    return previous;
}

void UISplitViewStateStorage::resetNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= splitViewsByNodeIndex_.size())
    {
        return;
    }
    if (splitViewsByNodeIndex_[nodeIndex].node.hasValue())
    {
        static_cast<void>(unlinkSplitView(splitViewsByNodeIndex_[nodeIndex].node));
    }
    const UINodeId owner = splitViewForPartByNodeIndex_[nodeIndex];
    if (containsSplitView(owner))
    {
        static_cast<void>(unlinkSplitView(owner));
    }
    splitViewsByNodeIndex_[nodeIndex] = {};
    splittersByNodeIndex_[nodeIndex] = {};
    layoutScratchByNodeIndex_[nodeIndex] = {};
    splitViewForPartByNodeIndex_[nodeIndex] = {};
}

bool UISplitViewStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue() || node.index() >= splitViewsByNodeIndex_.size())
    {
        return false;
    }
    const bool related = containsSplitView(node) || splitViewForPart(node).hasValue();
    resetNode(node.index());
    return related;
}

void UISplitViewStateStorage::setRequestedFraction(
    UINodeId splitView, float fraction) noexcept
{
    if (SplitViewState* state = trySplitView(splitView); state != nullptr)
    {
        state->requestedFraction = fraction;
    }
}

float UISplitViewStateStorage::requestedFraction(UINodeId splitView) const noexcept
{
    const SplitViewState* state = trySplitView(splitView);
    return state != nullptr ? state->requestedFraction : 0.5F;
}

void UISplitViewStateStorage::publishMetrics(u32 splitViewIndex) noexcept
{
    assert(splitViewIndex < splitViewsByNodeIndex_.size());
    splitViewsByNodeIndex_[splitViewIndex].committedMetrics =
        layoutScratchByNodeIndex_[splitViewIndex].metrics;
}

UISplitViewMetrics UISplitViewStateStorage::committedMetrics(
    UINodeId splitView) const noexcept
{
    const SplitViewState* state = trySplitView(splitView);
    return state != nullptr ? state->committedMetrics : UISplitViewMetrics{};
}

} // namespace Tina::UI::Detail
