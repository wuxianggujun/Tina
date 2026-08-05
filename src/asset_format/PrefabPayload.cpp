#include <tina/asset_format/PrefabPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace Tina::AssetFormat {
namespace {

using Core::i32;
using Core::u16;
using Core::u32;
using Core::u8;
using Core::usize;

[[nodiscard]] u8 readU8(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::to_integer<u8>(bytes[offset]);
}

[[nodiscard]] u16 readU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(readU8(bytes, offset)) |
           static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u32>(readU8(bytes, offset)) |
           (static_cast<u32>(readU8(bytes, offset + 1U)) << 8U) |
           (static_cast<u32>(readU8(bytes, offset + 2U)) << 16U) |
           (static_cast<u32>(readU8(bytes, offset + 3U)) << 24U);
}

[[nodiscard]] i32 readI32(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<i32>(readU32(bytes, offset));
}

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

void writeU8(std::vector<std::byte>& bytes, usize offset, u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value)
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
    writeU8(bytes, offset + 2U, static_cast<u8>((value >> 16U) & 0xFFU));
    writeU8(bytes, offset + 3U, static_cast<u8>((value >> 24U) & 0xFFU));
}

void writeI32(std::vector<std::byte>& bytes, usize offset, i32 value)
{
    writeU32(bytes, offset, static_cast<u32>(value));
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

void writeAssetId(std::vector<std::byte>& bytes, usize offset, Core::AssetId assetId)
{
    const auto& idBytes = assetId.bytes();
    std::copy(idBytes.begin(), idBytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] Core::AssetId readAssetId(std::span<const std::byte> bytes, usize offset) noexcept
{
    Core::AssetId::Bytes idBytes{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), idBytes.size(), idBytes.begin());
    return Core::AssetId::fromBytes(idBytes).value_or(Core::AssetId{});
}

[[nodiscard]] bool isFiniteVec3(float x, float y, float z) noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

[[nodiscard]] bool isFiniteQuat(float x, float y, float z, float w) noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
}

[[nodiscard]] Core::Status validatePrefabDesc(const PrefabPayloadDesc& desc) noexcept
{
    if (desc.nodes.empty())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab must contain at least one node");
    }
    if (desc.nodes.size() > PrefabWire::MaxNodes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "prefab nodeCount exceeds MaxNodes");
    }
    for (usize index = 0; index < desc.nodes.size(); ++index)
    {
        const PrefabNodeDesc& node = desc.nodes[index];
        if (node.parentIndex < -1 || node.parentIndex >= static_cast<i32>(index))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab parentIndex must be -1 or a prior node index");
        }
        if (!isFiniteVec3(node.positionX, node.positionY, node.positionZ) ||
            !isFiniteQuat(node.rotationX, node.rotationY, node.rotationZ, node.rotationW) ||
            !isFiniteVec3(node.scaleX, node.scaleY, node.scaleZ))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node transform must be finite");
        }
        const float rotationLengthSquared = node.rotationX * node.rotationX + node.rotationY * node.rotationY +
                                            node.rotationZ * node.rotationZ + node.rotationW * node.rotationW;
        if (!(rotationLengthSquared > 1.0e-12F))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node rotation must be non-zero");
        }
        if (static_cast<bool>(node.meshId) != static_cast<bool>(node.materialId))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab mesh and material AssetIds must both be present or absent");
        }
        if (node.meshId && node.meshId == node.materialId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab mesh and material AssetIds must be distinct");
        }
        for (usize previousIndex = 0; previousIndex < index; ++previousIndex)
        {
            const PrefabNodeDesc& previous = desc.nodes[previousIndex];
            if ((node.meshId && node.meshId == previous.materialId) ||
                (node.materialId && node.materialId == previous.meshId))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "prefab AssetId cannot be used as both mesh and material");
            }
        }
    }
    return Core::success();
}

} // namespace

Core::Result<std::vector<std::byte>> writePrefabPayloadBytes(const PrefabPayloadDesc& desc)
{
    if (auto status = validatePrefabDesc(desc); !status)
    {
        return Core::failure(status.error());
    }
    const u16 nodeCount = static_cast<u16>(desc.nodes.size());
    const usize bytes = PrefabWire::HeaderBytes + static_cast<usize>(nodeCount) * PrefabWire::NodeBytes;
    std::vector<std::byte> payload(bytes, std::byte{0});
    writeU16(payload, 0, PrefabWire::SchemaVersion);
    writeU16(payload, 2, nodeCount);
    writeU16(payload, 4, 0);
    writeU16(payload, 6, 0);
    writeU32(payload, 8, 0);
    writeU32(payload, 12, 0);

    for (usize index = 0; index < desc.nodes.size(); ++index)
    {
        const PrefabNodeDesc& node = desc.nodes[index];
        const usize base = PrefabWire::HeaderBytes + index * PrefabWire::NodeBytes;
        writeU32(payload, base + 0, node.stableNodeId);
        writeI32(payload, base + 4, node.parentIndex);
        writeF32(payload, base + 8, node.positionX);
        writeF32(payload, base + 12, node.positionY);
        writeF32(payload, base + 16, node.positionZ);
        writeF32(payload, base + 20, node.rotationX);
        writeF32(payload, base + 24, node.rotationY);
        writeF32(payload, base + 28, node.rotationZ);
        writeF32(payload, base + 32, node.rotationW);
        writeF32(payload, base + 36, node.scaleX);
        writeF32(payload, base + 40, node.scaleY);
        writeF32(payload, base + 44, node.scaleZ);
        const u16 flags = node.visible ? 0U : PrefabWire::FlagHidden;
        writeU16(payload, base + 48, flags);
        writeU16(payload, base + 50, 0);
        writeAssetId(payload, base + 52, node.meshId);
        writeAssetId(payload, base + 68, node.materialId);
    }
    return payload;
}

