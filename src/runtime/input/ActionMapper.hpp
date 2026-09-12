#pragma once

#include "SimulationActionLatch.hpp"
#include "InputBindingRules.hpp"

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/InputActionMap.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/ui/InputRouting.hpp>

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Tina {

// Runtime-owned capacities whose producers live outside the game-facing Action
// map. EngineHost derives raw capacity from Platform and shares claim capacity
// with the UI route producer.
struct InputActionMapperCapacityConfig final {
    static constexpr u32 DefaultContinuousControlClaimCapacity = 64;
    static constexpr u32 MaximumContinuousControlClaimCapacity = 1024;

    u32 rawInputTransitionCapacity = Platform::PlatformFrameCapacityConfig::DefaultInputTransitionCapacity;
    u32 continuousControlClaimCapacity = DefaultContinuousControlClaimCapacity;
};

} // namespace Tina

namespace Tina::Runtime::Input {

class LastPresentedCamera2DLatch;

struct ActionMapperStatistics final {
    u64 frameActionCapacityResetCount = 0;
    u64 simulationActionCapacityResetCount = 0;
    u64 rawInputResetCount = 0;
    u64 rebindApplyCount = 0;
    u64 rebindConflictCount = 0;
    u64 rebindDeviceCancellationCount = 0;
};

// Runtime-owned default Input Context. It resolves unified digital/analog
// bindings after UI routing, owns cross-frame suppression and runtime rebind
// transactions, and feeds a fixed-tick latch. Single-threaded, owner-loop only.
class ActionMapper final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<ActionMapper>>
    Create(InputActionMapConfig config, InputActionMapperCapacityConfig capacities = {});

    ActionMapper(const ActionMapper&) = delete;
    ActionMapper& operator=(const ActionMapper&) = delete;
    ActionMapper(ActionMapper&&) = delete;
    ActionMapper& operator=(ActionMapper&&) = delete;

    [[nodiscard]] Core::Status mapFrame(const Platform::PlatformFrameView& platformFrame,
                                        const UI::InputTransitionConsumptionView& consumption,
                                        const UI::ContinuousControlClaimsView& claims, u64 engineFrameIndex,
                                        u64 nextUncompletedSimulationTick,
                                        const LastPresentedCamera2DLatch* lastPresentedCamera2D = nullptr);

    [[nodiscard]] FrameActionSnapshot frameActions() const noexcept;
    [[nodiscard]] Core::Result<SimulationActionSnapshot> simulationActionsForTick(u64 simulationTick) const;
    [[nodiscard]] Core::Status completeSimulationTick(u64 simulationTick);

