#include <tina/asset_format/StaticMeshPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cmath>
#include <cstring>
#include <limits>

namespace Tina::AssetFormat {
namespace {

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
    u32 value = 0;
    for (usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
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
    for (usize index = 0; index < 4U; ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

[[nodiscard]] bool checkedMultiply(u32 a, u32 b, u32& out) noexcept
{
    const auto wide = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    if (wide > (std::numeric_limits<u32>::max)())
    {
        return false;
    }
    out = static_cast<u32>(wide);
    return true;
}

[[nodiscard]] bool checkedAdd(u32 a, u32 b, u32& out) noexcept
{
    const auto wide = static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b);
    if (wide > (std::numeric_limits<u32>::max)())
    {
        return false;
    }
    out = static_cast<u32>(wide);
    return true;
}

[[nodiscard]] u32 floatsPerVertex(StaticMeshVertexLayout layout) noexcept
{
    switch (layout)
    {
    case StaticMeshVertexLayout::P3N3UV2:
        return StaticMeshWire::P3N3UV2FloatsPerVertex;
    case StaticMeshVertexLayout::P3N3T4UV2:
        return StaticMeshWire::P3N3T4UV2FloatsPerVertex;
    default:
        return 0;
    }
}

[[nodiscard]] Core::Status validateMeshGeometry(u32 vertexCount, u32 indexCount, u16 submeshCount) noexcept
{
    if (vertexCount == 0 || vertexCount > StaticMeshWire::MaxVertexCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh vertexCount out of range");
    }
    if (indexCount == 0 || indexCount > StaticMeshWire::MaxIndexCount || (indexCount % 3U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh indexCount must be a positive multiple of 3");
    }
    if (submeshCount == 0 || submeshCount > StaticMeshWire::MaxSubmeshes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh submeshCount out of range");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateSubmeshes(std::span<const StaticMeshSubmeshDesc> submeshes, u32 indexCount) noexcept
{
    for (const StaticMeshSubmeshDesc& submesh : submeshes)
    {
        if (submesh.indexCount == 0 || (submesh.indexCount % 3U) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh submesh indexCount invalid");
        }
        if (submesh.firstIndex > indexCount ||
            submesh.indexCount > indexCount - submesh.firstIndex)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh submesh range out of bounds");
        }
        if (submesh.reserved != 0)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh submesh reserved must be zero");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateIndices(std::span<const u16> indices, u32 vertexCount) noexcept
{
    for (const u16 index : indices)
    {
        if (static_cast<u32>(index) >= vertexCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh index out of vertex range");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateVertices(StaticMeshVertexLayout layout,
                                            std::span<const float> vertices) noexcept
{
    for (const float value : vertices)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh vertices must be finite");
        }
    }
    if (layout == StaticMeshVertexLayout::P3N3T4UV2)
    {
        for (usize base = 0; base < vertices.size(); base += StaticMeshWire::P3N3T4UV2FloatsPerVertex)
        {
            const float lengthSquared = vertices[base + 6U] * vertices[base + 6U] +
                                        vertices[base + 7U] * vertices[base + 7U] +
                                        vertices[base + 8U] * vertices[base + 8U];
            if (!(lengthSquared > 1.0e-12F))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "static mesh tangent xyz must be normalizable");
            }
            if (vertices[base + 9U] != -1.0F && vertices[base + 9U] != 1.0F)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "static mesh tangent handedness must be -1 or +1");
            }
        }
    }
    return Core::success();
}

} // namespace

Core::Result<std::vector<std::byte>> writeStaticMeshPayloadBytes(const StaticMeshPayloadDesc& desc)
{
    const u32 vertexStrideFloats = floatsPerVertex(desc.vertexLayout);
    if (vertexStrideFloats == 0)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported static mesh vertex layout");
    }
    if (desc.indexType != StaticMeshIndexType::U16)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported static mesh index type");
    }
    if (!std::isfinite(desc.boundsCenterX) || !std::isfinite(desc.boundsCenterY) ||
        !std::isfinite(desc.boundsCenterZ) || !std::isfinite(desc.boundsRadius) ||
        !(desc.boundsRadius > 0.0F))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh bounds invalid");
    }

    if (desc.vertices.size() % vertexStrideFloats != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "static mesh vertex float count does not match vertex layout");
    }
    const usize vertexCountWide = desc.vertices.size() / vertexStrideFloats;
    if (vertexCountWide > StaticMeshWire::MaxVertexCount ||
        desc.indices.size() > StaticMeshWire::MaxIndexCount ||
        desc.submeshes.size() > StaticMeshWire::MaxSubmeshes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "static mesh geometry exceeds format limits");
    }
    const u32 vertexCount = static_cast<u32>(vertexCountWide);
    const u32 indexCount = static_cast<u32>(desc.indices.size());
    const u16 submeshCount = static_cast<u16>(desc.submeshes.size());

    if (Core::Status status = validateMeshGeometry(vertexCount, indexCount, submeshCount); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = validateSubmeshes(desc.submeshes, indexCount); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = validateVertices(desc.vertexLayout, desc.vertices); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = validateIndices(desc.indices, vertexCount); !status)
    {
        return Core::failure(status.error());
    }

    u32 vertexBytes = 0;
    u32 vertexStrideBytes = 0;
    if (!checkedMultiply(vertexStrideFloats, static_cast<u32>(sizeof(float)), vertexStrideBytes) ||
        !checkedMultiply(vertexCount, vertexStrideBytes, vertexBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh vertex bytes overflow");
    }
    u32 indexBytes = 0;
    if (!checkedMultiply(indexCount, static_cast<u32>(sizeof(u16)), indexBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh index bytes overflow");
    }
    u32 submeshBytes = 0;
    if (!checkedMultiply(submeshCount, StaticMeshWire::SubmeshBytes, submeshBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh submesh bytes overflow");
    }

    u32 total = StaticMeshWire::HeaderBytes;
    if (!checkedAdd(total, submeshBytes, total) || !checkedAdd(total, vertexBytes, total) ||
        !checkedAdd(total, indexBytes, total))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh payload size overflow");
    }

    try
    {
        std::vector<std::byte> bytes(total, std::byte{0});
        writeU16(bytes, 0U, StaticMeshWire::SchemaVersion);
        writeU16(bytes, 2U, static_cast<u16>(desc.vertexLayout));
        writeU16(bytes, 4U, static_cast<u16>(desc.indexType));
        writeU16(bytes, 6U, submeshCount);
        writeU32(bytes, 8U, vertexCount);
        writeU32(bytes, 12U, indexCount);
        writeF32(bytes, 16U, desc.boundsCenterX);
        writeF32(bytes, 20U, desc.boundsCenterY);
        writeF32(bytes, 24U, desc.boundsCenterZ);
        writeF32(bytes, 28U, desc.boundsRadius);

        usize offset = StaticMeshWire::HeaderBytes;
        for (const StaticMeshSubmeshDesc& submesh : desc.submeshes)
        {
            writeU32(bytes, offset + 0U, submesh.firstIndex);
            writeU32(bytes, offset + 4U, submesh.indexCount);
            writeU32(bytes, offset + 8U, submesh.materialSlot);
            writeU32(bytes, offset + 12U, submesh.reserved);
            offset += StaticMeshWire::SubmeshBytes;
        }

        std::memcpy(bytes.data() + offset, desc.vertices.data(), vertexBytes);
        offset += vertexBytes;
        std::memcpy(bytes.data() + offset, desc.indices.data(), indexBytes);
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "static mesh payload allocation failed");
    }
}

