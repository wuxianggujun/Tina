#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// 1x1 red PNG (public domain fixture).
[[nodiscard]] std::vector<unsigned char> tinyRedPngBytes()
{
    // iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==
    static constexpr unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xFC, 0xCF, 0xC0, 0x50,
        0x0F, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xA9, 0x8C, 0x21, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    return std::vector<unsigned char>(std::begin(kPng), std::end(kPng));
}

[[nodiscard]] std::string texturedTriangleGltfJson()
{
    // Same triangle as minimal, material uses baseColorTexture index 0 → image "tex.png".
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "TEXCOORD_0": 1},
      "indices": 2,
      "mode": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "metallicFactor": 0.2,
      "roughnessFactor": 0.8,
      "baseColorTexture": {"index": 0}
    }
  }],
  "textures": [{"source": 0}],
  "images": [{"uri": "tex.png"}],
  "accessors": [
    {
      "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"
    },
    {
      "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 24},
    {"buffer": 0, "byteOffset": 60, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 68,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/AACAPwAAgD8AAAAAAAAAAAAAgD8BAAAAAgAAAA=="
  }]
})json";
}

TEST(GltfCookTests, CooksBaseColorTextureToTexture2DDependency)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_tex_cook";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    {
        const auto png = tinyRedPngBytes();
        std::ofstream out(dir / "tex.png", std::ios::binary);
        ASSERT_TRUE(out.good());
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    const auto gltfPath = dir / "textured.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << texturedTriangleGltfJson();
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);

    bool sawTexture = false;
    bool materialHasTexDep = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::Texture2D)
        {
            sawTexture = true;
            auto view = AssetFormat::parseTexture2DPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
            EXPECT_EQ(view->width, 1U);
            EXPECT_EQ(view->height, 1U);
            EXPECT_EQ(view->pixelFormat, AssetFormat::Texture2DPixelFormat::Rgba8Unorm);
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            auto mat = AssetFormat::parseMaterialPayload(asset.payload);
            ASSERT_TRUE(mat.has_value()) << (mat ? "" : mat.error().message);
            EXPECT_TRUE(mat->hasBaseColorTexture);
            EXPECT_FALSE(mat->hasMetallicRoughnessTexture);
            EXPECT_FALSE(mat->hasNormalTexture);
            EXPECT_FLOAT_EQ(mat->metallicFactor, 0.2F);
            EXPECT_FLOAT_EQ(mat->roughnessFactor, 0.8F);
            ASSERT_EQ(asset.dependencies.size(), 1U);
            EXPECT_EQ(asset.dependencies[0].expectedKind, AssetFormat::AssetKind::Texture2D);
            materialHasTexDep = true;
        }
    }
    EXPECT_TRUE(sawTexture);
    EXPECT_TRUE(materialHasTexDep);

    const auto catalogRoot = dir / "catalog";
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}

TEST(GltfCookTests, CooksMetallicRoughnessAndNormalTextureDeps)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_pbr_mr_normal";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    {
        const auto png = tinyRedPngBytes();
        for (const char* name : {"base.png", "mr.png", "n.png"})
        {
            std::ofstream out(dir / name, std::ios::binary);
            ASSERT_TRUE(out.good());
            out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        }
    }
    const auto gltfPath = dir / "pbr.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "TEXCOORD_0": 1},
      "indices": 2,
      "mode": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.9, 0.1, 0.2, 1.0],
      "metallicFactor": 0.3,
      "roughnessFactor": 0.6,
      "baseColorTexture": {"index": 0},
      "metallicRoughnessTexture": {"index": 1}
    },
    "normalTexture": {"index": 2}
  }],
  "textures": [{"source": 0}, {"source": 1}, {"source": 2}],
  "images": [{"uri": "base.png"}, {"uri": "mr.png"}, {"uri": "n.png"}],
  "accessors": [
    {
      "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"
    },
    {
      "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 24},
    {"buffer": 0, "byteOffset": 60, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 68,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/AACAPwAAgD8AAAAAAAAAAAAAgD8BAAAAAgAAAA=="
  }]
})json";
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);

    std::size_t textureCount = 0;
    bool sawMaterial = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::Texture2D)
        {
            ++textureCount;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            sawMaterial = true;
            auto mat = AssetFormat::parseMaterialPayload(asset.payload);
            ASSERT_TRUE(mat.has_value()) << (mat ? "" : mat.error().message);
            EXPECT_FLOAT_EQ(mat->baseColorR, 0.9F);
            EXPECT_FLOAT_EQ(mat->metallicFactor, 0.3F);
            EXPECT_FLOAT_EQ(mat->roughnessFactor, 0.6F);
            EXPECT_TRUE(mat->hasBaseColorTexture);
            EXPECT_TRUE(mat->hasMetallicRoughnessTexture);
            EXPECT_TRUE(mat->hasNormalTexture);
            ASSERT_EQ(asset.dependencies.size(), 3U);
            EXPECT_EQ(asset.dependencies[0].expectedKind, AssetFormat::AssetKind::Texture2D);
            EXPECT_EQ(asset.dependencies[1].expectedKind, AssetFormat::AssetKind::Texture2D);
            EXPECT_EQ(asset.dependencies[2].expectedKind, AssetFormat::AssetKind::Texture2D);
            EXPECT_NE(asset.dependencies[0].assetId, asset.dependencies[1].assetId);
            EXPECT_NE(asset.dependencies[1].assetId, asset.dependencies[2].assetId);
        }
    }
    EXPECT_EQ(textureCount, 3U);
    EXPECT_TRUE(sawMaterial);

    const auto catalogRoot = dir / "catalog";
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}

