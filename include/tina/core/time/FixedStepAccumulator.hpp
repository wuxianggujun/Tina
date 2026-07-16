#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>

namespace Tina::Core {

struct FixedStepConfig final {
    Duration fixedDelta{1.0 / 60.0};
    Duration maximumAcceptedRealDelta{0.25};
    u32 maximumStepsPerFrame = 4;
};

struct FixedStepFramePlan final {
    Duration realDelta{};
    Duration acceptedRealDelta{};
    Duration rejectedRealDelta{};
    Duration updateDelta{};
    Duration discardedSimulationDelta{};
    Duration fixedDelta{};
    u32 stepCount = 0;
    double interpolation = 0.0;
};

class FixedStepAccumulator final {
public:
    [[nodiscard]] static Result<FixedStepAccumulator> Create(FixedStepConfig config);

    FixedStepAccumulator(const FixedStepAccumulator&) = default;
    FixedStepAccumulator& operator=(const FixedStepAccumulator&) = default;
    FixedStepAccumulator(FixedStepAccumulator&&) noexcept = default;
    FixedStepAccumulator& operator=(FixedStepAccumulator&&) noexcept = default;

    [[nodiscard]] Result<FixedStepFramePlan> advance(
        Duration realDelta,
        double gameplayTimeScale = 1.0);
    void reset() noexcept;

    [[nodiscard]] const FixedStepConfig& config() const noexcept { return m_config; }
    [[nodiscard]] Duration pendingDelta() const noexcept { return m_accumulator; }
    [[nodiscard]] Duration totalRejectedRealDelta() const noexcept { return m_totalRejectedReal; }
    [[nodiscard]] Duration totalDiscardedSimulationDelta() const noexcept
    {
        return m_totalDiscardedSimulation;
    }
    [[nodiscard]] double interpolation() const noexcept;

private:
    explicit FixedStepAccumulator(FixedStepConfig config) noexcept
        : m_config(config)
    {
    }

    FixedStepConfig m_config;
    Duration m_accumulator{};
    Duration m_totalRejectedReal{};
    Duration m_totalDiscardedSimulation{};
};

} // namespace Tina::Core
