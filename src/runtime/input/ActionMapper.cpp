#include "ActionMapper.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <ranges>
#include <string_view>
#include <utility>

namespace Tina::Runtime::Input {
namespace {

inline constexpr usize InvalidIndex = (std::numeric_limits<usize>::max)();

template <typename... Callables> struct Overloaded : Callables... {
    using Callables::operator()...;
};

template <typename... Callables> Overloaded(Callables...) -> Overloaded<Callables...>;

[[nodiscard]] Core::Status invariantFailure(std::string_view message)
{
    return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation, message);
}

[[nodiscard]] bool capacitiesAreValid(const InputActionMapperCapacityConfig& capacities) noexcept
{
    return capacities.rawInputTransitionCapacity != 0 &&
           capacities.rawInputTransitionCapacity <=
               Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity &&
           capacities.continuousControlClaimCapacity != 0 &&
           capacities.continuousControlClaimCapacity <=
               InputActionMapperCapacityConfig::MaximumContinuousControlClaimCapacity &&
           capacities.simulationActionTransitionCapacity != 0 &&
           capacities.simulationActionTransitionCapacity <=
               InputActionMapCapacityConfig::MaximumSimulationActionTransitionCapacity &&
           capacities.frameActionTransitionCapacity != 0 &&
           capacities.frameActionTransitionCapacity <=
               InputActionMapCapacityConfig::MaximumFrameActionTransitionCapacity &&
           capacities.digitalActionBindingCapacity != 0 &&
           capacities.digitalActionBindingCapacity <= InputActionMapCapacityConfig::MaximumDigitalActionBindingCapacity;
}

[[nodiscard]] bool bindingPatternIsValid(const DigitalActionBindingPattern& input) noexcept
{
    return std::visit(Overloaded{
                          [](const PrimaryWindowKeyBinding& binding) {
                              return binding.key != Platform::Key::Unknown &&
                                     static_cast<usize>(binding.key) < Platform::KeyCount;
                          },
                          [](const PrimaryPointerButtonBinding& binding) {
                              return binding.pointer == Platform::PrimaryPointerId &&
                                     static_cast<usize>(binding.button) < Platform::PointerButtonCount;
                          },
                          [](const StandardGamepadButtonBinding& binding) {
                              return static_cast<usize>(binding.button) < Platform::GamepadButtonCount;
                          },
                      },
                      input);
}

[[nodiscard]] usize sourceCountForBinding(const DigitalActionBindingPattern& input) noexcept
{
    return std::holds_alternative<StandardGamepadButtonBinding>(input) ? Platform::PlatformFrameBuilder::MaximumGamepads
                                                                       : 1U;
}

template <typename Record>
[[nodiscard]] usize findActionRecordIndex(const std::vector<Record>& records, InputActionId action) noexcept
{
    const auto iterator = std::ranges::lower_bound(records, action, {}, &Record::action);
    return iterator != records.end() && iterator->action == action
               ? static_cast<usize>(std::distance(records.begin(), iterator))
               : InvalidIndex;
}

[[nodiscard]] const Platform::WindowFrameSnapshot* primaryWindow(const Platform::PlatformFrameView& frame) noexcept
{
    return frame.primaryWindow();
}

} // namespace

