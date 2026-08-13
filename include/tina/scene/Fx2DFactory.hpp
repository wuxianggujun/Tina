#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset_format/Fx2DPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/scene/ParticleSystem2D.hpp>
#include <tina/scene/Trail2D.hpp>

#include <memory_resource>

namespace Tina::Scene {

struct Fx2DInstance final {
    ParticleSystem2D particles;
    ParticleBurst2D initialBurst{};
    Trail2D trail;
};

[[nodiscard]] Core::Result<Fx2DInstance> createFx2DFromAsset(
    const AssetFormat::Fx2DPayloadDesc& asset,
    Asset::AssetHandle resolvedSprite,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::Scene
