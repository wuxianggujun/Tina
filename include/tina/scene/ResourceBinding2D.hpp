#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/id/AssetId.hpp>

namespace Tina::Scene {

// Which authored node kind this binding came from. TileMap/Fx/Navigation/Audio
// stay bindings only. PrefabInstance is expanded by instantiateWorld2DSnapshot
// into runtime children; capture skips those children. The kind is preserved so
// capture can reproduce the authored node kind, which the wire format encodes in
// nodeKind rather than the payload.
enum class ResourceBindingKind2D : u8 {
    TileMap = 0,
    FxEmitter = 1,
    NavigationRegion = 2,
    AudioPlayer = 3,
    PrefabInstance = 4,
};

// One authored resource reference on an entity. TileMap, Fx2D, NavigationGrid2D
// and AudioClip AssetIds stay uninterpreted here. Prefab2D AssetIds are resolved
// during World2D instantiate so Play and Editor preview share expansion. Carrying
// the binding is what makes a saved scene survive a reload without losing the
// reference.
struct ResourceBinding2D final {
    Core::AssetId assetId{};
    ResourceBindingKind2D kind = ResourceBindingKind2D::TileMap;
    bool active = true;
    // AudioPlayer only. 0 = Once, 1 = Loop. Other kinds must keep 0.
    Core::u8 audioLoopMode = 0;

    friend constexpr bool operator==(const ResourceBinding2D&, const ResourceBinding2D&) noexcept = default;
};

[[nodiscard]] constexpr bool isValidResourceBindingKind2D(ResourceBindingKind2D kind) noexcept
{
    return kind == ResourceBindingKind2D::TileMap || kind == ResourceBindingKind2D::FxEmitter ||
           kind == ResourceBindingKind2D::NavigationRegion ||
           kind == ResourceBindingKind2D::AudioPlayer ||
           kind == ResourceBindingKind2D::PrefabInstance;
}

[[nodiscard]] constexpr bool isValid(const ResourceBinding2D& binding) noexcept
{
    if (!binding.assetId || !isValidResourceBindingKind2D(binding.kind) || binding.audioLoopMode > 1U)
    {
        return false;
    }
    return binding.kind == ResourceBindingKind2D::AudioPlayer || binding.audioLoopMode == 0U;
}

} // namespace Tina::Scene