Core::Result<PrefabPayloadView> parsePrefabPayload(std::span<const std::byte> payload,
                                                   std::vector<PrefabNodeView>& nodeStorage)
{
    if (payload.size() < PrefabWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab payload shorter than header");
    }
    const u16 schema = readU16(payload, 0);
    if (schema != PrefabWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported prefab schemaVersion");
    }
    const u16 nodeCount = readU16(payload, 2);
    if (nodeCount == 0 || nodeCount > PrefabWire::MaxNodes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab nodeCount is invalid");
    }
    if (readU16(payload, 4) != 0 || readU16(payload, 6) != 0 || readU32(payload, 8) != 0 || readU32(payload, 12) != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab header reserved fields must be zero");
    }
    const usize expected =
        PrefabWire::HeaderBytes + static_cast<usize>(nodeCount) * PrefabWire::NodeBytes;
    if (payload.size() != expected)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab payload size mismatch");
    }

    nodeStorage.clear();
    nodeStorage.reserve(nodeCount);
    for (u16 index = 0; index < nodeCount; ++index)
    {
        const usize base = PrefabWire::HeaderBytes + static_cast<usize>(index) * PrefabWire::NodeBytes;
        PrefabNodeView node{};
        node.stableNodeId = readU32(payload, base + 0);
        node.parentIndex = readI32(payload, base + 4);
        if (node.parentIndex < -1 || node.parentIndex >= static_cast<i32>(index))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab parentIndex is invalid");
        }
        node.positionX = readF32(payload, base + 8);
        node.positionY = readF32(payload, base + 12);
        node.positionZ = readF32(payload, base + 16);
        node.rotationX = readF32(payload, base + 20);
        node.rotationY = readF32(payload, base + 24);
        node.rotationZ = readF32(payload, base + 28);
        node.rotationW = readF32(payload, base + 32);
        node.scaleX = readF32(payload, base + 36);
        node.scaleY = readF32(payload, base + 40);
        node.scaleZ = readF32(payload, base + 44);
        if (!isFiniteVec3(node.positionX, node.positionY, node.positionZ) ||
            !isFiniteQuat(node.rotationX, node.rotationY, node.rotationZ, node.rotationW) ||
            !isFiniteVec3(node.scaleX, node.scaleY, node.scaleZ))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node transform must be finite");
        }
        const u16 flags = readU16(payload, base + 48);
        if ((flags & static_cast<u16>(~PrefabWire::FlagHidden)) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node flags are invalid");
        }
        node.meshId = readAssetId(payload, base + 52);
        node.materialId = readAssetId(payload, base + 68);
        node.hasMesh = static_cast<bool>(node.meshId);
        node.hasMaterial = static_cast<bool>(node.materialId);
        node.visible = (flags & PrefabWire::FlagHidden) == 0U;
        if (node.hasMesh != node.hasMaterial)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab mesh and material AssetIds must both be present or absent");
        }
        if (node.hasMesh && node.meshId == node.materialId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab mesh and material AssetIds must be distinct");
        }
        for (const PrefabNodeView& previous : nodeStorage)
        {
            if ((node.meshId && node.meshId == previous.materialId) ||
                (node.materialId && node.materialId == previous.meshId))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "prefab AssetId cannot be used as both mesh and material");
            }
        }
        if (readU16(payload, base + 50) != 0)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node reserved fields must be zero");
        }
        nodeStorage.push_back(node);
    }

    PrefabPayloadView view{};
    view.schemaVersion = schema;
    view.nodes = nodeStorage;
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedPrefabAsset(Core::AssetId assetId, const PrefabPayloadDesc& desc,
                                                            TargetPlatform platform)
{
    auto payload = writePrefabPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(payload.error());
    }
    std::vector<CookedAssetWriteDependency> deps;
    deps.reserve(desc.nodes.size() * 2U);
    for (const PrefabNodeDesc& node : desc.nodes)
    {
        if (static_cast<bool>(node.meshId))
        {
            deps.push_back(CookedAssetWriteDependency{
                .assetId = node.meshId,
                .expectedKind = AssetKind::StaticMesh,
                .flags = DependencyFlags::Required,
            });
        }
        if (static_cast<bool>(node.materialId))
        {
            deps.push_back(CookedAssetWriteDependency{
                .assetId = node.materialId,
                .expectedKind = AssetKind::Material,
                .flags = DependencyFlags::Required,
            });
        }
    }
    std::sort(deps.begin(), deps.end(), [](const CookedAssetWriteDependency& left,
                                           const CookedAssetWriteDependency& right) {
        return left.assetId < right.assetId;
    });
    const auto conflicting = std::adjacent_find(
        deps.begin(), deps.end(), [](const CookedAssetWriteDependency& left,
                                     const CookedAssetWriteDependency& right) {
            return left.assetId == right.assetId && left.expectedKind != right.expectedKind;
        });
    if (conflicting != deps.end())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "prefab AssetId cannot have conflicting dependency kinds");
    }
    deps.erase(std::unique(deps.begin(), deps.end(), [](const CookedAssetWriteDependency& left,
                                                        const CookedAssetWriteDependency& right) {
                   return left.assetId == right.assetId;
               }),
               deps.end());
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Prefab,
        .assetTypeVersion = PrefabWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .dependencies = deps,
        .payload = *payload,
        .payloadAlignment = 4,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
