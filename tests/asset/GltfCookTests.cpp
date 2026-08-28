#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/GltfCook.hpp>
// For the cross-importer identity check: glTF and media outputs share role-tag
// values, so their ids have to be compared against each other somewhere.
#include <tina/asset/MediaCook.hpp>
#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>

struct MountPointReparseData final {
    DWORD reparseTag = 0;
    WORD reparseDataLength = 0;
    WORD reserved = 0;
    WORD substituteNameOffset = 0;
    WORD substituteNameLength = 0;
    WORD printNameOffset = 0;
    WORD printNameLength = 0;
    WCHAR pathBuffer[1]{};
};
#endif

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string skinnedTriangleGltfJson(std::string_view interpolation = "LINEAR");
[[nodiscard]] std::vector<unsigned char> skinnedTriangleBufferBytes(bool invalidWeight = false);

// Minimal glTF 2.0 triangle with required POSITION/NORMAL/TEXCOORD_0 + indices.
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
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
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
      "componentType": 5126,
      "count": 3,
      "type": "VEC3"
    },
    {
      "bufferView": 2,
      "componentType": 5126,
      "count": 3,
      "type": "VEC2"
    },
    {
      "bufferView": 3,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA="
  }]
})json";
}

[[nodiscard]] std::string externalImageTriangleGltfJson(std::string_view imageUri)
{
    return std::string{R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
      "mode": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorTexture": {"index": 0}
    }
  }],
  "textures": [{"source": 0}],
  "images": [{"uri": ")json"} +
           std::string{imageUri} + R"json("}],
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
      "componentType": 5126,
      "count": 3,
      "type": "VEC3"
    },
    {
      "bufferView": 2,
      "componentType": 5126,
      "count": 3,
      "type": "VEC2"
    },
    {
      "bufferView": 3,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA="
  }]
})json";
}

[[nodiscard]] std::string externalBufferTriangleGltfJson(std::string_view uri)
{
    return std::string{R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{"byteLength": 104, "uri": ")json"} +
           std::string{uri} + R"json("}]
})json";
}

[[nodiscard]] std::array<unsigned char, 104> externalTriangleBufferBytes()
{
    std::array<unsigned char, 104> bytes{};
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<float, 9> normals{0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::array<float, 6> texcoords{0, 0, 1, 0, 0, 1};
    std::memcpy(bytes.data(), positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + 36U, normals.data(), sizeof(normals));
    std::memcpy(bytes.data() + 72U, texcoords.data(), sizeof(texcoords));
    bytes[98] = 1;
    bytes[100] = 2;
    return bytes;
}

[[nodiscard]] std::vector<unsigned char> tangentTriangleBufferBytes(bool includeAuthoredTangents)
{
    const std::size_t indexOffset = includeAuthoredTangents ? 144U : 96U;
    std::vector<unsigned char> bytes(includeAuthoredTangents ? 152U : 104U, 0U);
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<float, 9> normals{0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::array<float, 6> texcoords{0, 0, 1, 0, 0, 1};
    std::memcpy(bytes.data() + 0U, positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + 36U, normals.data(), sizeof(normals));
    std::memcpy(bytes.data() + 72U, texcoords.data(), sizeof(texcoords));
    if (includeAuthoredTangents)
    {
        const std::array<float, 12> tangents{0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1};
        std::memcpy(bytes.data() + 96U, tangents.data(), sizeof(tangents));
    }
    bytes[indexOffset + 2U] = 1U;
    bytes[indexOffset + 4U] = 2U;
    return bytes;
}

[[nodiscard]] std::string tangentTriangleGltfJson(bool includeAuthoredTangents)
{
    if (includeAuthoredTangents)
    {
        return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3},
    "indices": 4, "mode": 4
  }]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1, 1, 0], "min": [0, 0, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 48},
    {"buffer": 0, "byteOffset": 144, "byteLength": 6}
  ],
  "buffers": [{"byteLength": 152, "uri": "geometry.bin"}]
})json";
    }
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "indices": 3, "mode": 4
  }]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1, 1, 0], "min": [0, 0, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{"byteLength": 104, "uri": "geometry.bin"}]
})json";
}

[[nodiscard]] std::vector<unsigned char> tangentDiscontinuityBufferBytes()
{
    std::vector<unsigned char> bytes(172U, 0U);
    const std::array<float, 15> positions{
        0, 0, 0, 1, 0, 0, 0, 1, 0, -1, 0, 0, 0, 1, 0,
    };
    const std::array<float, 15> normals{
        0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
    };
    const std::array<float, 10> texcoords{
        0, 0, 1, 0, 0, 1, 1, 0, 0, 1,
    };
    const std::array<Core::u16, 6> indices{0, 1, 2, 0, 4, 3};
    std::memcpy(bytes.data() + 0U, positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + 60U, normals.data(), sizeof(normals));
    std::memcpy(bytes.data() + 120U, texcoords.data(), sizeof(texcoords));
    std::memcpy(bytes.data() + 160U, indices.data(), sizeof(indices));
    return bytes;
}

[[nodiscard]] std::string tangentDiscontinuityGltfJson()
{
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "indices": 3, "mode": 4
  }]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 5, "type": "VEC3",
     "max": [1, 1, 0], "min": [-1, 0, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 5, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 5, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 60},
    {"buffer": 0, "byteOffset": 60, "byteLength": 60},
    {"buffer": 0, "byteOffset": 120, "byteLength": 40},
    {"buffer": 0, "byteOffset": 160, "byteLength": 12}
  ],
  "buffers": [{"byteLength": 172, "uri": "geometry.bin"}]
})json";
}

void writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.good());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    ASSERT_TRUE(output.good());
}

template <typename Container> void writeBinaryFile(const std::filesystem::path& path, const Container& bytes)
{
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

[[nodiscard]] bool createDirectoryLink(const std::filesystem::path& target, const std::filesystem::path& link,
                                       std::error_code& error)
{
#if defined(_WIN32)
    std::filesystem::create_directory(link, error);
    if (error)
    {
        return false;
    }

    const std::wstring printName = std::filesystem::absolute(target, error).native();
    if (error)
    {
        return false;
    }
    const std::wstring substituteName = L"\\??\\" + printName;
    const std::size_t substituteBytes = substituteName.size() * sizeof(wchar_t);
    const std::size_t printBytes = printName.size() * sizeof(wchar_t);
    constexpr std::size_t reparseHeaderBytes = offsetof(MountPointReparseData, substituteNameOffset);
    constexpr std::size_t pathBufferOffset = offsetof(MountPointReparseData, pathBuffer);
    const std::size_t pathBufferBytes = substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
    if (pathBufferOffset + pathBufferBytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE ||
        pathBufferOffset - reparseHeaderBytes + pathBufferBytes > (std::numeric_limits<USHORT>::max)())
    {
        error = std::make_error_code(std::errc::filename_too_long);
        return false;
    }

    std::vector<unsigned char> storage(pathBufferOffset + pathBufferBytes, 0);
    auto* reparse = reinterpret_cast<MountPointReparseData*>(storage.data());
    reparse->reparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    reparse->reparseDataLength = static_cast<USHORT>(pathBufferOffset - reparseHeaderBytes + pathBufferBytes);
    reparse->substituteNameOffset = 0;
    reparse->substituteNameLength = static_cast<USHORT>(substituteBytes);
    reparse->printNameOffset = static_cast<USHORT>(substituteBytes + sizeof(wchar_t));
    reparse->printNameLength = static_cast<USHORT>(printBytes);
    std::memcpy(reparse->pathBuffer, substituteName.data(), substituteBytes);
    std::memcpy(reinterpret_cast<unsigned char*>(reparse->pathBuffer) + reparse->printNameOffset, printName.data(),
                printBytes);

    const HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
    {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    DWORD returned = 0;
    const BOOL result = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, reparse,
                                        static_cast<DWORD>(reparseHeaderBytes + reparse->reparseDataLength), nullptr, 0,
                                        &returned, nullptr);
    const DWORD nativeError = result ? ERROR_SUCCESS : GetLastError();
    CloseHandle(directory);
    if (!result)
    {
        error = std::error_code(static_cast<int>(nativeError), std::system_category());
        return false;
    }
    error.clear();
    return true;
#else
    std::filesystem::create_directory_symlink(target, link, error);
    return !error;
#endif
}

void writeBigEndianU32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value)
{
    ASSERT_LE(offset + 4U, bytes.size());
    bytes[offset + 0U] = static_cast<unsigned char>((value >> 24U) & 0xFFU);
    bytes[offset + 1U] = static_cast<unsigned char>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<unsigned char>(value & 0xFFU);
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    EXPECT_EQ(request->targetPlatform, AssetFormat::TargetPlatform::LinuxX64);
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
            EXPECT_EQ(view->vertices.size(), 3U * AssetFormat::StaticMeshWire::FloatsPerVertex);
        } else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            sawMaterial = true;
            auto view = AssetFormat::parseMaterialPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
            EXPECT_EQ(view->alphaMode, AssetFormat::MaterialAlphaMode::Opaque);
        } else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
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

