#include <tina/scene/ParticleSystem3D.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Scene {
namespace {

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(Math::Vec2 value) noexcept
{
    return finite(value.x) && finite(value.y);
}

[[nodiscard]] bool finite(Math::Vec3 value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool validRange(ParticleVec3Range range) noexcept
{
    return finite(range.minimum) && finite(range.maximum) && range.minimum.x <= range.maximum.x &&
           range.minimum.y <= range.maximum.y && range.minimum.z <= range.maximum.z;
}

[[nodiscard]] bool fitsFloat(double value) noexcept
{
    constexpr double MaximumFloat = static_cast<double>((std::numeric_limits<float>::max)());
    return std::isfinite(value) && value >= -MaximumFloat && value <= MaximumFloat;
}

[[nodiscard]] bool validOriginRange(float origin, float minimumOffset, float maximumOffset) noexcept
{
    return fitsFloat(static_cast<double>(origin) + static_cast<double>(minimumOffset)) &&
           fitsFloat(static_cast<double>(origin) + static_cast<double>(maximumOffset));
}

[[nodiscard]] Core::Status validateBurst(const ParticleBurst3D& burst) noexcept
{
    const double minimumLifetime = burst.lifetime.minimum.count();
    const double maximumLifetime = burst.lifetime.maximum.count();
    if (!burst.sprite || !finite(burst.origin) || !validRange(burst.positionOffset) ||
        !validRange(burst.velocity) || !finite(burst.gravityMetersPerSecondSquared) ||
        !finite(burst.startSizeMeters) || !finite(burst.endSizeMeters) || !finite(burst.rotationRadians) ||
        !Core::isSupportedBlendMode(burst.blendMode)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst3D contains a missing sprite, unsupported blend mode, or non-finite value");
    }
    if (!(minimumLifetime > 0.0) || !std::isfinite(minimumLifetime) || !std::isfinite(maximumLifetime) ||
        maximumLifetime < minimumLifetime) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst3D lifetime range must be finite, ordered, and greater than zero");
    }
    if (!(burst.startSizeMeters.x > 0.0F) || !(burst.startSizeMeters.y > 0.0F) ||
        !(burst.endSizeMeters.x > 0.0F) || !(burst.endSizeMeters.y > 0.0F)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst3D start and end sizes must be finite and greater than zero");
    }
    if (!validOriginRange(burst.origin.x, burst.positionOffset.minimum.x, burst.positionOffset.maximum.x) ||
        !validOriginRange(burst.origin.y, burst.positionOffset.minimum.y, burst.positionOffset.maximum.y) ||
        !validOriginRange(burst.origin.z, burst.positionOffset.minimum.z, burst.positionOffset.maximum.z)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst3D position range exceeds finite float coordinates");
    }
    return Core::success();
}

[[nodiscard]] float interpolate(float minimum, float maximum, double unit) noexcept
{
    return static_cast<float>(
        static_cast<double>(minimum) + (static_cast<double>(maximum) - static_cast<double>(minimum)) * unit);
}

[[nodiscard]] u8 interpolateChannel(u8 start, u8 end, double normalizedAge) noexcept
{
    const double value =
        static_cast<double>(start) + (static_cast<double>(end) - static_cast<double>(start)) * normalizedAge;
    return static_cast<u8>(std::clamp(value + 0.5, 0.0, 255.0));
}

[[nodiscard]] double randomUnit(u64& state) noexcept
{
    state += 0x9E3779B97F4A7C15ULL;
    u64 mixed = state;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31U;
    // The high 53 bits map exactly to [0, 1) in IEEE-754 binary64.
    return static_cast<double>(mixed >> 11U) * (1.0 / 9007199254740992.0);
}

// Semi-implicit Euler: velocity is advanced before position, so a particle under
// gravity is never integrated with its stale velocity for the frame it is born in.
struct IntegratedParticle final {
    Math::Vec3 velocity{};
    Math::Vec3 position{};
};

[[nodiscard]] bool integrate(const Particle3D& particle, double deltaSeconds,
                             IntegratedParticle& out) noexcept
{
    const double velocityX = static_cast<double>(particle.velocity.x) +
                             static_cast<double>(particle.gravityMetersPerSecondSquared.x) * deltaSeconds;
    const double velocityY = static_cast<double>(particle.velocity.y) +
                             static_cast<double>(particle.gravityMetersPerSecondSquared.y) * deltaSeconds;
    const double velocityZ = static_cast<double>(particle.velocity.z) +
                             static_cast<double>(particle.gravityMetersPerSecondSquared.z) * deltaSeconds;
    if (!fitsFloat(velocityX) || !fitsFloat(velocityY) || !fitsFloat(velocityZ)) {
        return false;
    }
    const double positionX = static_cast<double>(particle.position.x) + velocityX * deltaSeconds;
    const double positionY = static_cast<double>(particle.position.y) + velocityY * deltaSeconds;
    const double positionZ = static_cast<double>(particle.position.z) + velocityZ * deltaSeconds;
    if (!fitsFloat(positionX) || !fitsFloat(positionY) || !fitsFloat(positionZ)) {
        return false;
    }
    out.velocity = {static_cast<float>(velocityX), static_cast<float>(velocityY),
                    static_cast<float>(velocityZ)};
    out.position = {static_cast<float>(positionX), static_cast<float>(positionY),
                    static_cast<float>(positionZ)};
    return true;
}

} // namespace

