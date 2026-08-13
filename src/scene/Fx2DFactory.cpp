#include <tina/scene/Fx2DFactory.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <utility>

namespace Tina::Scene {
namespace {

[[nodiscard]] ColorRgba8 color(Core::u32 rgba) noexcept
{
    return {
        .red = static_cast<Core::u8>(rgba),
        .green = static_cast<Core::u8>(rgba >> 8U),
        .blue = static_cast<Core::u8>(rgba >> 16U),
        .alpha = static_cast<Core::u8>(rgba >> 24U),
    };
}

} // namespace

Core::Result<Fx2DInstance> createFx2DFromAsset(
    const AssetFormat::Fx2DPayloadDesc& asset,
    Asset::AssetHandle resolvedSprite,
    std::pmr::memory_resource& resource)
{
    if (!resolvedSprite) {
        return Core::failure(
            SceneErrorCode::UnresolvedSprite,
            "Fx2D factory requires a resolved Sprite AssetHandle");
    }
    if (auto status = AssetFormat::validateFx2DPayloadDesc(asset); !status) {
        return Core::failure(std::move(status.error()));
    }
    auto particles = ParticleSystem2D::Create(
        {
            .capacity = asset.particle.capacity,
            .randomSeed = asset.particle.randomSeed,
            .firstStableParticleKey = asset.particle.firstStableParticleKey,
        },
        resource);
    if (!particles) {
        return Core::failure(std::move(particles.error()));
    }

    ParticleBurst2D burst{
        .count = asset.particle.count,
        .sprite = resolvedSprite,
        .origin = {asset.particle.originX, asset.particle.originY},
        .positionOffset = {
            {asset.particle.positionOffsetMinX, asset.particle.positionOffsetMinY},
            {asset.particle.positionOffsetMaxX, asset.particle.positionOffsetMaxY},
        },
        .velocity = {
            {asset.particle.velocityMinX, asset.particle.velocityMinY},
            {asset.particle.velocityMaxX, asset.particle.velocityMaxY},
        },
        .lifetime = {
            Core::Duration{asset.particle.lifetimeMinSeconds},
            Core::Duration{asset.particle.lifetimeMaxSeconds},
        },
        .startSizeMeters = {
            asset.particle.startWidthMeters,
            asset.particle.startHeightMeters,
        },
        .endSizeMeters = {
            asset.particle.endWidthMeters,
            asset.particle.endHeightMeters,
        },
        .startColor = color(asset.particle.startColorRgba),
        .endColor = color(asset.particle.endColorRgba),
        .rotationRadians = asset.particle.rotationRadians,
        .sortingLayer = asset.particle.sortingLayer,
        .orderInLayer = asset.particle.orderInLayer,
    };

    auto trail = Trail2D::Create(
        {
            .segmentCapacity = asset.trail.segmentCapacity,
            .segmentLifetime = Core::Duration{asset.trail.segmentLifetimeSeconds},
            .startWidthMeters = asset.trail.startWidthMeters,
            .endWidthMeters = asset.trail.endWidthMeters,
            .sprite = resolvedSprite,
            .stableEntityKeyBase = asset.trail.stableEntityKeyBase,
            .uvRect = {asset.trail.u0, asset.trail.v0, asset.trail.u1, asset.trail.v1},
            .color = color(asset.trail.colorRgba),
            .sortingLayer = asset.trail.sortingLayer,
            .orderInLayer = asset.trail.orderInLayer,
        },
        resource);
    if (!trail) {
        return Core::failure(std::move(trail.error()));
    }
    return Fx2DInstance{
        .particles = std::move(*particles),
        .initialBurst = burst,
        .trail = std::move(*trail),
    };
}

} // namespace Tina::Scene