TEST(GltfCookTests, VersionedDefaultIdsUseCanonicalLocatorAndAvoidLegacyCollisions)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_gltf_versioned_ids";
    const auto movedRoot = std::filesystem::temp_directory_path() / "tina_gltf_versioned_ids_moved";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
    std::filesystem::remove_all(movedRoot, errorCode);
    std::filesystem::create_directories(root, errorCode);
    ASSERT_FALSE(errorCode) << errorCode.message();
    std::filesystem::create_directories(movedRoot, errorCode);
    ASSERT_FALSE(errorCode) << errorCode.message();

    writeTextFile(root / "scene.gltf", minimalTriangleGltfJson());
    writeTextFile(movedRoot / "scene.gltf", minimalTriangleGltfJson());
    writeTextFile(root / "ab.gltf", minimalTriangleGltfJson());
    writeTextFile(root / "ba.gltf", minimalTriangleGltfJson());

    const auto cookWithRoot = [](const std::filesystem::path& path,
                                 const std::filesystem::path& sourceRoot) {
        return cookGltfFileToCatalogSourceResult(
            path.string(), AssetFormat::TargetPlatform::WindowsX64,
            SourceImportCaptureConfig{.sourceRootUtf8 = sourceRoot.string()});
    };

    auto original = cookWithRoot(root / "scene.gltf", root);
    auto moved = cookWithRoot(movedRoot / "scene.gltf", movedRoot);
    ASSERT_TRUE(original) << original.error().message;
    ASSERT_TRUE(moved) << moved.error().message;
    ASSERT_EQ(original->request.assets.size(), moved->request.assets.size());
    for (std::size_t index = 0U; index < original->request.assets.size(); ++index)
    {
        EXPECT_EQ(original->request.assets[index].assetKind,
                  moved->request.assets[index].assetKind);
        EXPECT_EQ(original->request.assets[index].assetId,
                  moved->request.assets[index].assetId)
            << "output index " << index;
    }
    ASSERT_EQ(original->sourceImports.units.size(), 1U);
    EXPECT_EQ(original->sourceImports.units.front().importerVersion, 2U);

    auto transposedLeft = cookWithRoot(root / "ab.gltf", root);
    auto transposedRight = cookWithRoot(root / "ba.gltf", root);
    ASSERT_TRUE(transposedLeft) << transposedLeft.error().message;
    ASSERT_TRUE(transposedRight) << transposedRight.error().message;
    ASSERT_EQ(transposedLeft->request.assets.size(), transposedRight->request.assets.size());
    for (std::size_t index = 0U; index < transposedLeft->request.assets.size(); ++index)
    {
        EXPECT_NE(transposedLeft->request.assets[index].assetId,
                  transposedRight->request.assets[index].assetId)
            << "legacy path/XOR collision at output index " << index;
    }

    std::filesystem::remove_all(root, errorCode);
    std::filesystem::remove_all(movedRoot, errorCode);
}

// Cross-importer corpus for a mesh-only document: its mesh, material and prefab
// outputs must not collide with what the media cooker derives for the same or
// neighbouring locators, and must be distinct among themselves.
//
// Scope note: this fixture emits no texture or animation, so it does not exercise the
// two reused role-tag values (0x75 metallic-roughness/imported-texture, 0x77
// animation/imported-audio). That pair is checked in
// CooksMetallicRoughnessAndNormalTextureDeps, which actually produces the sharing
// output. Removing AssetKind and channel from the derivation does not fail this test,
// which is exactly why the claim is stated narrowly here rather than implied.
TEST(GltfCookTests, MeshOnlyGltfOutputsStayDistinctFromMediaIds)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_gltf_media_identity";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
    std::filesystem::create_directories(root, errorCode);
    ASSERT_FALSE(errorCode) << errorCode.message();

    // The glTF document and the imported media share a stem, which is what makes the
    // canonical locators overlap.
    writeTextFile(root / "shared.gltf", minimalTriangleGltfJson());

    auto cooked = cookGltfFileToCatalogSourceResult(
        (root / "shared.gltf").string(), AssetFormat::TargetPlatform::WindowsX64,
        SourceImportCaptureConfig{.sourceRootUtf8 = root.string()});
    ASSERT_TRUE(cooked) << cooked.error().message;
    ASSERT_FALSE(cooked->request.assets.empty());

    // Media ids for every locator a project could plausibly hold alongside the
    // document, *including the document's own locator*. That last one is the case
    // that matters: it is the only way the two importers can present byte-identical
    // input to the shared derivation, so it is where the reused role tags would
    // actually collide. The others are the realistic sibling-file spellings.
    std::vector<Core::AssetId> mediaIds;
    for (const std::string_view locator : {"shared.gltf", "shared", "shared.png", "shared.wav"})
    {
        const auto textureId = deriveTextureMediaAssetId(locator);
        const auto audioId = deriveAudioMediaAssetId(locator);
        ASSERT_TRUE(textureId) << locator << ": " << textureId.error().message;
        ASSERT_TRUE(audioId) << locator << ": " << audioId.error().message;
        mediaIds.push_back(*textureId);
        mediaIds.push_back(*audioId);
    }
    // Distinctness among the media ids themselves, so a duplicate below cannot be
    // masked by two media locators having already collapsed onto one id.
    for (std::size_t index = 0U; index < mediaIds.size(); ++index)
    {
        for (std::size_t other = index + 1U; other < mediaIds.size(); ++other)
        {
            EXPECT_NE(mediaIds[index], mediaIds[other])
                << "two media locators collapsed onto one id (" << index << " vs " << other << ")";
        }
    }

    for (const auto& asset : cooked->request.assets)
    {
        EXPECT_EQ(std::find(mediaIds.begin(), mediaIds.end(), asset.assetId), mediaIds.end())
            << "a glTF output collided with a media id derived from the same locator; "
               "the shared role-tag values (0x75, 0x77) are separated only by the "
               "remaining hash inputs";
    }

    // Every glTF output within one document must also be distinct from the others,
    // which is what the ordinal and channel inputs exist for.
    std::vector<Core::AssetId> seen;
    for (const auto& asset : cooked->request.assets)
    {
        EXPECT_EQ(std::find(seen.begin(), seen.end(), asset.assetId), seen.end())
            << "duplicate AssetId among one document's outputs";
        seen.push_back(asset.assetId);
    }

    std::filesystem::remove_all(root, errorCode);
}

