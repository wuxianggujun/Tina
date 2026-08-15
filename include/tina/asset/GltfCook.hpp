#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <string_view>

namespace Tina::Asset {

// M11-E7/E10 glTF cook (cgltf PRIVATE to the cook TU / assetc).
// Supported:
// - strict UTF-8 glTF 2.0 JSON or GLB path, read from one bounded opened-file snapshot
// - relative external buffers/images, percent-decoded as UTF-8 and read from opened-file
//   snapshots only after their final paths are contained by the glTF authoring root
// - every primitive: TRIANGLES with POSITION(float3) + optional NORMAL/TEXCOORD_0
// - multi-mesh files produce StaticMesh + Material assets (multi-prim SPLIT below)
// - multi-primitive meshes: SPLIT (not merge) — one StaticMesh + Material per prim;
//   Prefab expands the referencing node into a transform parent + identity child nodes
//   (fits Prefab 1 mesh/1 material per node and preserves per-prim materials)
// - pbrMetallicRoughness: baseColorFactor, metallicFactor, roughnessFactor,
//   baseColorTexture / metallicRoughnessTexture (PNG/JPEG → Texture2D deps)
// - material alphaMode OPAQUE or BLEND; MASK and unknown values fail explicitly
// - optional normalTexture → Texture2D dependency (cooked data; GPU PBR separate)
// - scene nodes store mesh/material AssetIds in Prefab payload nodes; Catalog deps are canonical refs
// Output is a CatalogCookRequest ready for cookCatalogPackage / publish.
//
// Not supported (structured failure): Draco, morph, multi-submesh merge,
// data-URI images without bufferView, non-triangle primitives, sparse accessors.

struct GltfCookIds final {
    // Optional fixed ids for single-mesh files (or mesh index 0 / its material / prefab).
    // Multi-mesh files always derive additional mesh/material ids from path + index.
    Core::AssetId meshId{};
    Core::AssetId materialId{};
    Core::AssetId prefabId{};
};

// targetPlatform is explicit because the same source may be cooked for Windows or Linux.
// When ids are default/empty, deterministic ids are derived from the glTF path string.
[[nodiscard]] Core::Result<CatalogCookRequest> cookGltfFileToCatalogRequest(
    std::string_view gltfUtf8Path,
    AssetFormat::TargetPlatform targetPlatform,
    GltfCookIds ids = {}) noexcept;

// Captures the exact source bytes consumed by the cook under an explicit authoring root. One
// document produces one glTF import unit; embedded buffers/images remain part of the primary source.
[[nodiscard]] Core::Result<CatalogCookSourceResult> cookGltfFileToCatalogSourceResult(
    std::string_view gltfUtf8Path,
    AssetFormat::TargetPlatform targetPlatform,
    SourceImportCaptureConfig captureConfig,
    GltfCookIds ids = {}) noexcept;

} // namespace Tina::Asset
