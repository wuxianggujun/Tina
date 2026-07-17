#include "SimulationActionLatch.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/InputRouting.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace Tina::Runtime::Input {
namespace {

inline constexpr usize InvalidActionIndex = (std::numeric_limits<usize>::max)();

[[nodiscard]] Core::Status invariantFailure(std::string_view message)
{
    return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation, message);
}

} // namespace

Core::Result<SimulationActionLatch> SimulationActionLatch::Create(std::span<const InputActionId> sortedActions,
                                                                  u32 transitionCapacity)
{
    if (transitionCapacity == 0 ||
        transitionCapacity > InputActionMapCapacityConfig::MaximumSimulationActionTransitionCapacity)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Simulation Action transition capacity is outside the supported range");
    }
    if (!std::ranges::is_sorted(sortedActions) || std::ranges::adjacent_find(sortedActions) != sortedActions.end() ||
        std::ranges::any_of(sortedActions, [](InputActionId action) { return !action.hasValue(); }))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Simulation Action ids must be non-zero, sorted, and unique");
    }

    try
    {
        std::vector<InputActionState> states;
        states.reserve(sortedActions.size());
        for (InputActionId action : sortedActions)
        {
            states.push_back(InputActionState{.action = action});
        }

        std::vector<bool> deliveredHeld(sortedActions.size(), false);
        std::vector<SimulationActionTransition> transitions;
        transitions.reserve(static_cast<usize>(transitionCapacity) + 1U);
        std::vector<ActionSourceToken> transitionSources;
        transitionSources.reserve(static_cast<usize>(transitionCapacity) + 1U);
        return SimulationActionLatch(std::move(states), std::move(deliveredHeld), std::move(transitions),
                                     std::move(transitionSources), transitionCapacity);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Simulation Action latch allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Simulation Action latch creation failed with an unknown exception");
    }
}

SimulationActionLatch::SimulationActionLatch(std::vector<InputActionState> states, std::vector<bool> deliveredHeld,
                                             std::vector<SimulationActionTransition> transitionStorage,
                                             std::vector<ActionSourceToken> transitionSourceStorage,
                                             usize transitionCapacity) noexcept
    : states_(std::move(states)), deliveredHeld_(std::move(deliveredHeld)), transitions_(std::move(transitionStorage)),
      transitionSources_(std::move(transitionSourceStorage)), transitionCapacity_(transitionCapacity)
{
}

Core::Status SimulationActionLatch::validateNextUncompletedTick(u64 tick) const
{
    if (lastCompletedTick_.has_value() && tick <= *lastCompletedTick_)
    {
        return invariantFailure("next uncompleted Simulation tick must be greater than the last completed tick");
    }
    if (targetTick_.has_value() && *targetTick_ != tick)
    {
        return invariantFailure("pending Simulation Action transitions belong to another target tick");
    }
    return Core::success();
}

Core::Status SimulationActionLatch::setHeld(InputActionId action, bool held) noexcept
{
    const usize index = findActionIndex(action);
    if (index == InvalidActionIndex)
    {
        return invariantFailure("Simulation Action state references an unknown action id");
    }
    states_[index].held = held;
    states_[index].axis = held ? 1.0F : 0.0F;
    return Core::success();
}

bool SimulationActionLatch::isHeld(InputActionId action) const noexcept
{
    const usize index = findActionIndex(action);
    return index != InvalidActionIndex && states_[index].held;
}

Core::Result<SimulationLatchAppendResult>
SimulationActionLatch::append(u64 targetTick, DigitalActionTransition transition, ActionSourceToken source)
{
    if (!transition.action.hasValue() || findActionIndex(transition.action) == InvalidActionIndex)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "Simulation Action transition references an unknown action id");
    }
    if (auto targetStatus = ensureTarget(targetTick); !targetStatus)
    {
        return Core::failure(std::move(targetStatus.error()));
    }

    if (normalTransitionCount_ >= transitionCapacity_)
    {
        transitions_.clear();
        transitionSources_.clear();
        normalTransitionCount_ = 0;
        transitions_.emplace_back(SimulationInputStreamReset{
            .reason = ActionInputStreamResetReason::ActionTransitionCapacityExceeded,
            .sourceSequence = transition.sourceSequence,
        });
        transitionSources_.push_back(InvalidActionSourceToken);
        ++statistics_.capacityResetCount;
        return SimulationLatchAppendResult::CapacityResetInserted;
    }

    transitions_.emplace_back(std::move(transition));
    transitionSources_.push_back(source);
    ++normalTransitionCount_;
    return SimulationLatchAppendResult::Appended;
}

