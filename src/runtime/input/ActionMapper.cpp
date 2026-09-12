#include "ActionMapper.hpp"
#include "LastPresentedCamera2DLatch.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina::Runtime::Input {
namespace {

inline constexpr float ActionEpsilon = ActionValueEpsilon;
inline constexpr usize InvalidIndex = InvalidBindingIndex;

template <typename... Callables> struct Overloaded : Callables... {
    using Callables::operator()...;
};
template <typename... Callables> Overloaded(Callables...) -> Overloaded<Callables...>;

[[nodiscard]] bool active(float value) noexcept
{
    return std::abs(value) > ActionEpsilon;
}

[[nodiscard]] bool sameValue(float left, float right) noexcept
{
    return std::abs(left - right) <= ActionEpsilon;
}

[[nodiscard]] bool validMapperCapacities(const InputActionMapperCapacityConfig& capacities) noexcept
{
    return capacities.rawInputTransitionCapacity != 0 &&
           capacities.rawInputTransitionCapacity <=
               Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity &&
           capacities.continuousControlClaimCapacity != 0 &&
           capacities.continuousControlClaimCapacity <=
               InputActionMapperCapacityConfig::MaximumContinuousControlClaimCapacity;
}

[[nodiscard]] bool gamepadPattern(const ActionBindingPattern& pattern) noexcept
{
    return std::holds_alternative<StandardGamepadButtonBinding>(pattern) ||
           std::holds_alternative<StandardGamepadAxisBinding>(pattern);
}

[[nodiscard]] usize sourceCount(const ActionBindingPattern& pattern) noexcept
{
    return gamepadPattern(pattern) ? Platform::PlatformFrameBuilder::MaximumGamepads : 1U;
}

[[nodiscard]] float normalizeAxis(float raw, GamepadAxisValueMode mode, float deadzone, float scale) noexcept
{
    float normalized = 0.0F;
    switch (mode)
    {
    case GamepadAxisValueMode::Signed:
        normalized = std::clamp(raw, -1.0F, 1.0F);
        break;
    case GamepadAxisValueMode::PositiveHalf:
        normalized = std::clamp(raw, 0.0F, 1.0F);
        break;
    case GamepadAxisValueMode::NegativeHalf:
        normalized = std::clamp(-raw, 0.0F, 1.0F);
        break;
    case GamepadAxisValueMode::Trigger:
        normalized = std::clamp((raw + 1.0F) * 0.5F, 0.0F, 1.0F);
        break;
    }

    const float magnitude = std::abs(normalized);
    if (magnitude <= deadzone)
    {
        return 0.0F;
    }
    const float outsideDeadzone = (magnitude - deadzone) / (1.0F - deadzone);
    return std::copysign(outsideDeadzone, normalized) * scale;
}

[[nodiscard]] InputActionTransitionKind transitionKind(float previous, float current, bool cancelled) noexcept
{
    if (!active(previous) && active(current))
    {
        return InputActionTransitionKind::Started;
    }
    if (active(previous) && !active(current))
    {
        return cancelled ? InputActionTransitionKind::Cancelled : InputActionTransitionKind::Completed;
    }
    return InputActionTransitionKind::ValueChanged;
}

[[nodiscard]] Core::Status invariantFailure(std::string_view message)
{
    return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation, message);
}

[[nodiscard]] Core::Status invalidTransaction(std::string_view message)
{
    return Core::failure(RuntimeErrorCode::InvalidRebindTransaction, message);
}

[[nodiscard]] const Platform::WindowFrameSnapshot*
primaryWindow(const Platform::PlatformFrameView& frame) noexcept
{
    return frame.primaryWindow();
}

[[nodiscard]] Core::Result<std::optional<Render::WorldPointerSample>>
pickWorldPointerSample(const Platform::PlatformFrameView& platformFrame, InputActionDomain domain,
                       bool emitsChange, const Platform::PointerButtonTransition* pointerTransition,
                       const LastPresentedCamera2DLatch* lastPresentedCamera2D, u64 sequence)
{
    if (!emitsChange || domain != InputActionDomain::Simulation || pointerTransition == nullptr)
    {
        return std::optional<Render::WorldPointerSample>{};
    }
    if (lastPresentedCamera2D == nullptr)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "World pointer Simulation Action requires the last-presented Camera2D latch");
    }
    const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
    if (primary == nullptr)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "World pointer Simulation Action requires a primary Window");
    }

    auto pick = lastPresentedCamera2D->pickLogical(
        pointerTransition->logicalX, pointerTransition->logicalY,
        primary->metrics.logicalExtent.width, primary->metrics.logicalExtent.height, sequence);
    if (!pick)
    {
        return Core::failure(std::move(pick.error()));
    }
    return std::optional<Render::WorldPointerSample>{std::move(*pick)};
}

} // namespace

Core::Result<std::unique_ptr<ActionMapper>>
ActionMapper::Create(InputActionMapConfig config, InputActionMapperCapacityConfig capacities)
{
    if (!validMapperCapacities(capacities))
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "Input Action capacity is outside the supported range");
    }
    try
    {
        if (auto status = config.validate(); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        u32 nextAutomaticBindingId = 1;
        for (InputActionBinding& binding : config.bindings)
        {
            if (!binding.binding.hasValue())
            {
                while (std::ranges::any_of(config.bindings, [nextAutomaticBindingId](const InputActionBinding& other) {
                    return other.binding == InputBindingId{nextAutomaticBindingId};
                }))
                {
                    if (nextAutomaticBindingId == (std::numeric_limits<u32>::max)())
                    {
                        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                                             "Automatic Action binding id space is exhausted");
                    }
                    ++nextAutomaticBindingId;
                }
                binding.binding = InputBindingId{nextAutomaticBindingId};
                if (nextAutomaticBindingId != (std::numeric_limits<u32>::max)())
                {
                    ++nextAutomaticBindingId;
                }
            }
        }

        std::vector<ActionRecord> actions;
        actions.reserve(config.bindings.size());
        for (const InputActionBinding& binding : config.bindings)
        {
            if (std::ranges::none_of(actions, [&binding](const ActionRecord& action) {
                    return action.action == binding.action;
                }))
            {
                actions.push_back(ActionRecord{
                    .action = binding.action,
                    .domain = binding.domain,
                    .composition = binding.composition,
                });
            }
        }
        std::ranges::sort(actions, [](const ActionRecord& left, const ActionRecord& right) {
            if (left.domain != right.domain)
            {
                return left.domain < right.domain;
            }
            return left.action < right.action;
        });

        std::vector<InputActionId> simulationActionIds;
        std::vector<InputActionState> frameStates;
        simulationActionIds.reserve(actions.size());
        frameStates.reserve(actions.size());
        for (ActionRecord& action : actions)
        {
            if (action.domain == InputActionDomain::Simulation)
            {
                action.stateIndex = simulationActionIds.size();
                simulationActionIds.push_back(action.action);
            } else
            {
                action.stateIndex = frameStates.size();
                frameStates.push_back(InputActionState{.action = action.action});
            }
        }

        auto latchResult = SimulationActionLatch::Create(
            simulationActionIds, config.capacities.simulationActionTransitionCapacity);
        if (!latchResult)
        {
            return Core::failure(std::move(latchResult.error()));
        }

        std::vector<BindingRecord> records;
        records.reserve(config.bindings.size());
        for (usize index = 0; index < config.bindings.size(); ++index)
        {
            const InputActionBinding& binding = config.bindings[index];
            const auto action = std::ranges::find_if(actions, [&binding](const ActionRecord& candidate) {
                return candidate.action == binding.action;
            });
            if (action == actions.end())
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "Action binding could not resolve its normalized Action record");
            }
            records.push_back(BindingRecord{
                .actionIndex = static_cast<usize>(std::distance(actions.begin(), action)),
                .sourceOffset = index * Platform::PlatformFrameBuilder::MaximumGamepads,
            });
        }

        const usize sourceStorageSize =
            config.bindings.size() * Platform::PlatformFrameBuilder::MaximumGamepads;
        std::vector<SourceState> sources(sourceStorageSize);
        std::vector<FrameActionTransition> frameTransitions;
        frameTransitions.reserve(static_cast<usize>(config.capacities.frameActionTransitionCapacity) + 1U);
        std::vector<u8> affectedActionsScratch(actions.size(), u8{0});

        auto mapper = std::unique_ptr<ActionMapper>(new (std::nothrow) ActionMapper(
            capacities, config.capacities, std::move(config.bindings), std::move(records),
            std::move(sources), std::move(actions), std::move(frameStates),
            std::move(frameTransitions), std::move(affectedActionsScratch), std::move(*latchResult)));
        if (mapper == nullptr)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "Action Mapper allocation failed");
        }
        return mapper;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Action Mapper storage allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Action Mapper creation failed with an unknown exception");
    }
}

