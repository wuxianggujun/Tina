#include "UITooltipStateStorage.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>

namespace Tina::UI::Detail {

UITooltipStateStorage::UITooltipStateStorage(usize capacity,
                                             std::pmr::memory_resource& resource)
    : states_(capacity, resource), rollbackStates_(&resource)
{
    rollbackStates_.reserve(capacity);
}

usize UITooltipStateStorage::capacity() const noexcept
{
    return states_.capacity();
}

usize UITooltipStateStorage::activeCount() const noexcept
{
    return states_.size();
}

usize UITooltipStateStorage::availableCount() const noexcept
{
    return states_.availableCount();
}

bool UITooltipStateStorage::containsTooltip(UINodeId tooltip) const noexcept
{
    return states_.contains(tooltip);
}

TooltipState* UITooltipStateStorage::tryState(UINodeId tooltip) noexcept
{
    return const_cast<TooltipState*>(std::as_const(*this).tryState(tooltip));
}

const TooltipState* UITooltipStateStorage::tryState(UINodeId tooltip) const noexcept
{
    return states_.tryGet(tooltip);
}

TooltipState& UITooltipStateStorage::stateByIndex(u32 nodeIndex) noexcept
{
    TooltipState* state = states_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return *state;
}

const TooltipState& UITooltipStateStorage::stateByIndex(u32 nodeIndex) const noexcept
{
    const TooltipState* state = states_.tryGetByIndex(nodeIndex);
    assert(state != nullptr);
    return *state;
}

TooltipLayoutScratch& UITooltipStateStorage::layoutScratchByIndex(u32 nodeIndex) noexcept
{
    return stateByIndex(nodeIndex).layoutScratch;
}

const TooltipLayoutScratch& UITooltipStateStorage::layoutScratchByIndex(u32 nodeIndex) const noexcept
{
    return stateByIndex(nodeIndex).layoutScratch;
}

bool UITooltipStateStorage::initializeTooltip(UINodeId tooltip,
                                              const UITooltipConfig& config) noexcept
{
    assert(tooltip.hasValue());
    resetNode(tooltip.index());
    return states_.insertOrAssign(TooltipState{
        .node = tooltip,
        .config = config,
    });
}

void UITooltipStateStorage::resetNode(u32 nodeIndex) noexcept
{
    TooltipState* nodeState = states_.tryGetByIndex(nodeIndex);
    const UINodeId tooltip = nodeState != nullptr ? nodeState->node : UINodeId{};
    if (tooltip.hasValue())
    {
        static_cast<void>(unlinkTooltip(tooltip));
        if (presentation_.active == tooltip)
        {
            presentation_.active = {};
        }
        if (presentation_.pending == tooltip)
        {
            cancelPendingOpen();
        }
        if (presentation_.manual == tooltip)
        {
            presentation_.manual = {};
        }
    }

    for (TooltipState& state : states_.states())
    {
        if (state.anchor.hasValue() && state.anchor.index() == nodeIndex)
        {
            state.anchor = {};
            state.suppressedUntilTriggerReset = false;
            break;
        }
    }
    if (presentation_.hoveredAnchor.hasValue() &&
        presentation_.hoveredAnchor.index() == nodeIndex)
    {
        presentation_.hoveredAnchor = {};
    }
    static_cast<void>(states_.eraseByIndex(nodeIndex));
}

bool UITooltipStateStorage::releaseNode(UINodeId node,
                                        Core::MonotonicTimePoint now) noexcept
{
    if (!node.hasValue())
    {
        return false;
    }

    bool changed = false;
    if (containsTooltip(node))
    {
        changed = dismiss(node, false, now) || changed;
        static_cast<void>(unlinkTooltip(node));
    }
    if (const UINodeId tooltip = tooltipForAnchor(node); tooltip.hasValue())
    {
        changed = dismiss(tooltip, false, now) || changed;
        static_cast<void>(unlinkAnchor(node));
    }
    resetNode(node.index());
    return changed;
}

bool UITooltipStateStorage::hasRelationship(UINodeId tooltip,
                                            UINodeId anchor) const noexcept
{
    const TooltipState* state = tryState(tooltip);
    return state != nullptr && anchor.hasValue() && state->anchor == anchor &&
           tooltipForAnchor(anchor) == tooltip;
}

UINodeId UITooltipStateStorage::tooltipForAnchor(UINodeId anchor) const noexcept
{
    if (!anchor.hasValue())
    {
        return {};
    }
    for (const TooltipState& state : states_.states())
    {
        if (state.anchor == anchor)
        {
            return state.node;
        }
    }
    return {};
}

UINodeId UITooltipStateStorage::anchorForTooltip(UINodeId tooltip) const noexcept
{
    const TooltipState* state = tryState(tooltip);
    return state != nullptr && hasRelationship(tooltip, state->anchor)
               ? state->anchor
               : UINodeId{};
}

void UITooltipStateStorage::linkValidated(UINodeId tooltip, UINodeId anchor) noexcept
{
    assert(containsTooltip(tooltip));
    assert(anchor.hasValue());
    static_cast<void>(unlinkTooltip(tooltip));
    static_cast<void>(unlinkAnchor(anchor));
    TooltipState* state = tryState(tooltip);
    assert(state != nullptr);
    state->anchor = anchor;
    state->suppressedUntilTriggerReset = false;
}

UINodeId UITooltipStateStorage::unlinkTooltip(UINodeId tooltip) noexcept
{
    TooltipState* state = tryState(tooltip);
    if (state == nullptr)
    {
        return {};
    }
    const UINodeId anchor = state->anchor;
    state->anchor = {};
    state->suppressedUntilTriggerReset = false;
    return anchor;
}

UINodeId UITooltipStateStorage::unlinkAnchor(UINodeId anchor) noexcept
{
    if (!anchor.hasValue())
    {
        return {};
    }
    const UINodeId tooltip = tooltipForAnchor(anchor);
    if (TooltipState* state = tryState(tooltip); state != nullptr && state->anchor == anchor)
    {
        state->anchor = {};
        state->suppressedUntilTriggerReset = false;
    }
    return tooltip;
}

void UITooltipStateStorage::setHoveredAnchor(UINodeId anchor) noexcept
{
    presentation_.hoveredAnchor = anchor;
}

UINodeId UITooltipStateStorage::hoveredAnchor() const noexcept
{
    return presentation_.hoveredAnchor;
}

UINodeId UITooltipStateStorage::activeTooltip() const noexcept
{
    return presentation_.active;
}

UINodeId UITooltipStateStorage::pendingTooltip() const noexcept
{
    return presentation_.pending;
}

UINodeId UITooltipStateStorage::manualTooltip() const noexcept
{
    return presentation_.manual;
}

Core::MonotonicTimePoint UITooltipStateStorage::deadlineAfter(
    Core::MonotonicTimePoint now, Core::Duration delay) noexcept
{
    if (delay <= Core::Duration::zero())
    {
        return now;
    }
    const Core::Duration maximumNativeDelay =
        std::chrono::duration_cast<Core::Duration>(Core::MonotonicDuration::max());
    if (delay >= maximumNativeDelay)
    {
        return Core::MonotonicTimePoint::max();
    }
    const Core::MonotonicDuration nativeDelay =
        std::chrono::duration_cast<Core::MonotonicDuration>(delay);
    const auto nowTicks = now.time_since_epoch().count();
    const auto delayTicks = nativeDelay.count();
    const auto maximumTicks =
        (std::numeric_limits<Core::MonotonicDuration::rep>::max)();
    if (delayTicks > 0 && nowTicks > maximumTicks - delayTicks)
    {
        return Core::MonotonicTimePoint::max();
    }
    return Core::MonotonicTimePoint{Core::MonotonicDuration{nowTicks + delayTicks}};
}

bool UITooltipStateStorage::rawTriggerActive(const TooltipState& state,
                                             UINodeId focusedAnchor) const noexcept
{
    if (!hasRelationship(state.node, state.anchor))
    {
        return false;
    }
    return (hasTooltipTrigger(state.config.triggers, UITooltipTrigger::PointerHover) &&
            presentation_.hoveredAnchor == state.anchor) ||
           (hasTooltipTrigger(state.config.triggers, UITooltipTrigger::KeyboardFocus) &&
            focusedAnchor == state.anchor) ||
           (hasTooltipTrigger(state.config.triggers, UITooltipTrigger::Manual) &&
            state.manualRequested && presentation_.manual == state.node);
}

void UITooltipStateStorage::clearResettableSuppression(UINodeId focusedAnchor) noexcept
{
    for (TooltipState& state : states_.states())
    {
        if (state.node.hasValue() && state.suppressedUntilTriggerReset &&
            !rawTriggerActive(state, focusedAnchor))
        {
            state.suppressedUntilTriggerReset = false;
        }
    }
}

void UITooltipStateStorage::cancelPendingOpen() noexcept
{
    presentation_.pending = {};
    presentation_.openDeadline = {};
    presentation_.openPending = false;
}

bool UITooltipStateStorage::activate(UINodeId tooltip) noexcept
{
    TooltipState* state = tryState(tooltip);
    if (state == nullptr)
    {
        return false;
    }
    bool changed = false;
    if (presentation_.active.hasValue() && presentation_.active != tooltip)
    {
        if (TooltipState* previous = tryState(presentation_.active); previous != nullptr)
        {
            changed = previous->open || changed;
            previous->open = false;
        }
    }
    changed = changed || !state->open || presentation_.active != tooltip;
    state->open = true;
    state->suppressedUntilTriggerReset = false;
    presentation_.active = tooltip;
    cancelPendingOpen();
    presentation_.closeDeadline = {};
    presentation_.closePending = false;
    presentation_.reshowExpiry = {};
    presentation_.reshowAvailable = false;
    return changed;
}

bool UITooltipStateStorage::deactivate(UINodeId tooltip, bool suppressState,
                                      bool allowReshow,
                                      Core::MonotonicTimePoint now) noexcept
{
    TooltipState* state = tryState(tooltip);
    if (state == nullptr)
    {
        if (presentation_.active == tooltip)
        {
            presentation_.active = {};
        }
        if (presentation_.pending == tooltip)
        {
            cancelPendingOpen();
        }
        presentation_.closePending = false;
        presentation_.closeDeadline = {};
        return false;
    }
    const bool changed = state->open || presentation_.active == tooltip;
    state->open = false;
    state->suppressedUntilTriggerReset =
        state->suppressedUntilTriggerReset || suppressState;
    if (presentation_.active == tooltip)
    {
        presentation_.active = {};
    }
    if (presentation_.pending == tooltip)
    {
        cancelPendingOpen();
    }
    presentation_.closePending = false;
    presentation_.closeDeadline = {};
    if (allowReshow)
    {
        const double windowSeconds = (std::max)(state->config.initialDelay.count(),
                                                state->config.reshowDelay.count());
        presentation_.reshowAvailable = true;
        presentation_.reshowExpiry = deadlineAfter(now, Core::Duration{windowSeconds});
    }
    else
    {
        presentation_.reshowAvailable = false;
        presentation_.reshowExpiry = {};
    }
    return changed;
}

void UITooltipStateStorage::suppress(UINodeId tooltip) noexcept
{
    if (TooltipState* state = tryState(tooltip); state != nullptr)
    {
        state->suppressedUntilTriggerReset = true;
    }
}

bool UITooltipStateStorage::requestManual(UINodeId tooltip, bool eligible) noexcept
{
    TooltipState* state = tryState(tooltip);
    if (state == nullptr)
    {
        return false;
    }
    if (presentation_.manual.hasValue() && presentation_.manual != tooltip)
    {
        if (TooltipState* previous = tryState(presentation_.manual); previous != nullptr)
        {
            previous->manualRequested = false;
        }
    }
    presentation_.manual = tooltip;
    state->manualRequested = true;
    state->suppressedUntilTriggerReset = false;
    return eligible ? activate(tooltip) : true;
}

bool UITooltipStateStorage::dismiss(UINodeId tooltip, bool suppressState,
                                    Core::MonotonicTimePoint now) noexcept
{
    TooltipState* state = tryState(tooltip);
    if (state == nullptr)
    {
        return false;
    }
    state->manualRequested = false;
    state->suppressedUntilTriggerReset =
        state->suppressedUntilTriggerReset || suppressState;
    if (presentation_.manual == tooltip)
    {
        presentation_.manual = {};
    }
    if (presentation_.pending == tooltip)
    {
        cancelPendingOpen();
    }
    return presentation_.active == tooltip || state->open
               ? deactivate(tooltip, suppressState, false, now)
               : false;
}

bool UITooltipStateStorage::hardDismiss(UINodeId focusedAnchor, bool suppressState) noexcept
{
    if (suppressState)
    {
        suppress(presentation_.active);
        suppress(presentation_.pending);
        suppress(presentation_.manual);
        suppress(tooltipForAnchor(presentation_.hoveredAnchor));
        suppress(tooltipForAnchor(focusedAnchor));
    }
    bool changed = false;
    for (TooltipState& state : states_.states())
    {
        if (!state.node.hasValue())
        {
            continue;
        }
        changed = changed || state.open;
        state.open = false;
        state.manualRequested = false;
    }
    presentation_.active = {};
    cancelPendingOpen();
    presentation_.manual = {};
    presentation_.closeDeadline = {};
    presentation_.closePending = false;
    presentation_.reshowExpiry = {};
    presentation_.reshowAvailable = false;
    return changed;
}

UINodeId UITooltipStateStorage::desiredTooltip(
    const UITooltipAdvanceInput& input) const noexcept
{
    const auto candidate = [this](const UITooltipAdvanceCandidate& value,
                                  UITooltipTrigger trigger) noexcept {
        const TooltipState* state = tryState(value.tooltip);
        return state != nullptr && value.eligible &&
                       (trigger != UITooltipTrigger::Manual || state->manualRequested) &&
                       !state->suppressedUntilTriggerReset &&
                       hasTooltipTrigger(state->config.triggers, trigger)
                   ? value.tooltip
                   : UINodeId{};
    };
    if (const UINodeId manual = candidate(input.manual, UITooltipTrigger::Manual);
        manual.hasValue())
    {
        return manual;
    }
    if (const UINodeId hover = candidate(input.hover, UITooltipTrigger::PointerHover);
        hover.hasValue())
    {
        return hover;
    }
    return candidate(input.focus, UITooltipTrigger::KeyboardFocus);
}

bool UITooltipStateStorage::advance(const UITooltipAdvanceInput& input) noexcept
{
    bool changed = false;
    if (presentation_.reshowAvailable && input.now > presentation_.reshowExpiry)
    {
        presentation_.reshowAvailable = false;
        presentation_.reshowExpiry = {};
    }
    clearResettableSuppression(input.focusedAnchor);

    if (presentation_.active.hasValue() &&
        (input.active.tooltip != presentation_.active || !input.active.eligible))
    {
        if (TooltipState* active = tryState(presentation_.active); active != nullptr)
        {
            active->manualRequested = false;
        }
        if (presentation_.manual == presentation_.active)
        {
            presentation_.manual = {};
        }
        changed = deactivate(presentation_.active, true, false, input.now) || changed;
    }

    UINodeId desired = desiredTooltip(input);
    if (presentation_.active.hasValue())
    {
        if (desired == presentation_.active)
        {
            presentation_.closePending = false;
            presentation_.closeDeadline = {};
            cancelPendingOpen();
            return changed;
        }
        if (desired.hasValue() && desired == presentation_.manual)
        {
            return activate(desired) || changed;
        }
        if (!presentation_.closePending)
        {
            const TooltipState* active = tryState(presentation_.active);
            presentation_.closeDeadline =
                deadlineAfter(input.now, active != nullptr ? active->config.dismissDelay
                                                           : Core::Duration::zero());
            presentation_.closePending = true;
        }
        if (input.now < presentation_.closeDeadline)
        {
            return changed;
        }
        const UINodeId closing = presentation_.active;
        changed = deactivate(closing, false, true, input.now) || changed;
        desired = desiredTooltip(input);
    }

    if (!desired.hasValue())
    {
        cancelPendingOpen();
        return changed;
    }
    if (desired == presentation_.manual)
    {
        return activate(desired) || changed;
    }
    if (!presentation_.openPending || presentation_.pending != desired)
    {
        const TooltipState* state = tryState(desired);
        assert(state != nullptr);
        const Core::Duration delay = presentation_.reshowAvailable
                                         ? state->config.reshowDelay
                                         : state->config.initialDelay;
        presentation_.pending = desired;
        presentation_.openDeadline = deadlineAfter(input.now, delay);
        presentation_.openPending = true;
    }
    if (input.now >= presentation_.openDeadline)
    {
        changed = activate(desired) || changed;
    }
    return changed;
}

void UITooltipStateStorage::publishMetrics(u32 tooltipIndex) noexcept
{
    TooltipState* state = states_.tryGetByIndex(tooltipIndex);
    assert(state != nullptr);
    state->committedMetrics = state->layoutScratch.metrics;
}

UITooltipMetrics UITooltipStateStorage::committedMetrics(UINodeId tooltip) const noexcept
{
    const TooltipState* state = tryState(tooltip);
    return state != nullptr ? state->committedMetrics : UITooltipMetrics{};
}

void UITooltipStateStorage::beginCommitTransaction() noexcept
{
    rollbackStates_.assign(states_.states().begin(), states_.states().end());
    rollbackPresentation_ = presentation_;
}

void UITooltipStateStorage::rollbackCommitTransaction() noexcept
{
    assert(rollbackStates_.size() == states_.size());
    std::copy(rollbackStates_.begin(), rollbackStates_.end(), states_.states().begin());
    presentation_ = rollbackPresentation_;
}

} // namespace Tina::UI::Detail