Core::Result<std::unique_ptr<ActionMapper>> ActionMapper::Create(std::span<const DigitalActionBinding> bindings,
                                                                 InputActionMapperCapacityConfig capacities)
{
    if (!capacitiesAreValid(capacities))
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "Input Action routing capacity is outside the supported range");
    }
    if (bindings.size() > capacities.digitalActionBindingCapacity)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "Digital Action binding count exceeds its configured capacity");
    }

    try
    {
        std::vector<BindingRecord> records;
        records.reserve(bindings.size());
        for (const DigitalActionBinding& binding : bindings)
        {
            const bool validDomain =
                binding.domain == InputActionDomain::Simulation || binding.domain == InputActionDomain::Frame;
            if (!binding.action.hasValue() || !bindingPatternIsValid(binding.input) || !validDomain)
            {
                return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                                     "Digital Action bindings require a valid pattern, "
                                     "action id, and domain");
            }
            const bool actionAlreadyUsesAnotherDomain =
                std::ranges::any_of(records, [&binding](const BindingRecord& existing) {
                    return existing.action == binding.action && existing.domain != binding.domain;
                });
            if (actionAlreadyUsesAnotherDomain)
            {
                return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                                     "one Input Action id cannot belong to both "
                                     "Simulation and Frame domains");
            }
            records.push_back(BindingRecord{
                .input = binding.input,
                .action = binding.action,
                .domain = binding.domain,
            });
        }
        std::ranges::sort(records, {}, &BindingRecord::input);
        if (std::ranges::adjacent_find(records, [](const BindingRecord& left, const BindingRecord& right) {
                return left.input == right.input;
            }) != records.end())
        {
            return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                                 "one physical input pattern cannot have multiple "
                                 "default-context bindings");
        }

        std::vector<InputActionId> simulationActionIds;
        std::vector<InputActionId> frameActionIds;
        simulationActionIds.reserve(records.size());
        frameActionIds.reserve(records.size());
        for (const BindingRecord& record : records)
        {
            auto& destination = record.domain == InputActionDomain::Simulation ? simulationActionIds : frameActionIds;
            destination.push_back(record.action);
        }
        const auto sortAndUnique = [](std::vector<InputActionId>& actions) {
            std::ranges::sort(actions);
            actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
        };
        sortAndUnique(simulationActionIds);
        sortAndUnique(frameActionIds);

        auto latchResult =
            SimulationActionLatch::Create(simulationActionIds, capacities.simulationActionTransitionCapacity);
        if (!latchResult)
        {
            return Core::failure(std::move(latchResult.error()));
        }

        std::vector<ActionRecord> simulationActions;
        simulationActions.reserve(simulationActionIds.size());
        for (InputActionId action : simulationActionIds)
        {
            simulationActions.push_back(ActionRecord{.action = action});
        }
        std::vector<ActionRecord> frameActionRecords;
        std::vector<InputActionState> frameActionStates;
        frameActionRecords.reserve(frameActionIds.size());
        frameActionStates.reserve(frameActionIds.size());
        for (InputActionId action : frameActionIds)
        {
            frameActionRecords.push_back(ActionRecord{.action = action});
            frameActionStates.push_back(InputActionState{.action = action});
        }
        std::vector<bool> frameStartHeld(frameActionIds.size(), false);

        usize totalSourceCount = 0;
        for (const BindingRecord& record : records)
        {
            const usize count = sourceCountForBinding(record.input);
            if (totalSourceCount > (std::numeric_limits<usize>::max)() - count)
            {
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "Digital Action source storage size overflowed");
            }
            totalSourceCount += count;
        }
        std::vector<SourceState> sources(totalSourceCount);

        usize nextSource = 0;
        for (BindingRecord& record : records)
        {
            record.sourceOffset = nextSource;
            record.sourceCount = sourceCountForBinding(record.input);
            nextSource += record.sourceCount;
            record.actionIndex = record.domain == InputActionDomain::Simulation
                                     ? findActionRecordIndex(simulationActions, record.action)
                                     : findActionRecordIndex(frameActionRecords, record.action);
            if (record.actionIndex == InvalidIndex)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "Digital Action binding could not resolve its normalized action");
            }
        }

        std::vector<FrameActionTransition> frameTransitions;
        frameTransitions.reserve(static_cast<usize>(capacities.frameActionTransitionCapacity) + 1U);
        std::vector<ActionSourceToken> frameTransitionSources;
        frameTransitionSources.reserve(static_cast<usize>(capacities.frameActionTransitionCapacity) + 1U);

        auto mapper = std::unique_ptr<ActionMapper>(new (std::nothrow) ActionMapper(
            capacities, std::move(records), std::move(sources), std::move(simulationActions),
            std::move(frameActionRecords), std::move(frameActionStates), std::move(frameStartHeld),
            std::move(frameTransitions), std::move(frameTransitionSources), std::move(*latchResult)));
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
        return Core::failure(Core::CoreErrorCode::Internal, "Action Mapper creation failed with an unknown exception");
    }
}

ActionMapper::ActionMapper(InputActionMapperCapacityConfig capacities, std::vector<BindingRecord> bindings,
                           std::vector<SourceState> sources, std::vector<ActionRecord> simulationActions,
                           std::vector<ActionRecord> frameActionRecords,
                           std::vector<InputActionState> frameActionStates, std::vector<bool> frameStartHeld,
                           std::vector<FrameActionTransition> frameTransitions,
                           std::vector<ActionSourceToken> frameTransitionSources,
                           SimulationActionLatch simulationLatch) noexcept
    : capacities_(capacities), bindings_(std::move(bindings)), sources_(std::move(sources)),
      simulationActions_(std::move(simulationActions)), frameActionRecords_(std::move(frameActionRecords)),
      frameActionStates_(std::move(frameActionStates)), frameStartHeld_(std::move(frameStartHeld)),
      frameTransitions_(std::move(frameTransitions)), frameTransitionSources_(std::move(frameTransitionSources)),
      simulationLatch_(std::move(simulationLatch))
{
}