TEST(GltfCookTests, MapsBlendAlphaModeAndRejectsUnsupportedModes)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_alpha_mode";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";

    const auto writeWithAlphaMode = [&](std::string_view alphaMode) {
        std::string json = minimalTriangleGltfJson();
        constexpr std::string_view materialMember = "\"pbrMetallicRoughness\"";
        const std::size_t offset = json.find(materialMember);
        if (offset == std::string::npos)
        {
            return false;
        }
        json.insert(offset, "\"alphaMode\": \"" + std::string{alphaMode} + "\",\n    ");
        writeTextFile(gltfPath, json);
        return true;
    };

    ASSERT_TRUE(writeWithAlphaMode("BLEND"));
    auto blendRequest = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(blendRequest.has_value()) << (blendRequest ? "" : blendRequest.error().message);
    const auto material =
        std::find_if(blendRequest->assets.begin(), blendRequest->assets.end(), [](const CatalogCookAssetSpec& asset) {
            return asset.assetKind == AssetFormat::AssetKind::Material;
        });
    ASSERT_NE(material, blendRequest->assets.end());
    auto materialView = AssetFormat::parseMaterialPayload(material->payload);
    ASSERT_TRUE(materialView.has_value()) << (materialView ? "" : materialView.error().message);
    EXPECT_EQ(materialView->alphaMode, AssetFormat::MaterialAlphaMode::Blend);

    for (const std::string_view unsupported : {std::string_view{"MASK"}, std::string_view{"UNKNOWN"}})
    {
        SCOPED_TRACE(unsupported);
        ASSERT_TRUE(writeWithAlphaMode(unsupported));
        auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
        ASSERT_FALSE(request.has_value());
        if (unsupported == "MASK")
        {
            EXPECT_NE(request.error().message.find("MASK"), std::string::npos) << request.error().message;
        } else
        {
            EXPECT_NE(request.error().message.find("alphaMode"), std::string::npos) << request.error().message;
        }
    }

    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, CooksSkinAndAnimationClip3D)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skin_animation";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";
    writeTextFile(gltfPath, skinnedTriangleGltfJson());
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    bool sawSkinned = false;
    bool sawAnimation = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::SkinnedMesh)
        {
            sawSkinned = true;
            auto view = AssetFormat::parseSkinnedMeshPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << view.error().message;
            EXPECT_EQ(view->jointCount, 1U);
            EXPECT_EQ(view->jointWeights[0], AssetFormat::SkinnedMeshWire::WeightScale);
        }
        if (asset.assetKind == AssetFormat::AssetKind::AnimationClip3D)
        {
            sawAnimation = true;
            auto view = AssetFormat::parseAnimationClip3DPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << view.error().message;
            EXPECT_EQ(view->jointCount, 1U);
            EXPECT_FLOAT_EQ(view->durationSeconds, 1.0F);
        }
    }
    EXPECT_TRUE(sawSkinned);
    EXPECT_TRUE(sawAnimation);
    const auto catalogRoot = dir / "catalog";
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, CanonicalizesSkinInfluencesAfterWeightQuantization)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skin_quantized_order";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";

    std::string json = skinnedTriangleGltfJson();
    const std::string authoredNodes = "\"nodes\":[{\"children\":[1]},{\"mesh\":0,\"skin\":0}]";
    const std::string authoredSkin = "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":7}]";
    ASSERT_NE(json.find(authoredNodes), std::string::npos);
    ASSERT_NE(json.find(authoredSkin), std::string::npos);
    json.replace(json.find(authoredNodes), authoredNodes.size(),
                 "\"nodes\":[{\"children\":[1,2,3,4]},{},{},{},{\"mesh\":0,\"skin\":0}]");
    json.replace(json.find(authoredSkin), authoredSkin.size(), "\"skins\":[{\"joints\":[0,1,2,3]}]");
    writeTextFile(gltfPath, json);

    auto bytes = skinnedTriangleBufferBytes();
    const std::array<Core::u16, 12> joints{3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0};
    const std::array<float, 12> weights{
        32767.1F, 32766.9F, 0.5F, 0.5F, 32767.1F, 32766.9F, 0.5F, 0.5F, 32767.1F, 32766.9F, 0.5F, 0.5F,
    };
    std::memcpy(bytes.data() + 144, joints.data(), sizeof(joints));
    std::memcpy(bytes.data() + 168, weights.data(), sizeof(weights));
    writeBinaryFile(dir / "geometry.bin", bytes);

    const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind != AssetFormat::AssetKind::SkinnedMesh)
        {
            continue;
        }
        const auto view = AssetFormat::parseSkinnedMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        ASSERT_GE(view->jointIndices.size(), 4U);
        ASSERT_GE(view->jointWeights.size(), 4U);
        EXPECT_EQ(view->jointIndices[0], 2U);
        EXPECT_EQ(view->jointWeights[0], 32767U);
        EXPECT_EQ(view->jointIndices[1], 3U);
        EXPECT_EQ(view->jointWeights[1], 32767U);
        EXPECT_EQ(view->jointIndices[2], 0U);
        EXPECT_EQ(view->jointWeights[2], 1U);
        EXPECT_EQ(view->jointIndices[3], 0U);
        EXPECT_EQ(view->jointWeights[3], 0U);
        std::filesystem::remove_all(dir, ec);
        return;
    }
    FAIL() << "cooked request did not contain a SkinnedMesh";
}

TEST(GltfCookTests, MergesDuplicateSkinInfluencesBeforeQuantization)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skin_duplicate_influences";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";

    std::string json = skinnedTriangleGltfJson();
    const std::string authoredNodes = "\"nodes\":[{\"children\":[1]},{\"mesh\":0,\"skin\":0}]";
    const std::string authoredSkin = "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":7}]";
    ASSERT_NE(json.find(authoredNodes), std::string::npos);
    ASSERT_NE(json.find(authoredSkin), std::string::npos);
    json.replace(json.find(authoredNodes), authoredNodes.size(),
                 "\"nodes\":[{\"children\":[1,2]},{},{\"mesh\":0,\"skin\":0}]");
    json.replace(json.find(authoredSkin), authoredSkin.size(), "\"skins\":[{\"joints\":[0,1,2]}]");
    writeTextFile(gltfPath, json);

    auto bytes = skinnedTriangleBufferBytes();
    const std::array<Core::u16, 12> joints{
        1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0,
    };
    const std::array<float, 12> weights{
        0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F,
    };
    std::memcpy(bytes.data() + 144, joints.data(), sizeof(joints));
    std::memcpy(bytes.data() + 168, weights.data(), sizeof(weights));
    writeBinaryFile(dir / "geometry.bin", bytes);

    const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind != AssetFormat::AssetKind::SkinnedMesh)
        {
            continue;
        }
        const auto view = AssetFormat::parseSkinnedMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        ASSERT_GE(view->jointIndices.size(), 4U);
        EXPECT_EQ(view->jointIndices[0], 1U);
        EXPECT_EQ(view->jointWeights[0], 32767U);
        EXPECT_EQ(view->jointIndices[1], 0U);
        EXPECT_EQ(view->jointWeights[1], 16384U);
        EXPECT_EQ(view->jointIndices[2], 2U);
        EXPECT_EQ(view->jointWeights[2], 16384U);
        EXPECT_EQ(view->jointIndices[3], 0U);
        EXPECT_EQ(view->jointWeights[3], 0U);
        std::filesystem::remove_all(dir, ec);
        return;
    }
    FAIL() << "cooked request did not contain a SkinnedMesh";
}

TEST(GltfCookTests, RejectsMalformedSkinWeightAndCubicAnimation)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skin_malformed";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";
    writeTextFile(gltfPath, skinnedTriangleGltfJson());
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes(true));
    auto badWeight = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_FALSE(badWeight.has_value());

    writeTextFile(gltfPath, skinnedTriangleGltfJson("CUBICSPLINE"));
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());
    auto cubic = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_FALSE(cubic.has_value());
    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, RejectsUnsupportedOrUnboundSkinAttributeSets)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skin_attribute_contract";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());

    const auto rejectMutation = [&](std::string_view from, std::string_view to, std::string_view expectedError) {
        std::string json = skinnedTriangleGltfJson();
        const std::size_t offset = json.find(from);
        ASSERT_NE(offset, std::string::npos);
        json.replace(offset, from.size(), to);
        writeTextFile(gltfPath, json);

        const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
        ASSERT_FALSE(request.has_value());
        EXPECT_NE(request.error().message.find(expectedError), std::string::npos) << request.error().message;
    };

    rejectMutation("\"JOINTS_0\":4,\"WEIGHTS_0\":5", "\"JOINTS_1\":4,\"WEIGHTS_1\":5", "only JOINTS_0/WEIGHTS_0");
    rejectMutation("\"JOINTS_0\":4,\"WEIGHTS_0\":5", "\"JOINTS_0\":4", "must be provided together");
    rejectMutation("{\"mesh\":0,\"skin\":0}", "{\"mesh\":0}", "must be bound to node.skin");
    rejectMutation(",\"JOINTS_0\":4,\"WEIGHTS_0\":5", "", "bound to node.skin requires");

    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, RejectsAnimationTargetOutsideSkin)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_animation_target_outside_skin";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";
    std::string json = skinnedTriangleGltfJson();
    const std::string target = "\"target\":{\"node\":0";
    const std::size_t targetOffset = json.find(target);
    ASSERT_NE(targetOffset, std::string::npos);
    json.replace(targetOffset, target.size(), "\"target\":{\"node\":1");
    writeTextFile(gltfPath, json);
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());
    const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    EXPECT_FALSE(request.has_value());
    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, RejectsAnimationTargetSharedByMultipleSkins)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_animation_ambiguous_skin";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";
    std::string json = skinnedTriangleGltfJson();
    const std::string authoredSkin = "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":7}]";
    const std::size_t authoredSkinOffset = json.find(authoredSkin);
    ASSERT_NE(authoredSkinOffset, std::string::npos);
    json.replace(authoredSkinOffset, authoredSkin.size(),
                 "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":7},{\"joints\":[0]}]");
    writeTextFile(gltfPath, json);
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());

    const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    EXPECT_FALSE(request.has_value());
    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, DefaultsMissingInverseBindMatricesToIdentity)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_default_inverse_bind";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";
    std::string json = skinnedTriangleGltfJson();
    const std::string authored = ",\"inverseBindMatrices\":7";
    const std::size_t authoredOffset = json.find(authored);
    ASSERT_NE(authoredOffset, std::string::npos);
    json.erase(authoredOffset, authored.size());
    writeTextFile(gltfPath, json);
    writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());

    const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind != AssetFormat::AssetKind::SkinnedMesh)
        {
            continue;
        }
        const auto view = AssetFormat::parseSkinnedMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        const auto matrix = view->inverseBindMatrix(0);
        ASSERT_EQ(matrix.size(), 16U);
        EXPECT_FLOAT_EQ(matrix[0], 1.0F);
        EXPECT_FLOAT_EQ(matrix[5], 1.0F);
        EXPECT_FLOAT_EQ(matrix[10], 1.0F);
        EXPECT_FLOAT_EQ(matrix[15], 1.0F);
        std::filesystem::remove_all(dir, ec);
        return;
    }
    FAIL() << "cooked request did not contain a SkinnedMesh";
}