TEST(GltfCookTests, RejectsExternalImagePathTraversal)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_path_escape";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "escape.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4, "material": 0}]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "textures": [{"source": 0}],
  "images": [{"uri": "../secret.png"}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("traversal"), std::string::npos)
        << request.error().message;
}

TEST(GltfCookTests, RejectsAbsoluteExternalImageUri)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_abs_uri";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "abs.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4, "material": 0}]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "textures": [{"source": 0}],
  "images": [{"uri": "file:///C:/Windows/System32/drivers/etc/hosts"}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_FALSE(request.has_value());
    EXPECT_FALSE(request.error().message.empty());
}

// One mesh, two TRIANGLES prims with distinct materials (external multi-prim models).
// Cooker SPLITs into two StaticMesh + two Material; Prefab expands to transform parent + 2 children.
[[nodiscard]] std::string multiPrimitiveMeshGltfJson()
{
    // Shared buffer: 6*float3 positions + 6*u16 indices (two independent triangles).
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [1.0, 0.0, 0.0]}],
  "meshes": [{
    "primitives": [
      {"attributes": {"POSITION": 0}, "indices": 2, "mode": 4, "material": 0},
      {"attributes": {"POSITION": 1}, "indices": 3, "mode": 4, "material": 1}
    ]
  }],
  "materials": [
    {"pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.0, 0.0, 1.0]}},
    {"pbrMetallicRoughness": {"baseColorFactor": [0.0, 1.0, 0.0, 1.0]}}
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

TEST(GltfCookTests, SplitsMultiPrimitiveMeshIntoDistinctAssetsAndPrefabChildren)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_multi_prim";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "multi_prim.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << multiPrimitiveMeshGltfJson();
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    // 2 mesh + 2 material + 1 prefab
    ASSERT_EQ(request->assets.size(), 5U);

    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::StaticMesh)
        {
            ++meshCount;
            auto view = AssetFormat::parseStaticMeshPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
            EXPECT_EQ(view->vertexCount, 3U);
            EXPECT_EQ(view->indexCount, 3U);
            EXPECT_EQ(view->submeshCount, 1U);
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            ++materialCount;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
        {
            // Parent transform + 2 prim children → 4 deps (mesh+mat × 2)
            EXPECT_EQ(asset.dependencies.size(), 4U);
            std::vector<AssetFormat::PrefabNodeView> nodes;
            auto prefab = AssetFormat::parsePrefabPayload(asset.payload, nodes);
            ASSERT_TRUE(prefab.has_value()) << (prefab ? "" : prefab.error().message);
            ASSERT_EQ(prefab->nodes.size(), 3U);
            EXPECT_FALSE(prefab->nodes[0].hasMesh);
            EXPECT_FLOAT_EQ(prefab->nodes[0].positionX, 1.0F);
            EXPECT_TRUE(prefab->nodes[1].hasMesh);
            EXPECT_TRUE(prefab->nodes[2].hasMesh);
            EXPECT_EQ(prefab->nodes[1].parentIndex, 0);
            EXPECT_EQ(prefab->nodes[2].parentIndex, 0);
        }
    }
    EXPECT_EQ(meshCount, 2U);
    EXPECT_EQ(materialCount, 2U);

    const auto catalogRoot = dir / "catalog";
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}

TEST(GltfCookTests, RejectsNonTrianglePrimitiveInMultiPrimMesh)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_multi_prim_bad";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "bad_mode.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        // mode 1 = LINES (unsupported); second prim TRIANGLES.
        out << R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [
      {"attributes": {"POSITION": 0}, "indices": 1, "mode": 1, "material": 0},
      {"attributes": {"POSITION": 0}, "indices": 1, "mode": 4, "material": 0}
    ]
  }],
  "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0]}}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("TRIANGLES"), std::string::npos)
        << request.error().message;
}

