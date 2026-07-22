#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace Tina::Asset {
namespace {

// Minimal glTF 2.0 triangle with POSITION + indices (no external buffers).
// Unit right triangle in XY.
[[nodiscard]] std::string minimalTriangleGltfJson()
{
    // buffer: 3*float3 positions + 3*u16 indices, padded
    // We'll embed via data URI base64 for portability.
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0},
      "indices": 1,
      "mode": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.2, 0.6, 0.9, 1.0]
    }
  }],
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,
      "count": 3,
      "type": "VEC3",
      "max": [1.0, 1.0, 0.0],
      "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 1,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 44,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAEAAAACAAAA"
  }]
})json";
}

TEST(GltfCookTests, CooksMinimalTriangleToMeshMaterialPrefab)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_cook_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << minimalTriangleGltfJson();
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    ASSERT_EQ(request->assets.size(), 3U);

    bool sawMesh = false;
    bool sawMaterial = false;
    bool sawPrefab = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::StaticMesh)
        {
            sawMesh = true;
            auto view = AssetFormat::parseStaticMeshPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
            EXPECT_EQ(view->vertexCount, 3U);
            EXPECT_EQ(view->indexCount, 3U);
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            sawMaterial = true;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
        {
            sawPrefab = true;
            std::vector<AssetFormat::PrefabNodeView> nodes;
            auto prefab = AssetFormat::parsePrefabPayload(asset.payload, nodes);
            ASSERT_TRUE(prefab.has_value()) << (prefab ? "" : prefab.error().message);
            EXPECT_FALSE(prefab->nodes.empty());
        }
    }
    EXPECT_TRUE(sawMesh);
    EXPECT_TRUE(sawMaterial);
    EXPECT_TRUE(sawPrefab);

    const auto catalogRoot = dir / "catalog";
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}

// Two TRIANGLES meshes, two scene roots referencing each mesh.
[[nodiscard]] std::string twoMeshGltfJson()
{
    // Mesh0: triangle at origin; Mesh1: second triangle translated in node TRS.
    // Shared buffer layout: 6*float3 positions + 6*u16 indices.
    // Positions: (0,0,0)(1,0,0)(0,1,0) and (0,0,0)(1,0,0)(0,1,0) again.
    // Base64 for 72 bytes pos + 12 bytes indices = 84 bytes padded.
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"mesh": 0},
    {"mesh": 1, "translation": [2.0, 0.0, 0.0]}
  ],
  "meshes": [
    {"primitives": [{"attributes": {"POSITION": 0}, "indices": 2, "mode": 4, "material": 0}]},
    {"primitives": [{"attributes": {"POSITION": 1}, "indices": 3, "mode": 4, "material": 1}]}
  ],
  "materials": [
    {"pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.2, 0.2, 1.0]}},
    {"pbrMetallicRoughness": {"baseColorFactor": [0.2, 1.0, 0.2, 1.0]}}
  ],
  "accessors": [
    {
      "bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 0, "byteOffset": 36, "componentType": 5126, "count": 3, "type": "VEC3",
      "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]
    },
    {"bufferView": 1, "byteOffset": 0, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 1, "byteOffset": 6, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 72},
    {"buffer": 0, "byteOffset": 72, "byteLength": 12}
  ],
  "buffers": [{
    "byteLength": 84,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAEAAAACAAAAAAAAAAEAAAACAAAA"
  }]
})json";
}

TEST(GltfCookTests, CooksMultipleMeshesToDistinctAssets)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_multi_mesh";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "two_mesh.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << twoMeshGltfJson();
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    // 2 mesh + 2 material + 1 prefab
    ASSERT_EQ(request->assets.size(), 5U);

    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    std::size_t prefabDeps = 0;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::StaticMesh)
        {
            ++meshCount;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            ++materialCount;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
        {
            prefabDeps = asset.dependencies.size();
            std::vector<AssetFormat::PrefabNodeView> nodes;
            auto prefab = AssetFormat::parsePrefabPayload(asset.payload, nodes);
            ASSERT_TRUE(prefab.has_value()) << (prefab ? "" : prefab.error().message);
            ASSERT_EQ(prefab->nodes.size(), 2U);
            EXPECT_TRUE(prefab->nodes[0].hasMesh);
            EXPECT_TRUE(prefab->nodes[1].hasMesh);
            EXPECT_FLOAT_EQ(prefab->nodes[1].positionX, 2.0F);
        }
    }
    EXPECT_EQ(meshCount, 2U);
    EXPECT_EQ(materialCount, 2U);
    EXPECT_EQ(prefabDeps, 4U);

    const auto catalogRoot = dir / "catalog";
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}

} // namespace
} // namespace Tina::Asset