TEST(GltfCookTests, RejectsFrozenSkinAndAnimationCapacityLimits)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skin_animation_limits";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "skinned.gltf";

    {
        std::string json = skinnedTriangleGltfJson();
        std::string nodes = "\"nodes\":[";
        std::string joints = "\"skins\":[{\"joints\":[";
        for (Core::u32 index = 0; index <= AssetFormat::SkinnedMeshWire::MaxJointCount; ++index)
        {
            if (index != 0U)
            {
                nodes += ',';
                joints += ',';
            }
            nodes += "{}";
            joints += std::to_string(index);
        }
        nodes += ",{\"mesh\":0,\"skin\":0}]";
        joints += "]}]";
        const std::string oldNodes = "\"nodes\":[{\"children\":[1]},{\"mesh\":0,\"skin\":0}]";
        const std::string oldSkin = "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":7}]";
        ASSERT_NE(json.find(oldNodes), std::string::npos);
        ASSERT_NE(json.find(oldSkin), std::string::npos);
        json.replace(json.find(oldNodes), oldNodes.size(), nodes);
        json.replace(json.find(oldSkin), oldSkin.size(), joints);
        writeTextFile(gltfPath, json);
        writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());
        EXPECT_FALSE(
            cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64).has_value());
    }

    {
        std::string json = skinnedTriangleGltfJson();
        std::string channels = "\"channels\":[";
        for (Core::u32 index = 0; index <= AssetFormat::AnimationClip3DWire::MaxTracks; ++index)
        {
            if (index != 0U)
            {
                channels += ',';
            }
            channels += "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}";
        }
        channels += ']';
        const std::string oldChannels =
            "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]";
        ASSERT_NE(json.find(oldChannels), std::string::npos);
        json.replace(json.find(oldChannels), oldChannels.size(), channels);
        writeTextFile(gltfPath, json);
        writeBinaryFile(dir / "geometry.bin", skinnedTriangleBufferBytes());
        EXPECT_FALSE(
            cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64).has_value());
    }

    {
        constexpr std::size_t KeyCount = AssetFormat::AnimationClip3DWire::MaxKeyframesPerTrack + 1U;
        constexpr std::size_t TimeOffset = 288U;
        constexpr std::size_t ValueOffset = TimeOffset + KeyCount * sizeof(float);
        std::string json = skinnedTriangleGltfJson();
        const std::array replacements{
            std::pair{std::string{"{\"bufferView\":8,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}"},
                      std::string{"{\"bufferView\":8,\"componentType\":5126,\"count\":"} + std::to_string(KeyCount) +
                          ",\"type\":\"SCALAR\"}"},
            std::pair{std::string{"{\"bufferView\":9,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}"},
                      std::string{"{\"bufferView\":9,\"componentType\":5126,\"count\":"} + std::to_string(KeyCount) +
                          ",\"type\":\"VEC3\"}"},
            std::pair{std::string{"{\"buffer\":0,\"byteOffset\":288,\"byteLength\":8}"},
                      std::string{"{\"buffer\":0,\"byteOffset\":288,\"byteLength\":"} +
                          std::to_string(KeyCount * sizeof(float)) + '}'},
            std::pair{std::string{"{\"buffer\":0,\"byteOffset\":296,\"byteLength\":24}"},
                      std::string{"{\"buffer\":0,\"byteOffset\":"} + std::to_string(ValueOffset) +
                          ",\"byteLength\":" + std::to_string(KeyCount * 3U * sizeof(float)) + '}'},
            std::pair{std::string{"\"buffers\":[{\"byteLength\":320"},
                      std::string{"\"buffers\":[{\"byteLength\":"} +
                          std::to_string(ValueOffset + KeyCount * 3U * sizeof(float))},
        };
        for (const auto& [from, to] : replacements)
        {
            ASSERT_NE(json.find(from), std::string::npos);
            json.replace(json.find(from), from.size(), to);
        }
        std::vector<unsigned char> bytes(ValueOffset + KeyCount * 3U * sizeof(float), 0U);
        const auto baseBytes = skinnedTriangleBufferBytes();
        std::copy_n(baseBytes.begin(), TimeOffset, bytes.begin());
        for (std::size_t index = 0; index < KeyCount; ++index)
        {
            const float time = static_cast<float>(index) * 0.25F;
            std::memcpy(bytes.data() + TimeOffset + index * sizeof(float), &time, sizeof(time));
        }
        writeTextFile(gltfPath, json);
        writeBinaryFile(dir / "geometry.bin", bytes);
        EXPECT_FALSE(
            cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::LinuxX64).has_value());
    }

    std::filesystem::remove_all(dir, ec);
}

[[nodiscard]] std::string skinnedTriangleGltfJson(std::string_view interpolation)
{
    return std::string{R"json({
  "asset":{"version":"2.0"},
  "scene":0,
  "scenes":[{"nodes":[0]}],
  "nodes":[{"children":[1]},{"mesh":0,"skin":0}],
  "skins":[{"joints":[0],"inverseBindMatrices":7}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3,"JOINTS_0":4,"WEIGHTS_0":5},"indices":6,"mode":4}]}],
  "animations":[{"samplers":[{"input":8,"output":9,"interpolation":")json"} +
           std::string{interpolation} +
           R"json("}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
    {"bufferView":4,"componentType":5123,"count":3,"type":"VEC4"},
    {"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"},
    {"bufferView":6,"componentType":5123,"count":3,"type":"SCALAR"},
    {"bufferView":7,"componentType":5126,"count":1,"type":"MAT4"},
    {"bufferView":8,"componentType":5126,"count":2,"type":"SCALAR"},
    {"bufferView":9,"componentType":5126,"count":2,"type":"VEC3"}
  ],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":36},
    {"buffer":0,"byteOffset":36,"byteLength":36},
    {"buffer":0,"byteOffset":72,"byteLength":24},
    {"buffer":0,"byteOffset":96,"byteLength":48},
    {"buffer":0,"byteOffset":144,"byteLength":24},
    {"buffer":0,"byteOffset":168,"byteLength":48},
    {"buffer":0,"byteOffset":216,"byteLength":6},
    {"buffer":0,"byteOffset":224,"byteLength":64},
    {"buffer":0,"byteOffset":288,"byteLength":8},
    {"buffer":0,"byteOffset":296,"byteLength":24}
  ],
  "buffers":[{"byteLength":320,"uri":"geometry.bin"}]
})json";
}

[[nodiscard]] std::vector<unsigned char> skinnedTriangleBufferBytes(bool invalidWeight)
{
    std::vector<unsigned char> bytes(320U, 0U);
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<float, 9> normals{0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::array<float, 6> uv{0, 0, 1, 0, 0, 1};
    const std::array<float, 12> tangents{1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    const std::array<Core::u16, 12> joints{};
    const std::array<float, 12> weights{1, 0, 0, 0, 1, 0, 0, 0, invalidWeight ? 0.0F : 1.0F, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};
    const std::array<float, 16> inverseBind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<float, 2> times{0, 1};
    const std::array<float, 6> translations{0, 0, 0, 1, 0, 0};
    std::memcpy(bytes.data() + 0, positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + 36, normals.data(), sizeof(normals));
    std::memcpy(bytes.data() + 72, uv.data(), sizeof(uv));
    std::memcpy(bytes.data() + 96, tangents.data(), sizeof(tangents));
    std::memcpy(bytes.data() + 144, joints.data(), sizeof(joints));
    std::memcpy(bytes.data() + 168, weights.data(), sizeof(weights));
    std::memcpy(bytes.data() + 216, indices.data(), sizeof(indices));
    std::memcpy(bytes.data() + 224, inverseBind.data(), sizeof(inverseBind));
    std::memcpy(bytes.data() + 288, times.data(), sizeof(times));
    std::memcpy(bytes.data() + 296, translations.data(), sizeof(translations));
    return bytes;
}

[[nodiscard]] std::string tangentDiscontinuitySkinnedGltfJson()
{
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"children": [1]}, {"children": [2]}, {"mesh": 0, "skin": 0}],
  "skins": [{"joints": [1, 0], "inverseBindMatrices": 6}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2,
                    "JOINTS_0": 4, "WEIGHTS_0": 5},
    "indices": 3, "mode": 4
  }]}],
  "animations": [{
    "samplers": [{"input": 7, "output": 8, "interpolation": "LINEAR"}],
    "channels": [{"sampler": 0, "target": {"node": 1, "path": "translation"}}]
  }],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 5, "type": "VEC3",
     "max": [1, 1, 0], "min": [-1, 0, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 5, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 5, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5123, "count": 5, "type": "VEC4"},
    {"bufferView": 5, "componentType": 5126, "count": 5, "type": "VEC4"},
    {"bufferView": 6, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 7, "componentType": 5126, "count": 2, "type": "SCALAR"},
    {"bufferView": 8, "componentType": 5126, "count": 2, "type": "VEC3"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 60},
    {"buffer": 0, "byteOffset": 60, "byteLength": 60},
    {"buffer": 0, "byteOffset": 120, "byteLength": 40},
    {"buffer": 0, "byteOffset": 160, "byteLength": 12},
    {"buffer": 0, "byteOffset": 172, "byteLength": 40},
    {"buffer": 0, "byteOffset": 212, "byteLength": 80},
    {"buffer": 0, "byteOffset": 292, "byteLength": 128},
    {"buffer": 0, "byteOffset": 420, "byteLength": 8},
    {"buffer": 0, "byteOffset": 428, "byteLength": 24}
  ],
  "buffers": [{"byteLength": 452, "uri": "geometry.bin"}]
})json";
}