ParticleSystem3D::ParticleSystem3D(
    ParticleSystem3DConfig config,
    std::pmr::vector<Particle3D> particles) noexcept
    : m_capacity(config.capacity),
      m_randomSeed(config.randomSeed),
      m_randomState(config.randomSeed),
      m_nextStableParticleKey(config.firstStableParticleKey),
      m_particles(std::move(particles))
{
}

ParticleSystem3D::ParticleSystem3D(ParticleSystem3D&& other) noexcept
    : m_capacity(std::exchange(other.m_capacity, 0)),
      m_liveCount(std::exchange(other.m_liveCount, 0)),
      m_randomSeed(other.m_randomSeed),
      m_randomState(other.m_randomState),
      m_nextStableParticleKey(other.m_nextStableParticleKey),
      m_stableParticleKeysExhausted(other.m_stableParticleKeysExhausted),
      m_particles(std::move(other.m_particles))
{
}

Core::Result<ParticleSystem3D> ParticleSystem3D::Create(
    ParticleSystem3DConfig config,
    std::pmr::memory_resource& resource)
{
    if (config.capacity == 0) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem3D capacity must be greater than zero");
    }
    if (config.firstStableParticleKey == 0) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleSystem3D first stable particle key must be non-zero");
    }

    std::pmr::vector<Particle3D> particles{&resource};
    if (config.capacity > particles.max_size()) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem3D capacity exceeds addressable PMR storage");
    }
    try {
        particles.resize(config.capacity);
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem3D fixed particle storage allocation failed");
    } catch (...) {
        return Core::failure(
            SceneErrorCode::ConstructionFailed,
            "ParticleSystem3D construction failed with an unknown exception");
    }
    return ParticleSystem3D{config, std::move(particles)};
}

Core::Status ParticleSystem3D::emitBurst(const ParticleBurst3D& burst) noexcept
{
    if (const Core::Status status = validateBurst(burst); !status) {
        return status;
    }
    if (burst.count > availableCapacity()) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem3D burst exceeds remaining fixed capacity");
    }
    if (burst.count == 0) {
        return Core::success();
    }
    const u64 remainingStableKeyCount =
        (std::numeric_limits<u64>::max)() - m_nextStableParticleKey + 1U;
    if (m_stableParticleKeysExhausted || burst.count > remainingStableKeyCount) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem3D stable particle key space is exhausted");
    }

    u64 nextRandomState = m_randomState;
    const u64 firstStableKey = m_nextStableParticleKey;
    for (usize index = 0; index < burst.count; ++index) {
        const double offsetXUnit = randomUnit(nextRandomState);
        const double offsetYUnit = randomUnit(nextRandomState);
        const double offsetZUnit = randomUnit(nextRandomState);
        const double velocityXUnit = randomUnit(nextRandomState);
        const double velocityYUnit = randomUnit(nextRandomState);
        const double velocityZUnit = randomUnit(nextRandomState);
        const double lifetimeUnit = randomUnit(nextRandomState);

        Particle3D particle{
            .stableParticleKey = firstStableKey + static_cast<u64>(index),
            .sprite = burst.sprite,
            .position = {
                burst.origin.x + interpolate(
                    burst.positionOffset.minimum.x, burst.positionOffset.maximum.x, offsetXUnit),
                burst.origin.y + interpolate(
                    burst.positionOffset.minimum.y, burst.positionOffset.maximum.y, offsetYUnit),
                burst.origin.z + interpolate(
                    burst.positionOffset.minimum.z, burst.positionOffset.maximum.z, offsetZUnit),
            },
            .velocity = {
                interpolate(burst.velocity.minimum.x, burst.velocity.maximum.x, velocityXUnit),
                interpolate(burst.velocity.minimum.y, burst.velocity.maximum.y, velocityYUnit),
                interpolate(burst.velocity.minimum.z, burst.velocity.maximum.z, velocityZUnit),
            },
            .gravityMetersPerSecondSquared = burst.gravityMetersPerSecondSquared,
            .age = Core::Duration::zero(),
            .lifetime = Core::Duration{
                burst.lifetime.minimum.count() +
                (burst.lifetime.maximum.count() - burst.lifetime.minimum.count()) * lifetimeUnit},
            .startSizeMeters = burst.startSizeMeters,
            .endSizeMeters = burst.endSizeMeters,
            .startColor = burst.startColor,
            .endColor = burst.endColor,
            .rotationRadians = burst.rotationRadians,
            .blendMode = burst.blendMode,
        };
        m_particles[m_liveCount + index] = particle;
    }

    const u64 lastStableKey = firstStableKey + static_cast<u64>(burst.count - 1);
    m_randomState = nextRandomState;
    m_liveCount += burst.count;
    if (lastStableKey == (std::numeric_limits<u64>::max)()) {
        m_stableParticleKeysExhausted = true;
    } else {
        m_nextStableParticleKey = lastStableKey + 1;
    }
    return Core::success();
}