ActionMapper::ActionMapper(InputActionMapperCapacityConfig capacities,
                           InputActionMapCapacityConfig mapCapacities,
                           std::vector<InputActionBinding> bindings,
                           std::vector<BindingRecord> records, std::vector<SourceState> sources,
                           std::vector<ActionRecord> actions,
                           std::vector<InputActionState> frameStates,
                           std::vector<FrameActionTransition> frameTransitions,
                           std::vector<u8> affectedActionsScratch,
                           SimulationActionLatch simulationLatch)
    : capacities_(capacities), mapCapacities_(mapCapacities), bindings_(std::move(bindings)),
      records_(std::move(records)), sources_(std::move(sources)), actions_(std::move(actions)),
      stagedSources_(bindings_.size()), stagedActions_(actions_.size()), stagedActionOrder_(actions_.size()),
      frameActionStates_(std::move(frameStates)), frameTransitions_(std::move(frameTransitions)),
      affectedActionsScratch_(std::move(affectedActionsScratch)),
      simulationLatch_(std::move(simulationLatch))
{
    rebindConflicts_.reserve(bindings_.size());
    cancellationActionsScratch_.reserve(actions_.size());
    rebuildControlIndex();
    for (usize index = bindings_.size(); index-- > 0;)
    {
        ActionRecord& action = actions_[records_[index].actionIndex];
        records_[index].nextForAction = action.firstBinding;
        action.firstBinding = index;
    }
}

Core::Status ActionMapper::mapFrame(const Platform::PlatformFrameView& platformFrame,
                                    const UI::InputTransitionConsumptionView& consumption,
                                    const UI::ContinuousControlClaimsView& claims, u64 engineFrameIndex,
                                    u64 nextUncompletedSimulationTick,
                                    const LastPresentedCamera2DLatch* lastPresentedCamera2D)
{
    if (auto validation = validateFrameInputs(platformFrame, consumption, claims, engineFrameIndex,
                                              nextUncompletedSimulationTick);
        !validation)
    {
        return validation;
    }
    if (auto windowStatus = synchronizePrimaryWindow(platformFrame); !windowStatus)
    {
        return windowStatus;
    }

    const auto transitions = platformFrame.inputTransitions();
    const u64 frameAnchor =
        transitions.empty() ? lastAcceptedRawInputSequence_.value_or(0) : transitions.front().sequence;

    currentEngineFrameIndex_ = engineFrameIndex;
    frameTransitions_.clear();
    frameNormalTransitionCount_ = 0;
    frameResetWritten_ = false;
    framePointerLookClaimed_ = false;
    framePointerLookDeltaX_ = 0.0;
    framePointerLookDeltaY_ = 0.0;
    framePointerDeltaClaimed_.fill(false);
    framePointerWheelClaimed_.fill(false);
    for (usize slot = 0; slot < framePointers_.size(); ++slot)
    {
        // Reset rather than reuse: an absent slot must not publish the previous frame's
        // buttons or motion, and a slot the backend no longer reports must go quiet.
        framePointers_[slot] = FramePointerState{.pointer = static_cast<Platform::PointerId>(slot)};
    }
    if (const Platform::WindowFrameSnapshot* window = primaryWindow(platformFrame); window != nullptr)
    {
        for (usize slot = 0; slot < framePointers_.size(); ++slot)
        {
            const Platform::PointerSnapshot* pointer =
                window->input.pointerSnapshot(static_cast<Platform::PointerId>(slot));
            if (pointer == nullptr)
            {
                continue;
            }
            FramePointerState& state = framePointers_[slot];
            state.present = pointer->present;
            state.logicalX = pointer->logicalX;
            state.logicalY = pointer->logicalY;
            state.deltaX = pointer->accumulatedDeltaX;
            state.deltaY = pointer->accumulatedDeltaY;
            state.heldButtons = pointer->heldButtons;
        }
        // The scalar look delta stays a separate field rather than a read through the table,
        // because it has its own claim flag and its own published contract.
        framePointerLookDeltaX_ = framePointers_[Platform::PrimaryPointerId].deltaX;
        framePointerLookDeltaY_ = framePointers_[Platform::PrimaryPointerId].deltaY;
    }
    for (ActionRecord& action : actions_)
    {
        action.frameStartValue = action.value;
    }
    for (SourceState& source : sources_)
    {
        source.claimedThisFrame = false;
    }

    observeRebindDeviceChanges(platformFrame);
    if (pendingRebind_.has_value())
    {
        if (auto status = applyPendingRebind(platformFrame, frameAnchor, nextUncompletedSimulationTick); !status)
        {
            return status;
        }
    }
    if (auto claimStatus = applyClaims(platformFrame, claims, frameAnchor,
                                       nextUncompletedSimulationTick);
        !claimStatus)
    {
        return claimStatus;
    }

    for (usize ordinal = 0; ordinal < transitions.size(); ++ordinal)
    {
        const bool consumed = consumption.isConsumed(ordinal);
        accumulatePointerWheel(transitions[ordinal], consumed);
        if (auto transitionStatus =
                mapTransition(platformFrame, transitions[ordinal], consumed,
                              nextUncompletedSimulationTick, lastPresentedCamera2D);
            !transitionStatus)
        {
            return transitionStatus;
        }
        lastAcceptedRawInputSequence_ = transitions[ordinal].sequence;
    }
    applyPointerClaims();
    if (auto snapshotStatus = validateRetainedSources(platformFrame); !snapshotStatus)
    {
        return snapshotStatus;
    }

    lastMappedEngineFrame_ = engineFrameIndex;
    lastMappedPlatformFrame_ = platformFrame.id();
    return Core::success();
}

void ActionMapper::accumulatePointerWheel(const Platform::InputTransition& transition, bool consumed) noexcept
{
    // A consumed wheel transition belongs to the UI, so it must not reach the game. This is
    // the per-transition counterpart of a claim: consumption is decided per event, a claim
    // covers the whole frame.
    if (consumed)
    {
        return;
    }
    const auto* wheel = std::get_if<Platform::PointerWheelTransition>(&transition.payload);
    if (wheel == nullptr || wheel->pointer >= framePointers_.size())
    {
        return;
    }
    // Summed rather than replaced: a frame can carry several wheel transitions, and dropping
    // all but the last would silently scale fast scrolling down to one notch.
    framePointers_[wheel->pointer].wheelDeltaX += wheel->deltaX;
    framePointers_[wheel->pointer].wheelDeltaY += wheel->deltaY;
}

void ActionMapper::applyPointerClaims() noexcept
{
    // Zeroing happens once here rather than at publish time so that frameActions() stays a
    // const view over already-settled state.
    for (usize slot = 0; slot < framePointers_.size(); ++slot)
    {
        if (framePointerDeltaClaimed_[slot])
        {
            framePointers_[slot].deltaX = 0.0;
            framePointers_[slot].deltaY = 0.0;
        }
        if (framePointerWheelClaimed_[slot])
        {
            framePointers_[slot].wheelDeltaX = 0.0;
            framePointers_[slot].wheelDeltaY = 0.0;
        }
    }
}

