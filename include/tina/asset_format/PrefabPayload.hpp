#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Prefab cooked payload schema v2 (little-endian, after CookedAsset header/deps).
// Layout:
//   PrefabWire header 16B:
//     u16 schemaVersion (=2)
//     u16 nodeCount
//     u16 reserved0 (=0)
//     u16 reserved1 (=0)
//     u32 reserved2 (=0)
//     u32 reserved3 (=0)
//   PrefabNodeWire[nodeCount] 84B each:
//     u32 stableNodeId
//     i32 parentIndex              // -1 = root
//     f32 posX, posY, posZ
//     f32 rotX, rotY, rotZ, rotW
//     f32 scaleX, scaleY, scaleZ
//     u16 flags                    // bit0 hidden
//     u16 reserved
//     u8 meshAssetId[16]           // zero only when the node has no mesh
//     u8 materialAssetId[16]       // zero only when the node has no mesh
// CookedAsset dependencies are the unique mesh/material references sorted by AssetId.
namespace PrefabWire {
inline constexpr Core::u16 SchemaVersion = 2;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u32 NodeBytes = 84;
inline constexpr Core::u16 FlagHidden = 1U << 0U;
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

// Node views are stored in caller-owned `nodeStorage`.
[[nodiscard]] Core::Result<PrefabPayloadView> parsePrefabPayload(std::span<const std::byte> payload,
                                                                 std::vector<PrefabNodeView>& nodeStorage);

// Full cooked Prefab: dependencies are the unique mesh/material AssetIds sorted by AssetId.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedPrefabAsset(Core::AssetId assetId, const PrefabPayloadDesc& desc,
                       TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
