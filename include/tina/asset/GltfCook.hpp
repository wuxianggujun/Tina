#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <string_view>

namespace Tina::Asset {

// M11-E7 minimal glTF cook (cgltf PRIVATE to the cook TU / assetc).
// Supported first slice:
// - glTF 2.0 JSON or GLB via cgltf_parse_file
// - first mesh, first primitive: TRIANGLES + POSITION(float3) + optional NORMAL/TEXCOORD_0
// - indices UNSIGNED_SHORT or converted from UNSIGNED_INT if all < 65536
// - materials: baseColorFactor → UnlitBaseColor Material; textures deferred (solid only)
// - scene nodes → Prefab with mesh/material deps for nodes that reference the first mesh
// Output is a CatalogCookRequest ready for cookCatalogPackage / publish.
//
// Not supported (structured failure): Draco, morph, skin, multi-mesh pack, external image
// textures, non-triangle primitives, sparse accessors.

struct GltfCookIds final {
    Core::AssetId meshId{};
    Core::AssetId materialId{};
    Core::AssetId prefabId{};
};

// When ids are default/empty, deterministic ids are derived from the glTF path string.
[[nodiscard]] Core::Result<CatalogCookRequest> cookGltfFileToCatalogRequest(
    std::string_view gltfUtf8Path,
    GltfCookIds ids = {}) noexcept;

} // namespace Tina::Asset