Core::Status ActionMapper::mapFrame(const Platform::PlatformFrameView& platformFrame,
                                    const InputTransitionConsumption& consumption,
                                    const ContinuousControlClaims& claims, u64 engineFrameIndex,
                                    u64 nextUncompletedSimulationTick)
{
    if (auto validation =
            validateFrameInputs(platformFrame, consumption, claims, engineFrameIndex, nextUncompletedSimulationTick);
        !validation)
    {
        return validation;
    }
    if (auto windowStatus = synchronizePrimaryWindow(platformFrame); !windowStatus)
    {
        return windowStatus;
    }

    for (SourceState& source : sources_)
    {
        source.claimedThisFrame = false;
    }
    frameTransitions_.clear();
    frameTransitionSources_.clear();
    frameNormalTransitionCount_ = 0;
    for (usize index = 0; index < frameActionStates_.size(); ++index)
    {
        frameStartHeld_[index] = frameActionStates_[index].held;
    }
    currentEngineFrameIndex_ = engineFrameIndex;

    const auto transitions = platformFrame.inputTransitions();
    const u64 claimSequence =
        transitions.empty() ? lastAcceptedRawInputSequence_.value_or(0) : transitions.front().sequence;
    if (auto claimStatus = applyClaims(platformFrame, claims, claimSequence, nextUncompletedSimulationTick);
        !claimStatus)
    {
        return claimStatus;
    }

    for (usize ordinal = 0; ordinal < transitions.size(); ++ordinal)
    {
        if (auto transitionStatus = mapTransition(platformFrame, transitions[ordinal], consumption.isConsumed(ordinal),
                                                  nextUncompletedSimulationTick);
            !transitionStatus)
        {
            return transitionStatus;
        }
    }
    if (auto snapshotStatus = validateRetainedSourceSnapshots(platformFrame); !snapshotStatus)
    {
        return snapshotStatus;
    }

    lastMappedEngineFrame_ = engineFrameIndex;
    lastMappedPlatformFrame_ = platformFrame.id();
    if (!transitions.empty())
    {
        lastAcceptedRawInputSequence_ = transitions.back().sequence;
    }
    return Core::success();
}

FrameActionSnapshot ActionMapper::frameActions() const noexcept
{
    return FrameActionSnapshot{
        .engineFrameIndex = currentEngineFrameIndex_,
        .states = frameActionStates_,
        .transitions = frameTransitions_,
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

Core::Status ActionMapper::validateFrameInputs(const Platform::PlatformFrameView& platformFrame,
                                               const InputTransitionConsumption& consumption,
                                               const ContinuousControlClaims& claims, u64 engineFrameIndex,
                                               u64 nextUncompletedSimulationTick) const
{
    if (!platformFrame.id().hasValue())
    {
        return invariantFailure("Action Mapping requires a valid Platform frame id");
    }
    if (lastMappedPlatformFrame_.has_value() && platformFrame.id().value <= lastMappedPlatformFrame_->value)
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
        (transitions.empty() || !std::holds_alternative<Platform::InputStreamReset>(transitions.back().payload)))
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
            return source.routedWindow.hasValue() && (source.active || source.suppressedUntilRelease);
        });
        return retainedWindowState ? invariantFailure("a Platform frame lost its primary window "
                                                      "without cancelling input")
                                   : Core::success();
    }

    for (BindingRecord& binding : bindings_)
    {
        for (usize sourceIndex = 0; sourceIndex < binding.sourceCount; ++sourceIndex)
        {
            SourceState& source = sources_[binding.sourceOffset + sourceIndex];
            if (!source.routedWindow.hasValue())
            {
                source.routedWindow = primary->metrics.window;
                continue;
            }
            if (source.routedWindow == primary->metrics.window)
            {
                continue;
            }
            if (source.active || source.suppressedUntilRelease)
            {
                return invariantFailure("primary Window generation changed without "
                                        "cancelling retained input");
            }
            source.routedWindow = primary->metrics.window;
        }
    }
    return Core::success();
}

