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

} // namespace
} // namespace Tina::Asset