[[nodiscard]] std::vector<unsigned char> tangentDiscontinuitySkinnedBufferBytes()
{
    auto bytes = tangentDiscontinuityBufferBytes();
    bytes.resize(452U, 0U);
    std::array<Core::u16, 20> joints{};
    std::array<float, 20> weights{};
    for (std::size_t vertex = 0; vertex < 5U; ++vertex)
    {
        // skin.joints is [child, root]; the shared split vertex follows the child.
        joints[vertex * 4U] = vertex == 0U ? 0U : 1U;
        weights[vertex * 4U] = 1.0F;
    }
    std::array<float, 32> inverseBind{};
    inverseBind[0] = 1.0F;
    inverseBind[5] = 1.0F;
    inverseBind[10] = 1.0F;
    inverseBind[15] = 1.0F;
    inverseBind[16 + 0] = 1.0F;
    inverseBind[16 + 5] = 1.0F;
    inverseBind[16 + 10] = 1.0F;
    inverseBind[16 + 15] = 1.0F;
    inverseBind[12] = 5.0F;
    inverseBind[16 + 12] = 7.0F;
    const std::array<float, 2> times{0.0F, 1.0F};
    const std::array<float, 6> translations{0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    std::memcpy(bytes.data() + 172U, joints.data(), sizeof(joints));
    std::memcpy(bytes.data() + 212U, weights.data(), sizeof(weights));
    std::memcpy(bytes.data() + 292U, inverseBind.data(), sizeof(inverseBind));
    std::memcpy(bytes.data() + 420U, times.data(), sizeof(times));
    std::memcpy(bytes.data() + 428U, translations.data(), sizeof(translations));
    return bytes;
}

TEST(GltfCookTests, AcceptsFixedMeshIdAfterMaterialId)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_fixed_id_order";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";
    {
        std::ofstream out(gltfPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << minimalTriangleGltfJson();
    }

    Core::AssetId::Bytes meshBytes{};
    meshBytes[0] = std::byte{0xF0};
    Core::AssetId::Bytes materialBytes{};
    materialBytes[0] = std::byte{0x10};
    const auto meshId = *Core::AssetId::fromBytes(meshBytes);
    const auto materialId = *Core::AssetId::fromBytes(materialBytes);
    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64,
                                                GltfCookIds{.meshId = meshId, .materialId = materialId});
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);

    const auto prefab =
        std::find_if(request->assets.begin(), request->assets.end(), [](const CatalogCookAssetSpec& asset) {
            return asset.assetKind == AssetFormat::AssetKind::Prefab;
        });
    ASSERT_NE(prefab, request->assets.end());
    ASSERT_EQ(prefab->dependencies.size(), 2U);
    EXPECT_EQ(prefab->dependencies[0].assetId, materialId);
    EXPECT_EQ(prefab->dependencies[1].assetId, meshId);
    std::vector<AssetFormat::PrefabNodeView> nodes;
    auto payload = AssetFormat::parsePrefabPayload(prefab->payload, nodes);
    ASSERT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    ASSERT_EQ(payload->nodes.size(), 1U);
    EXPECT_EQ(payload->nodes[0].meshId, meshId);
    EXPECT_EQ(payload->nodes[0].materialId, materialId);

    std::filesystem::remove_all(dir, ec);
}

TEST(GltfCookTests, PreservesAuthoredTangents)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_authored_tangents";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";
    writeTextFile(gltfPath, tangentTriangleGltfJson(true));
    writeBinaryFile(dir / "geometry.bin", tangentTriangleBufferBytes(true));

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind != AssetFormat::AssetKind::StaticMesh)
        {
            continue;
        }
        auto view = AssetFormat::parseStaticMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        ASSERT_EQ(view->vertices.size(), 3U * AssetFormat::StaticMeshWire::FloatsPerVertex);
        EXPECT_FLOAT_EQ(view->vertices[6], 0.0F);
        EXPECT_FLOAT_EQ(view->vertices[7], 1.0F);
        EXPECT_FLOAT_EQ(view->vertices[8], 0.0F);
        EXPECT_FLOAT_EQ(view->vertices[9], -1.0F);
        EXPECT_FLOAT_EQ(view->vertices[10], 0.0F);
        EXPECT_FLOAT_EQ(view->vertices[11], 0.0F);
        return;
    }
    FAIL() << "cooked request did not contain a StaticMesh";
}

TEST(GltfCookTests, RejectsInvalidAuthoredTangentHandedness)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_invalid_authored_tangent";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";
    writeTextFile(gltfPath, tangentTriangleGltfJson(true));
    auto bytes = tangentTriangleBufferBytes(true);
    const float invalidHandedness = 0.5F;
    std::memcpy(bytes.data() + 96U + 3U * sizeof(float), &invalidHandedness, sizeof(float));
    writeBinaryFile(dir / "geometry.bin", bytes);

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("TANGENT w"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsOverflowingAuthoredTangentLength)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_overflowing_authored_tangent";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";
    writeTextFile(gltfPath, tangentTriangleGltfJson(true));
    auto bytes = tangentTriangleBufferBytes(true);
    const float overflowingComponent = (std::numeric_limits<float>::max)();
    std::memcpy(bytes.data() + 96U, &overflowingComponent, sizeof(float));
    writeBinaryFile(dir / "geometry.bin", bytes);

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("TANGENT xyz"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, GeneratesMissingTangentsWithMikkTSpace)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_generated_tangents";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "triangle.gltf";
    writeTextFile(gltfPath, tangentTriangleGltfJson(false));
    writeBinaryFile(dir / "geometry.bin", tangentTriangleBufferBytes(false));

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind != AssetFormat::AssetKind::StaticMesh)
        {
            continue;
        }
        auto view = AssetFormat::parseStaticMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        ASSERT_EQ(view->vertices.size(), 3U * AssetFormat::StaticMeshWire::FloatsPerVertex);
        for (std::size_t vertex = 0; vertex < view->vertexCount; ++vertex)
        {
            const std::size_t base = vertex * AssetFormat::StaticMeshWire::FloatsPerVertex;
            const float tangentLength = std::sqrt(view->vertices[base + 6U] * view->vertices[base + 6U] +
                                                  view->vertices[base + 7U] * view->vertices[base + 7U] +
                                                  view->vertices[base + 8U] * view->vertices[base + 8U]);
            EXPECT_NEAR(tangentLength, 1.0F, 1.0e-4F);
            EXPECT_NEAR(std::abs(view->vertices[base + 9U]), 1.0F, 1.0e-4F);
        }
        return;
    }
    FAIL() << "cooked request did not contain a StaticMesh";
}

TEST(GltfCookTests, RejectsPrimitiveWithoutRequiredNormal)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_missing_normal";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    std::string json = tangentTriangleGltfJson(false);
    const std::string normalAttribute = "\"NORMAL\": 1, ";
    const std::size_t attributeOffset = json.find(normalAttribute);
    ASSERT_NE(attributeOffset, std::string::npos);
    json.erase(attributeOffset, normalAttribute.size());
    writeTextFile(dir / "triangle.gltf", json);
    writeBinaryFile(dir / "geometry.bin", tangentTriangleBufferBytes(false));

    auto request =
        cookGltfFileToCatalogRequest((dir / "triangle.gltf").string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("NORMAL"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsPrimitiveWithoutRequiredTexcoord)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_missing_texcoord";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    std::string json = tangentTriangleGltfJson(false);
    const std::string texcoordAttribute = ", \"TEXCOORD_0\": 2";
    const std::size_t attributeOffset = json.find(texcoordAttribute);
    ASSERT_NE(attributeOffset, std::string::npos);
    json.erase(attributeOffset, texcoordAttribute.size());
    writeTextFile(dir / "triangle.gltf", json);
    writeBinaryFile(dir / "geometry.bin", tangentTriangleBufferBytes(false));

    auto request =
        cookGltfFileToCatalogRequest((dir / "triangle.gltf").string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("TEXCOORD_0"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, SplitsSharedVertexAcrossMikkTangentHandednessDiscontinuity)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_tangent_discontinuity";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "mirrored_uv.gltf";
    writeTextFile(gltfPath, tangentDiscontinuityGltfJson());
    writeBinaryFile(dir / "geometry.bin", tangentDiscontinuityBufferBytes());

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind != AssetFormat::AssetKind::StaticMesh)
        {
            continue;
        }
        auto view = AssetFormat::parseStaticMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        ASSERT_EQ(view->indexCount, 6U);
        ASSERT_GT(view->vertexCount, 5U);
        ASSERT_NE(view->indices[0], view->indices[3]);
        const std::size_t first =
            static_cast<std::size_t>(view->indices[0]) * AssetFormat::StaticMeshWire::FloatsPerVertex;
        const std::size_t mirrored =
            static_cast<std::size_t>(view->indices[3]) * AssetFormat::StaticMeshWire::FloatsPerVertex;
        EXPECT_FLOAT_EQ(view->vertices[first + 9U], -view->vertices[mirrored + 9U]);
        return;
    }
    FAIL() << "cooked request did not contain a StaticMesh";
}

