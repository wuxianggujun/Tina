#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Prefab cooked payload schema v1 (little-endian, after CookedAsset header/deps).
// M11-E6 minimal hierarchy stub for product 3D toward M12 glTF gate.
// Layout:
//   PrefabWire header 16B:
//     u16 schemaVersion (=1)
//     u16 nodeCount
//     u16 reserved0 (=0)
//     u16 reserved1 (=0)
//     u32 reserved2 (=0)
//     u32 reserved3 (=0)
//   PrefabNodeWire[nodeCount] 64B each:
//     u32 stableNodeId
//     i32 parentIndex              // -1 = root
//     f32 posX, posY, posZ
//     f32 rotX, rotY, rotZ, rotW
//     f32 scaleX, scaleY, scaleZ
//     u16 flags                    // bit0 hasMesh, bit1 hasMaterial
//     u16 reserved
//     // mesh/material AssetIds are CookedAsset dependencies (required when flags set),
//     // ordered in the same sequence as nodes that declare them (stable node order).
namespace PrefabWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u32 NodeBytes = 64;
inline constexpr Core::u16 FlagHasMesh = 1U << 0U;
inline constexpr Core::u16 FlagHasMaterial = 1U << 1U;
inline constexpr Core::u16 MaxNodes = 4096;
} // namespace PrefabWire

struct PrefabNodeDesc final {
    Core::u32 stableNodeId = 0;
    Core::i32 parentIndex = -1;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float rotationX = 0.0F;
    float rotationY = 0.0F;
    float rotationZ = 0.0F;
    float rotationW = 1.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
    Core::AssetId meshId{};
    Core::AssetId materialId{};
    bool visible = true;
};

struct PrefabNodeView final {
    Core::u32 stableNodeId = 0;
    Core::i32 parentIndex = -1;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float rotationX = 0.0F;
    float rotationY = 0.0F;
    float rotationZ = 0.0F;
    float rotationW = 1.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
    bool hasMesh = false;
    bool hasMaterial = false;
    bool visible = true;
    // Filled by Asset::parsePrefabFromCooked from cooked dependency stream
    // (mesh then material per hasMesh node). Empty when only payload-parsed.
    Core::AssetId meshId{};
    Core::AssetId materialId{};
};

struct PrefabPayloadDesc final {
    std::span<const PrefabNodeDesc> nodes{};
};

struct PrefabPayloadView final {
    Core::u16 schemaVersion = 0;
    std::span<const PrefabNodeView> nodes{};

    [[nodiscard]] bool empty() const noexcept
    {
        return schemaVersion == 0 || nodes.empty();
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writePrefabPayloadBytes(const PrefabPayloadDesc& desc);

// submesh/node views alias into `payload` storage via out storage for nodes.
[[nodiscard]] Core::Result<PrefabPayloadView> parsePrefabPayload(std::span<const std::byte> payload,
                                                                 std::vector<PrefabNodeView>& nodeStorage);

// Full cooked Prefab: dependencies list StaticMesh/Material AssetIds in node order
// for every node that declares mesh and/or material.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedPrefabAsset(Core::AssetId assetId, const PrefabPayloadDesc& desc,
                       TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
