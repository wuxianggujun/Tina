#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/math/Vec.hpp>

#include <cmath>

namespace Tina::Gameplay {

struct WaterWave2D final {
    Math::Vec2 direction{1.0F, 0.0F};
    float amplitude = 0.02F;
    float wavelength = 1.0F;
    float speed = 1.0F;
    float phase = 0.0F;
    float rippleStrength = 0.0F;
};

struct WaterWave3D final {
    Math::Vec2 direction{1.0F, 0.0F};
    float amplitude = 0.1F;
    float wavelength = 2.0F;
    float speed = 1.0F;
    float phase = 0.0F;
};

struct WaterSurfaceParams final {
    WaterWave2D waveA{};
    WaterWave2D waveB{.direction = {0.0F, 1.0F}, .amplitude = 0.01F, .wavelength = 0.6F,
                      .speed = -0.7F, .phase = 1.7F, .rippleStrength = 0.0F};
    float timeSeconds = 0.0F;
    float opacity = 1.0F;
};

// Runtime component payloads owned by a Gameplay system. They intentionally do
// not contain Scene::EntityId: association with an entity belongs to the
// gameplay owner, while these values remain usable by 2D and 3D tools alike.
struct WaterSurfaceComponent2D final {
    WaterSurfaceParams surface{};
    bool enabled = true;
};

struct WaterSurfaceComponent3D final {
    WaterWave3D wave{};
    float timeSeconds = 0.0F;
    bool enabled = true;
};


[[nodiscard]] inline bool isFinite(const WaterWave2D& wave) noexcept
{
    return Math::isFinite(wave.direction) && std::isfinite(wave.amplitude) &&
           std::isfinite(wave.wavelength) && std::isfinite(wave.speed) &&
           std::isfinite(wave.phase) && std::isfinite(wave.rippleStrength) &&
           wave.wavelength > 0.0F;
}

[[nodiscard]] inline bool isFinite(const WaterWave3D& wave) noexcept
{
    return Math::isFinite(wave.direction) && std::isfinite(wave.amplitude) &&
           std::isfinite(wave.wavelength) && std::isfinite(wave.speed) &&
           std::isfinite(wave.phase) && wave.wavelength > 0.0F;
}

[[nodiscard]] inline Core::Result<float> evaluateWave(const WaterWave2D& wave,
                                                       Math::Vec2 position, float timeSeconds) noexcept
{
    if (!isFinite(wave) || !Math::isFinite(position) || !std::isfinite(timeSeconds))
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "invalid 2D water wave parameters");
    const float length = std::sqrt(wave.direction.x * wave.direction.x + wave.direction.y * wave.direction.y);
    if (!(length > 1.0e-6F)) return Core::failure(Core::CoreErrorCode::InvalidArgument, "wave direction must be non-zero");
    const float projected = (position.x * wave.direction.x + position.y * wave.direction.y) / length;
    constexpr float Tau = 6.2831853071795864769F;
    return wave.amplitude * std::sin(Tau * projected / wave.wavelength + wave.speed * timeSeconds + wave.phase);
}

[[nodiscard]] inline Core::Result<Math::Vec3> evaluateWave3D(const WaterWave3D& wave,
                                                              Math::Vec3 position, float timeSeconds) noexcept
{
    if (!isFinite(wave) || !Math::isFinite(position) || !std::isfinite(timeSeconds))
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "invalid 3D water wave parameters");
    const float length = std::sqrt(wave.direction.x * wave.direction.x + wave.direction.y * wave.direction.y);
    if (!(length > 1.0e-6F)) return Core::failure(Core::CoreErrorCode::InvalidArgument, "wave direction must be non-zero");
    const float projected = (position.x * wave.direction.x + position.z * wave.direction.y) / length;
    constexpr float Tau = 6.2831853071795864769F;
    return Math::Vec3{position.x, position.y + wave.amplitude * std::sin(Tau * projected / wave.wavelength + wave.speed * timeSeconds + wave.phase), position.z};
}

} // namespace Tina::Gameplay