Core::Status ActionMapper::validateRetainedSourceSnapshots(const Platform::PlatformFrameView& platformFrame) const
{
    for (const BindingRecord& binding : bindings_)
    {
        for (usize sourceIndex = 0; sourceIndex < binding.sourceCount; ++sourceIndex)
        {
            const SourceState& source = sources_[binding.sourceOffset + sourceIndex];
            if (!source.active && !source.suppressedUntilRelease)
            {
                continue;
            }
            if (!physicalHeld(platformFrame, binding, source))
            {
                return invariantFailure("retained Input Action source contradicts the "
                                        "final Platform snapshot");
            }
        }
    }
    return Core::success();
}

Core::Status ActionMapper::applyClaims(const Platform::PlatformFrameView& platformFrame,
                                       const ContinuousControlClaims& claims, u64 claimSequence, u64 nextSimulationTick)
{
    for (const ContinuousControlClaim& claim : claims.controls)
    {
        LocatedSource located =
            std::visit(Overloaded{
                           [this, &platformFrame](const Platform::KeyControlIdentity& control) {
                               return locate(platformFrame, control);
                           },
                           [this, &platformFrame](const Platform::PointerButtonControlIdentity& control) {
                               return locate(platformFrame, control);
                           },
                           [this, &platformFrame](const Platform::GamepadButtonControlIdentity& control) {
                               return locate(platformFrame, control);
                           },
                           [](const Platform::GamepadAxisControlIdentity&) { return LocatedSource{}; },
                           [](const Platform::PointerContinuousControlIdentity&) { return LocatedSource{}; },
                       },
                       claim.control);
        if (located.binding == nullptr || located.source == nullptr)
        {
            continue;
        }

        located.source->claimedThisFrame = true;
        if (auto status =
                cancelSource(platformFrame, *located.binding, *located.source, claimSequence, nextSimulationTick, true);
            !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status ActionMapper::mapTransition(const Platform::PlatformFrameView& platformFrame,
                                         const Platform::InputTransition& transition, bool consumed,
                                         u64 nextSimulationTick)
{
    const auto applyDigital = [this, &platformFrame, sequence = transition.sequence, consumed,
                               nextSimulationTick](LocatedSource located, Platform::DigitalTransition state,
                                                   bool repeat) -> Core::Status {
        if (located.binding == nullptr || located.source == nullptr || repeat)
        {
            return Core::success();
        }
        SourceState& source = *located.source;
        BindingRecord& binding = *located.binding;
        const bool intercepted = consumed || source.claimedThisFrame;

        if (state == Platform::DigitalTransition::Down)
        {
            if (intercepted)
            {
                if (auto status = cancelSource(platformFrame, binding, source, sequence, nextSimulationTick, false);
                    !status)
                {
                    return status;
                }
                source.suppressedUntilRelease = true;
                return Core::success();
            }
            if (source.suppressedUntilRelease)
            {
                return Core::success();
            }
            return activateSource(platformFrame, binding, source, sequence, nextSimulationTick);
        }

        if (source.suppressedUntilRelease)
        {
            source.active = false;
            source.suppressedUntilRelease = false;
            return Core::success();
        }
        if (intercepted)
        {
            return cancelSource(platformFrame, binding, source, sequence, nextSimulationTick, false);
        }
        return releaseSource(platformFrame, binding, source, sequence, nextSimulationTick);
    };

    return std::visit(
        Overloaded{
            [this, &platformFrame, &applyDigital](const Platform::KeyTransition& key) {
                return applyDigital(locate(platformFrame, Platform::KeyControlIdentity{key.window, key.key}), key.state,
                                    key.repeat);
            },
            [this, &platformFrame, &applyDigital](const Platform::PointerButtonTransition& pointer) {
                return applyDigital(locate(platformFrame,
                                           Platform::PointerButtonControlIdentity{
                                               pointer.window,
                                               pointer.pointer,
                                               pointer.button,
                                           }),
                                    pointer.state, false);
            },
            [this, &platformFrame, &applyDigital](const Platform::GamepadButtonTransition& gamepad) {
                return applyDigital(locate(platformFrame,
                                           Platform::GamepadButtonControlIdentity{
                                               gamepad.routedWindow,
                                               gamepad.gamepad,
                                               gamepad.button,
                                           }),
                                    gamepad.state, false);
            },
            [this, &platformFrame, &transition, nextSimulationTick](const Platform::InputCancelTransition& cancel) {
                return applyCancelTransition(platformFrame, cancel, transition.sequence, nextSimulationTick);
            },
            [this, &platformFrame, &transition, nextSimulationTick](const Platform::InputStreamReset& reset) {
                return applyRawReset(platformFrame, reset, transition.sequence, nextSimulationTick);
            },
            [](const auto&) { return Core::success(); },
        },
        transition.payload);
}

ActionMapper::LocatedSource ActionMapper::locate(const Platform::PlatformFrameView& platformFrame,
                                                 const Platform::KeyControlIdentity& control)
{
    const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
    if (primary == nullptr || control.window != primary->metrics.window)
    {
        return {};
    }
    BindingRecord* binding = findBinding(PrimaryWindowKeyBinding{control.key});
    return binding == nullptr ? LocatedSource{} : LocatedSource{binding, &sources_[binding->sourceOffset]};
}

ActionMapper::LocatedSource ActionMapper::locate(const Platform::PlatformFrameView& platformFrame,
                                                 const Platform::PointerButtonControlIdentity& control)
{
    const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
    if (primary == nullptr || control.window != primary->metrics.window)
    {
        return {};
    }
    BindingRecord* binding = findBinding(PrimaryPointerButtonBinding{
        control.pointer,
        control.button,
    });
    return binding == nullptr ? LocatedSource{} : LocatedSource{binding, &sources_[binding->sourceOffset]};
}

ActionMapper::LocatedSource ActionMapper::locate(const Platform::PlatformFrameView& platformFrame,
                                                 const Platform::GamepadButtonControlIdentity& control)
{
    const Platform::WindowFrameSnapshot* primary = primaryWindow(platformFrame);
    if (primary == nullptr || control.routedWindow != primary->metrics.window || !control.gamepad.hasValue() ||
        control.gamepad.index() >= Platform::PlatformFrameBuilder::MaximumGamepads)
    {
        return {};
    }
    BindingRecord* binding = findBinding(StandardGamepadButtonBinding{control.button});
    if (binding == nullptr)
    {
        return {};
    }

    SourceState& source = sources_[binding->sourceOffset + control.gamepad.index()];
    if (!source.gamepad.hasValue())
    {
        source.gamepad = control.gamepad;
        source.routedWindow = control.routedWindow;
    } else if (source.gamepad != control.gamepad)
    {
        if (source.active || source.suppressedUntilRelease)
        {
            return {};
        }
        source.gamepad = control.gamepad;
        source.routedWindow = control.routedWindow;
    }
    return LocatedSource{binding, &source};
}

ActionMapper::BindingRecord* ActionMapper::findBinding(const DigitalActionBindingPattern& input) noexcept
{
    const auto iterator = std::ranges::lower_bound(bindings_, input, {}, &BindingRecord::input);
    return iterator != bindings_.end() && iterator->input == input ? std::addressof(*iterator) : nullptr;
}

bool ActionMapper::physicalHeld(const Platform::PlatformFrameView& platformFrame, const BindingRecord& binding,
                                const SourceState& source) const noexcept
{
    return std::visit(Overloaded{
                          [&platformFrame](const PrimaryWindowKeyBinding& key) {
                              const auto* primary = primaryWindow(platformFrame);
                              return primary != nullptr && primary->input.isHeld(key.key);
                          },
                          [&platformFrame](const PrimaryPointerButtonBinding& pointer) {
                              const auto* primary = primaryWindow(platformFrame);
                              return primary != nullptr && primary->input.pointer.pointer == pointer.pointer &&
                                     primary->input.pointer.isHeld(pointer.button);
                          },
                          [&platformFrame, &source](const StandardGamepadButtonBinding& gamepad) {
                              if (!source.gamepad.hasValue())
                              {
                                  return false;
                              }
                              const auto snapshots = platformFrame.gamepads();
                              const auto iterator =
                                  std::ranges::find(snapshots, source.gamepad, &Platform::GamepadSnapshot::gamepad);
                              return iterator != snapshots.end() && iterator->isHeld(gamepad.button);
                          },
                      },
                      binding.input);
}

Core::Status ActionMapper::activateSource(const Platform::PlatformFrameView& platformFrame, BindingRecord& binding,
                                          SourceState& source, u64 sequence, u64 nextSimulationTick)
{
    if (source.active)
    {
        return Core::success();
    }
    source.active = true;
    ActionRecord& action = binding.domain == InputActionDomain::Simulation ? simulationActions_[binding.actionIndex]
                                                                           : frameActionRecords_[binding.actionIndex];
    const bool firstSource = action.activeSourceCount == 0;
    ++action.activeSourceCount;
    if (!firstSource)
    {
        return Core::success();
    }

    if (binding.domain == InputActionDomain::Simulation)
    {
        if (auto status = simulationLatch_.setHeld(binding.action, true); !status)
        {
            return status;
        }
    } else
    {
        frameActionStates_[binding.actionIndex].held = true;
        frameActionStates_[binding.actionIndex].axis = 1.0F;
    }
    return appendNormalTransition(platformFrame, binding,
                                  static_cast<ActionSourceToken>(std::addressof(source) - sources_.data()),
                                  DigitalActionTransitionKind::Pressed, sequence, nextSimulationTick);
}

Core::Status ActionMapper::releaseSource(const Platform::PlatformFrameView& platformFrame, BindingRecord& binding,
                                         SourceState& source, u64 sequence, u64 nextSimulationTick)
{
    if (!source.active)
    {
        return Core::success();
    }
    source.active = false;
    ActionRecord& action = binding.domain == InputActionDomain::Simulation ? simulationActions_[binding.actionIndex]
                                                                           : frameActionRecords_[binding.actionIndex];
    if (action.activeSourceCount == 0)
    {
        return invariantFailure("Digital Action active source count underflowed");
    }
    --action.activeSourceCount;
    if (action.activeSourceCount != 0)
    {
        return Core::success();
    }

    if (binding.domain == InputActionDomain::Simulation)
    {
        if (auto status = simulationLatch_.setHeld(binding.action, false); !status)
        {
            return status;
        }
    } else
    {
        frameActionStates_[binding.actionIndex].held = false;
        frameActionStates_[binding.actionIndex].axis = 0.0F;
    }
    return appendNormalTransition(platformFrame, binding,
                                  static_cast<ActionSourceToken>(std::addressof(source) - sources_.data()),
                                  DigitalActionTransitionKind::Released, sequence, nextSimulationTick);
}

Core::Status ActionMapper::cancelSource(const Platform::PlatformFrameView& platformFrame, BindingRecord& binding,
                                        SourceState& source, u64 sequence, u64 nextSimulationTick,
                                        bool suppressWhilePhysicallyHeld)
{
    const bool wasActive = source.active;
    if (wasActive)
    {
        source.active = false;
        ActionRecord& action = binding.domain == InputActionDomain::Simulation
                                   ? simulationActions_[binding.actionIndex]
                                   : frameActionRecords_[binding.actionIndex];
        if (action.activeSourceCount == 0)
        {
            return invariantFailure("Digital Action active source count underflowed");
        }
        --action.activeSourceCount;
        const bool held = action.activeSourceCount != 0;
        if (binding.domain == InputActionDomain::Simulation)
        {
            if (auto status = simulationLatch_.setHeld(binding.action, held); !status)
            {
                return status;
            }
        } else
        {
            frameActionStates_[binding.actionIndex].held = held;
            frameActionStates_[binding.actionIndex].axis = held ? 1.0F : 0.0F;
        }
        if (auto status = reconcileCancelledAction(
                platformFrame, binding, static_cast<ActionSourceToken>(std::addressof(source) - sources_.data()),
                sequence, nextSimulationTick, true);
            !status)
        {
            return status;
        }
    }
    source.suppressedUntilRelease = suppressWhilePhysicallyHeld && physicalHeld(platformFrame, binding, source);
    return Core::success();
}

Core::Status ActionMapper::appendNormalTransition(const Platform::PlatformFrameView& platformFrame,
                                                  BindingRecord& binding, ActionSourceToken source,
                                                  DigitalActionTransitionKind kind, u64 sequence,
                                                  u64 nextSimulationTick)
{
    const DigitalActionTransition transition{
        .action = binding.action,
        .kind = kind,
        .sourceSequence = sequence,
    };
    if (binding.domain == InputActionDomain::Simulation)
    {
        auto result = simulationLatch_.append(nextSimulationTick, transition, source);
        if (!result)
        {
            return Core::failure(std::move(result.error()));
        }
        if (*result == SimulationLatchAppendResult::CapacityResetInserted)
        {
            suppressDomainAfterCapacityReset(binding.domain);
        }
        return Core::success();
    }
    return appendFrameTransition(platformFrame, FrameActionTransition{transition}, source, nextSimulationTick);
}

Core::Status ActionMapper::reconcileCancelledAction(const Platform::PlatformFrameView& platformFrame,
                                                    BindingRecord& binding, ActionSourceToken source, u64 sequence,
                                                    u64 nextSimulationTick, bool forceStateReconciliation)
{
    if (binding.domain == InputActionDomain::Simulation)
    {
        auto result = simulationLatch_.reconcileCancellation(nextSimulationTick, binding.action, source, sequence,
                                                             forceStateReconciliation);
        if (!result)
        {
            return Core::failure(std::move(result.error()));
        }
        if (*result == SimulationLatchAppendResult::CapacityResetInserted)
        {
            suppressDomainAfterCapacityReset(binding.domain);
        }
        return Core::success();
    }
    return reconcileFrameCancellation(platformFrame, binding.actionIndex, source, sequence, nextSimulationTick,
                                      forceStateReconciliation);
}

Core::Status ActionMapper::applyCancelTransition(const Platform::PlatformFrameView& platformFrame,
                                                 const Platform::InputCancelTransition& cancel, u64 sequence,
                                                 u64 nextSimulationTick)
{
    for (BindingRecord& binding : bindings_)
    {
        for (usize sourceIndex = 0; sourceIndex < binding.sourceCount; ++sourceIndex)
        {
            SourceState& source = sources_[binding.sourceOffset + sourceIndex];
            const bool windowMatches = !cancel.routedWindow.hasValue() || source.routedWindow == cancel.routedWindow;
            const bool deviceMatches = !cancel.gamepad.has_value() || source.gamepad == *cancel.gamepad;
            if (!windowMatches || !deviceMatches)
            {
                continue;
            }

            const bool wasActive = source.active;
            if (auto status = cancelSource(platformFrame, binding, source, sequence, nextSimulationTick, true); !status)
            {
                return status;
            }
            if (!wasActive)
            {
                if (auto status = reconcileCancelledAction(
                        platformFrame, binding,
                        static_cast<ActionSourceToken>(std::addressof(source) - sources_.data()), sequence,
                        nextSimulationTick, false);
                    !status)
                {
                    return status;
                }
            }
        }
    }
    return Core::success();
}

Core::Status ActionMapper::applyRawReset(const Platform::PlatformFrameView& platformFrame,
                                         const Platform::InputStreamReset& reset, u64 sequence, u64 nextSimulationTick)
{
    if (auto status = simulationLatch_.resetStream(nextSimulationTick,
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

    for (BindingRecord& binding : bindings_)
    {
        for (usize sourceIndex = 0; sourceIndex < binding.sourceCount; ++sourceIndex)
        {
            SourceState& source = sources_[binding.sourceOffset + sourceIndex];
            if (reset.routedWindow.has_value() && source.routedWindow != *reset.routedWindow)
            {
                continue;
            }
            source.active = false;
            source.suppressedUntilRelease = physicalHeld(platformFrame, binding, source);
        }
    }
    recomputeActionStates();
    ++statistics_.rawInputResetCount;
    return Core::success();
}

void ActionMapper::suppressDomainAfterCapacityReset(InputActionDomain domain) noexcept
{
    for (BindingRecord& binding : bindings_)
    {
        if (binding.domain != domain)
        {
            continue;
        }
        for (usize sourceIndex = 0; sourceIndex < binding.sourceCount; ++sourceIndex)
        {
            SourceState& source = sources_[binding.sourceOffset + sourceIndex];
            // Capacity reset can occur in the middle of the ordered raw batch.
            // Freeze only state already observed at this sequence; consulting the
            // frame-end physical snapshot would suppress later Down transitions.
            source.suppressedUntilRelease = source.suppressedUntilRelease || source.active;
            source.active = false;
        }
    }

    auto& actions = domain == InputActionDomain::Simulation ? simulationActions_ : frameActionRecords_;
    for (usize index = 0; index < actions.size(); ++index)
    {
        actions[index].activeSourceCount = 0;
        if (domain == InputActionDomain::Simulation)
        {
            static_cast<void>(simulationLatch_.setHeld(actions[index].action, false));
        } else
        {
            frameActionStates_[index].held = false;
            frameActionStates_[index].axis = 0.0F;
        }
    }
}

void ActionMapper::recomputeActionStates() noexcept
{
    for (ActionRecord& action : simulationActions_)
    {
        action.activeSourceCount = 0;
    }
    for (ActionRecord& action : frameActionRecords_)
    {
        action.activeSourceCount = 0;
    }
    for (const BindingRecord& binding : bindings_)
    {
        usize activeCount = 0;
        for (usize sourceIndex = 0; sourceIndex < binding.sourceCount; ++sourceIndex)
        {
            if (sources_[binding.sourceOffset + sourceIndex].active)
            {
                ++activeCount;
            }
        }
        auto& actions = binding.domain == InputActionDomain::Simulation ? simulationActions_ : frameActionRecords_;
        actions[binding.actionIndex].activeSourceCount += activeCount;
    }
    for (usize index = 0; index < simulationActions_.size(); ++index)
    {
        const bool held = simulationActions_[index].activeSourceCount != 0;
        static_cast<void>(simulationLatch_.setHeld(simulationActions_[index].action, held));
    }
    for (usize index = 0; index < frameActionRecords_.size(); ++index)
    {
        const bool held = frameActionRecords_[index].activeSourceCount != 0;
        frameActionStates_[index].held = held;
        frameActionStates_[index].axis = held ? 1.0F : 0.0F;
    }
}

Core::Status ActionMapper::appendFrameTransition(const Platform::PlatformFrameView& /*platformFrame*/,
                                                 FrameActionTransition transition, ActionSourceToken source,
                                                 u64 /*nextSimulationTick*/)
{
    if (frameNormalTransitionCount_ >= capacities_.frameActionTransitionCapacity)
    {
        const auto* digital = std::get_if<DigitalActionTransition>(&transition);
        const u64 sequence = digital == nullptr ? 0 : digital->sourceSequence;
        resetFrameActionStream(FrameInputStreamReset{
            .reason = ActionInputStreamResetReason::ActionTransitionCapacityExceeded,
            .sourceSequence = sequence,
        });
        ++statistics_.frameActionCapacityResetCount;
        suppressDomainAfterCapacityReset(InputActionDomain::Frame);
        return Core::success();
    }
    frameTransitions_.emplace_back(std::move(transition));
    frameTransitionSources_.push_back(source);
    ++frameNormalTransitionCount_;
    return Core::success();
}

Core::Status ActionMapper::reconcileFrameCancellation(const Platform::PlatformFrameView& platformFrame,
                                                      usize actionIndex, ActionSourceToken source, u64 sourceSequence,
                                                      u64 nextSimulationTick, bool forceStateReconciliation)
{
    if (actionIndex >= frameActionStates_.size())
    {
        return invariantFailure("Frame Action cancellation references an unknown action");
    }
    const InputActionId action = frameActionStates_[actionIndex].action;
    usize destination = 0;
    bool removedPendingSource = false;
    for (usize index = 0; index < frameTransitions_.size(); ++index)
    {
        const auto* digital = std::get_if<DigitalActionTransition>(&frameTransitions_[index]);
        const bool remove = digital != nullptr && digital->action == action && frameTransitionSources_[index] == source;
        if (remove)
        {
            removedPendingSource = true;
            continue;
        }
        if (destination != index)
        {
            frameTransitions_[destination] = std::move(frameTransitions_[index]);
            frameTransitionSources_[destination] = frameTransitionSources_[index];
        }
        ++destination;
    }
    frameTransitions_.resize(destination);
    frameTransitionSources_.resize(destination);
    frameNormalTransitionCount_ =
        static_cast<usize>(std::ranges::count_if(frameTransitions_, [](const FrameActionTransition& transition) {
            return std::holds_alternative<DigitalActionTransition>(transition);
        }));

    if (!forceStateReconciliation && !removedPendingSource)
    {
        return Core::success();
    }
    const bool startedHeld = frameStartHeld_[actionIndex];
    const bool currentHeld = frameActionStates_[actionIndex].held;
    bool pendingResult = startedHeld;
    for (const FrameActionTransition& pending : frameTransitions_)
    {
        const auto* digital = std::get_if<DigitalActionTransition>(&pending);
        if (digital == nullptr || digital->action != action)
        {
            continue;
        }
        pendingResult = digital->kind == DigitalActionTransitionKind::Pressed;
    }
    if (pendingResult == currentHeld)
    {
        return Core::success();
    }
    return appendFrameTransition(
        platformFrame,
        FrameActionTransition{DigitalActionTransition{
            .action = action,
            .kind = currentHeld ? DigitalActionTransitionKind::Pressed : DigitalActionTransitionKind::Cancelled,
            .sourceSequence = sourceSequence,
        }},
        source, nextSimulationTick);
}

void ActionMapper::resetFrameActionStream(FrameInputStreamReset reset) noexcept
{
    frameTransitions_.clear();
    frameTransitionSources_.clear();
    frameNormalTransitionCount_ = 0;
    frameTransitions_.emplace_back(reset);
    frameTransitionSources_.push_back(InvalidActionSourceToken);
}

} // namespace Tina::Runtime::Input