Core::Result<ParticleSystem3DUpdateStats> ParticleSystem3D::update(Core::Duration delta) noexcept
{
    const double deltaSeconds = delta.count();
    if (deltaSeconds < 0.0 || !std::isfinite(deltaSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleSystem3D update delta must be finite and non-negative");
    }

    // Preflight all survivors so an overflowing integration cannot partially mutate the system.
    for (usize index = 0; index < m_liveCount; ++index) {
        const Particle3D& particle = m_particles[index];
        const double nextAge = particle.age.count() + deltaSeconds;
        if (!std::isfinite(nextAge)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "ParticleSystem3D particle age overflowed during update");
        }
        if (nextAge < particle.lifetime.count()) {
            IntegratedParticle integrated{};
            if (!integrate(particle, deltaSeconds, integrated)) {
                return Core::failure(
                    SceneErrorCode::InvalidComponent,
                    "ParticleSystem3D particle velocity or position overflowed during update");
            }
        }
    }

    const usize previousLiveCount = m_liveCount;
    usize writeIndex = 0;
    for (usize readIndex = 0; readIndex < previousLiveCount; ++readIndex) {
        Particle3D particle = m_particles[readIndex];
        const double nextAge = particle.age.count() + deltaSeconds;
        if (nextAge >= particle.lifetime.count()) {
            continue;
        }
        IntegratedParticle integrated{};
        if (!integrate(particle, deltaSeconds, integrated)) {
            // Unreachable: the preflight above proved every survivor integrates.
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "ParticleSystem3D particle velocity or position overflowed during update");
        }
        particle.age = Core::Duration{nextAge};
        particle.velocity = integrated.velocity;
        particle.position = integrated.position;
        m_particles[writeIndex] = particle;
        ++writeIndex;
    }
    m_liveCount = writeIndex;

    return ParticleSystem3DUpdateStats{
        .advanced = previousLiveCount,
        .expired = previousLiveCount - writeIndex,
        .alive = writeIndex,
    };
}

Core::Result<ParticleSystem3DExtractStats>
ParticleSystem3D::extract(
    Render::RenderSceneWriter& writer,
    Render::FrameResourceSink& frameResources,
    Asset::AssetFrameResourceResolver spriteBindingResolver) const
{
    ParticleSystem3DExtractStats stats{};
    if (m_liveCount == 0) {
        return stats;
    }
    Asset::AssetHandle lastSprite{};
    Render::FrameResourceRef texture{};
    for (const Particle3D& particle : particles()) {
        if (!particle.sprite || !spriteBindingResolver) {
            return Core::failure(
                SceneErrorCode::UnresolvedSprite,
                "ParticleSystem3D particle has no resolvable sprite asset");
        }
        if (particle.sprite != lastSprite) {
            auto resolved = spriteBindingResolver(particle.sprite, frameResources);
            if (!resolved) {
                return Core::failure(std::move(resolved.error()));
            }
            if (!resolved->hasValue()) {
                return Core::failure(
                    SceneErrorCode::UnresolvedSprite,
                    "ParticleSystem3D particle sprite asset has no render binding");
            }
            lastSprite = particle.sprite;
            texture = *resolved;
        }
        const double normalizedAge = std::clamp(
            particle.age.count() / particle.lifetime.count(), 0.0, 1.0);
        const Render::RenderParticle3DInput input{
            .texture = texture,
            .stableParticleKey = particle.stableParticleKey,
            .worldX = particle.position.x,
            .worldY = particle.position.y,
            .worldZ = particle.position.z,
            .widthMeters = interpolate(
                particle.startSizeMeters.x, particle.endSizeMeters.x, normalizedAge),
            .heightMeters = interpolate(
                particle.startSizeMeters.y, particle.endSizeMeters.y, normalizedAge),
            .rotationRadians = particle.rotationRadians,
            .red = interpolateChannel(particle.startColor.red, particle.endColor.red, normalizedAge),
            .green = interpolateChannel(particle.startColor.green, particle.endColor.green, normalizedAge),
            .blue = interpolateChannel(particle.startColor.blue, particle.endColor.blue, normalizedAge),
            .alpha = interpolateChannel(particle.startColor.alpha, particle.endColor.alpha, normalizedAge),
            .blendMode = particle.blendMode,
            .visible = true,
        };
        if (Core::Status status = writer.addParticle3D(input); !status) {
            return Core::failure(std::move(status.error()));
        }
        ++stats.submitted;
    }
    return stats;
}

} // namespace Tina::Scene