    [[nodiscard]] Core::Result<RebindTransaction>
    beginRebind(InputBindingId binding, std::optional<Platform::GamepadId> capturedGamepad = std::nullopt);
    [[nodiscard]] Core::Result<RebindCommitResult> commitRebind(RebindTransaction transaction,
                                                                ActionBindingPattern replacement,
                                                                RebindOptions options = {});
    [[nodiscard]] Core::Status cancelRebind(RebindTransaction transaction) noexcept;
    [[nodiscard]] RebindStateView rebindState() const noexcept;
    [[nodiscard]] std::span<const InputActionBinding> bindings() const noexcept;

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
        float physicalValue = 0.0F;
        float outputValue = 0.0F;
        bool suppressedUntilNeutral = false;
        bool claimedThisFrame = false;
    };

    struct BindingRecord final {
        usize actionIndex = 0;
        usize sourceOffset = 0;
        usize nextForControl = InvalidBindingIndex;
        usize nextForAction = InvalidBindingIndex;
    };

    struct ActionRecord final {
        InputActionId action{};
        InputActionDomain domain = InputActionDomain::Simulation;
        ActionCompositionMode composition = ActionCompositionMode::SumClamped;
        usize stateIndex = 0;
        float value = 0.0F;
        float frameStartValue = 0.0F;
        usize firstBinding = InvalidBindingIndex;
    };

    struct StagedSourceChange final {
        ActionSourceToken token = InvalidActionSourceToken;
        SourceState previous{};
    };

    struct StagedActionChange final {
        usize firstSource = InvalidBindingIndex;
        bool cancelled = false;
    };

    struct PendingRebind final {
        RebindTransaction transaction{};
        std::optional<Platform::GamepadId> capturedGamepad{};
        ActionBindingPattern replacement{};
        std::optional<usize> swapBindingIndex{};
    };

    ActionMapper(InputActionMapperCapacityConfig capacities, InputActionMapCapacityConfig mapCapacities,
                 std::vector<InputActionBinding> bindings, std::vector<BindingRecord> records,
                 std::vector<SourceState> sources, std::vector<ActionRecord> actions,
                 std::vector<InputActionState> frameStates,
                 std::vector<FrameActionTransition> frameTransitions,
                 std::vector<u8> affectedActionsScratch,
                 SimulationActionLatch simulationLatch);

    [[nodiscard]] Core::Status validateFrameInputs(const Platform::PlatformFrameView& platformFrame,
                                                   const UI::InputTransitionConsumptionView& consumption,
                                                   const UI::ContinuousControlClaimsView& claims,
                                                   u64 engineFrameIndex,
                                                   u64 nextUncompletedSimulationTick) const;
    [[nodiscard]] Core::Status synchronizePrimaryWindow(const Platform::PlatformFrameView& platformFrame);
    void observeRebindDeviceChanges(const Platform::PlatformFrameView& platformFrame) noexcept;
    [[nodiscard]] Core::Status applyPendingRebind(const Platform::PlatformFrameView& platformFrame,
                                                  u64 sequence, u64 nextSimulationTick);
    [[nodiscard]] Core::Status applyClaims(const Platform::PlatformFrameView& platformFrame,
                                           const UI::ContinuousControlClaimsView& claims, u64 sequence,
                                           u64 nextSimulationTick);
    [[nodiscard]] Core::Status mapTransition(const Platform::PlatformFrameView& platformFrame,
                                             const Platform::InputTransition& transition, bool consumed,
                                             u64 nextSimulationTick,
                                             const LastPresentedCamera2DLatch* lastPresentedCamera2D);
    // Wheel is transition-only in the Platform snapshot, so it is summed while walking the
    // frame rather than read out of the settled state the way pointer motion is.
    void accumulatePointerWheel(const Platform::InputTransition& transition, bool consumed) noexcept;
    // Applies this frame's pointer claims to the published table. Runs after the transition
    // walk so it also covers wheel accumulated during it.
    void applyPointerClaims() noexcept;
    [[nodiscard]] Core::Status mapControl(const Platform::PlatformFrameView& platformFrame,
                                          const ActionBindingPattern& pattern, Platform::WindowId window,
                                          Platform::GamepadId gamepad, float rawValue,
                                          u64 sequence, bool consumed, u64 nextSimulationTick,
                                          const Platform::PointerButtonTransition* pointerTransition,
                                          const LastPresentedCamera2DLatch* lastPresentedCamera2D);
    [[nodiscard]] Core::Status claimControl(const Platform::PlatformFrameView& platformFrame,
                                            const ActionBindingPattern& pattern, Platform::WindowId window,
                                            Platform::GamepadId gamepad, u64 sequence, u64 nextSimulationTick);
    void beginSourceChanges() noexcept;
    [[nodiscard]] SourceState* stageSourceChange(usize bindingIndex, Platform::WindowId window,
                                                 Platform::GamepadId gamepad) noexcept;
    void rollbackSourceChanges() noexcept;
    [[nodiscard]] Core::Status publishSourceChanges(
        const Platform::PlatformFrameView& platformFrame, u64 sequence, u64 nextSimulationTick,
        const Platform::PointerButtonTransition* pointerTransition = nullptr,
        const LastPresentedCamera2DLatch* lastPresentedCamera2D = nullptr);
    [[nodiscard]] Core::Status applyCancel(const Platform::PlatformFrameView& platformFrame,
                                           const Platform::InputCancelTransition& cancel, u64 sequence,
                                           u64 nextSimulationTick);
    [[nodiscard]] Core::Status applyRawReset(const Platform::PlatformFrameView& platformFrame,
                                             const Platform::InputStreamReset& reset, u64 sequence,
                                             u64 nextSimulationTick);
    [[nodiscard]] Core::Status validateRetainedSources(const Platform::PlatformFrameView& platformFrame);

    [[nodiscard]] Core::Status appendActionChange(
        usize actionIndex, ActionSourceToken source, u64 sequence, u64 nextSimulationTick,
        const std::optional<Render::WorldPointerSample>& worldPointerSample);
    [[nodiscard]] Core::Status reconcileCancelledAction(usize actionIndex,
                                                        u64 sourceSequence, u64 nextSimulationTick);
    [[nodiscard]] Core::Status appendTransition(usize actionIndex, InputActionTransition transition,
                                                ActionSourceToken source, u64 nextSimulationTick);
    [[nodiscard]] Core::Status appendFrameTransition(InputActionTransition transition);
    [[nodiscard]] Core::Status reconcileFrameCancellation(usize actionIndex,
                                                          u64 sourceSequence);
    void removeMarkedPendingTransitions() noexcept;
    void resetFrameActionStream(FrameInputStreamReset reset) noexcept;
    [[nodiscard]] bool domainIsReset(InputActionDomain domain) const noexcept;
    void suppressDomain(InputActionDomain domain) noexcept;

    [[nodiscard]] float composeActionValue(usize actionIndex) const noexcept;
    [[nodiscard]] Core::Status setActionValue(usize actionIndex, float value) noexcept;
    void recomputeAllActionValues() noexcept;
    void clearBindingSources(usize bindingIndex, bool markCancelled) noexcept;
    void seedSuppressionFromSnapshots(const Platform::PlatformFrameView& platformFrame,
                                      usize bindingIndex) noexcept;
    [[nodiscard]] SourceState* resolveSource(usize bindingIndex, Platform::WindowId window,
                                             Platform::GamepadId gamepad) noexcept;
    [[nodiscard]] float physicalValue(const Platform::PlatformFrameView& platformFrame,
                                      usize bindingIndex, const SourceState& source) const noexcept;
    [[nodiscard]] usize findBindingIndex(InputBindingId binding) const noexcept;
    [[nodiscard]] usize firstBindingForControl(const ActionBindingPattern& pattern) const noexcept;
    void rebuildControlIndex() noexcept;
    [[nodiscard]] ActionSourceToken sourceToken(usize bindingIndex, usize sourceIndex) const noexcept;
    [[nodiscard]] Core::Status reconcileMarkedCancellations(u64 sequence, u64 nextSimulationTick);

    InputActionMapperCapacityConfig capacities_{};
    InputActionMapCapacityConfig mapCapacities_{};
    std::vector<InputActionBinding> bindings_;
    std::vector<BindingRecord> records_;
    std::vector<SourceState> sources_;
    std::vector<ActionRecord> actions_;
    std::array<usize, PhysicalControlCount> controlBindings_{};
    // Reused transaction scratch; one physical control touches each binding at
    // most once. No heap growth or first-match dispatch in the mapping path.
    std::vector<StagedSourceChange> stagedSources_;
    std::vector<StagedActionChange> stagedActions_;
    std::vector<usize> stagedActionOrder_;
    usize stagedSourceCount_ = 0;
    usize stagedActionCount_ = 0;
    std::vector<InputBindingId> rebindConflicts_;
    std::vector<InputActionState> frameActionStates_;
    std::vector<FrameActionTransition> frameTransitions_;
    std::vector<u8> affectedActionsScratch_;
    std::vector<InputActionId> cancellationActionsScratch_;
    SimulationActionLatch simulationLatch_;
    usize frameNormalTransitionCount_ = 0;
    bool frameResetWritten_ = false;
    // Primary-pointer motion for the frame being mapped, published through
    // FrameActionSnapshot. Recomputed every mapFrame and zeroed when the UI claims
    // pointer delta, so a stale value cannot survive into a frame the UI owns.
    double framePointerLookDeltaX_ = 0.0;
    double framePointerLookDeltaY_ = 0.0;
    bool framePointerLookClaimed_ = false;
    // The whole pointer table for the frame being mapped, published through
    // FrameActionSnapshot. Fixed size and indexed by PointerId so a slot's identity never
    // depends on how many pointers happen to be present. Claim-aware: a claimed pointer's
    // delta or wheel is zeroed here, which is why this cannot be the raw Platform table.
    std::array<FramePointerState, Platform::PointerCapacity> framePointers_{};
    // Wheel has no persistent state in the Platform snapshot -- it exists only as
    // PointerWheelTransition -- so it has to be accumulated per pointer while walking the
    // frame's transitions, rather than read out of the final snapshot the way motion is.
    std::array<bool, Platform::PointerCapacity> framePointerDeltaClaimed_{};
    std::array<bool, Platform::PointerCapacity> framePointerWheelClaimed_{};
    u64 currentEngineFrameIndex_ = 0;
    std::optional<u64> lastMappedEngineFrame_;
    std::optional<Platform::PlatformFrameId> lastMappedPlatformFrame_;
    std::optional<u64> lastAcceptedRawInputSequence_;
    std::optional<PendingRebind> activeRebind_;
    std::optional<PendingRebind> pendingRebind_;
    RebindState rebindState_ = RebindState::Idle;
    RebindTransaction lastRebindTransaction_{};
    u64 nextRebindTransaction_ = 1;
    ActionMapperStatistics statistics_{};
};

} // namespace Tina::Runtime::Input
