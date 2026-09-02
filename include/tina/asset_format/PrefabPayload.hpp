#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <optional>
#include <string>
#include <vector>

namespace Tina::AssetFormat {

// Prefab cooked payload schema v4 (little-endian, after CookedAsset header/deps).
// Layout:
//   PrefabWire header 16B:
//     u16 schemaVersion (=4)
//     u16 nodeCount
//     u16 reserved0 (=0)
//     u16 reserved1 (=0)
//     u32 reserved2 (=0)
//     u32 reserved3 (=0)
//   PrefabNodeWire[nodeCount] 208B each:
//     u32 stableNodeId
//     i32 parentIndex              // -1 = root
//     f32 posX, posY, posZ
//     f32 rotX, rotY, rotZ, rotW
//     f32 scaleX, scaleY, scaleZ
//     u16 flags                    // bit0 hidden
//     u16 nodeKind
//     u8 meshAssetId[16]           // zero only when the node has no mesh
//     u8 materialAssetId[16]       // zero only when the node has no mesh
//     u8 typedPayload[60]          // Camera3D or 3D light payload
//     u8 name[64]                  // UTF-8, NUL-terminated, zero padded
// CookedAsset dependencies are the unique mesh/material references sorted by AssetId.
namespace PrefabWire {
inline constexpr Core::u16 SchemaVersion = 4;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u32 NodeBytes = 208;
inline constexpr Core::u32 NameOffset = 144;
inline constexpr Core::u32 NameBytes = 64;
inline constexpr Core::u32 MaximumNameBytes = NameBytes - 1U;
inline constexpr Core::u16 FlagHidden = 1U << 0U;
inline constexpr Core::u16 MaxNodes = 4096;
} // namespace PrefabWire

enum class PrefabNodeKind : Core::u16 {
    Node3D = 0,
    Marker3D = 1,
    Mesh3D = 2,
    SkinnedMesh3D = 3,
    Camera3D = 4,
    DirectionalLight3D = 5,
    PointLight3D = 6,
    SpotLight3D = 7,
};

inline constexpr Core::usize PrefabNodeKindCount = 8;

struct PrefabCamera3DDesc final {
    float verticalFovRadians = 1.0471975512F;
    float nearPlane = 0.1F;
    float farPlane = 1'000.0F;
    bool active = true;

    friend bool operator==(const PrefabCamera3DDesc&,
                           const PrefabCamera3DDesc&) = default;
};

struct PrefabLight3DDesc final {
    float colorRed = 1.0F;
    float colorGreen = 1.0F;
    float colorBlue = 1.0F;
    float colorAlpha = 1.0F;
    float intensity = 1.0F;
    float rangeMeters = 10.0F;
    float innerConeRadians = 0.35F;
    float outerConeRadians = 0.7F;
    bool active = true;

    friend bool operator==(const PrefabLight3DDesc&,
                           const PrefabLight3DDesc&) = default;
};

struct PrefabNodeDesc final {
    Core::u32 stableNodeId = 0;
    Core::i32 parentIndex = -1;
    PrefabNodeKind nodeKind = PrefabNodeKind::Node3D;
    // Empty uses the Editor's deterministic kind/#id fallback label.
    std::string name{};
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
    std::optional<PrefabCamera3DDesc> camera{};
    std::optional<PrefabLight3DDesc> light{};
};

struct PrefabNodeView final {
    Core::u32 stableNodeId = 0;
    Core::i32 parentIndex = -1;
    PrefabNodeKind nodeKind = PrefabNodeKind::Node3D;
    std::string name{};
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
    std::optional<PrefabCamera3DDesc> camera{};
    std::optional<PrefabLight3DDesc> light{};
};

// Every field of PrefabNodeDesc must be assigned here: a missing one is accepted by
// designated initialization and silently republishes the default instead of the authored value.
[[nodiscard]] inline PrefabNodeDesc prefabNodeDescFromView(const PrefabNodeView& node)
{
    return PrefabNodeDesc{
        .stableNodeId = node.stableNodeId,
        .parentIndex = node.parentIndex,
        .nodeKind = node.nodeKind,
        .name = node.name,
        .positionX = node.positionX,
        .positionY = node.positionY,
        .positionZ = node.positionZ,
        .rotationX = node.rotationX,
        .rotationY = node.rotationY,
        .rotationZ = node.rotationZ,
        .rotationW = node.rotationW,
        .scaleX = node.scaleX,
        .scaleY = node.scaleY,
        .scaleZ = node.scaleZ,
        .meshId = node.meshId,
        .materialId = node.materialId,
        .visible = node.visible,
        .camera = node.camera,
        .light = node.light,
    };
}

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