#if defined(TINA_COMPLETE_PBR_GLTF_FIXTURE)
TEST(GltfCookTests, CooksRepoCompletePbrFixture)
{
    // Product gate fixture: multi-mesh + NORMAL/UV + baseColor/MR/normal textures + distinct MR factors.
    const std::filesystem::path gltfPath{TINA_COMPLETE_PBR_GLTF_FIXTURE};
    ASSERT_TRUE(std::filesystem::exists(gltfPath)) << TINA_COMPLETE_PBR_GLTF_FIXTURE;

    auto request = cookGltfFileToCatalogRequest(gltfPath.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);

    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::size_t prefabCount = 0;
    bool sawDielectric = false;
    bool sawMetal = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::StaticMesh)
        {
            ++meshCount;
            auto mesh = AssetFormat::parseStaticMeshPayload(asset.payload);
            ASSERT_TRUE(mesh.has_value()) << (mesh ? "" : mesh.error().message);
            EXPECT_GT(mesh->vertexCount, 0U);
            EXPECT_GT(mesh->indexCount, 0U);
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            ++materialCount;
            auto mat = AssetFormat::parseMaterialPayload(asset.payload);
            ASSERT_TRUE(mat.has_value()) << (mat ? "" : mat.error().message);
            EXPECT_TRUE(mat->hasBaseColorTexture);
            EXPECT_TRUE(mat->hasMetallicRoughnessTexture);
            EXPECT_TRUE(mat->hasNormalTexture);
            ASSERT_EQ(asset.dependencies.size(), 3U);
            if (mat->metallicFactor < 0.5F)
            {
                sawDielectric = true;
                EXPECT_NEAR(mat->metallicFactor, 0.1F, 1.0e-4F);
                EXPECT_NEAR(mat->roughnessFactor, 0.7F, 1.0e-4F);
            }
            else
            {
                sawMetal = true;
                EXPECT_NEAR(mat->metallicFactor, 0.9F, 1.0e-4F);
                EXPECT_NEAR(mat->roughnessFactor, 0.2F, 1.0e-4F);
            }
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Texture2D)
        {
            ++textureCount;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
        {
            ++prefabCount;
            std::vector<AssetFormat::PrefabNodeView> nodes;
            auto prefab = AssetFormat::parsePrefabPayload(asset.payload, nodes);
            ASSERT_TRUE(prefab.has_value()) << (prefab ? "" : prefab.error().message);
            EXPECT_GE(prefab->nodes.size(), 2U);
        }
    }
    EXPECT_EQ(meshCount, 2U);
    EXPECT_EQ(materialCount, 2U);
    EXPECT_EQ(textureCount, 3U);
    EXPECT_EQ(prefabCount, 1U);
    EXPECT_TRUE(sawDielectric);
    EXPECT_TRUE(sawMetal);

    const auto catalogRoot =
        std::filesystem::temp_directory_path() / "tina_gltf_complete_pbr_catalog";
    std::error_code ec;
    std::filesystem::remove_all(catalogRoot, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}
#endif

TEST(GltfCookTests, CooksKhronosMetalRoughSpheresNoTexturesWhenPresent)
{
    // Optional large multi-mesh MR grid from Khronos Sample Models (vendored under tests/fixtures).
#if !defined(TINA_COMPLETE_PBR_GLTF_FIXTURE)
    GTEST_SKIP() << "TINA_COMPLETE_PBR_GLTF_FIXTURE not defined; cannot locate fixtures tree";
#else
    const auto path = std::filesystem::path{TINA_COMPLETE_PBR_GLTF_FIXTURE}.parent_path().parent_path() /
                      "metal_rough_spheres" / "MetalRoughSpheresNoTextures.glb";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "MetalRoughSpheresNoTextures.glb not vendored";
    }

    auto request = cookGltfFileToCatalogRequest(path.string());
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);

    std::size_t meshCount = 0;
    std::size_t materialCount = 0;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::StaticMesh)
        {
            ++meshCount;
        }
        else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            ++materialCount;
            auto mat = AssetFormat::parseMaterialPayload(asset.payload);
            ASSERT_TRUE(mat.has_value());
            EXPECT_GE(mat->metallicFactor, 0.0F);
            EXPECT_LE(mat->metallicFactor, 1.0F);
            EXPECT_GE(mat->roughnessFactor, 0.0F);
            EXPECT_LE(mat->roughnessFactor, 1.0F);
        }
    }
    EXPECT_GT(meshCount, 8U);
    EXPECT_EQ(meshCount, materialCount);

    const auto catalogRoot =
        std::filesystem::temp_directory_path() / "tina_gltf_metal_rough_spheres_catalog";
    std::error_code ec;
    std::filesystem::remove_all(catalogRoot, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
#endif
}

} // namespace
} // namespace Tina::Asset