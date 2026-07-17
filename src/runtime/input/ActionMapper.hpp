#pragma once

#include "SimulationActionLatch.hpp"

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/InputActionMap.hpp>
#include <tina/ui/InputRouting.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Tina {

// Runtime-internal storage capacities for the default ActionMapper. UI route
// result ownership belongs to tina_ui; only the consumer-side capacities live
// with Runtime.
struct InputActionMapperCapacityConfig final {
    static constexpr u32 DefaultContinuousControlClaimCapacity = 64;
    static constexpr u32 MaximumContinuousControlClaimCapacity = 1024;

    u32 rawInputTransitionCapacity = Platform::PlatformFrameCapacityConfig::DefaultInputTransitionCapacity;
    u32 continuousControlClaimCapacity = DefaultContinuousControlClaimCapacity;
    u32 simulationActionTransitionCapacity = InputActionMapCapacityConfig::DefaultSimulationActionTransitionCapacity;
    u32 frameActionTransitionCapacity = InputActionMapCapacityConfig::DefaultFrameActionTransitionCapacity;
    u32 digitalActionBindingCapacity = InputActionMapCapacityConfig::DefaultDigitalActionBindingCapacity;
};

} // namespace Tina

namespace Tina::Runtime::Input {

struct ActionMapperStatistics final {
    u64 frameActionCapacityResetCount = 0;
    u64 rawInputResetCount = 0;
};

// Runtime-owned immutable default Input Context used by M7-A. It maps one
// PlatformFrameView after UI routing, owns cross-frame suppression, and feeds a
// separate fixed-tick latch. It is single-threaded and main-loop only.
class ActionMapper final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<ActionMapper>>
    Create(std::span<const DigitalActionBinding> bindings, InputActionMapperCapacityConfig capacities = {});

    ActionMapper(const ActionMapper&) = delete;
    ActionMapper& operator=(const ActionMapper&) = delete;
    ActionMapper(ActionMapper&&) = delete;
    ActionMapper& operator=(ActionMapper&&) = delete;

    [[nodiscard]] Core::Status mapFrame(const Platform::PlatformFrameView& platformFrame,
                                        const UI::InputTransitionConsumptionView& consumption,
                                        const UI::ContinuousControlClaimsView& claims, u64 engineFrameIndex,
                                        u64 nextUncompletedSimulationTick);

    [[nodiscard]] FrameActionSnapshot frameActions() const noexcept;
    [[nodiscard]] Core::Result<SimulationActionSnapshot> simulationActionsForTick(u64 simulationTick) const;
    [[nodiscard]] Core::Status completeSimulationTick(u64 simulationTick);

    [[nodiscard]] const ActionMapperStatistics& statistics() const noexcept
    {
        return statistics_;
    }

    [[nodiscard]] const SimulationActionLatchStatistics& simulationLatchStatistics() const noexcept
    {
        return simulationLatch_.statistics();
    }

  private:
    struct SourceState final {
        Platform::WindowId routedWindow{};
        Platform::GamepadId gamepad{};
        bool active = false;
        bool suppressedUntilRelease = false;
        bool claimedThisFrame = false;
    };

    struct BindingRecord final {
        DigitalActionBindingPattern input{};
        InputActionId action{};
        InputActionDomain domain = InputActionDomain::Simulation;
        usize actionIndex = 0;
        usize sourceOffset = 0;
        usize sourceCount = 0;
    };

    struct ActionRecord final {
        InputActionId action{};
        usize activeSourceCount = 0;
    };

    struct LocatedSource final {
        BindingRecord* binding = nullptr;
        SourceState* source = nullptr;
    };

    ActionMapper(InputActionMapperCapacityConfig capacities, std::vector<BindingRecord> bindings,
                 std::vector<SourceState> sources, std::vector<ActionRecord> simulationActions,
                 std::vector<ActionRecord> frameActionRecords, std::vector<InputActionState> frameActionStates,
                 std::vector<bool> frameStartHeld, std::vector<FrameActionTransition> frameTransitions,
                 std::vector<ActionSourceToken> frameTransitionSources, SimulationActionLatch simulationLatch) noexcept;

    [[nodiscard]] Core::Status validateFrameInputs(const Platform::PlatformFrameView& platformFrame,
                                                   const UI::InputTransitionConsumptionView& consumption,
                                                   const UI::ContinuousControlClaimsView& claims, u64 engineFrameIndex,
                                                   u64 nextUncompletedSimulationTick) const;

