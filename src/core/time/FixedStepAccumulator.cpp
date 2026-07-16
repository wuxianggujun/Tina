#include <tina/core/time/FixedStepAccumulator.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Core {
namespace {

[[nodiscard]] bool isFinitePositive(Duration value) noexcept
{
    return std::isfinite(value.count()) && value.count() > 0.0;
}

} // namespace

Result<FixedStepAccumulator> FixedStepAccumulator::Create(FixedStepConfig config)
{
    if (!isFinitePositive(config.fixedDelta)) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "fixedDelta must be finite and greater than zero");
    }
    if (!isFinitePositive(config.maximumAcceptedRealDelta)) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "maximumAcceptedRealDelta must be finite and greater than zero");
    }
    if (config.maximumStepsPerFrame == 0) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "maximumStepsPerFrame must be greater than zero");
    }
    return FixedStepAccumulator(config);
}

Result<FixedStepFramePlan> FixedStepAccumulator::advance(
    Duration realDelta,
    double gameplayTimeScale)
{
    const double realSeconds = realDelta.count();
    if (!std::isfinite(realSeconds) || realSeconds < 0.0) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "realDelta must be finite and non-negative");
    }
    if (!std::isfinite(gameplayTimeScale) || gameplayTimeScale < 0.0) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "gameplayTimeScale must be finite and non-negative");
    }

    const double maximumAcceptedSeconds = m_config.maximumAcceptedRealDelta.count();
    const double acceptedRealSeconds = std::min(realSeconds, maximumAcceptedSeconds);
    const double updateSeconds = acceptedRealSeconds * gameplayTimeScale;
    const double totalSeconds = m_accumulator.count() + updateSeconds;
    if (!std::isfinite(totalSeconds)) {
        return failure(
            CoreErrorCode::CapacityExceeded,
            "fixed-step accumulator overflowed");
    }

    const double fixedSeconds = m_config.fixedDelta.count();
    const double tolerance = fixedSeconds * 1.0e-12;
    const double availableSteps = std::floor((totalSeconds + tolerance) / fixedSeconds);
    const auto stepCount = static_cast<u32>(std::min(
        availableSteps,
        static_cast<double>(m_config.maximumStepsPerFrame)));
    const double discardedWholeSteps = std::max(
        0.0,
        availableSteps - static_cast<double>(stepCount));

    double remainderSeconds = totalSeconds - availableSteps * fixedSeconds;
    if (remainderSeconds < 0.0 && remainderSeconds >= -tolerance) {
        remainderSeconds = 0.0;
    }
    remainderSeconds = std::clamp(
        remainderSeconds,
        0.0,
        std::nextafter(fixedSeconds, 0.0));

    const double rejectedRealSeconds = realSeconds - acceptedRealSeconds;
    const double discardedSimulationSeconds = discardedWholeSteps * fixedSeconds;
    const double totalRejectedRealSeconds = m_totalRejectedReal.count() + rejectedRealSeconds;
    const double totalDiscardedSimulationSeconds = m_totalDiscardedSimulation.count()
        + discardedSimulationSeconds;
    if (!std::isfinite(totalRejectedRealSeconds)
        || !std::isfinite(totalDiscardedSimulationSeconds)) {
        return failure(
            CoreErrorCode::CapacityExceeded,
            "fixed-step diagnostics overflowed");
    }

    m_accumulator = Duration{remainderSeconds};
    m_totalRejectedReal = Duration{totalRejectedRealSeconds};
    m_totalDiscardedSimulation = Duration{totalDiscardedSimulationSeconds};

    return FixedStepFramePlan{
        .realDelta = realDelta,
        .acceptedRealDelta = Duration{acceptedRealSeconds},
        .rejectedRealDelta = Duration{rejectedRealSeconds},
        .updateDelta = Duration{updateSeconds},
        .discardedSimulationDelta = Duration{discardedSimulationSeconds},
        .fixedDelta = m_config.fixedDelta,
        .stepCount = stepCount,
        .interpolation = interpolation(),
    };
}

void FixedStepAccumulator::reset() noexcept
{
    m_accumulator = Duration{};
    m_totalRejectedReal = Duration{};
    m_totalDiscardedSimulation = Duration{};
}

double FixedStepAccumulator::interpolation() const noexcept
{
    const double value = m_accumulator.count() / m_config.fixedDelta.count();
    return std::clamp(value, 0.0, std::nextafter(1.0, 0.0));
}

} // namespace Tina::Core