FrameActionSnapshot ActionMapper::frameActions() const noexcept
{
    return FrameActionSnapshot{
        .engineFrameIndex = currentEngineFrameIndex_,
        .states = frameActionStates_,
        .transitions = frameTransitions_,
        .pointerLookDeltaX = framePointerLookClaimed_ ? 0.0 : framePointerLookDeltaX_,
        .pointerLookDeltaY = framePointerLookClaimed_ ? 0.0 : framePointerLookDeltaY_,
        // Already claim-adjusted by applyPointerClaims, so no conditional here.
        .wheelDeltaX = framePointers_[Platform::PrimaryPointerId].wheelDeltaX,
        .wheelDeltaY = framePointers_[Platform::PrimaryPointerId].wheelDeltaY,
        .pointers = framePointers_,
    };
}

Core::Result<SimulationActionSnapshot> ActionMapper::simulationActionsForTick(u64 simulationTick) const
{
    return simulationLatch_.snapshotForTick(simulationTick);
}

Core::Status ActionMapper::completeSimulationTick(u64 simulationTick)
{
    return simulationLatch_.completeTick(simulationTick);
}

Core::Result<RebindTransaction>
ActionMapper::beginRebind(InputBindingId binding, std::optional<Platform::GamepadId> capturedGamepad)
{
    rebindConflicts_.clear();
    if (findBindingIndex(binding) == InvalidIndex)
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind target does not identify an existing binding");
    }
    if (capturedGamepad.has_value() && !capturedGamepad->hasValue())
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind capture requires a valid gamepad generation");
    }
    if (activeRebind_.has_value() || pendingRebind_.has_value())
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Only one rebind transaction may be active or queued");
    }
    if (nextRebindTransaction_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind transaction id space is exhausted");
    }

    const RebindTransaction transaction{
        .transaction = RebindTransactionId{nextRebindTransaction_++},
        .binding = binding,
    };
    activeRebind_ = PendingRebind{
        .transaction = transaction,
        .capturedGamepad = capturedGamepad,
    };
    rebindState_ = RebindState::Capturing;
    lastRebindTransaction_ = transaction;
    return transaction;
}

Core::Result<RebindCommitResult>
ActionMapper::commitRebind(RebindTransaction transaction, ActionBindingPattern replacement,
                           RebindOptions options)
{
    rebindConflicts_.clear();
    if (!activeRebind_.has_value() || activeRebind_->transaction != transaction)
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind commit does not match the active transaction");
    }
    if (!isValidBindingPattern(replacement))
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind replacement pattern is invalid");
    }
    if ((options.resolution != RebindResolution::RejectConflicts &&
         options.resolution != RebindResolution::Share && options.resolution != RebindResolution::Swap) ||
        (options.resolution == RebindResolution::Swap) != options.swapBinding.hasValue())
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind resolution is invalid or Swap has no explicit binding id");
    }

    const usize target = findBindingIndex(transaction.binding);
    if (target == InvalidIndex ||
        !isValidBindingTransform(replacement, bindings_[target].deadzone, bindings_[target].scale))
    {
        return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                             "Rebind replacement is incompatible with the target binding transform");
    }
    for (usize index = firstBindingForControl(replacement); index != InvalidIndex;
         index = records_[index].nextForControl)
    {
        if (index != target)
        {
            rebindConflicts_.push_back(bindings_[index].binding);
        }
    }
    const bool changesPattern = bindings_[target].input != replacement;
    if (changesPattern && !rebindConflicts_.empty() && options.resolution == RebindResolution::RejectConflicts)
    {
        ++statistics_.rebindConflictCount;
        return RebindCommitResult{
            .outcome = RebindCommitOutcome::Conflict,
            .conflictingBindings = rebindConflicts_,
        };
    }

    std::optional<usize> swapIndex;
    if (options.resolution == RebindResolution::Swap)
    {
        const usize selected = findBindingIndex(options.swapBinding);
        if (selected == InvalidIndex || selected == target || bindings_[selected].input != replacement)
        {
            return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                                 "Rebind Swap requires another binding with the exact replacement pattern");
        }
        if (!isValidBindingTransform(bindings_[target].input, bindings_[selected].deadzone,
                                     bindings_[selected].scale))
        {
            return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                                 "Rebind Swap is incompatible with the selected binding transform");
        }
        swapIndex = selected;
    }

    // Validate the prospective graph, not just the chosen pair: either side of
    // a swap may share its control with additional bindings of the same Action.
    const auto candidatePattern = [&](usize index) -> const ActionBindingPattern& {
        if (index == target) { return replacement; }
        if (swapIndex == index) { return bindings_[target].input; }
        return bindings_[index].input;
    };
    for (usize changed : {target, swapIndex.value_or(InvalidIndex)})
    {
        if (changed == InvalidIndex) { continue; }
        const usize actionIndex = records_[changed].actionIndex;
        for (usize other = actions_[actionIndex].firstBinding; other != InvalidIndex;
             other = records_[other].nextForAction)
        {
            if (other != changed && candidatePattern(other) == candidatePattern(changed))
            {
                return Core::failure(RuntimeErrorCode::InvalidRebindTransaction,
                                     "Rebind would bind the same Action to the same input pattern twice");
            }
        }
    }

    activeRebind_->replacement = std::move(replacement);
    activeRebind_->swapBindingIndex = swapIndex;
    pendingRebind_ = std::move(activeRebind_);
    activeRebind_.reset();
    rebindState_ = RebindState::Queued;
    return RebindCommitResult{.outcome = RebindCommitOutcome::Queued, .conflictingBindings = rebindConflicts_};
}

Core::Status ActionMapper::cancelRebind(RebindTransaction transaction) noexcept
{
    rebindConflicts_.clear();
    if (activeRebind_.has_value() && activeRebind_->transaction == transaction)
    {
        activeRebind_.reset();
    } else if (pendingRebind_.has_value() && pendingRebind_->transaction == transaction)
    {
        pendingRebind_.reset();
    } else
    {
        return invalidTransaction("Rebind cancellation does not match the active or queued transaction");
    }
    rebindState_ = RebindState::Cancelled;
    lastRebindTransaction_ = transaction;
    return Core::success();
}

RebindStateView ActionMapper::rebindState() const noexcept
{
    return RebindStateView{.state = rebindState_, .transaction = lastRebindTransaction_};
}

std::span<const InputActionBinding> ActionMapper::bindings() const noexcept
{
    return bindings_;
}

Core::Status ActionMapper::validateFrameInputs(const Platform::PlatformFrameView& platformFrame,
                                               const UI::InputTransitionConsumptionView& consumption,
                                               const UI::ContinuousControlClaimsView& claims,
                                               u64 engineFrameIndex,
                                               u64 nextUncompletedSimulationTick) const
{
    if (!platformFrame.id().hasValue())
    {
        return invariantFailure("Action Mapping requires a valid Platform frame id");
    }
    if (lastMappedPlatformFrame_.has_value() &&
        platformFrame.id().value <= lastMappedPlatformFrame_->value)
    {
        return invariantFailure("Platform frame ids must be strictly monotonic");
    }
    if (lastMappedEngineFrame_.has_value() && engineFrameIndex <= *lastMappedEngineFrame_)
    {
        return invariantFailure("engine frame indices must be strictly monotonic");
    }
    if (consumption.platformFrame != platformFrame.id() || claims.platformFrame != platformFrame.id())
    {
        return invariantFailure("UI consumption and claims must belong to the mapped Platform frame");
    }

    const auto transitions = platformFrame.inputTransitions();
    if (consumption.transitionCount != transitions.size())
    {
        return invariantFailure("UI transition consumption count does not match the Platform frame");
    }
    constexpr usize BitsPerWord = sizeof(u64) * 8U;
    const usize requiredConsumptionWords = (transitions.size() + BitsPerWord - 1U) / BitsPerWord;
    if (!consumption.consumedOrdinalWords.empty() &&
        consumption.consumedOrdinalWords.size() != requiredConsumptionWords)
    {
        return invariantFailure("UI transition consumption bit storage has the wrong size");
    }
    if (claims.controls.size() > capacities_.continuousControlClaimCapacity)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "UI continuous control claims exceed configured capacity");
    }
    if (transitions.size() > static_cast<usize>(capacities_.rawInputTransitionCapacity) + 1U)
    {
        return invariantFailure("raw Input transition batch exceeds configured capacity");
    }
    if (transitions.size() > capacities_.rawInputTransitionCapacity &&
        !std::holds_alternative<Platform::InputStreamReset>(transitions.back().payload))
    {
        return invariantFailure("raw Input reserved slot is not an InputStreamReset");
    }

    u64 previousSequence = 0;
    bool sawReset = false;
    if (!transitions.empty() && lastAcceptedRawInputSequence_.has_value() &&
        transitions.front().sequence <= *lastAcceptedRawInputSequence_)
    {
        return invariantFailure("raw Input transition sequence regressed across Platform frames");
    }
    for (const Platform::InputTransition& transition : transitions)
    {
        if (transition.sequence == 0 || transition.sequence <= previousSequence)
        {
            return invariantFailure("raw Input transition sequence must be strictly monotonic");
        }
        if (sawReset)
        {
            return invariantFailure("raw Input transitions cannot follow InputStreamReset");
        }
        sawReset = std::holds_alternative<Platform::InputStreamReset>(transition.payload);
        previousSequence = transition.sequence;
    }
    return simulationLatch_.validateNextUncompletedTick(nextUncompletedSimulationTick);
}

