#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Sprite2DBindingResolver.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Scene {

struct ParticleSystem2DConfig final {
    usize capacity = 0;
    // Every seed, including zero, selects a fixed deterministic sequence. The
    // particle system never consults platform entropy or wall-clock time.
    u64 randomSeed = 0;
    // First render-facing stable key. Keys are retained by particles when the
    // dense live set is compacted and are never reused by this system.
    u64 firstStableParticleKey = 1;
};

struct ParticleVec2Range final {
    Vec2 minimum{};
    Vec2 maximum{};
};

struct ParticleLifetimeRange final {
    Core::Duration minimum{1.0};
    Core::Duration maximum{1.0};
};

struct ParticleBurst2D final {
    usize count = 1;
    // Copyable weak handle; emitting copies it into each particle and acquires no AssetLease.
    Asset::AssetHandle sprite{};
    Vec2 origin{};
    ParticleVec2Range positionOffset{};
    ParticleVec2Range velocity{};
    ParticleLifetimeRange lifetime{};
    Vec2 startSizeMeters{1.0F, 1.0F};
    Vec2 endSizeMeters{1.0F, 1.0F};
    ColorRgba8 startColor{};
    ColorRgba8 endColor{};
    float rotationRadians = 0.0F;
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
};

// Read-only live-particle state. Render interpolation is derived from age /
// lifetime during extract(), so update() only advances simulation fields.
struct Particle2D final {
    u64 stableParticleKey = 0;
    // Retained weak handle only; the particle system does not own the asset lifetime.
    Asset::AssetHandle sprite{};
    Vec2 position{};
    Vec2 velocity{};
    Core::Duration age{};
    Core::Duration lifetime{};
    Vec2 startSizeMeters{};
    Vec2 endSizeMeters{};
    ColorRgba8 startColor{};
    ColorRgba8 endColor{};
    float rotationRadians = 0.0F;
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
};

struct ParticleSystem2DUpdateStats final {
    usize advanced = 0;
    usize expired = 0;
    usize alive = 0;
};

struct ParticleSystem2DExtractStats final {
    usize submitted = 0;
};

// Owner-thread fixed-capacity particle simulation. Create() performs the only
// persistent PMR allocation; successful emit/update/extract calls do not grow storage.
class ParticleSystem2D final {
public:
    [[nodiscard]] static Core::Result<ParticleSystem2D> Create(
        ParticleSystem2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~ParticleSystem2D() noexcept = default;

    ParticleSystem2D(const ParticleSystem2D&) = delete;
    ParticleSystem2D& operator=(const ParticleSystem2D&) = delete;
    ParticleSystem2D(ParticleSystem2D&& other) noexcept;
    ParticleSystem2D& operator=(ParticleSystem2D&&) = delete;

    // The burst is atomic: validation/capacity failure leaves live particles,
    // stable-key allocation, and deterministic random state unchanged.
    [[nodiscard]] Core::Status emitBurst(const ParticleBurst2D& burst) noexcept;
    [[nodiscard]] Core::Result<ParticleSystem2DUpdateStats> update(Core::Duration delta) noexcept;
    // Borrows the resolver for this call only. Live particles require a resolver
    // and non-zero binding; either failure returns UnresolvedSprite.
    [[nodiscard]] Core::Result<ParticleSystem2DExtractStats>
    extract(Render::RenderSceneWriter& writer, Sprite2DBindingResolver spriteBindingResolver) const;

    void clear() noexcept { m_liveCount = 0; }

    [[nodiscard]] usize capacity() const noexcept { return m_capacity; }
    [[nodiscard]] usize liveCount() const noexcept { return m_liveCount; }
    [[nodiscard]] usize availableCapacity() const noexcept { return m_capacity - m_liveCount; }
    [[nodiscard]] u64 randomSeed() const noexcept { return m_randomSeed; }
    [[nodiscard]] std::span<const Particle2D> particles() const noexcept
    {
        return {m_particles.data(), m_liveCount};
    }

private:
    ParticleSystem2D(ParticleSystem2DConfig config, std::pmr::vector<Particle2D> particles) noexcept;

    usize m_capacity = 0;
    usize m_liveCount = 0;
    u64 m_randomSeed = 0;
    u64 m_randomState = 0;
    u64 m_nextStableParticleKey = 1;
    bool m_stableParticleKeysExhausted = false;
    std::pmr::vector<Particle2D> m_particles;
};

} // namespace Tina::Scene