Core::Result<StaticMeshPayloadView> parseStaticMeshPayload(std::span<const std::byte> payload)
{
    if (payload.size() < StaticMeshWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh payload too short");
    }

    StaticMeshPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.vertexLayout = static_cast<StaticMeshVertexLayout>(readU16(payload, 2U));
    view.indexType = static_cast<StaticMeshIndexType>(readU16(payload, 4U));
    view.submeshCount = readU16(payload, 6U);
    view.vertexCount = readU32(payload, 8U);
    view.indexCount = readU32(payload, 12U);
    view.boundsCenterX = readF32(payload, 16U);
    view.boundsCenterY = readF32(payload, 20U);
    view.boundsCenterZ = readF32(payload, 24U);
    view.boundsRadius = readF32(payload, 28U);

    if (view.schemaVersion != StaticMeshWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported static mesh schema version");
    }
    const u32 vertexStrideFloats = floatsPerVertex(view.vertexLayout);
    if (vertexStrideFloats == 0)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported static mesh vertex layout");
    }
    if (view.indexType != StaticMeshIndexType::U16)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported static mesh index type");
    }
    if (!std::isfinite(view.boundsCenterX) || !std::isfinite(view.boundsCenterY) ||
        !std::isfinite(view.boundsCenterZ) || !std::isfinite(view.boundsRadius) ||
        !(view.boundsRadius > 0.0F))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh bounds invalid");
    }
    if (Core::Status status = validateMeshGeometry(view.vertexCount, view.indexCount, view.submeshCount); !status)
    {
        return Core::failure(status.error());
    }

    u32 submeshBytes = 0;
    if (!checkedMultiply(view.submeshCount, StaticMeshWire::SubmeshBytes, submeshBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh submesh bytes overflow");
    }
    u32 vertexBytes = 0;
    u32 vertexStrideBytes = 0;
    if (!checkedMultiply(vertexStrideFloats, static_cast<u32>(sizeof(float)), vertexStrideBytes) ||
        !checkedMultiply(view.vertexCount, vertexStrideBytes, vertexBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh vertex bytes overflow");
    }
    u32 indexBytes = 0;
    if (!checkedMultiply(view.indexCount, static_cast<u32>(sizeof(u16)), indexBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh index bytes overflow");
    }
    u32 expected = StaticMeshWire::HeaderBytes;
    if (!checkedAdd(expected, submeshBytes, expected) || !checkedAdd(expected, vertexBytes, expected) ||
        !checkedAdd(expected, indexBytes, expected))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "static mesh payload size overflow");
    }
    if (payload.size() != expected)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh payload size mismatch");
    }

    const usize submeshOffset = StaticMeshWire::HeaderBytes;
    const auto* submeshPtr =
        reinterpret_cast<const StaticMeshSubmeshView*>(payload.data() + submeshOffset);
    view.submeshes = std::span<const StaticMeshSubmeshView>{submeshPtr, view.submeshCount};

    for (const StaticMeshSubmeshView& submesh : view.submeshes)
    {
        StaticMeshSubmeshDesc desc{
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .materialSlot = submesh.materialSlot,
            .reserved = submesh.reserved,
        };
        if (Core::Status status = validateSubmeshes(std::span<const StaticMeshSubmeshDesc>{&desc, 1}, view.indexCount);
            !status)
        {
            return Core::failure(status.error());
        }
    }

    const usize vertexOffset = submeshOffset + submeshBytes;
    if ((vertexOffset % alignof(float)) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh vertex alignment invalid");
    }
    const auto* vertexPtr = reinterpret_cast<const float*>(payload.data() + vertexOffset);
    view.vertices =
        std::span<const float>{vertexPtr, static_cast<usize>(view.vertexCount) * vertexStrideFloats};
    if (Core::Status status = validateVertices(view.vertexLayout, view.vertices); !status)
    {
        return Core::failure(status.error());
    }

    const usize indexOffset = vertexOffset + vertexBytes;
    if ((indexOffset % alignof(u16)) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "static mesh index alignment invalid");
    }
    const auto* indexPtr = reinterpret_cast<const u16*>(payload.data() + indexOffset);
    view.indices = std::span<const u16>{indexPtr, view.indexCount};
    if (Core::Status status = validateIndices(view.indices, view.vertexCount); !status)
    {
        return Core::failure(status.error());
    }

    return view;
}