Core::Status ActionMapper::synchronizePrimaryWindow(const Platform::PlatformFrameView& platformFrame)
{
    const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
    if (primary == nullptr)
    {
        const bool retainedWindowState = std::ranges::any_of(sources_, [](const SourceState& source) {
            return source.routedWindow.hasValue() &&
                   (active(source.outputValue) || source.suppressedUntilNeutral);
        });
        return retainedWindowState
                   ? invariantFailure("a Platform frame lost its primary window without cancelling input")
                   : Core::success();
    }

    for (usize bindingIndex = 0; bindingIndex < bindings_.size(); ++bindingIndex)
    {
        const usize count = sourceCount(bindings_[bindingIndex].input);
        for (usize sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            SourceState& source = sources_[records_[bindingIndex].sourceOffset + sourceIndex];
            if (!source.routedWindow.hasValue())
            {
                source.routedWindow = primary->metrics.window;
                continue;
            }
            if (source.routedWindow == primary->metrics.window)
            {
                continue;
            }
            if (active(source.outputValue) || source.suppressedUntilNeutral)
            {
                return invariantFailure("primary Window generation changed without cancelling retained input");
            }
            source.routedWindow = primary->metrics.window;
        }
    }
    return Core::success();
}

void ActionMapper::observeRebindDeviceChanges(const Platform::PlatformFrameView& platformFrame) noexcept
{
    const auto disconnectedByFrame = [&platformFrame](const PendingRebind& rebind) {
        if (!rebind.capturedGamepad.has_value())
        {
            return false;
        }
        const auto transitions = platformFrame.inputTransitions();
        const bool explicitDisconnect = std::ranges::any_of(transitions, [&rebind](const Platform::InputTransition& item) {
            const auto* cancel = std::get_if<Platform::InputCancelTransition>(&item.payload);
            return cancel != nullptr && cancel->reason == Platform::InputCancelReason::DeviceDisconnected &&
                   cancel->gamepad == rebind.capturedGamepad;
        });
        if (explicitDisconnect)
        {
            return true;
        }
        const bool rawReset = std::ranges::any_of(transitions, [](const Platform::InputTransition& item) {
            return std::holds_alternative<Platform::InputStreamReset>(item.payload);
        });
        return rawReset &&
               std::ranges::none_of(platformFrame.gamepads(), [&rebind](const Platform::GamepadSnapshot& snapshot) {
                   return snapshot.gamepad == *rebind.capturedGamepad;
               });
    };

    const auto cancelDisconnected = [this, &disconnectedByFrame](std::optional<PendingRebind>& rebind) {
        if (!rebind.has_value() || !disconnectedByFrame(*rebind))
        {
            return;
        }
        lastRebindTransaction_ = rebind->transaction;
        rebind.reset();
        rebindState_ = RebindState::DeviceDisconnected;
        ++statistics_.rebindDeviceCancellationCount;
    };
    cancelDisconnected(activeRebind_);
    cancelDisconnected(pendingRebind_);
}

Core::Status ActionMapper::applyPendingRebind(const Platform::PlatformFrameView& platformFrame,
                                              u64 sequence, u64 nextSimulationTick)
{
    PendingRebind rebind = *pendingRebind_;
    pendingRebind_.reset();
    const usize target = findBindingIndex(rebind.transaction.binding);
    if (target == InvalidIndex)
    {
        return invalidTransaction("Queued rebind target no longer exists");
    }
    if (bindings_[target].input == rebind.replacement)
    {
        rebindState_ = RebindState::Applied;
        lastRebindTransaction_ = rebind.transaction;
        ++statistics_.rebindApplyCount;
        return Core::success();
    }

    std::ranges::fill(affectedActionsScratch_, u8{0});
    std::array<usize, 2> changed{target, InvalidIndex};
    usize changedCount = 1;
    if (rebind.swapBindingIndex.has_value())
    {
        changed[changedCount++] = *rebind.swapBindingIndex;
    }
    for (usize index = 0; index < changedCount; ++index)
    {
        const usize bindingIndex = changed[index];
        affectedActionsScratch_[records_[bindingIndex].actionIndex] = 1;
        clearBindingSources(bindingIndex, true);
    }

    if (rebind.swapBindingIndex.has_value())
    {
        const usize conflict = *rebind.swapBindingIndex;
        std::swap(bindings_[target].input, bindings_[conflict].input);
    } else
    {
        bindings_[target].input = std::move(rebind.replacement);
    }
    rebuildControlIndex();
    for (usize index = 0; index < changedCount; ++index)
    {
        seedSuppressionFromSnapshots(platformFrame, changed[index]);
    }
    recomputeAllActionValues();
    if (auto status = reconcileMarkedCancellations(sequence, nextSimulationTick); !status)
    {
        return status;
    }

    rebindState_ = RebindState::Applied;
    lastRebindTransaction_ = rebind.transaction;
    ++statistics_.rebindApplyCount;
    return Core::success();
}

