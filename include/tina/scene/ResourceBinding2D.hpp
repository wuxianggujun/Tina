#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/id/AssetId.hpp>

namespace Tina::Scene {

// Which authored node kind this binding came from. Scene does not instantiate any
// of these resources; the kind is preserved so capture can reproduce the authored
// node kind, which the wire format encodes in nodeKind rather than the payload.
enum class ResourceBindingKind2D : u8 {
    TileMap = 0,
    FxEmitter = 1,
    NavigationRegion = 2,
    AudioPlayer = 3,
};

// One authored resource reference on an entity. Scene deliberately does not
// interpret the AssetId: instantiating a TileMap, Fx2D, NavigationGrid2D or
// AudioClip belongs to the game or the Asset layer. Carrying it here is what
// makes a saved scene survive a reload without losing the reference.
struct ResourceBinding2D final {
    Core::AssetId assetId{};
    ResourceBindingKind2D kind = ResourceBindingKind2D::TileMap;
    bool active = true;

    friend constexpr bool operator==(const ResourceBinding2D&, const ResourceBinding2D&) noexcept = default;
};

[[nodiscard]] constexpr bool isValidResourceBindingKind2D(ResourceBindingKind2D kind) noexcept
{
    return kind == ResourceBindingKind2D::TileMap || kind == ResourceBindingKind2D::FxEmitter ||
           kind == ResourceBindingKind2D::NavigationRegion ||
           kind == ResourceBindingKind2D::AudioPlayer;
}

[[nodiscard]] constexpr bool isValid(const ResourceBinding2D& binding) noexcept
{
    return static_cast<bool>(binding.assetId) && isValidResourceBindingKind2D(binding.kind);
}

} // namespace Tina::Scene