TEST(GltfCookTests, TopologicallySortsChildFirstSkinAcrossMikkVertexSplits)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_skinned_tangent_discontinuity";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto gltfPath = dir / "mirrored_uv_skinned.gltf";
    writeTextFile(gltfPath, tangentDiscontinuitySkinnedGltfJson());
    writeBinaryFile(dir / "geometry.bin", tangentDiscontinuitySkinnedBufferBytes());

    const auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    bool sawSkinnedMesh = false;
    bool sawAnimation = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::AnimationClip3D)
        {
            const auto view = AssetFormat::parseAnimationClip3DPayload(asset.payload);
            ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
            ASSERT_EQ(view->jointCount, 2U);
            ASSERT_EQ(view->trackCount, 1U);
            const auto track = view->track(0U);
            ASSERT_TRUE(track.has_value());
            EXPECT_EQ(track->jointIndex, 1U);
            sawAnimation = true;
            continue;
        }
        if (asset.assetKind != AssetFormat::AssetKind::SkinnedMesh)
        {
            continue;
        }
        const auto view = AssetFormat::parseSkinnedMeshPayload(asset.payload);
        ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        ASSERT_EQ(view->jointCount, 2U);
        const auto root = view->joint(0U);
        const auto child = view->joint(1U);
        ASSERT_TRUE(root.has_value());
        ASSERT_TRUE(child.has_value());
        EXPECT_EQ(root->parentJoint, AssetFormat::SkinnedMeshWire::JointIndexNone);
        EXPECT_EQ(child->parentJoint, 0U);
        EXPECT_FLOAT_EQ(view->inverseBindMatrix(0U)[12], 7.0F);
        EXPECT_FLOAT_EQ(view->inverseBindMatrix(1U)[12], 5.0F);
        ASSERT_GT(view->vertexCount, 5U);
        ASSERT_NE(view->indices[0], view->indices[3]);
        for (const Core::u16 index : {view->indices[0], view->indices[3]})
        {
            const std::size_t base =
                static_cast<std::size_t>(index) * AssetFormat::SkinnedMeshWire::InfluencesPerVertex;
            ASSERT_LT(base, view->jointIndices.size());
            EXPECT_EQ(view->jointIndices[base], 1U);
            EXPECT_EQ(view->jointWeights[base], AssetFormat::SkinnedMeshWire::WeightScale);
        }
        sawSkinnedMesh = true;
    }
    EXPECT_TRUE(sawSkinnedMesh);
    EXPECT_TRUE(sawAnimation);
    std::filesystem::remove_all(dir, ec);
}

// Two TRIANGLES meshes, two scene roots referencing each mesh.
[[nodiscard]] std::string twoMeshGltfJson()
{
    // Mesh0: triangle at origin; Mesh1: second triangle translated in node TRS.
    // Both mesh definitions share one complete vertex/index stream.
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"mesh": 0},
    {"mesh": 1, "translation": [2.0, 0.0, 0.0]}
  ],
  "meshes": [
    {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4, "material": 0}]},
    {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4, "material": 1}]}
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
      "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"
    },
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA="
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
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
        } else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            ++materialCount;
        } else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
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
    std::pmr::monotonic_buffer_resource memory;
    auto catalog =
        openCatalogPackage(catalogRoot.string(),
                           CatalogPackageOpenConfig{
                               .manifest = CatalogFileLoadConfig{.catalog = CatalogConfig{.maxEntries = 16,
                                                                                          .maxDependencies = 32,
                                                                                          .maxDependenciesPerAsset = 16,
                                                                                          .memoryResource = &memory}},
                               .validateOnOpen = true,
                               .validation =
                                   CatalogPackageValidationConfig{
                                       .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                                       .verifyContent = true,
                                       .verifyTypedPayload = true,
                                   },
                           });
    ASSERT_TRUE(catalog.has_value()) << (catalog ? "" : catalog.error().message);
    std::filesystem::remove_all(dir, ec);
}

// 1x1 red PNG (public domain fixture).
[[nodiscard]] std::vector<unsigned char> tinyRedPngBytes()
{
    // iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==
    static constexpr unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xFC, 0xCF, 0xC0, 0x50, 0x0F, 0x00, 0x04, 0x85, 0x01, 0x80,
        0x84, 0xA9, 0x8C, 0x21, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
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
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
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
      "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"
    },
    {
      "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"
    },
    {
      "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA="
  }]
})json";
}

[[nodiscard]] std::string externalBufferTexturedTriangleGltfJson()
{
    std::string json = texturedTriangleGltfJson();
    const std::size_t uriStart = json.find("data:application/octet-stream;base64,");
    if (uriStart == std::string::npos)
    {
        return {};
    }
    const std::size_t uriEnd = json.find('"', uriStart);
    if (uriEnd == std::string::npos)
    {
        return {};
    }
    json.replace(uriStart, uriEnd - uriStart, "geometry.bin");
    return json;
}

void appendLittleEndianU32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

[[nodiscard]] std::vector<unsigned char> embeddedImageTriangleGlb()
{
    auto bin = externalTriangleBufferBytes();
    const auto png = tinyRedPngBytes();
    const std::size_t imageOffset = bin.size();
    std::vector<unsigned char> binChunk(bin.begin(), bin.end());
    binChunk.insert(binChunk.end(), png.begin(), png.end());

    std::string json = std::string{R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "indices": 3, "mode": 4, "material": 0
  }]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "textures": [{"source": 0}],
  "images": [{"bufferView": 4, "mimeType": "image/png"}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6},
    {"buffer": 0, "byteOffset": )json"} +
                       std::to_string(imageOffset) + ", \"byteLength\": " + std::to_string(png.size()) + R"json(}
  ],
  "buffers": [{"byteLength": )json" +
                       std::to_string(binChunk.size()) + R"json(}]
})json";

    while ((json.size() & 3U) != 0U)
    {
        json.push_back(' ');
    }
    while ((binChunk.size() & 3U) != 0U)
    {
        binChunk.push_back(0U);
    }

    std::vector<unsigned char> glb;
    glb.reserve(12U + 8U + json.size() + 8U + binChunk.size());
    appendLittleEndianU32(glb, 0x46546C67U);
    appendLittleEndianU32(glb, 2U);
    appendLittleEndianU32(glb, static_cast<std::uint32_t>(12U + 8U + json.size() + 8U + binChunk.size()));
    appendLittleEndianU32(glb, static_cast<std::uint32_t>(json.size()));
    appendLittleEndianU32(glb, 0x4E4F534AU);
    glb.insert(glb.end(), json.begin(), json.end());
    appendLittleEndianU32(glb, static_cast<std::uint32_t>(binChunk.size()));
    appendLittleEndianU32(glb, 0x004E4942U);
    glb.insert(glb.end(), binChunk.begin(), binChunk.end());
    return glb;
}

