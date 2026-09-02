#include <tina/asset_format/PrefabPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
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
    return std::bit_cast<float>(readU32(bytes, offset));
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
    writeU32(bytes, offset, std::bit_cast<u32>(value));
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

[[nodiscard]] bool bytesAreZero(std::span<const std::byte> bytes, usize offset,
                                usize size) noexcept
{
    return std::all_of(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size),
        [](std::byte value) { return value == std::byte{0}; });
}

[[nodiscard]] bool isFiniteVec3(float x, float y, float z) noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

[[nodiscard]] bool isFiniteQuat(float x, float y, float z, float w) noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
}

[[nodiscard]] bool isNonZeroQuat(float x, float y, float z, float w) noexcept
{
    const double lengthSquared = static_cast<double>(x) * x + static_cast<double>(y) * y +
                                 static_cast<double>(z) * z + static_cast<double>(w) * w;
    return std::isfinite(lengthSquared) && lengthSquared > 1.0e-12;
}

[[nodiscard]] Core::Status validateName(std::string_view name) noexcept
{
    if (name.size() > PrefabWire::MaximumNameBytes ||
        (!name.empty() && !Core::isStrictUtf8WithoutNul(name))) {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "prefab node name must be valid UTF-8 and fit the wire field");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateCamera(
    const PrefabCamera3DDesc& camera) noexcept
{
    constexpr float Pi = 3.14159265358979323846F;
    if (!std::isfinite(camera.verticalFovRadians) ||
        !(camera.verticalFovRadians > 0.0F) ||
        !(camera.verticalFovRadians < Pi) ||
        !std::isfinite(camera.nearPlane) || !(camera.nearPlane > 0.0F) ||
        !std::isfinite(camera.farPlane) ||
        !(camera.farPlane > camera.nearPlane))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "prefab Camera3D projection values are invalid");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateLight(PrefabNodeKind kind,
                                         const PrefabLight3DDesc& light) noexcept
{
    constexpr float HalfPi = 1.57079632679489661923F;
    if (!std::isfinite(light.colorRed) || light.colorRed < 0.0F ||
        !std::isfinite(light.colorGreen) || light.colorGreen < 0.0F ||
        !std::isfinite(light.colorBlue) || light.colorBlue < 0.0F ||
        !std::isfinite(light.colorAlpha) || light.colorAlpha != 1.0F ||
        !std::isfinite(light.intensity) || light.intensity < 0.0F)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "prefab 3D light color or intensity is invalid");
    }
    if ((kind == PrefabNodeKind::PointLight3D ||
         kind == PrefabNodeKind::SpotLight3D) &&
        (!std::isfinite(light.rangeMeters) || !(light.rangeMeters > 0.0F)))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "prefab local 3D light range must be positive");
    }
    if (kind == PrefabNodeKind::SpotLight3D &&
        (!std::isfinite(light.innerConeRadians) ||
         !std::isfinite(light.outerConeRadians) ||
         light.innerConeRadians < 0.0F ||
         !(light.innerConeRadians < light.outerConeRadians) ||
        !(light.outerConeRadians < HalfPi)))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "prefab SpotLight3D cone angles are invalid");
    }
    return Core::success();
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
        if (node.stableNodeId == 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                 "prefab stableNodeId must be non-zero");
        }
        if (auto status = validateName(node.name); !status) {
            return status;
        }
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
        if (!isNonZeroQuat(node.rotationX, node.rotationY, node.rotationZ, node.rotationW))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node rotation must be non-zero");
        }
        if (static_cast<u16>(node.nodeKind) >= PrefabNodeKindCount)
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                 "prefab node kind is unsupported");
        }
        if (static_cast<bool>(node.meshId) != static_cast<bool>(node.materialId))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab mesh and material AssetIds must both be present or absent");
        }
        const bool hasMesh = static_cast<bool>(node.meshId);
        const bool hasCamera = node.camera.has_value();
        const bool hasLight = node.light.has_value();
        bool payloadMatchesKind = false;
        switch (node.nodeKind)
        {
        case PrefabNodeKind::Node3D:
        case PrefabNodeKind::Marker3D:
            payloadMatchesKind = !hasMesh && !hasCamera && !hasLight;
            break;
        case PrefabNodeKind::Mesh3D:
        case PrefabNodeKind::SkinnedMesh3D:
            payloadMatchesKind = hasMesh && !hasCamera && !hasLight;
            break;
        case PrefabNodeKind::Camera3D:
            payloadMatchesKind = !hasMesh && hasCamera && !hasLight;
            break;
        case PrefabNodeKind::DirectionalLight3D:
        case PrefabNodeKind::PointLight3D:
        case PrefabNodeKind::SpotLight3D:
            payloadMatchesKind = !hasMesh && !hasCamera && hasLight;
            break;
        }
        if (!payloadMatchesKind)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab node kind and typed payload do not match");
        }
        if (node.camera)
        {
            if (auto status = validateCamera(*node.camera); !status)
            {
                return status;
            }
        }
        if (node.light)
        {
            if (auto status = validateLight(node.nodeKind, *node.light); !status)
            {
                return status;
            }
        }
        if (node.meshId && node.meshId == node.materialId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "prefab mesh and material AssetIds must be distinct");
        }
        for (usize previousIndex = 0; previousIndex < index; ++previousIndex)
        {
            const PrefabNodeDesc& previous = desc.nodes[previousIndex];
            if (node.stableNodeId == previous.stableNodeId)
            {
                return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                     "prefab stableNodeId values must be unique");
            }
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
        writeU16(payload, base + 50, static_cast<u16>(node.nodeKind));
        writeAssetId(payload, base + 52, node.meshId);
        writeAssetId(payload, base + 68, node.materialId);
        for (usize nameIndex = 0; nameIndex < node.name.size(); ++nameIndex) {
            writeU8(payload, base + PrefabWire::NameOffset + nameIndex,
                    static_cast<u8>(node.name[nameIndex]));
        }
        if (node.camera)
        {
            writeF32(payload, base + 84, node.camera->verticalFovRadians);
            writeF32(payload, base + 88, node.camera->nearPlane);
            writeF32(payload, base + 92, node.camera->farPlane);
            writeU8(payload, base + 96, node.camera->active ? 1U : 0U);
        }
        if (node.light)
        {
            writeF32(payload, base + 84, node.light->colorRed);
            writeF32(payload, base + 88, node.light->colorGreen);
            writeF32(payload, base + 92, node.light->colorBlue);
            writeF32(payload, base + 96, node.light->colorAlpha);
            writeF32(payload, base + 100, node.light->intensity);
            writeF32(payload, base + 104, node.light->rangeMeters);
            writeF32(payload, base + 108, node.light->innerConeRadians);
            writeF32(payload, base + 112, node.light->outerConeRadians);
            writeU8(payload, base + 116, node.light->active ? 1U : 0U);
        }
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

    try
    {
        std::vector<PrefabNodeView> parsed;
        parsed.reserve(nodeCount);
        for (u16 index = 0; index < nodeCount; ++index)
        {
            const usize base = PrefabWire::HeaderBytes + static_cast<usize>(index) * PrefabWire::NodeBytes;
            PrefabNodeView node{};
            node.stableNodeId = readU32(payload, base + 0);
            if (node.stableNodeId == 0U)
            {
                return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                     "prefab stableNodeId must be non-zero");
            }
            node.parentIndex = readI32(payload, base + 4);
            if (node.parentIndex < -1 || node.parentIndex >= static_cast<i32>(index))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab parentIndex is invalid");
            }
            node.nodeKind = static_cast<PrefabNodeKind>(readU16(payload, base + 50));
            if (static_cast<u16>(node.nodeKind) >= PrefabNodeKindCount)
            {
                return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                     "prefab node kind is unsupported");
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
            usize nameLength = 0U;
            while (nameLength < PrefabWire::NameBytes &&
                   payload[base + PrefabWire::NameOffset + nameLength] != std::byte{0}) {
                ++nameLength;
            }
            if (nameLength == PrefabWire::NameBytes ||
                !bytesAreZero(payload, base + PrefabWire::NameOffset + nameLength + 1U,
                              PrefabWire::NameBytes - nameLength - 1U)) {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "prefab node name field is not NUL-terminated or zero-padded");
            }
            node.name.assign(
                reinterpret_cast<const char*>(payload.data() + base + PrefabWire::NameOffset),
                nameLength);
            if (auto status = validateName(node.name); !status) {
                return Core::failure(std::move(status.error()));
            }
            if (!isFiniteVec3(node.positionX, node.positionY, node.positionZ) ||
                !isFiniteQuat(node.rotationX, node.rotationY, node.rotationZ, node.rotationW) ||
                !isFiniteVec3(node.scaleX, node.scaleY, node.scaleZ))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout, "prefab node transform must be finite");
            }
            if (!isNonZeroQuat(node.rotationX, node.rotationY, node.rotationZ, node.rotationW))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "prefab node rotation must be non-zero");
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
            switch (node.nodeKind)
            {
            case PrefabNodeKind::Node3D:
            case PrefabNodeKind::Marker3D:
            case PrefabNodeKind::Mesh3D:
            case PrefabNodeKind::SkinnedMesh3D:
                if (!bytesAreZero(payload, base + 84, PrefabWire::NameOffset - 84U))
                {
                    return Core::failure(
                        AssetFormatErrorCode::InvalidLayout,
                        "prefab node has a non-canonical absent typed payload");
                }
                break;
            case PrefabNodeKind::Camera3D: {
                PrefabCamera3DDesc camera{
                    .verticalFovRadians = readF32(payload, base + 84),
                    .nearPlane = readF32(payload, base + 88),
                    .farPlane = readF32(payload, base + 92),
                };
                const u8 active = readU8(payload, base + 96);
                if (active > 1U ||
                    !bytesAreZero(payload, base + 97,
                                  PrefabWire::NameOffset - 97U))
                {
                    return Core::failure(
                        AssetFormatErrorCode::InvalidLayout,
                        "prefab Camera3D active or reserved bytes are invalid");
                }
                camera.active = active != 0U;
                if (auto status = validateCamera(camera); !status)
                {
                    return Core::failure(std::move(status.error()));
                }
                node.camera = camera;
                break;
            }
            case PrefabNodeKind::DirectionalLight3D:
            case PrefabNodeKind::PointLight3D:
            case PrefabNodeKind::SpotLight3D: {
                PrefabLight3DDesc light{
                    .colorRed = readF32(payload, base + 84),
                    .colorGreen = readF32(payload, base + 88),
                    .colorBlue = readF32(payload, base + 92),
                    .colorAlpha = readF32(payload, base + 96),
                    .intensity = readF32(payload, base + 100),
                    .rangeMeters = readF32(payload, base + 104),
                    .innerConeRadians = readF32(payload, base + 108),
                    .outerConeRadians = readF32(payload, base + 112),
                };
                const u8 active = readU8(payload, base + 116);
                if (active > 1U ||
                    !bytesAreZero(payload, base + 117,
                                  PrefabWire::NameOffset - 117U))
                {
                    return Core::failure(
                        AssetFormatErrorCode::InvalidLayout,
                        "prefab 3D light active or reserved bytes are invalid");
                }
                light.active = active != 0U;
                if (auto status = validateLight(node.nodeKind, light); !status)
                {
                    return Core::failure(std::move(status.error()));
                }
                node.light = light;
                break;
            }
            }
            const bool payloadMatchesKind =
                ((node.nodeKind == PrefabNodeKind::Node3D ||
                  node.nodeKind == PrefabNodeKind::Marker3D) && !node.hasMesh) ||
                ((node.nodeKind == PrefabNodeKind::Mesh3D ||
                  node.nodeKind == PrefabNodeKind::SkinnedMesh3D) && node.hasMesh) ||
                (node.nodeKind == PrefabNodeKind::Camera3D && !node.hasMesh) ||
                ((node.nodeKind == PrefabNodeKind::DirectionalLight3D ||
                  node.nodeKind == PrefabNodeKind::PointLight3D ||
                  node.nodeKind == PrefabNodeKind::SpotLight3D) && !node.hasMesh);
            if (!payloadMatchesKind)
            {
                return Core::failure(
                    AssetFormatErrorCode::InvalidLayout,
                    "prefab node kind and asset payload do not match");
            }
            for (const PrefabNodeView& previous : parsed)
            {
                if (node.stableNodeId == previous.stableNodeId)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                         "prefab stableNodeId values must be unique");
                }
                if ((node.meshId && node.meshId == previous.materialId) ||
                    (node.materialId && node.materialId == previous.meshId))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "prefab AssetId cannot be used as both mesh and material");
                }
            }
            parsed.push_back(node);
        }

        nodeStorage.swap(parsed);
        PrefabPayloadView view{};
        view.schemaVersion = schema;
        view.nodes = nodeStorage;
        return view;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "prefab payload parsing allocation failed");
    }
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
                .expectedKind =
                    node.nodeKind == PrefabNodeKind::SkinnedMesh3D
                        ? AssetKind::SkinnedMesh
                        : AssetKind::StaticMesh,
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
