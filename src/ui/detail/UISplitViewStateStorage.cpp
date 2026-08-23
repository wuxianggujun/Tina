#include "UISplitViewStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UISplitViewStateStorage::UISplitViewStateStorage(
    usize splitViewCapacity, usize splitterCapacity,
    std::pmr::memory_resource& resource)
    : splitViews_(splitViewCapacity, resource),
      splitters_(splitterCapacity, resource)
{}

usize UISplitViewStateStorage::capacity() const noexcept
{
    return splitViews_.capacity();
}

usize UISplitViewStateStorage::availableSplitViewCount() const noexcept
{
    return splitViews_.availableCount();
}

usize UISplitViewStateStorage::availableSplitterCount() const noexcept
{
    return splitters_.availableCount();
}

bool UISplitViewStateStorage::containsSplitView(UINodeId splitView) const noexcept
{
    return splitViews_.contains(splitView);
}

bool UISplitViewStateStorage::containsSplitter(UINodeId splitter) const noexcept
{
    return splitters_.contains(splitter);
}

SplitViewState* UISplitViewStateStorage::trySplitView(UINodeId splitView) noexcept
{
    return const_cast<SplitViewState*>(std::as_const(*this).trySplitView(splitView));
}

const SplitViewState* UISplitViewStateStorage::trySplitView(UINodeId splitView) const noexcept
{
    return splitViews_.tryGet(splitView);
}

SplitterState* UISplitViewStateStorage::trySplitter(UINodeId splitter) noexcept
{
    return const_cast<SplitterState*>(std::as_const(*this).trySplitter(splitter));
}

const SplitterState* UISplitViewStateStorage::trySplitter(UINodeId splitter) const noexcept
{
    return splitters_.tryGet(splitter);
}

UISplitterPaint& UISplitViewStateStorage::splitterPaintByIndex(u32 nodeIndex) noexcept
{
    SplitterState* state = splitters_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->paint;
}

const UISplitterPaint& UISplitViewStateStorage::splitterPaintByIndex(u32 nodeIndex) const noexcept
{
    const SplitterState* state = splitters_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->paint;
}

SplitViewLayoutScratch& UISplitViewStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    SplitViewState* state = splitViews_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return state->layoutScratch;
}

bool UISplitViewStateStorage::initializeSplitView(
    UINodeId splitView, const UISplitViewConfig& config) noexcept
{
    assert(splitView.hasValue());
    resetNode(splitView.index());
    return splitViews_.insertOrAssign(SplitViewState{
        .node = splitView,
        .config = config,
        .requestedFraction = config.initialFraction,
    });
}

bool UISplitViewStateStorage::initializeSplitter(
    UINodeId splitter, const UISplitterConfig& config) noexcept
{
    assert(splitter.hasValue());
    resetNode(splitter.index());
    return splitters_.insertOrAssign(SplitterState{
        .node = splitter,
        .config = config,
    });
}

bool UISplitViewStateStorage::relationshipMatches(
    UINodeId splitView, const UISplitViewParts& parts) const noexcept
{
    if (!containsSplitView(splitView) || !parts.hasValue())
    {
        return false;
    }
    const SplitViewState* state = trySplitView(splitView);
    const auto owns = [state](UINodeId part) noexcept {
        return state != nullptr &&
               (state->parts.primaryPane == part || state->parts.splitter == part ||
                state->parts.secondaryPane == part);
    };
    const SplitterState* splitter = trySplitter(parts.splitter);
    return state->parts == parts && owns(parts.primaryPane) && owns(parts.splitter) &&
           owns(parts.secondaryPane) &&
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
    if (!part.hasValue())
    {
        return {};
    }
    for (const SplitViewState& state : splitViews_.states())
    {
        if (relationshipMatches(state.node, state.parts) &&
            (state.parts.primaryPane == part || state.parts.splitter == part ||
             state.parts.secondaryPane == part))
        {
            return state.node;
        }
    }
    return {};
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

    SplitViewState* state = trySplitView(splitView);
    SplitterState* splitter = trySplitter(newParts.splitter);
    assert(state != nullptr && splitter != nullptr);
    state->parts = newParts;
    splitter->splitView = splitView;
}

UISplitViewParts UISplitViewStateStorage::unlinkSplitView(UINodeId splitView) noexcept
{
    SplitViewState* state = trySplitView(splitView);
    if (state == nullptr)
    {
        return {};
    }
    const UISplitViewParts previous = state->parts;
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
    if (SplitViewState* view = splitViews_.tryGetByIndex(nodeIndex); view != nullptr)
    {
        static_cast<void>(unlinkSplitView(view->node));
    }
    UINodeId owner{};
    for (const SplitViewState& state : splitViews_.states())
    {
        if ((state.parts.primaryPane.hasValue() && state.parts.primaryPane.index() == nodeIndex) ||
            (state.parts.splitter.hasValue() && state.parts.splitter.index() == nodeIndex) ||
            (state.parts.secondaryPane.hasValue() && state.parts.secondaryPane.index() == nodeIndex))
        {
            owner = state.node;
            break;
        }
    }
    if (containsSplitView(owner))
    {
        static_cast<void>(unlinkSplitView(owner));
    }
    static_cast<void>(splitViews_.eraseByIndex(nodeIndex));
    static_cast<void>(splitters_.eraseByIndex(nodeIndex));
}

bool UISplitViewStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue())
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
    SplitViewState* state = splitViews_.tryGetByIndex(splitViewIndex);
    assert(state != nullptr);
    state->committedMetrics = state->layoutScratch.metrics;
}

UISplitViewMetrics UISplitViewStateStorage::committedMetrics(
    UINodeId splitView) const noexcept
{
    const SplitViewState* state = trySplitView(splitView);
    return state != nullptr ? state->committedMetrics : UISplitViewMetrics{};
}

} // namespace Tina::UI::Detail
