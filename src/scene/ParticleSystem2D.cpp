#include <tina/scene/ParticleSystem2D.hpp>

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

[[nodiscard]] bool finite(Vec2 value) noexcept
{
    return finite(value.x) && finite(value.y);
}

[[nodiscard]] bool validRange(ParticleVec2Range range) noexcept
{
    return finite(range.minimum) && finite(range.maximum) &&
           range.minimum.x <= range.maximum.x && range.minimum.y <= range.maximum.y;
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

[[nodiscard]] Core::Status validateBurst(const ParticleBurst2D& burst) noexcept
{
    const double minimumLifetime = burst.lifetime.minimum.count();
    const double maximumLifetime = burst.lifetime.maximum.count();
    if (burst.spriteKey == 0 || !finite(burst.origin) || !validRange(burst.positionOffset) ||
        !validRange(burst.velocity) || !finite(burst.startSizeMeters) || !finite(burst.endSizeMeters) ||
        !finite(burst.rotationRadians)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst2D contains a missing sprite or non-finite value");
    }
    if (!(minimumLifetime > 0.0) || !std::isfinite(minimumLifetime) ||
        !std::isfinite(maximumLifetime) || maximumLifetime < minimumLifetime) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst2D lifetime range must be finite, ordered, and greater than zero");
    }
    if (!(burst.startSizeMeters.x > 0.0F) || !(burst.startSizeMeters.y > 0.0F) ||
        !(burst.endSizeMeters.x > 0.0F) || !(burst.endSizeMeters.y > 0.0F)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst2D start and end sizes must be finite and greater than zero");
    }
    if (!validOriginRange(burst.origin.x, burst.positionOffset.minimum.x, burst.positionOffset.maximum.x) ||
        !validOriginRange(burst.origin.y, burst.positionOffset.minimum.y, burst.positionOffset.maximum.y)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleBurst2D position range exceeds finite float coordinates");
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
    const double value = static_cast<double>(start) +
                         (static_cast<double>(end) - static_cast<double>(start)) * normalizedAge;
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

} // namespace

ParticleSystem2D::ParticleSystem2D(
    ParticleSystem2DConfig config,
    std::pmr::vector<Particle2D> particles) noexcept
    : m_capacity(config.capacity),
      m_randomSeed(config.randomSeed),
      m_randomState(config.randomSeed),
      m_nextStableParticleKey(config.firstStableParticleKey),
      m_particles(std::move(particles))
{
}

ParticleSystem2D::ParticleSystem2D(ParticleSystem2D&& other) noexcept
    : m_capacity(std::exchange(other.m_capacity, 0)),
      m_liveCount(std::exchange(other.m_liveCount, 0)),
      m_randomSeed(other.m_randomSeed),
      m_randomState(other.m_randomState),
      m_nextStableParticleKey(other.m_nextStableParticleKey),
      m_stableParticleKeysExhausted(other.m_stableParticleKeysExhausted),
      m_particles(std::move(other.m_particles))
{
}

Core::Result<ParticleSystem2D> ParticleSystem2D::Create(
    ParticleSystem2DConfig config,
    std::pmr::memory_resource& resource)
{
    if (config.capacity == 0) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem2D capacity must be greater than zero");
    }
    if (config.firstStableParticleKey == 0) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleSystem2D first stable particle key must be non-zero");
    }

    std::pmr::vector<Particle2D> particles{&resource};
    if (config.capacity > particles.max_size()) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem2D capacity exceeds addressable PMR storage");
    }
    try {
        particles.resize(config.capacity);
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem2D fixed particle storage allocation failed");
    } catch (...) {
        return Core::failure(
            SceneErrorCode::ConstructionFailed,
            "ParticleSystem2D construction failed with an unknown exception");
    }
    return ParticleSystem2D{config, std::move(particles)};
}