Core::Result<std::vector<std::byte>> writeCookedStaticMeshAsset(Core::AssetId assetId, const StaticMeshPayloadDesc& desc,
                                                                TargetPlatform platform)
{
    auto payload = writeStaticMeshPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(payload.error());
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::StaticMesh,
        .assetTypeVersion = StaticMeshWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

StaticMeshPayloadDesc makeCanonicalUnitCubeMeshDesc(std::span<StaticMeshSubmeshDesc, 1> submeshStorage,
                                                    std::span<float> vertexStorage,
                                                    std::span<Core::u16> indexStorage) noexcept
{
    // Matches BgfxOpaque3DGeometry canonical cube (half-extent 1, 24 verts / 36 indices).
    static constexpr float kVertices[] = {
        // +Z
        -1, -1, 1, 0, 0, 1, 0, 1, 1, -1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, -1, 1, 1, 0, 0, 1, 0, 0,
        // -Z
        1, -1, -1, 0, 0, -1, 0, 1, -1, -1, -1, 0, 0, -1, 1, 1, -1, 1, -1, 0, 0, -1, 1, 0, 1, 1, -1, 0, 0, -1, 0, 0,
        // +X
        1, -1, 1, 1, 0, 0, 0, 1, 1, -1, -1, 1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0,
        // -X
        -1, -1, -1, -1, 0, 0, 0, 1, -1, -1, 1, -1, 0, 0, 1, 1, -1, 1, 1, -1, 0, 0, 1, 0, -1, 1, -1, -1, 0, 0, 0, 0,
        // +Y
        -1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, -1, 0, 1, 0, 1, 0, -1, 1, -1, 0, 1, 0, 0, 0,
        // -Y
        -1, -1, -1, 0, -1, 0, 0, 1, 1, -1, -1, 0, -1, 0, 1, 1, 1, -1, 1, 0, -1, 0, 1, 0, -1, -1, 1, 0, -1, 0, 0, 0,
    };
    static constexpr u16 kIndices[] = {
        0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };

    StaticMeshPayloadDesc desc{};
    if (vertexStorage.size() < std::size(kVertices) || indexStorage.size() < std::size(kIndices))
    {
        return desc;
    }
    std::memcpy(vertexStorage.data(), kVertices, sizeof(kVertices));
    std::memcpy(indexStorage.data(), kIndices, sizeof(kIndices));
    submeshStorage[0] = StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 36, .materialSlot = 0, .reserved = 0};
    desc.vertexLayout = StaticMeshVertexLayout::P3N3UV2;
    desc.indexType = StaticMeshIndexType::U16;
    desc.boundsCenterX = 0.0F;
    desc.boundsCenterY = 0.0F;
    desc.boundsCenterZ = 0.0F;
    desc.boundsRadius = std::sqrt(3.0F); // corner of unit half-extent cube
    desc.submeshes = submeshStorage;
    desc.vertices = std::span<const float>{vertexStorage.data(), std::size(kVertices)};
    desc.indices = std::span<const u16>{indexStorage.data(), std::size(kIndices)};
    return desc;
}

} // namespace Tina::AssetFormat
