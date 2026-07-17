#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/InputActions.hpp>

#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace Tina::Runtime::Input {

using ActionSourceToken = usize;
inline constexpr ActionSourceToken InvalidActionSourceToken = (std::numeric_limits<ActionSourceToken>::max)();

enum class SimulationLatchAppendResult : u8 {
    Appended,
    NoTransitionNeeded,
    CapacityResetInserted,
};

struct SimulationActionLatchStatistics final {
    u64 capacityResetCount = 0;
    u64 rawInputResetCount = 0;
};

// Runtime-private fixed-capacity bridge between render-frame input routing and
// fixed simulation ticks. Create performs every allocation; mapping and tick
// consumption never grow storage.
class SimulationActionLatch final {
  public:
    [[nodiscard]] static Core::Result<SimulationActionLatch> Create(std::span<const InputActionId> sortedActions,
                                                                    u32 transitionCapacity);

    SimulationActionLatch(const SimulationActionLatch&) = delete;
    SimulationActionLatch& operator=(const SimulationActionLatch&) = delete;
    SimulationActionLatch(SimulationActionLatch&&) noexcept = default;
    SimulationActionLatch& operator=(SimulationActionLatch&&) noexcept = default;

    [[nodiscard]] Core::Status validateNextUncompletedTick(u64 tick) const;

    [[nodiscard]] Core::Status setHeld(InputActionId action, bool held) noexcept;
    [[nodiscard]] bool isHeld(InputActionId action) const noexcept;

    [[nodiscard]] Core::Result<SimulationLatchAppendResult> append(u64 targetTick, DigitalActionTransition transition,
                                                                   ActionSourceToken source = InvalidActionSourceToken);

    // Removes still-unconsumed edges produced by this physical source. If the
    // remaining batch no longer reaches the current aggregate Action state, one
    // replacement edge (Pressed or Cancelled) is emitted; otherwise cancellation
    // stays silent.
    [[nodiscard]] Core::Result<SimulationLatchAppendResult> reconcileCancellation(u64 targetTick, InputActionId action,
                                                                                  ActionSourceToken source,
                                                                                  u64 sourceSequence,
                                                                                  bool forceStateReconciliation);

    [[nodiscard]] Core::Status resetStream(u64 targetTick, SimulationInputStreamReset reset);

    [[nodiscard]] Core::Result<SimulationActionSnapshot> snapshotForTick(u64 tick) const;
    [[nodiscard]] Core::Status completeTick(u64 tick);

    [[nodiscard]] const SimulationActionLatchStatistics& statistics() const noexcept
    {
        return statistics_;
    }

    [[nodiscard]] usize transitionCapacity() const noexcept
    {
        return transitionCapacity_;
    }

  private:
    SimulationActionLatch(std::vector<InputActionState> states, std::vector<bool> deliveredHeld,
                          std::vector<SimulationActionTransition> transitionStorage,
                          std::vector<ActionSourceToken> transitionSourceStorage, usize transitionCapacity) noexcept;

    [[nodiscard]] usize findActionIndex(InputActionId action) const noexcept;
    [[nodiscard]] Core::Status ensureTarget(u64 targetTick);
    [[nodiscard]] bool removePendingTransitions(InputActionId action, ActionSourceToken source) noexcept;
    void clearPending() noexcept;

    std::vector<InputActionState> states_;
    std::vector<bool> deliveredHeld_;
    std::vector<SimulationActionTransition> transitions_;
    std::vector<ActionSourceToken> transitionSources_;
    usize transitionCapacity_ = 0;
    usize normalTransitionCount_ = 0;
    std::optional<u64> targetTick_;
    std::optional<u64> lastCompletedTick_;
    SimulationActionLatchStatistics statistics_{};
};

} // namespace Tina::Runtime::Input
