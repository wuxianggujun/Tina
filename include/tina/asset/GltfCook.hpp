#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <string_view>

namespace Tina::Asset {

// M11-E7/E10 glTF cook (cgltf PRIVATE to the cook TU / assetc).
// Supported:
// - glTF 2.0 JSON or GLB via cgltf_parse_file
// - every mesh: single TRIANGLES primitive with POSITION(float3) + optional NORMAL/TEXCOORD_0
// - multi-mesh files produce one StaticMesh + one Material per mesh index
// - baseColorTexture (PNG/JPEG via stb_image) → Texture2D cook + Material dep (M11-E11)
// - scene nodes → Prefab deps bind each node's mesh/material AssetIds
// Output is a CatalogCookRequest ready for cookCatalogPackage / publish.
//
// Not supported (structured failure): Draco, morph, skin, multi-primitive mesh merge,
// data-URI images without bufferView, non-triangle primitives, sparse accessors.

struct GltfCookIds final {
    // Optional fixed ids for single-mesh files (or mesh index 0 / its material / prefab).
    // Multi-mesh files always derive additional mesh/material ids from path + index.
    Core::AssetId meshId{};
    Core::AssetId materialId{};
    Core::AssetId prefabId{};
};

// When ids are default/empty, deterministic ids are derived from the glTF path string.
[[nodiscard]] Core::Result<CatalogCookRequest> cookGltfFileToCatalogRequest(
    std::string_view gltfUtf8Path,
    GltfCookIds ids = {}) noexcept;

} // namespace Tina::Asset