Core::Result<SimulationLatchAppendResult>
SimulationActionLatch::reconcileCancellation(u64 targetTick, InputActionId action, ActionSourceToken source,
                                             u64 sourceSequence, bool forceStateReconciliation)
{
    const usize index = findActionIndex(action);
    if (index == InvalidActionIndex)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "Simulation Action cancellation references an unknown action id");
    }
    if (auto targetStatus = validateNextUncompletedTick(targetTick); !targetStatus)
    {
        return Core::failure(std::move(targetStatus.error()));
    }

    const bool removedPendingSource = removePendingTransitions(action, source);
    if (!forceStateReconciliation && !removedPendingSource)
    {
        return SimulationLatchAppendResult::NoTransitionNeeded;
    }
    const bool delivered = deliveredHeld_[index];
    const bool current = states_[index].held;
    bool pendingResult = delivered;
    for (const SimulationActionTransition& pending : transitions_)
    {
        const auto* digital = std::get_if<DigitalActionTransition>(&pending);
        if (digital == nullptr || digital->action != action)
        {
            continue;
        }
        pendingResult = digital->kind == DigitalActionTransitionKind::Pressed;
    }
    if (pendingResult == current)
    {
        return SimulationLatchAppendResult::NoTransitionNeeded;
    }

    return append(targetTick,
                  DigitalActionTransition{
                      .action = action,
                      .kind = current ? DigitalActionTransitionKind::Pressed : DigitalActionTransitionKind::Cancelled,
                      .sourceSequence = sourceSequence,
                  },
                  source);
}

Core::Status SimulationActionLatch::resetStream(u64 targetTick, SimulationInputStreamReset reset)
{
    if (auto targetStatus = validateNextUncompletedTick(targetTick); !targetStatus)
    {
        return targetStatus;
    }
    transitions_.clear();
    transitionSources_.clear();
    normalTransitionCount_ = 0;
    targetTick_ = targetTick;
    transitions_.emplace_back(reset);
    transitionSources_.push_back(InvalidActionSourceToken);
    if (reset.reason == ActionInputStreamResetReason::RawInputStreamReset)
    {
        ++statistics_.rawInputResetCount;
    } else
    {
        ++statistics_.capacityResetCount;
    }
    return Core::success();
}

Core::Result<SimulationActionSnapshot> SimulationActionLatch::snapshotForTick(u64 tick) const
{
    if (lastCompletedTick_.has_value() && tick <= *lastCompletedTick_)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "Simulation Action snapshot requested for an already completed tick");
    }
    if (targetTick_.has_value() && *targetTick_ != tick)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "Simulation Action snapshot requested before its pending target tick");
    }

    return SimulationActionSnapshot{
        .targetSimulationTick = tick,
        .states = states_,
        .transitions = targetTick_.has_value() ? std::span<const SimulationActionTransition>(transitions_)
                                               : std::span<const SimulationActionTransition>{},
    };
}

Core::Status SimulationActionLatch::completeTick(u64 tick)
{
    if (lastCompletedTick_.has_value() && tick <= *lastCompletedTick_)
    {
        return invariantFailure("Simulation Action tick completion must be strictly monotonic");
    }
    if (targetTick_.has_value() && *targetTick_ != tick)
    {
        return invariantFailure("a Simulation tick completed before the pending Action target tick");
    }

    for (usize index = 0; index < states_.size(); ++index)
    {
        deliveredHeld_[index] = states_[index].held;
    }
    clearPending();
    lastCompletedTick_ = tick;
    return Core::success();
}

usize SimulationActionLatch::findActionIndex(InputActionId action) const noexcept
{
    const auto iterator = std::ranges::lower_bound(states_, action, {}, &InputActionState::action);
    return iterator != states_.end() && iterator->action == action
               ? static_cast<usize>(std::distance(states_.begin(), iterator))
               : InvalidActionIndex;
}

Core::Status SimulationActionLatch::ensureTarget(u64 targetTick)
{
    if (auto status = validateNextUncompletedTick(targetTick); !status)
    {
        return status;
    }
    if (!targetTick_.has_value())
    {
        targetTick_ = targetTick;
    }
    return Core::success();
}

bool SimulationActionLatch::removePendingTransitions(InputActionId action, ActionSourceToken source) noexcept
{
    usize destination = 0;
    bool removed = false;
    for (usize index = 0; index < transitions_.size(); ++index)
    {
        const auto* digital = std::get_if<DigitalActionTransition>(&transitions_[index]);
        const bool remove = digital != nullptr && digital->action == action && transitionSources_[index] == source;
        if (remove)
        {
            removed = true;
            continue;
        }
        if (destination != index)
        {
            transitions_[destination] = std::move(transitions_[index]);
            transitionSources_[destination] = transitionSources_[index];
        }
        ++destination;
    }
    transitions_.resize(destination);
    transitionSources_.resize(destination);
    normalTransitionCount_ =
        static_cast<usize>(std::ranges::count_if(transitions_, [](const SimulationActionTransition& transition) {
            return std::holds_alternative<DigitalActionTransition>(transition);
        }));
    if (transitions_.empty())
    {
        targetTick_.reset();
    }
    return removed;
}

void SimulationActionLatch::clearPending() noexcept
{
    transitions_.clear();
    transitionSources_.clear();
    normalTransitionCount_ = 0;
    targetTick_.reset();
}

} // namespace Tina::Runtime::Input