Core::Status ParticleSystem2D::emitBurst(const ParticleBurst2D& burst) noexcept
{
    if (const Core::Status status = validateBurst(burst); !status) {
        return status;
    }
    if (burst.count > availableCapacity()) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem2D burst exceeds remaining fixed capacity");
    }
    if (burst.count == 0) {
        return Core::success();
    }
    const u64 remainingStableKeyCount =
        (std::numeric_limits<u64>::max)() - m_nextStableParticleKey + 1U;
    if (m_stableParticleKeysExhausted || burst.count > remainingStableKeyCount) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "ParticleSystem2D stable particle key space is exhausted");
    }

    u64 nextRandomState = m_randomState;
    const u64 firstStableKey = m_nextStableParticleKey;
    for (usize index = 0; index < burst.count; ++index) {
        const double offsetXUnit = randomUnit(nextRandomState);
        const double offsetYUnit = randomUnit(nextRandomState);
        const double velocityXUnit = randomUnit(nextRandomState);
        const double velocityYUnit = randomUnit(nextRandomState);
        const double lifetimeUnit = randomUnit(nextRandomState);

        Particle2D particle{
            .stableParticleKey = firstStableKey + static_cast<u64>(index),
            .spriteKey = burst.spriteKey,
            .position = {
                burst.origin.x + interpolate(
                    burst.positionOffset.minimum.x,
                    burst.positionOffset.maximum.x,
                    offsetXUnit),
                burst.origin.y + interpolate(
                    burst.positionOffset.minimum.y,
                    burst.positionOffset.maximum.y,
                    offsetYUnit),
            },
            .velocity = {
                interpolate(burst.velocity.minimum.x, burst.velocity.maximum.x, velocityXUnit),
                interpolate(burst.velocity.minimum.y, burst.velocity.maximum.y, velocityYUnit),
            },
            .age = Core::Duration::zero(),
            .lifetime = Core::Duration{
                burst.lifetime.minimum.count() +
                (burst.lifetime.maximum.count() - burst.lifetime.minimum.count()) * lifetimeUnit},
            .startSizeMeters = burst.startSizeMeters,
            .endSizeMeters = burst.endSizeMeters,
            .startColor = burst.startColor,
            .endColor = burst.endColor,
            .rotationRadians = burst.rotationRadians,
            .sortingLayer = burst.sortingLayer,
            .orderInLayer = burst.orderInLayer,
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

Core::Result<ParticleSystem2DUpdateStats> ParticleSystem2D::update(Core::Duration delta) noexcept
{
    const double deltaSeconds = delta.count();
    if (deltaSeconds < 0.0 || !std::isfinite(deltaSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "ParticleSystem2D update delta must be finite and non-negative");
    }

    // Preflight all survivors so an overflowing integration cannot partially mutate the system.
    for (usize index = 0; index < m_liveCount; ++index) {
        const Particle2D& particle = m_particles[index];
        const double nextAge = particle.age.count() + deltaSeconds;
        if (!std::isfinite(nextAge)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "ParticleSystem2D particle age overflowed during update");
        }
        if (nextAge < particle.lifetime.count()) {
            const double nextX = static_cast<double>(particle.position.x) +
                                 static_cast<double>(particle.velocity.x) * deltaSeconds;
            const double nextY = static_cast<double>(particle.position.y) +
                                 static_cast<double>(particle.velocity.y) * deltaSeconds;
            if (!fitsFloat(nextX) || !fitsFloat(nextY)) {
                return Core::failure(
                    SceneErrorCode::InvalidComponent,
                    "ParticleSystem2D particle position overflowed during update");
            }
        }
    }

    const usize previousLiveCount = m_liveCount;
    usize writeIndex = 0;
    for (usize readIndex = 0; readIndex < previousLiveCount; ++readIndex) {
        Particle2D particle = m_particles[readIndex];
        const double nextAge = particle.age.count() + deltaSeconds;
        if (nextAge >= particle.lifetime.count()) {
            continue;
        }
        particle.age = Core::Duration{nextAge};
        particle.position.x = static_cast<float>(
            static_cast<double>(particle.position.x) +
            static_cast<double>(particle.velocity.x) * deltaSeconds);
        particle.position.y = static_cast<float>(
            static_cast<double>(particle.position.y) +
            static_cast<double>(particle.velocity.y) * deltaSeconds);
        m_particles[writeIndex] = particle;
        ++writeIndex;
    }
    m_liveCount = writeIndex;

    return ParticleSystem2DUpdateStats{
        .advanced = previousLiveCount,
        .expired = previousLiveCount - writeIndex,
        .alive = writeIndex,
    };
}

Core::Result<ParticleSystem2DExtractStats>
ParticleSystem2D::extract(Render::RenderSceneWriter& writer) const
{
    ParticleSystem2DExtractStats stats{};
    for (const Particle2D& particle : particles()) {
        const double normalizedAge = std::clamp(
            particle.age.count() / particle.lifetime.count(),
            0.0,
            1.0);
        const Render::RenderSprite2DInput input{
            .spriteKey = particle.spriteKey,
            .stableEntityKey = particle.stableParticleKey,
            .centerX = particle.position.x,
            .centerY = particle.position.y,
            .rotationRadians = particle.rotationRadians,
            .widthMeters = interpolate(
                particle.startSizeMeters.x,
                particle.endSizeMeters.x,
                normalizedAge),
            .heightMeters = interpolate(
                particle.startSizeMeters.y,
                particle.endSizeMeters.y,
                normalizedAge),
            .sortingLayer = particle.sortingLayer,
            .orderInLayer = particle.orderInLayer,
            .red = interpolateChannel(particle.startColor.red, particle.endColor.red, normalizedAge),
            .green = interpolateChannel(particle.startColor.green, particle.endColor.green, normalizedAge),
            .blue = interpolateChannel(particle.startColor.blue, particle.endColor.blue, normalizedAge),
            .alpha = interpolateChannel(particle.startColor.alpha, particle.endColor.alpha, normalizedAge),
            .visible = true,
        };
        if (Core::Status status = writer.addSprite2D(input); !status) {
            return Core::failure(std::move(status.error()));
        }
        ++stats.submitted;
    }
    return stats;
}

} // namespace Tina::Scene
