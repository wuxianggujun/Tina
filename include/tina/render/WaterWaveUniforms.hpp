#pragma once

#include <tina/render/RenderDevice.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <utility>

namespace Tina::Render {

struct WaterWaveUniforms final {
    std::array<GpuShaderUniformValue, 6> values{};
    Core::u8 count = 0;

    [[nodiscard]] GpuShaderUniformBindingDesc descriptor() const noexcept
    { return {std::span<const GpuShaderUniformValue>{values.data(), count}}; }
};

inline void setWaterUniform(GpuShaderUniformValue& target, const char* name,
                            std::array<float, 4> value) noexcept
{
    std::fill(target.name.begin(), target.name.end(), char{});
    std::memcpy(target.name.data(), name, std::min(std::strlen(name), target.name.size() - 1));
    target.value = value;
}

template <typename Params>
requires requires(const Params& params) {
    params.waveA.direction.x; params.waveB.direction.x; params.timeSeconds; params.opacity;
    params.waveA.rippleStrength;
}
[[nodiscard]] inline Core::Result<WaterWaveUniforms>
makeWaterWaveUniforms(const Params& params) noexcept
{
    const auto finiteWave = [](const auto& wave) noexcept {
        return std::isfinite(wave.direction.x) && std::isfinite(wave.direction.y) &&
               std::isfinite(wave.amplitude) && std::isfinite(wave.wavelength) &&
               std::isfinite(wave.speed) && std::isfinite(wave.phase) &&
               std::isfinite(wave.rippleStrength) && wave.wavelength > 0.0F;
    };
    if (!finiteWave(params.waveA) || !finiteWave(params.waveB) ||
        !std::isfinite(params.timeSeconds) || !std::isfinite(params.opacity) ||
        params.opacity < 0.0F || params.opacity > 1.0F)
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "water surface uniforms contain invalid values");
    WaterWaveUniforms result{};
    setWaterUniform(result.values[result.count++], "u_waterWave2DA", {params.waveA.direction.x, params.waveA.direction.y, params.waveA.amplitude, params.waveA.wavelength});
    setWaterUniform(result.values[result.count++], "u_waterWave2DB", {params.waveB.direction.x, params.waveB.direction.y, params.waveB.amplitude, params.waveB.wavelength});
    setWaterUniform(result.values[result.count++], "u_waterWave2DTime", {params.waveA.speed, params.waveA.phase, params.waveB.speed, params.waveB.phase});
    setWaterUniform(result.values[result.count++], "u_waterSurfaceParams", {params.opacity, params.waveA.rippleStrength + params.waveB.rippleStrength, params.timeSeconds, 0.0F});
    return result;
}

template <typename Wave>
requires requires(const Wave& wave) { wave.direction.x; wave.direction.y; wave.amplitude; wave.wavelength; wave.speed; wave.phase; }
[[nodiscard]] inline Core::Result<WaterWaveUniforms>
makeWaterWaveUniforms(const Wave& wave, float timeSeconds) noexcept
{
    if (!std::isfinite(wave.direction.x) || !std::isfinite(wave.direction.y) ||
        !std::isfinite(wave.amplitude) || !std::isfinite(wave.wavelength) ||
        !std::isfinite(wave.speed) || !std::isfinite(wave.phase) ||
        wave.wavelength <= 0.0F || !std::isfinite(timeSeconds))
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "water wave uniforms contain invalid values");
    WaterWaveUniforms result{};
    setWaterUniform(result.values[result.count++], "u_waterWave3D", {wave.direction.x, wave.direction.y, wave.amplitude, wave.wavelength});
    setWaterUniform(result.values[result.count++], "u_waterWave3DTime", {wave.speed, wave.phase, timeSeconds, 0.0F});
    return result;
}

} // namespace Tina::Render