TEST(GltfCookTests, CapturesPrimaryExternalBufferPrefixAndExternalImage)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_source_capture_external";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    const std::string json = externalBufferTexturedTriangleGltfJson();
    ASSERT_FALSE(json.empty());
    const auto declaredGeometry = externalTriangleBufferBytes();
    std::vector<unsigned char> geometryFile(declaredGeometry.begin(), declaredGeometry.end());
    geometryFile.insert(geometryFile.end(), {0xA1U, 0xB2U, 0xC3U, 0xD4U});
    const auto png = tinyRedPngBytes();
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, json);
    writeBinaryFile(dir / "geometry.bin", geometryFile);
    writeBinaryFile(dir / "tex.png", png);

    const std::string sourceRoot = dir.string();
    auto cooked = cookGltfFileToCatalogSourceResult(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64,
                                                    SourceImportCaptureConfig{.sourceRootUtf8 = sourceRoot});
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);
    ASSERT_EQ(cooked->sourceImports.sources.size(), 3U);
    ASSERT_EQ(cooked->sourceImports.units.size(), 1U);

    const auto findSource = [&](std::string_view path) -> const SourceImportCapturedSource* {
        const auto found =
            std::find_if(cooked->sourceImports.sources.begin(), cooked->sourceImports.sources.end(),
                         [path](const SourceImportCapturedSource& source) { return source.path == path; });
        return found != cooked->sourceImports.sources.end() ? &*found : nullptr;
    };
    const auto digest = [](const auto& bytes) {
        return Core::digestContentHashV1(std::as_bytes(std::span(bytes.data(), bytes.size())));
    };

    const auto* primary = findSource("scene.gltf");
    const auto* geometry = findSource("geometry.bin");
    const auto* image = findSource("tex.png");
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(geometry, nullptr);
    ASSERT_NE(image, nullptr);
    const auto primaryHash = digest(json);
    const auto geometryHash = digest(declaredGeometry);
    const auto imageHash = digest(png);
    ASSERT_TRUE(primaryHash.has_value());
    ASSERT_TRUE(geometryHash.has_value());
    ASSERT_TRUE(imageHash.has_value());
    EXPECT_EQ(primary->fileBytes, json.size());
    EXPECT_EQ(primary->contentHash, *primaryHash);
    EXPECT_EQ(primary->readExtent, AssetFormat::SourceImportReadExtent::WholeFile);
    EXPECT_EQ(geometry->fileBytes, declaredGeometry.size());
    EXPECT_EQ(geometry->contentHash, *geometryHash);
    EXPECT_EQ(geometry->readExtent, AssetFormat::SourceImportReadExtent::Prefix);
    EXPECT_EQ(image->fileBytes, png.size());
    EXPECT_EQ(image->contentHash, *imageHash);
    EXPECT_EQ(image->readExtent, AssetFormat::SourceImportReadExtent::WholeFile);

    const auto& unit = cooked->sourceImports.units[0];
    EXPECT_EQ(unit.importerKind, SourceImporterKind::Gltf);
    EXPECT_EQ(unit.importerVersion, 2U);
    ASSERT_EQ(unit.inputs.size(), 3U);
    std::size_t primaryInputCount = 0;
    for (const auto& input : unit.inputs)
    {
        ASSERT_LT(input.sourceIndex, cooked->sourceImports.sources.size());
        if (AssetFormat::hasSourceImportInputFlag(input.flags, AssetFormat::SourceImportInputFlags::Primary))
        {
            ++primaryInputCount;
            EXPECT_EQ(cooked->sourceImports.sources[input.sourceIndex].path, "scene.gltf");
        }
    }
    EXPECT_EQ(primaryInputCount, 1U);
    ASSERT_EQ(unit.outputs.size(), cooked->request.assets.size());
    for (const auto& asset : cooked->request.assets)
    {
        const auto output =
            std::find_if(unit.outputs.begin(), unit.outputs.end(), [&](const SourceImportCapturedOutput& captured) {
                return captured.assetId == asset.assetId && captured.assetKind == asset.assetKind;
            });
        EXPECT_NE(output, unit.outputs.end());
    }
}

TEST(GltfCookTests, EmbeddedGlbBufferAndBufferViewImageRemainPrimarySource)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_source_capture_embedded";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    const auto glb = embeddedImageTriangleGlb();
    const auto gltfPath = dir / "scene.glb";
    writeBinaryFile(gltfPath, glb);
    const std::string sourceRoot = dir.string();
    auto cooked = cookGltfFileToCatalogSourceResult(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64,
                                                    SourceImportCaptureConfig{.sourceRootUtf8 = sourceRoot});
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);
    ASSERT_EQ(cooked->sourceImports.sources.size(), 1U);
    EXPECT_EQ(cooked->sourceImports.sources[0].path, "scene.glb");
    EXPECT_EQ(cooked->sourceImports.sources[0].fileBytes, glb.size());
    EXPECT_EQ(cooked->sourceImports.sources[0].readExtent, AssetFormat::SourceImportReadExtent::WholeFile);
    ASSERT_EQ(cooked->sourceImports.units.size(), 1U);
    ASSERT_EQ(cooked->sourceImports.units[0].inputs.size(), 1U);
    EXPECT_TRUE(AssetFormat::hasSourceImportInputFlag(cooked->sourceImports.units[0].inputs[0].flags,
                                                      AssetFormat::SourceImportInputFlags::Primary));
    EXPECT_EQ(cooked->sourceImports.units[0].outputs.size(), cooked->request.assets.size());
    EXPECT_EQ(cooked->request.assets.size(), 4U);
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
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
        } else if (asset.assetKind == AssetFormat::AssetKind::Material)
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
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
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
      "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"
    },
    {
      "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"
    },
    {
      "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA="
  }]
})json";
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);

    std::size_t textureCount = 0;
    bool sawMaterial = false;
    for (const auto& asset : request->assets)
    {
        if (asset.assetKind == AssetFormat::AssetKind::Texture2D)
        {
            ++textureCount;
        } else if (asset.assetKind == AssetFormat::AssetKind::Material)
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

    // Cross-importer identity. The metallic-roughness texture's role tag (0x75) is
    // also the imported-texture tag, and that byte leads the AssetId, so these two
    // producers are separated only by the remaining hash inputs -- AssetKind is
    // Texture2D on both sides, leaving the channel. This document is one of the few
    // fixtures that actually emits the tag-sharing output, so the check belongs here
    // rather than beside a mesh-only fixture where it cannot fail.
    //
    // The comparison uses the document's own locator: that is the only way both
    // importers can present byte-identical input to the shared derivation, and a
    // collision would surface as a duplicate-owner failure at publish time.
    {
        const auto mediaTextureId = deriveTextureMediaAssetId("pbr.gltf");
        ASSERT_TRUE(mediaTextureId) << mediaTextureId.error().message;
        for (const auto& asset : request->assets)
        {
            EXPECT_NE(asset.assetId, *mediaTextureId)
                << "a glTF output collided with the imported-texture id for the same "
                   "locator; role tag 0x75 is shared between them";
        }
    }

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
        out << externalImageTriangleGltfJson("../secret.png");
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("traversal"), std::string::npos) << request.error().message;
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
        out << externalImageTriangleGltfJson("file:///C:/Windows/System32/drivers/etc/hosts");
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_FALSE(request.error().message.empty());
}

TEST(GltfCookTests, AllowsExternalBufferThroughContainedSymlinkOrJunction)
{
    const auto base = std::filesystem::temp_directory_path() / "tina_gltf_buffer_contained_link";
    const auto root = base / "root";
    const auto actual = root / "actual";
    const auto link = root / "linked";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(actual, ec);
    ASSERT_FALSE(ec) << ec.message();
    writeBinaryFile(actual / "mesh.bin", externalTriangleBufferBytes());
    if (!createDirectoryLink(actual, link, ec))
    {
        std::filesystem::remove_all(base, ec);
        GTEST_SKIP() << "directory symlink/junction creation unavailable: " << ec.message();
    }

    const auto gltfPath = root / "scene.gltf";
    writeTextFile(gltfPath, externalBufferTriangleGltfJson("linked/mesh.bin"));
    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    EXPECT_EQ(request->assets.size(), 3U);

    std::filesystem::remove(link, ec);
    std::filesystem::remove_all(base, ec);
}

TEST(GltfCookTests, AllowsPercentEncodedUtf8ExternalBufferPath)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_utf8_external_buffer";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    const std::u8string utf8Name = u8"\u8D44\u6E90.bin";
    writeBinaryFile(dir / std::filesystem::path{utf8Name}, externalTriangleBufferBytes());
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, externalBufferTriangleGltfJson("%E8%B5%84%E6%BA%90.bin"));

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(request.has_value()) << (request ? "" : request.error().message);
    EXPECT_EQ(request->assets.size(), 3U);
}

TEST(GltfCookTests, RejectsPercentEncodedExternalBufferTraversal)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_encoded_traversal";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, externalBufferTriangleGltfJson("%2e%2e/secret.bin"));

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("traversal"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsPrimaryPathWithEmbeddedNul)
{
    std::string path = "scene.gltf";
    path.push_back('\0');
    path += "ignored.gltf";

    auto request = cookGltfFileToCatalogRequest(path, AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("strict UTF-8"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsExternalBufferThroughEscapingSymlinkOrJunction)
{
    const auto base = std::filesystem::temp_directory_path() / "tina_gltf_buffer_reparse_escape";
    const auto root = base / "root";
    const auto outside = base / "outside";
    const auto link = root / "linked";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec) << ec.message();
    std::filesystem::create_directories(outside, ec);
    ASSERT_FALSE(ec) << ec.message();
    writeBinaryFile(outside / "mesh.bin", externalTriangleBufferBytes());
    if (!createDirectoryLink(outside, link, ec))
    {
        std::filesystem::remove_all(base, ec);
        GTEST_SKIP() << "directory symlink/junction creation unavailable: " << ec.message();
    }

    const auto gltfPath = root / "scene.gltf";
    writeTextFile(gltfPath, externalBufferTriangleGltfJson("linked/mesh.bin"));
    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("escapes"), std::string::npos) << request.error().message;

    std::filesystem::remove(link, ec);
    std::filesystem::remove_all(base, ec);
}