    [[nodiscard]] Core::Status synchronizePrimaryWindow(const Platform::PlatformFrameView& platformFrame);
    [[nodiscard]] Core::Status validateRetainedSourceSnapshots(const Platform::PlatformFrameView& platformFrame) const;
    [[nodiscard]] Core::Status applyClaims(const Platform::PlatformFrameView& platformFrame,
                                           const UI::ContinuousControlClaimsView& claims, u64 claimSequence,
                                           u64 nextSimulationTick);
    [[nodiscard]] Core::Status mapTransition(const Platform::PlatformFrameView& platformFrame,
                                             const Platform::InputTransition& transition, bool consumed,
                                             u64 nextSimulationTick);

    [[nodiscard]] LocatedSource locate(const Platform::PlatformFrameView& platformFrame,
                                       const Platform::KeyControlIdentity& control);
    [[nodiscard]] LocatedSource locate(const Platform::PlatformFrameView& platformFrame,
                                       const Platform::PointerButtonControlIdentity& control);
    [[nodiscard]] LocatedSource locate(const Platform::PlatformFrameView& platformFrame,
                                       const Platform::GamepadButtonControlIdentity& control);

    [[nodiscard]] BindingRecord* findBinding(const DigitalActionBindingPattern& input) noexcept;
    [[nodiscard]] bool physicalHeld(const Platform::PlatformFrameView& platformFrame, const BindingRecord& binding,
                                    const SourceState& source) const noexcept;

    [[nodiscard]] Core::Status activateSource(const Platform::PlatformFrameView& platformFrame, BindingRecord& binding,
                                              SourceState& source, u64 sequence, u64 nextSimulationTick);
    [[nodiscard]] Core::Status releaseSource(const Platform::PlatformFrameView& platformFrame, BindingRecord& binding,
                                             SourceState& source, u64 sequence, u64 nextSimulationTick);
    [[nodiscard]] Core::Status cancelSource(const Platform::PlatformFrameView& platformFrame, BindingRecord& binding,
                                            SourceState& source, u64 sequence, u64 nextSimulationTick,
                                            bool suppressWhilePhysicallyHeld);

    [[nodiscard]] Core::Status appendNormalTransition(const Platform::PlatformFrameView& platformFrame,
                                                      BindingRecord& binding, ActionSourceToken source,
                                                      DigitalActionTransitionKind kind, u64 sequence,
                                                      u64 nextSimulationTick);
    [[nodiscard]] Core::Status reconcileCancelledAction(const Platform::PlatformFrameView& platformFrame,
                                                        BindingRecord& binding, ActionSourceToken source, u64 sequence,
                                                        u64 nextSimulationTick, bool forceStateReconciliation);

    [[nodiscard]] Core::Status applyCancelTransition(const Platform::PlatformFrameView& platformFrame,
                                                     const Platform::InputCancelTransition& cancel, u64 sequence,
                                                     u64 nextSimulationTick);
    [[nodiscard]] Core::Status applyRawReset(const Platform::PlatformFrameView& platformFrame,
                                             const Platform::InputStreamReset& reset, u64 sequence,
                                             u64 nextSimulationTick);

    void suppressDomainAfterCapacityReset(InputActionDomain domain) noexcept;
    void recomputeActionStates() noexcept;

    [[nodiscard]] Core::Status appendFrameTransition(const Platform::PlatformFrameView& platformFrame,
                                                     FrameActionTransition transition, ActionSourceToken source,
                                                     u64 nextSimulationTick);
    [[nodiscard]] Core::Status reconcileFrameCancellation(const Platform::PlatformFrameView& platformFrame,
                                                          usize actionIndex, ActionSourceToken source,
                                                          u64 sourceSequence, u64 nextSimulationTick,
                                                          bool forceStateReconciliation);
    void resetFrameActionStream(FrameInputStreamReset reset) noexcept;

    InputActionMapperCapacityConfig capacities_{};
    std::vector<BindingRecord> bindings_;
    std::vector<SourceState> sources_;
    std::vector<ActionRecord> simulationActions_;
    std::vector<ActionRecord> frameActionRecords_;
    std::vector<InputActionState> frameActionStates_;
    std::vector<bool> frameStartHeld_;
    std::vector<FrameActionTransition> frameTransitions_;
    std::vector<ActionSourceToken> frameTransitionSources_;
    SimulationActionLatch simulationLatch_;
    usize frameNormalTransitionCount_ = 0;
    u64 currentEngineFrameIndex_ = 0;
    std::optional<u64> lastMappedEngineFrame_;
    std::optional<Platform::PlatformFrameId> lastMappedPlatformFrame_;
    std::optional<u64> lastAcceptedRawInputSequence_;
    ActionMapperStatistics statistics_{};
};

} // namespace Tina::Runtime::Input