Core::Status ActionMapper::applyClaims(const Platform::PlatformFrameView& platformFrame,
                                       const UI::ContinuousControlClaimsView& claims, u64 sequence,
                                       u64 nextSimulationTick)
{
    for (const UI::ContinuousControlClaim& claim : claims.controls)
    {
        Core::Status status = std::visit(
            Overloaded{
                [&](const Platform::KeyControlIdentity& control) {
                    return claimControl(platformFrame, PrimaryWindowKeyBinding{control.key},
                                        control.window, {}, sequence, nextSimulationTick);
                },
                [&](const Platform::PointerButtonControlIdentity& control) {
                    return claimControl(platformFrame, PointerButtonBinding{control.pointer, control.button},
                                        control.window, {}, sequence, nextSimulationTick);
                },
                [&](const Platform::GamepadButtonControlIdentity& control) {
                    return claimControl(platformFrame, StandardGamepadButtonBinding{control.button},
                                        control.routedWindow, control.gamepad, sequence, nextSimulationTick);
                },
                [&](const Platform::GamepadAxisControlIdentity& control) {
                    return claimControl(platformFrame, StandardGamepadAxisBinding{control.axis},
                                        control.routedWindow, control.gamepad, sequence, nextSimulationTick);
                },
                [this](const Platform::PointerContinuousControlIdentity& control) {
                    // No binding pattern resolves to pointer delta or wheel, so there is no
                    // SourceState to suppress the way a gamepad axis claim does. The claim
                    // instead withholds the published value: a scene under a widget that owns
                    // the pointer must not keep turning its camera or scrolling its world.
                    if (control.pointer >= framePointers_.size())
                    {
                        return Core::success();
                    }
                    switch (control.control)
                    {
                    case Platform::PointerContinuousControl::Delta:
                        framePointerDeltaClaimed_[control.pointer] = true;
                        // The scalar look delta carries its own flag because it is published
                        // whether or not the pointer table is.
                        if (control.pointer == Platform::PrimaryPointerId)
                        {
                            framePointerLookClaimed_ = true;
                        }
                        break;
                    case Platform::PointerContinuousControl::Wheel:
                        framePointerWheelClaimed_[control.pointer] = true;
                        break;
                    }
                    return Core::success();
                },
            },
            claim.control);
        if (!status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status ActionMapper::mapTransition(const Platform::PlatformFrameView& platformFrame,
                                         const Platform::InputTransition& transition, bool consumed,
                                         u64 nextSimulationTick,
                                         const LastPresentedCamera2DLatch* lastPresentedCamera2D)
{
    return std::visit(
        Overloaded{
            [&](const Platform::KeyTransition& input) {
                if (input.repeat) { return Core::success(); }
                return mapControl(platformFrame, PrimaryWindowKeyBinding{input.key}, input.window, {},
                                  input.state == Platform::DigitalTransition::Down ? 1.0F : 0.0F,
                                  transition.sequence, consumed,
                                  nextSimulationTick, nullptr, lastPresentedCamera2D);
            },
            [&](const Platform::PointerButtonTransition& input) {
                return mapControl(platformFrame,
                                  PointerButtonBinding{input.pointer, input.button}, input.window,
                                  {}, input.state == Platform::DigitalTransition::Down ? 1.0F : 0.0F,
                                  transition.sequence, consumed,
                                  nextSimulationTick, &input, lastPresentedCamera2D);
            },
            [&](const Platform::GamepadButtonTransition& input) {
                return mapControl(platformFrame, StandardGamepadButtonBinding{input.button},
                                  input.routedWindow, input.gamepad,
                                  input.state == Platform::DigitalTransition::Down ? 1.0F : 0.0F,
                                  transition.sequence, consumed, nextSimulationTick, nullptr,
                                  lastPresentedCamera2D);
            },
            [&](const Platform::GamepadAxisTransition& input) {
                return mapControl(platformFrame, StandardGamepadAxisBinding{input.axis},
                                  input.routedWindow, input.gamepad, input.value, transition.sequence,
                                  consumed, nextSimulationTick, nullptr, nullptr);
            },
            [&](const Platform::InputCancelTransition& cancel) {
                return applyCancel(platformFrame, cancel, transition.sequence, nextSimulationTick);
            },
            [&](const Platform::InputStreamReset& reset) {
                return applyRawReset(platformFrame, reset, transition.sequence,
                                     nextSimulationTick);
            },
            [](const auto&) { return Core::success(); },
        },
        transition.payload);
}

Core::Status ActionMapper::mapControl(const Platform::PlatformFrameView& platformFrame,
                                      const ActionBindingPattern& pattern, Platform::WindowId window,
                                      Platform::GamepadId gamepad, float rawValue,
                                      u64 sequence, bool consumed,
                                      u64 nextSimulationTick,
                                      const Platform::PointerButtonTransition* pointerTransition,
                                      const LastPresentedCamera2DLatch* lastPresentedCamera2D)
{
    beginSourceChanges();
    for (usize bindingIndex = firstBindingForControl(pattern); bindingIndex != InvalidIndex;
         bindingIndex = records_[bindingIndex].nextForControl)
    {
        SourceState* source = stageSourceChange(bindingIndex, window, gamepad);
        if (source == nullptr)
        {
            rollbackSourceChanges();
            return invariantFailure("an input transition references an invalid device generation");
        }
        const InputActionBinding& binding = bindings_[bindingIndex];
        const auto* axis = std::get_if<StandardGamepadAxisBinding>(&binding.input);
        source->physicalValue = axis == nullptr ? rawValue * binding.scale
            : normalizeAxis(rawValue, axis->valueMode, binding.deadzone, binding.scale);

        if (domainIsReset(binding.domain))
        {
            source->outputValue = 0.0F;
            source->suppressedUntilNeutral = active(source->physicalValue);
            continue;
        }
        if (consumed || source->claimedThisFrame)
        {
            source->outputValue = 0.0F;
            source->suppressedUntilNeutral = active(source->physicalValue);
            stagedActions_[records_[bindingIndex].actionIndex].cancelled = true;
            continue;
        }
        if (source->suppressedUntilNeutral)
        {
            if (!active(source->physicalValue))
            {
                source->suppressedUntilNeutral = false;
            }
            source->outputValue = 0.0F;
            continue;
        }

        source->outputValue = source->physicalValue;
    }
    return publishSourceChanges(platformFrame, sequence, nextSimulationTick,
                                pointerTransition, lastPresentedCamera2D);
}

Core::Status ActionMapper::claimControl(const Platform::PlatformFrameView& platformFrame,
                                        const ActionBindingPattern& pattern, Platform::WindowId window,
                                        Platform::GamepadId gamepad, u64 sequence, u64 nextSimulationTick)
{
    beginSourceChanges();
    for (usize bindingIndex = firstBindingForControl(pattern); bindingIndex != InvalidIndex;
         bindingIndex = records_[bindingIndex].nextForControl)
    {
        SourceState* source = stageSourceChange(bindingIndex, window, gamepad);
        if (source == nullptr)
        {
            rollbackSourceChanges();
            return invariantFailure("a claimed control references an invalid device generation");
        }
        source->physicalValue = physicalValue(platformFrame, bindingIndex, *source);
        source->claimedThisFrame = true;
        source->suppressedUntilNeutral = active(source->physicalValue);
        source->outputValue = 0.0F;
        stagedActions_[records_[bindingIndex].actionIndex].cancelled = true;
    }
    return publishSourceChanges(platformFrame, sequence, nextSimulationTick);
}

void ActionMapper::beginSourceChanges() noexcept
{
    for (usize index = 0; index < stagedActionCount_; ++index)
    {
        stagedActions_[stagedActionOrder_[index]] = {};
    }
    stagedSourceCount_ = 0;
    stagedActionCount_ = 0;
}

ActionMapper::SourceState* ActionMapper::stageSourceChange(
    usize bindingIndex, Platform::WindowId window, Platform::GamepadId gamepad) noexcept
{
    const bool isGamepad = gamepadPattern(bindings_[bindingIndex].input);
    if (isGamepad && (!gamepad.hasValue() || gamepad.index() >= Platform::PlatformFrameBuilder::MaximumGamepads))
    {
        return nullptr;
    }
    const ActionSourceToken token = sourceToken(bindingIndex, isGamepad ? gamepad.index() : 0U);
    const SourceState previous = sources_[token];
    SourceState* source = resolveSource(bindingIndex, window, gamepad);
    if (source == nullptr) { return nullptr; }

    const usize actionIndex = records_[bindingIndex].actionIndex;
    StagedActionChange& action = stagedActions_[actionIndex];
    if (action.firstSource == InvalidIndex)
    {
        assert(stagedActionCount_ < stagedActionOrder_.size());
        stagedActionOrder_[stagedActionCount_++] = actionIndex;
        action.firstSource = stagedSourceCount_;
    }
    assert(stagedSourceCount_ < stagedSources_.size());
    stagedSources_[stagedSourceCount_++] = {token, previous};
    return source;
}

void ActionMapper::rollbackSourceChanges() noexcept
{
    for (usize index = 0; index < stagedSourceCount_; ++index)
    {
        sources_[stagedSources_[index].token] = stagedSources_[index].previous;
    }
}

Core::Status ActionMapper::publishSourceChanges(
    const Platform::PlatformFrameView& platformFrame, u64 sequence, u64 nextSimulationTick,
    const Platform::PointerButtonTransition* pointerTransition,
    const LastPresentedCamera2DLatch* lastPresentedCamera2D)
{
    usize frameChanges = 0;
    usize simulationChanges = 0;
    bool hasCancellation = false;
    for (usize index = 0; index < stagedActionCount_; ++index)
    {
        const usize actionIndex = stagedActionOrder_[index];
        const ActionRecord& action = actions_[actionIndex];
        hasCancellation = hasCancellation || stagedActions_[actionIndex].cancelled;
        if (stagedActions_[actionIndex].cancelled || domainIsReset(action.domain) ||
            sameValue(action.value, composeActionValue(actionIndex)))
        {
            continue;
        }
        if (action.domain == InputActionDomain::Simulation)
        {
            ++simulationChanges;
        } else
        {
            ++frameChanges;
        }
    }
    const bool resetSimulation = simulationChanges > simulationLatch_.remainingTransitionCapacity();
    const bool resetFrame = frameChanges > mapCapacities_.frameActionTransitionCapacity - frameNormalTransitionCount_;

    std::optional<Render::WorldPointerSample> worldPointerSample;
    // World picking is the fallible external dependency. Check it before either
    // domain publishes anything, even if a Frame binding precedes Simulation.
    for (usize index = 0; pointerTransition != nullptr && index < stagedActionCount_; ++index)
    {
        const usize actionIndex = stagedActionOrder_[index];
        const ActionRecord& action = actions_[actionIndex];
        if (stagedActions_[actionIndex].cancelled || action.domain != InputActionDomain::Simulation ||
            resetSimulation || domainIsReset(action.domain) ||
            sameValue(action.value, composeActionValue(actionIndex)))
        {
            continue;
        }
        auto sample = pickWorldPointerSample(platformFrame, action.domain, true,
                                             pointerTransition, lastPresentedCamera2D, sequence);
        if (!sample)
        {
            rollbackSourceChanges();
            return Core::failure(std::move(sample.error()));
        }
        worldPointerSample = std::move(*sample);
        break;
    }

    // Capacity belongs to a domain, not an individual edge. Reject the complete
    // fanout in an overflowing domain before publishing any of its actions.
    if (resetSimulation)
    {
        if (auto status = simulationLatch_.resetStream(nextSimulationTick, {
                .reason = ActionInputStreamResetReason::ActionTransitionCapacityExceeded,
                .sourceSequence = sequence,
            }); !status)
        {
            rollbackSourceChanges();
            return status;
        }
        ++statistics_.simulationActionCapacityResetCount;
        suppressDomain(InputActionDomain::Simulation);
    }
    if (resetFrame)
    {
        resetFrameActionStream({
            .reason = ActionInputStreamResetReason::ActionTransitionCapacityExceeded,
            .sourceSequence = sequence,
        });
        ++statistics_.frameActionCapacityResetCount;
        suppressDomain(InputActionDomain::Frame);
    }

    if (hasCancellation)
    {
        std::ranges::fill(affectedActionsScratch_, u8{0});
        for (usize index = 0; index < stagedActionCount_; ++index)
        {
            const usize actionIndex = stagedActionOrder_[index];
            affectedActionsScratch_[actionIndex] = stagedActions_[actionIndex].cancelled ? 1 : 0;
        }
        removeMarkedPendingTransitions();
    }

    // All sources for this physical event are now present. Compose once per
    // Action; a reset domain stays suppressed until its stream is consumed and
    // its controls return to neutral.
    for (usize index = 0; index < stagedActionCount_; ++index)
    {
        const usize actionIndex = stagedActionOrder_[index];
        if (domainIsReset(actions_[actionIndex].domain))
        {
            continue;
        }
        const StagedActionChange& change = stagedActions_[actionIndex];
        if (change.cancelled)
        {
            if (auto status = reconcileCancelledAction(actionIndex, sequence, nextSimulationTick); !status)
            {
                return status;
            }
        } else if (auto status = appendActionChange(actionIndex,
                       stagedSources_[change.firstSource].token, sequence, nextSimulationTick,
                       worldPointerSample); !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status ActionMapper::applyCancel(const Platform::PlatformFrameView& platformFrame,
                                       const Platform::InputCancelTransition& cancel, u64 sequence,
                                       u64 nextSimulationTick)
{
    std::ranges::fill(affectedActionsScratch_, u8{0});
    for (usize bindingIndex = 0; bindingIndex < bindings_.size(); ++bindingIndex)
    {
        if (cancel.pointer.has_value())
        {
            const auto* pointer = std::get_if<PointerButtonBinding>(&bindings_[bindingIndex].input);
            if (pointer == nullptr || pointer->pointer != *cancel.pointer)
            {
                continue;
            }
        }
        const usize count = sourceCount(bindings_[bindingIndex].input);
        for (usize sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            const ActionSourceToken token = sourceToken(bindingIndex, sourceIndex);
            SourceState& source = sources_[token];
            const bool matchesGamepad = cancel.gamepad.has_value() && source.gamepad == *cancel.gamepad;
            const bool matchesWindow = !cancel.gamepad.has_value() &&
                                       (!cancel.routedWindow.hasValue() ||
                                        source.routedWindow == cancel.routedWindow);
            if (!matchesGamepad && !matchesWindow)
            {
                continue;
            }
            source = {};
            affectedActionsScratch_[records_[bindingIndex].actionIndex] = 1;
        }
    }

    if (!cancel.gamepad.has_value())
    {
        const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
        if (primary != nullptr &&
            (!cancel.routedWindow.hasValue() || cancel.routedWindow == primary->metrics.window))
        {
            for (usize bindingIndex = 0; bindingIndex < bindings_.size(); ++bindingIndex)
            {
                if (cancel.pointer.has_value())
                {
                    const auto* pointer = std::get_if<PointerButtonBinding>(&bindings_[bindingIndex].input);
                    if (pointer == nullptr || pointer->pointer != *cancel.pointer)
                    {
                        continue;
                    }
                }
                seedSuppressionFromSnapshots(platformFrame, bindingIndex);
            }
        }
    }
    recomputeAllActionValues();
    return reconcileMarkedCancellations(sequence, nextSimulationTick);
}

Core::Status ActionMapper::applyRawReset(const Platform::PlatformFrameView& platformFrame,
                                         const Platform::InputStreamReset& reset, u64 sequence,
                                         u64 nextSimulationTick)
{
    if (auto status = simulationLatch_.resetStream(
            nextSimulationTick,
            SimulationInputStreamReset{
                .reason = ActionInputStreamResetReason::RawInputStreamReset,
                .sourceSequence = sequence,
            });
        !status)
    {
        return status;
    }
    resetFrameActionStream(FrameInputStreamReset{
        .reason = ActionInputStreamResetReason::RawInputStreamReset,
        .sourceSequence = sequence,
    });

    const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
    const bool affectsPrimary = !reset.routedWindow.has_value() ||
                                (primary != nullptr && reset.routedWindow == primary->metrics.window);
    if (affectsPrimary)
    {
        for (usize bindingIndex = 0; bindingIndex < bindings_.size(); ++bindingIndex)
        {
            clearBindingSources(bindingIndex, false);
            seedSuppressionFromSnapshots(platformFrame, bindingIndex);
        }
        recomputeAllActionValues();
    }
    ++statistics_.rawInputResetCount;
    return Core::success();
}

Core::Status ActionMapper::validateRetainedSources(const Platform::PlatformFrameView& platformFrame)
{
    for (usize bindingIndex = 0; bindingIndex < bindings_.size(); ++bindingIndex)
    {
        const usize count = sourceCount(bindings_[bindingIndex].input);
        for (usize sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            SourceState& source = sources_[records_[bindingIndex].sourceOffset + sourceIndex];
            if (!active(source.outputValue) && !source.suppressedUntilNeutral)
            {
                continue;
            }
            const float finalPhysical = physicalValue(platformFrame, bindingIndex, source);
            if (source.suppressedUntilNeutral)
            {
                source.physicalValue = finalPhysical;
                if (!active(finalPhysical))
                {
                    source.suppressedUntilNeutral = false;
                }
                continue;
            }
            if (!sameValue(source.physicalValue, finalPhysical))
            {
                return invariantFailure("a retained Action source disagrees with the final Platform snapshot");
            }
        }
    }
    return Core::success();
}

Core::Status ActionMapper::appendActionChange(
    usize actionIndex, ActionSourceToken source, u64 sequence, u64 nextSimulationTick,
    const std::optional<Render::WorldPointerSample>& worldPointerSample)
{
    ActionRecord& action = actions_[actionIndex];
    const float previous = action.value;
    const float current = composeActionValue(actionIndex);
    if (sameValue(previous, current))
    {
        return setActionValue(actionIndex, current);
    }

    if (auto stateStatus = setActionValue(actionIndex, current); !stateStatus)
    {
        return stateStatus;
    }
    return appendTransition(
        actionIndex,
        InputActionTransition{
            .action = action.action,
            .kind = transitionKind(previous, current, false),
            .value = current,
            .sourceSequence = sequence,
            .worldPointerSample = action.domain == InputActionDomain::Simulation ? worldPointerSample : std::nullopt,
        },
        source, nextSimulationTick);
}

Core::Status ActionMapper::reconcileCancelledAction(usize actionIndex,
                                                    u64 sourceSequence, u64 nextSimulationTick)
{
    const float current = composeActionValue(actionIndex);
    if (auto stateStatus = setActionValue(actionIndex, current); !stateStatus)
    {
        return stateStatus;
    }
    ActionRecord& action = actions_[actionIndex];
    if (action.domain == InputActionDomain::Simulation)
    {
        const u64 resetCountBefore = simulationLatch_.statistics().capacityResetCount;
        auto result = simulationLatch_.reconcileState(nextSimulationTick, action.action, sourceSequence);
        if (!result)
        {
            return Core::failure(std::move(result.error()));
        }
        if (*result == SimulationLatchAppendResult::CapacityResetInserted)
        {
            if (simulationLatch_.statistics().capacityResetCount != resetCountBefore)
            {
                ++statistics_.simulationActionCapacityResetCount;
            }
            suppressDomain(InputActionDomain::Simulation);
        }
        return Core::success();
    }
    return reconcileFrameCancellation(actionIndex, sourceSequence);
}

Core::Status ActionMapper::appendTransition(usize actionIndex, InputActionTransition transition,
                                            ActionSourceToken source, u64 nextSimulationTick)
{
    if (actions_[actionIndex].domain == InputActionDomain::Simulation)
    {
        const u64 resetCountBefore = simulationLatch_.statistics().capacityResetCount;
        auto result = simulationLatch_.append(nextSimulationTick, std::move(transition), source);
        if (!result)
        {
            return Core::failure(std::move(result.error()));
        }
        if (*result == SimulationLatchAppendResult::CapacityResetInserted)
        {
            if (simulationLatch_.statistics().capacityResetCount != resetCountBefore)
            {
                ++statistics_.simulationActionCapacityResetCount;
            }
            suppressDomain(InputActionDomain::Simulation);
        }
        return Core::success();
    }
    return appendFrameTransition(std::move(transition));
}

Core::Status ActionMapper::appendFrameTransition(InputActionTransition transition)
{
    if (frameResetWritten_)
    {
        suppressDomain(InputActionDomain::Frame);
        return Core::success();
    }
    if (frameNormalTransitionCount_ >= mapCapacities_.frameActionTransitionCapacity)
    {
        resetFrameActionStream(FrameInputStreamReset{
            .reason = ActionInputStreamResetReason::ActionTransitionCapacityExceeded,
            .sourceSequence = transition.sourceSequence,
        });
        ++statistics_.frameActionCapacityResetCount;
        suppressDomain(InputActionDomain::Frame);
        return Core::success();
    }
    frameTransitions_.emplace_back(std::move(transition));
    ++frameNormalTransitionCount_;
    return Core::success();
}

Core::Status ActionMapper::reconcileFrameCancellation(usize actionIndex,
                                                      u64 sourceSequence)
{
    if (frameResetWritten_)
    {
        suppressDomain(InputActionDomain::Frame);
        return Core::success();
    }
    const InputActionId action = actions_[actionIndex].action;
    const float baseline = actions_[actionIndex].frameStartValue;
    const float current = actions_[actionIndex].value;
    if (sameValue(baseline, current))
    {
        return Core::success();
    }
    return appendFrameTransition(
        InputActionTransition{
            .action = action,
            .kind = transitionKind(baseline, current, true),
            .value = current,
            .sourceSequence = sourceSequence,
        });
}

void ActionMapper::removeMarkedPendingTransitions() noexcept
{
    cancellationActionsScratch_.clear();
    for (usize index = 0; index < actions_.size(); ++index)
    {
        if (affectedActionsScratch_[index] != 0)
        {
            cancellationActionsScratch_.push_back(actions_[index].action);
        }
    }
    if (cancellationActionsScratch_.empty()) { return; }
    std::ranges::sort(cancellationActionsScratch_);
    // Reclaim the whole cancellation batch before appending any reconciliation:
    // a later Action can free the slot needed by an earlier Action. Each stream
    // is compacted once, rather than scanning it once per fanout edge.
    simulationLatch_.removePendingTransitions(cancellationActionsScratch_);
    if (frameResetWritten_) { return; }
    std::erase_if(frameTransitions_, [this](const FrameActionTransition& item) {
        const auto* transition = std::get_if<InputActionTransition>(&item);
        return transition != nullptr &&
               std::ranges::binary_search(cancellationActionsScratch_, transition->action);
    });
    frameNormalTransitionCount_ = frameTransitions_.size();
}

void ActionMapper::resetFrameActionStream(FrameInputStreamReset reset) noexcept
{
    frameTransitions_.clear();
    frameNormalTransitionCount_ = 0;
    frameResetWritten_ = true;
    frameTransitions_.emplace_back(reset);
}

bool ActionMapper::domainIsReset(InputActionDomain domain) const noexcept
{
    return domain == InputActionDomain::Simulation ? simulationLatch_.hasPendingReset() : frameResetWritten_;
}

void ActionMapper::suppressDomain(InputActionDomain domain) noexcept
{
    for (usize bindingIndex = 0; bindingIndex < bindings_.size(); ++bindingIndex)
    {
        if (actions_[records_[bindingIndex].actionIndex].domain != domain)
        {
            continue;
        }
        const usize count = sourceCount(bindings_[bindingIndex].input);
        for (usize sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            SourceState& source = sources_[records_[bindingIndex].sourceOffset + sourceIndex];
            source.suppressedUntilNeutral = source.suppressedUntilNeutral ||
                                            active(source.physicalValue);
            source.outputValue = 0.0F;
        }
    }
    for (usize actionIndex = 0; actionIndex < actions_.size(); ++actionIndex)
    {
        if (actions_[actionIndex].domain == domain)
        {
            static_cast<void>(setActionValue(actionIndex, 0.0F));
        }
    }
}

float ActionMapper::composeActionValue(usize actionIndex) const noexcept
{
    const ActionRecord& action = actions_[actionIndex];
    float value = 0.0F;
    float strongestMagnitude = -1.0F;
    for (usize bindingIndex = action.firstBinding; bindingIndex != InvalidIndex;
         bindingIndex = records_[bindingIndex].nextForAction)
    {
        const usize count = sourceCount(bindings_[bindingIndex].input);
        for (usize sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            const float contribution =
                sources_[records_[bindingIndex].sourceOffset + sourceIndex].outputValue;
            if (action.composition == ActionCompositionMode::SumClamped)
            {
                value += contribution;
            } else if (std::abs(contribution) > strongestMagnitude)
            {
                value = contribution;
                strongestMagnitude = std::abs(contribution);
            }
        }
    }
    return action.composition == ActionCompositionMode::SumClamped
               ? std::clamp(value, -1.0F, 1.0F)
               : value;
}

Core::Status ActionMapper::setActionValue(usize actionIndex, float value) noexcept
{
    if (actionIndex >= actions_.size() || !std::isfinite(value))
    {
        return invariantFailure("Action state update references an invalid action or value");
    }
    ActionRecord& action = actions_[actionIndex];
    action.value = active(value) ? value : 0.0F;
    if (action.domain == InputActionDomain::Simulation)
    {
        return simulationLatch_.setValue(action.action, action.value);
    }
    frameActionStates_[action.stateIndex].value = action.value;
    return Core::success();
}

void ActionMapper::recomputeAllActionValues() noexcept
{
    for (usize actionIndex = 0; actionIndex < actions_.size(); ++actionIndex)
    {
        static_cast<void>(setActionValue(actionIndex, composeActionValue(actionIndex)));
    }
}

void ActionMapper::clearBindingSources(usize bindingIndex, bool markCancelled) noexcept
{
    const usize offset = records_[bindingIndex].sourceOffset;
    if (markCancelled)
    {
        affectedActionsScratch_[records_[bindingIndex].actionIndex] = 1;
    }
    for (usize sourceIndex = 0; sourceIndex < Platform::PlatformFrameBuilder::MaximumGamepads;
         ++sourceIndex)
    {
        sources_[offset + sourceIndex] = {};
    }
}

void ActionMapper::seedSuppressionFromSnapshots(const Platform::PlatformFrameView& platformFrame,
                                                usize bindingIndex) noexcept
{
    const InputActionBinding& binding = bindings_[bindingIndex];
    const usize offset = records_[bindingIndex].sourceOffset;
    if (const auto* key = std::get_if<PrimaryWindowKeyBinding>(&binding.input); key != nullptr)
    {
        if (const Platform::WindowFrameSnapshot* window = primaryWindow(platformFrame); window != nullptr)
        {
            SourceState& source = sources_[offset];
            source.routedWindow = window->input.window;
            source.physicalValue = window->input.isHeld(key->key) ? binding.scale : 0.0F;
            source.suppressedUntilNeutral = active(source.physicalValue);
        }
        return;
    }
    if (const auto* pointer = std::get_if<PointerButtonBinding>(&binding.input);
        pointer != nullptr)
    {
        if (const Platform::WindowFrameSnapshot* window = primaryWindow(platformFrame); window != nullptr)
        {
            SourceState& source = sources_[offset];
            source.routedWindow = window->input.window;
            const Platform::PointerSnapshot* snapshot = window->input.pointerSnapshot(pointer->pointer);
            source.physicalValue = snapshot != nullptr && snapshot->isHeld(pointer->button)
                                       ? binding.scale
                                       : 0.0F;
            source.suppressedUntilNeutral = active(source.physicalValue);
        }
        return;
    }

    const Platform::WindowFrameSnapshot* window = primaryWindow(platformFrame);
    for (const Platform::GamepadSnapshot& gamepad : platformFrame.gamepads())
    {
        if (gamepad.gamepad.index() >= Platform::PlatformFrameBuilder::MaximumGamepads)
        {
            continue;
        }
        SourceState& source = sources_[offset + gamepad.gamepad.index()];
        source.gamepad = gamepad.gamepad;
        source.routedWindow = window == nullptr ? Platform::WindowId{} : window->input.window;
        if (const auto* button = std::get_if<StandardGamepadButtonBinding>(&binding.input);
            button != nullptr)
        {
            source.physicalValue = gamepad.isHeld(button->button) ? binding.scale : 0.0F;
        } else if (const auto* axis = std::get_if<StandardGamepadAxisBinding>(&binding.input);
                   axis != nullptr)
        {
            source.physicalValue = normalizeAxis(gamepad.axis(axis->axis), axis->valueMode,
                                                 binding.deadzone, binding.scale);
        }
        source.suppressedUntilNeutral = active(source.physicalValue);
    }
}

ActionMapper::SourceState* ActionMapper::resolveSource(usize bindingIndex,
                                                       Platform::WindowId window,
                                                       Platform::GamepadId gamepad) noexcept
{
    const InputActionBinding& binding = bindings_[bindingIndex];
    const usize offset = records_[bindingIndex].sourceOffset;
    if (!gamepadPattern(binding.input))
    {
        SourceState& source = sources_[offset];
        if (source.routedWindow.hasValue() && source.routedWindow != window &&
            (active(source.outputValue) || source.suppressedUntilNeutral))
        {
            return nullptr;
        }
        source.routedWindow = window;
        return &source;
    }
    if (!gamepad.hasValue() || gamepad.index() >= Platform::PlatformFrameBuilder::MaximumGamepads)
    {
        return nullptr;
    }
    SourceState& source = sources_[offset + gamepad.index()];
    if (source.gamepad.hasValue() && source.gamepad != gamepad &&
        (active(source.outputValue) || source.suppressedUntilNeutral))
    {
        return nullptr;
    }
    if (source.gamepad != gamepad)
    {
        source = {};
        source.gamepad = gamepad;
    }
    source.routedWindow = window;
    return &source;
}

float ActionMapper::physicalValue(const Platform::PlatformFrameView& platformFrame,
                                  usize bindingIndex, const SourceState& source) const noexcept
{
    const InputActionBinding& binding = bindings_[bindingIndex];
    if (const auto* key = std::get_if<PrimaryWindowKeyBinding>(&binding.input); key != nullptr)
    {
        const Platform::WindowFrameSnapshot* window = primaryWindow(platformFrame);
        return window != nullptr && window->input.window == source.routedWindow &&
                       window->input.isHeld(key->key)
                   ? binding.scale
                   : 0.0F;
    }
    if (const auto* pointer = std::get_if<PointerButtonBinding>(&binding.input);
        pointer != nullptr)
    {
        const Platform::WindowFrameSnapshot* window = primaryWindow(platformFrame);
        const Platform::PointerSnapshot* snapshot =
            window == nullptr ? nullptr : window->input.pointerSnapshot(pointer->pointer);
        return window != nullptr && window->input.window == source.routedWindow && snapshot != nullptr &&
                       snapshot->isHeld(pointer->button)
                   ? binding.scale
                   : 0.0F;
    }
    const auto gamepad = std::ranges::find(platformFrame.gamepads(), source.gamepad,
                                           &Platform::GamepadSnapshot::gamepad);
    if (gamepad == platformFrame.gamepads().end())
    {
        return 0.0F;
    }
    if (const auto* button = std::get_if<StandardGamepadButtonBinding>(&binding.input);
        button != nullptr)
    {
        return gamepad->isHeld(button->button) ? binding.scale : 0.0F;
    }
    const auto* axis = std::get_if<StandardGamepadAxisBinding>(&binding.input);
    return axis == nullptr
               ? 0.0F
               : normalizeAxis(gamepad->axis(axis->axis), axis->valueMode, binding.deadzone,
                               binding.scale);
}

usize ActionMapper::findBindingIndex(InputBindingId binding) const noexcept
{
    const auto iterator = std::ranges::find(bindings_, binding, &InputActionBinding::binding);
    return iterator == bindings_.end()
               ? InvalidIndex
               : static_cast<usize>(std::distance(bindings_.begin(), iterator));
}

usize ActionMapper::firstBindingForControl(const ActionBindingPattern& pattern) const noexcept
{
    const usize control = physicalControlIndex(pattern);
    return control == InvalidIndex ? InvalidIndex : controlBindings_[control];
}

void ActionMapper::rebuildControlIndex() noexcept
{
    controlBindings_.fill(InvalidIndex);
    for (usize index = bindings_.size(); index-- > 0;)
    {
        const usize control = physicalControlIndex(bindings_[index].input);
        assert(control < controlBindings_.size());
        records_[index].nextForControl = controlBindings_[control];
        controlBindings_[control] = index;
    }
}

ActionSourceToken ActionMapper::sourceToken(usize bindingIndex, usize sourceIndex) const noexcept
{
    return records_[bindingIndex].sourceOffset + sourceIndex;
}

Core::Status ActionMapper::reconcileMarkedCancellations(u64 sequence, u64 nextSimulationTick)
{
    removeMarkedPendingTransitions();
    for (usize actionIndex = 0; actionIndex < actions_.size(); ++actionIndex)
    {
        if (affectedActionsScratch_[actionIndex] == 0 || domainIsReset(actions_[actionIndex].domain))
        {
            continue;
        }
        if (auto status = reconcileCancelledAction(actionIndex, sequence, nextSimulationTick);
            !status)
        {
            return status;
        }
    }
    return Core::success();
}

} // namespace Tina::Runtime::Input
