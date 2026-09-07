#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/gameplay/WaterWave.hpp>

namespace Tina::Gameplay {

// Owner-thread, entity-agnostic animation state. It is intentionally separate
// from Scene render components so World2D snapshot schema cannot lose runtime
// water state. Callers convert the published values into material uniforms.
class WaterSurfaceAnimator final {
public:
    WaterSurfaceAnimator() noexcept = default;

    [[nodiscard]] static Core::Result<WaterSurfaceAnimator> Create(WaterSurfaceParams params)
    {
        if (!isFinite(params.waveA) || !isFinite(params.waveB) || !std::isfinite(params.timeSeconds) ||
            !std::isfinite(params.opacity) || params.opacity < 0.0F || params.opacity > 1.0F)
            return Core::failure(Core::CoreErrorCode::InvalidArgument, "water surface parameters are invalid");
        WaterSurfaceAnimator animator;
        animator.m_params = params;
        return animator;
    }

    [[nodiscard]] Core::Status advance(float deltaSeconds) noexcept
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F)
            return Core::failure(Core::CoreErrorCode::InvalidArgument, "water animation delta must be finite and non-negative");
        constexpr float Period = 4096.0F;
        m_params.timeSeconds = std::fmod(m_params.timeSeconds + deltaSeconds, Period);
        if (m_params.timeSeconds < 0.0F) m_params.timeSeconds += Period;
        return Core::success();
    }

    [[nodiscard]] Core::Status setParams(WaterSurfaceParams params) noexcept
    {
        if (!isFinite(params.waveA) || !isFinite(params.waveB) || !std::isfinite(params.timeSeconds) ||
            !std::isfinite(params.opacity) || params.opacity < 0.0F || params.opacity > 1.0F)
            return Core::failure(Core::CoreErrorCode::InvalidArgument, "water surface parameters are invalid");
        m_params = params;
        return Core::success();
    }

    [[nodiscard]] const WaterSurfaceParams& params() const noexcept { return m_params; }

private:
    WaterSurfaceParams m_params{};
};

} // namespace Tina::Gameplay