TEST(GltfCookTests, RejectsExternalImageThroughEscapingSymlinkOrJunction)
{
    const auto base = std::filesystem::temp_directory_path() / "tina_gltf_image_reparse_escape";
    const auto root = base / "root";
    const auto outside = base / "outside";
    const auto link = root / "linked";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec) << ec.message();
    std::filesystem::create_directories(outside, ec);
    ASSERT_FALSE(ec) << ec.message();
    writeBinaryFile(outside / "tex.png", tinyRedPngBytes());
    if (!createDirectoryLink(outside, link, ec))
    {
        std::filesystem::remove_all(base, ec);
        GTEST_SKIP() << "directory symlink/junction creation unavailable: " << ec.message();
    }

    std::string json = texturedTriangleGltfJson();
    const std::size_t imageUri = json.find("tex.png");
    ASSERT_NE(imageUri, std::string::npos);
    json.replace(imageUri, std::strlen("tex.png"), "linked/tex.png");
    const auto gltfPath = root / "scene.gltf";
    writeTextFile(gltfPath, json);

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("escapes"), std::string::npos) << request.error().message;

    std::filesystem::remove(link, ec);
    std::filesystem::remove_all(base, ec);
}

TEST(GltfCookTests, RejectsExternalBufferShorterThanDeclaredSnapshot)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_short_buffer";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    std::array<unsigned char, 43> shortBuffer{};
    writeBinaryFile(dir / "mesh.bin", shortBuffer);
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, externalBufferTriangleGltfJson("mesh.bin"));

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("size"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsSparseExternalBufferLargerThan64MiB)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_oversize_buffer";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream output(dir / "mesh.bin", std::ios::binary);
        ASSERT_TRUE(output.good());
        output.seekp(static_cast<std::streamoff>(64ULL * 1024ULL * 1024ULL));
        output.put('\0');
        ASSERT_TRUE(output.good());
    }
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, externalBufferTriangleGltfJson("mesh.bin"));

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("size"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsSparsePrimaryGltfLargerThan64MiBBeforeParse)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_oversize_primary";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    const auto gltfPath = dir / "scene.gltf";
    {
        std::ofstream output(gltfPath, std::ios::binary);
        ASSERT_TRUE(output.good());
        output << minimalTriangleGltfJson();
        output.seekp(static_cast<std::streamoff>(64ULL * 1024ULL * 1024ULL));
        output.put('\0');
        ASSERT_TRUE(output.good());
    }

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("size"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsBufferViewOffsetOverflowBeforeBufferLoad)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_buffer_view_overflow";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    std::string json = minimalTriangleGltfJson();
    const std::string original = "\"byteOffset\": 0, \"byteLength\": 36";
    const std::size_t view = json.find(original);
    ASSERT_NE(view, std::string::npos);
    json.replace(view, original.size(), "\"byteOffset\": 18446744073709551600, \"byteLength\": 36");
    const auto gltfPath = dir / "overflow.gltf";
    writeTextFile(gltfPath, json);

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("bufferView"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsAccessorCountBombBeforeBufferLoad)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_accessor_count_bomb";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    std::string json = minimalTriangleGltfJson();
    const std::string original = "\"count\": 3";
    const std::size_t accessor = json.find(original);
    ASSERT_NE(accessor, std::string::npos);
    json.replace(accessor, original.size(), "\"count\": 4294967296");
    const auto gltfPath = dir / "count.gltf";
    writeTextFile(gltfPath, json);

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("accessor"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsImageCountBombBeforeBufferLoad)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_image_count_bomb";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    std::string imageArray = "\n  \"images\": [";
    for (std::size_t i = 0; i < 4'097U; ++i)
    {
        if (i != 0)
        {
            imageArray.push_back(',');
        }
        imageArray += "{}";
    }
    imageArray += "],";
    std::string json = minimalTriangleGltfJson();
    json.insert(json.find('{') + 1U, imageArray);
    const auto gltfPath = dir / "count.gltf";
    writeTextFile(gltfPath, json);

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("object count"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsImageDimensionBombFromHeaderBeforeDecode)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_image_dimension_bomb";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    auto png = tinyRedPngBytes();
    writeBigEndianU32(png, 16U, 16'385U);
    writeBinaryFile(dir / "tex.png", png);
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, texturedTriangleGltfJson());

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("dimensions"), std::string::npos) << request.error().message;
}

TEST(GltfCookTests, RejectsDecodedImageByteBombFromHeaderBeforeDecode)
{
    const auto dir = std::filesystem::temp_directory_path() / "tina_gltf_image_decoded_bomb";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    auto png = tinyRedPngBytes();
    writeBigEndianU32(png, 16U, 5'000U);
    writeBigEndianU32(png, 20U, 5'000U);
    writeBinaryFile(dir / "tex.png", png);
    const auto gltfPath = dir / "scene.gltf";
    writeTextFile(gltfPath, texturedTriangleGltfJson());

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("decoded image byte budget"), std::string::npos) << request.error().message;
}

// One mesh, two TRIANGLES prims with distinct materials (external multi-prim models).
// Cooker SPLITs into two StaticMesh + two Material; Prefab expands to transform parent + 2 children.
[[nodiscard]] std::string multiPrimitiveMeshGltfJson()
{
    // Both primitives share one complete vertex/index stream.
    return R"json({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [1.0, 0.0, 0.0]}],
  "meshes": [{
    "primitives": [
      {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4, "material": 0},
      {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "mode": 4, "material": 1}
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
      "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"
    },
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA="
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
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
        } else if (asset.assetKind == AssetFormat::AssetKind::Material)
        {
            ++materialCount;
        } else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
        {
            // Parent transform + 2 primitive children, with identity carried by each payload node.
            EXPECT_EQ(asset.dependencies.size(), 4U);
            std::vector<AssetFormat::PrefabNodeView> nodes;
            auto prefab = AssetFormat::parsePrefabPayload(asset.payload, nodes);
            ASSERT_TRUE(prefab.has_value()) << (prefab ? "" : prefab.error().message);
            ASSERT_EQ(prefab->nodes.size(), 3U);
            EXPECT_FALSE(prefab->nodes[0].hasMesh);
            EXPECT_FLOAT_EQ(prefab->nodes[0].positionX, 1.0F);
            EXPECT_TRUE(prefab->nodes[1].hasMesh);
            EXPECT_TRUE(prefab->nodes[2].hasMesh);
            EXPECT_TRUE(prefab->nodes[1].meshId);
            EXPECT_TRUE(prefab->nodes[1].materialId);
            EXPECT_TRUE(prefab->nodes[2].meshId);
            EXPECT_TRUE(prefab->nodes[2].materialId);
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

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("TRIANGLES"), std::string::npos) << request.error().message;
}

#if defined(TINA_COMPLETE_PBR_GLTF_FIXTURE)
TEST(GltfCookTests, CooksRepoCompletePbrFixture)
{
    // Product gate fixture: multi-mesh + NORMAL/UV + baseColor/MR/normal textures + distinct MR factors.
    const std::filesystem::path gltfPath{TINA_COMPLETE_PBR_GLTF_FIXTURE};
    ASSERT_TRUE(std::filesystem::exists(gltfPath)) << TINA_COMPLETE_PBR_GLTF_FIXTURE;

    auto request = cookGltfFileToCatalogRequest(gltfPath.string(), AssetFormat::TargetPlatform::WindowsX64);
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
        } else if (asset.assetKind == AssetFormat::AssetKind::Material)
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
            } else
            {
                sawMetal = true;
                EXPECT_NEAR(mat->metallicFactor, 0.9F, 1.0e-4F);
                EXPECT_NEAR(mat->roughnessFactor, 0.2F, 1.0e-4F);
            }
        } else if (asset.assetKind == AssetFormat::AssetKind::Texture2D)
        {
            ++textureCount;
        } else if (asset.assetKind == AssetFormat::AssetKind::Prefab)
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

    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_gltf_complete_pbr_catalog";
    std::error_code ec;
    std::filesystem::remove_all(catalogRoot, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(catalogRoot.string(), *request).has_value());
}
#endif

TEST(GltfCookTests, RejectsKhronosMetalRoughSpheresWithoutRequiredTexcoordsWhenPresent)
{
    // This optional Khronos fixture intentionally omits TEXCOORD_0 because it has no textures.
#if !defined(TINA_COMPLETE_PBR_GLTF_FIXTURE)
    GTEST_SKIP() << "TINA_COMPLETE_PBR_GLTF_FIXTURE not defined; cannot locate fixtures tree";
#else
    const auto path = std::filesystem::path{TINA_COMPLETE_PBR_GLTF_FIXTURE}.parent_path().parent_path() /
                      "metal_rough_spheres" / "MetalRoughSpheresNoTextures.glb";
    if (!std::filesystem::exists(path))
    {
        GTEST_SKIP() << "MetalRoughSpheresNoTextures.glb not vendored";
    }

    auto request = cookGltfFileToCatalogRequest(path.string(), AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_FALSE(request.has_value());
    EXPECT_NE(request.error().message.find("TEXCOORD_0"), std::string::npos) << request.error().message;
#endif
}

} // namespace
} // namespace Tina::Asset
