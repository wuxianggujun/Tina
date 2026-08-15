#include <tina/asset_format/SkinnedMeshPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u64;
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
    const u32 bits = readU32(bytes, offset);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value) noexcept
{
    for (usize index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value) noexcept
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(bytes, offset, bits);
}

struct LayoutCounts final {
    u16 jointCount = 0;
    u16 submeshCount = 0;
    u32 vertexCount = 0;
    u32 indexCount = 0;
};

struct LayoutOffsets final {
    u64 inverseBindOffset = 0;
    u64 jointOffset = 0;
    u64 submeshOffset = 0;
    u64 vertexOffset = 0;
    u64 jointIndexOffset = 0;
    u64 jointWeightOffset = 0;
    u64 indexOffset = 0;
    u64 totalBytes = 0;
};

// Derives every block offset from the counts, so the writer and the parser can never
// disagree about the layout and a lying header is caught by the exact size check.
[[nodiscard]] Core::Result<LayoutOffsets> validateLayout(const LayoutCounts& counts)
{
    if (counts.jointCount == 0U || counts.jointCount > SkinnedMeshWire::MaxJointCount)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "skinned mesh joint count exceeds MaxJointCount");
    }
    if (counts.submeshCount == 0U || counts.submeshCount > SkinnedMeshWire::MaxSubmeshes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "skinned mesh submesh count exceeds MaxSubmeshes");
    }
    if (counts.vertexCount == 0U || counts.vertexCount > SkinnedMeshWire::MaxVertexCount)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "skinned mesh vertex count exceeds MaxVertexCount");
    }
    if (counts.indexCount == 0U || counts.indexCount > SkinnedMeshWire::MaxIndexCount)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "skinned mesh index count exceeds MaxIndexCount");
    }
    if ((counts.indexCount % 3U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh index count must be a multiple of three");
    }

    // Every term is bounded by the checks above, so u64 accumulation cannot overflow:
    // the widest total is 48 + 108*256 + 16*64 + 64*65535 + 2*4194304 bytes.
    LayoutOffsets offsets{};
    const auto joints = static_cast<u64>(counts.jointCount);
    const auto vertices = static_cast<u64>(counts.vertexCount);
    offsets.inverseBindOffset = SkinnedMeshWire::HeaderBytes;
    offsets.jointOffset =
        offsets.inverseBindOffset + (joints * SkinnedMeshWire::InverseBindMatrixBytes);
    offsets.submeshOffset = offsets.jointOffset + (joints * SkinnedMeshWire::JointBytes);
    offsets.vertexOffset =
        offsets.submeshOffset + (static_cast<u64>(counts.submeshCount) * SkinnedMeshWire::SubmeshBytes);
    offsets.jointIndexOffset =
        offsets.vertexOffset + (vertices * SkinnedMeshWire::FloatsPerVertex * sizeof(float));
    offsets.jointWeightOffset =
        offsets.jointIndexOffset + (vertices * SkinnedMeshWire::InfluencesPerVertex * sizeof(u16));
    offsets.indexOffset =
        offsets.jointWeightOffset + (vertices * SkinnedMeshWire::InfluencesPerVertex * sizeof(u16));
    offsets.totalBytes = offsets.indexOffset + (static_cast<u64>(counts.indexCount) * sizeof(u16));

    if (offsets.totalBytes > Wire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "skinned mesh payload exceeds the cooked payload limit");
    }
    return offsets;
}

[[nodiscard]] Core::Status validateBounds(float centerX, float centerY, float centerZ, float radius) noexcept
{
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(centerZ) ||
        !std::isfinite(radius) || !(radius > 0.0F))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "skinned mesh bounds invalid");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateInverseBindMatrices(std::span<const float> matrices) noexcept
{
    for (const float value : matrices)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh inverse bind matrices must be finite");
        }
    }
    return Core::success();
}

// Shared by the desc and the wire paths so both reject the same joint records.
[[nodiscard]] Core::Status validateJoint(const SkinnedMeshJointDesc& joint, u16 jointIndex) noexcept
{
    if (joint.reserved != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh reserved fields must be zero");
    }
    if (joint.parentJoint != SkinnedMeshWire::JointIndexNone && joint.parentJoint >= jointIndex)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh joint parents must precede their children");
    }
    for (const float value : joint.bindTranslation)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh bind translation must be finite");
        }
    }
    for (const float value : joint.bindScale)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh bind scale must be finite");
        }
    }
    for (const float value : joint.bindRotation)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh bind rotation must be finite");
        }
    }
    // Finite components can still square to inf, so the product is re-checked.
    const float lengthSquared = (joint.bindRotation[0] * joint.bindRotation[0]) +
                                (joint.bindRotation[1] * joint.bindRotation[1]) +
                                (joint.bindRotation[2] * joint.bindRotation[2]) +
                                (joint.bindRotation[3] * joint.bindRotation[3]);
    if (!std::isfinite(lengthSquared) || !(lengthSquared > 1.0e-12F))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh bind rotation must be normalizable");
    }
    return Core::success();
}

// Weights are validated as an exact integer sum rather than a float epsilon, which makes
// the normalization rule unambiguous and the encoding canonical for content hashing.
[[nodiscard]] Core::Status validateInfluences(std::span<const u16> jointIndices,
                                              std::span<const u16> jointWeights, u32 vertexCount,
                                              u16 jointCount) noexcept
{
    constexpr u32 influences = SkinnedMeshWire::InfluencesPerVertex;
    for (u32 vertex = 0; vertex < vertexCount; ++vertex)
    {
        const usize base = static_cast<usize>(vertex) * influences;
        u32 sum = 0;
        for (u32 slot = 0; slot < influences; ++slot)
        {
            // Widened before summing: four u16 weights reach 262140, past the u16 range.
            sum += static_cast<u32>(jointWeights[base + slot]);
        }
        if (sum != static_cast<u32>(SkinnedMeshWire::WeightScale))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh vertex weights must sum to 65535");
        }
        if (jointWeights[base] == 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh vertex requires at least one influence");
        }
        for (u32 slot = 0; slot < influences; ++slot)
        {
            if (jointIndices[base + slot] >= jointCount)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "skinned mesh joint index out of range");
            }
            if (jointWeights[base + slot] == 0U && jointIndices[base + slot] != 0U)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "skinned mesh zero-weight influences must use joint index zero");
            }
            for (u32 previous = 0; previous < slot && jointWeights[base + slot] != 0U; ++previous)
            {
                if (jointWeights[base + previous] != 0U &&
                    jointIndices[base + previous] == jointIndices[base + slot])
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "skinned mesh non-zero joint influences must be unique");
                }
            }
            if (slot + 1U == influences)
            {
                continue;
            }
            // Descending weight, ties broken by ascending joint index: the same influence
            // set always encodes to identical bytes.
            if (jointWeights[base + slot] < jointWeights[base + slot + 1U])
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "skinned mesh vertex influences must be sorted by descending weight");
            }
            if (jointWeights[base + slot] != 0U &&
                jointWeights[base + slot] == jointWeights[base + slot + 1U] &&
                jointIndices[base + slot] >= jointIndices[base + slot + 1U])
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "skinned mesh vertex influences must be sorted by descending weight");
            }
        }
    }
    return Core::success();
}

// Copied from StaticMeshPayload::validateVertices: the vertex block is the same record,
// so it must reject the same content.
[[nodiscard]] Core::Status validateVertices(std::span<const float> vertices) noexcept
{
    for (const float value : vertices)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "skinned mesh vertices must be finite");
        }
    }
    for (usize base = 0; base < vertices.size(); base += SkinnedMeshWire::FloatsPerVertex)
    {
        const float lengthSquared = vertices[base + 6U] * vertices[base + 6U] +
                                    vertices[base + 7U] * vertices[base + 7U] +
                                    vertices[base + 8U] * vertices[base + 8U];
        if (!std::isfinite(lengthSquared) || !(lengthSquared > 1.0e-12F))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh tangent xyz must be normalizable");
        }
        if (vertices[base + 9U] != -1.0F && vertices[base + 9U] != 1.0F)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh tangent handedness must be -1 or +1");
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
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "skinned mesh index out of range");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateSubmesh(u32 firstIndex, u32 indexCount, u32 reserved,
                                           u32 totalIndexCount) noexcept
{
    if (indexCount == 0U || (indexCount % 3U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "skinned mesh submesh indexCount invalid");
    }
    if (firstIndex > totalIndexCount || indexCount > totalIndexCount - firstIndex)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "skinned mesh submesh range out of bounds");
    }
    if (reserved != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh reserved fields must be zero");
    }
    return Core::success();
}

} // namespace

std::optional<SkinnedMeshJointView> SkinnedMeshPayloadView::joint(Core::u16 index) const noexcept
{
    if (index >= jointCount)
    {
        return std::nullopt;
    }
    const usize offset = static_cast<usize>(index) * SkinnedMeshWire::JointBytes;
    // The count field is validated against the physical extent, so a hand-built view
    // with a lying jointCount cannot read past the block.
    if (offset + SkinnedMeshWire::JointBytes > jointsBytes.size())
    {
        return std::nullopt;
    }
    SkinnedMeshJointView view{};
    view.parentJoint = readU16(jointsBytes, offset + 0U);
    view.reserved = readU16(jointsBytes, offset + 2U);
    for (usize element = 0; element < 3U; ++element)
    {
        view.bindTranslation[element] = readF32(jointsBytes, offset + 4U + (element * 4U));
    }
    for (usize element = 0; element < 4U; ++element)
    {
        view.bindRotation[element] = readF32(jointsBytes, offset + 16U + (element * 4U));
    }
    for (usize element = 0; element < 3U; ++element)
    {
        view.bindScale[element] = readF32(jointsBytes, offset + 32U + (element * 4U));
    }
    return view;
}

std::span<const float> SkinnedMeshPayloadView::inverseBindMatrix(Core::u16 index) const noexcept
{
    if (index >= jointCount)
    {
        return {};
    }
    const usize first = static_cast<usize>(index) * SkinnedMeshWire::FloatsPerInverseBindMatrix;
    if (first + SkinnedMeshWire::FloatsPerInverseBindMatrix > inverseBindMatrices.size())
    {
        return {};
    }
    return inverseBindMatrices.subspan(first, SkinnedMeshWire::FloatsPerInverseBindMatrix);
}

Core::Result<std::vector<std::byte>> writeSkinnedMeshPayloadBytes(const SkinnedMeshPayloadDesc& desc)
{
    constexpr u32 floatsPerVertex = SkinnedMeshWire::FloatsPerVertex;
    constexpr u32 influences = SkinnedMeshWire::InfluencesPerVertex;

    if (desc.indexType != StaticMeshIndexType::U16)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported skinned mesh index type");
    }
    if (Core::Status status =
            validateBounds(desc.boundsCenterX, desc.boundsCenterY, desc.boundsCenterZ, desc.boundsRadius);
        !status)
    {
        return Core::failure(status.error());
    }
    if (desc.vertices.size() % floatsPerVertex != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh vertex float count does not match vertex layout");
    }
    if (desc.joints.size() > SkinnedMeshWire::MaxJointCount ||
        desc.submeshes.size() > SkinnedMeshWire::MaxSubmeshes ||
        (desc.vertices.size() / floatsPerVertex) > SkinnedMeshWire::MaxVertexCount ||
        desc.indices.size() > SkinnedMeshWire::MaxIndexCount)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "skinned mesh geometry exceeds format limits");
    }

    const LayoutCounts counts{
        .jointCount = static_cast<u16>(desc.joints.size()),
        .submeshCount = static_cast<u16>(desc.submeshes.size()),
        .vertexCount = static_cast<u32>(desc.vertices.size() / floatsPerVertex),
        .indexCount = static_cast<u32>(desc.indices.size()),
    };
    auto offsets = validateLayout(counts);
    if (!offsets)
    {
        return Core::failure(offsets.error());
    }

    if (desc.inverseBindMatrices.size() !=
        static_cast<usize>(counts.jointCount) * SkinnedMeshWire::FloatsPerInverseBindMatrix)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh inverse bind matrix count does not match jointCount");
    }
    const usize expectedInfluenceEntries = static_cast<usize>(counts.vertexCount) * influences;
    if (desc.jointIndices.size() != expectedInfluenceEntries ||
        desc.jointWeights.size() != expectedInfluenceEntries)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh influence table sizes do not match vertexCount");
    }

    if (Core::Status status = validateInverseBindMatrices(desc.inverseBindMatrices); !status)
    {
        return Core::failure(status.error());
    }
    for (usize index = 0; index < desc.joints.size(); ++index)
    {
        if (Core::Status status = validateJoint(desc.joints[index], static_cast<u16>(index)); !status)
        {
            return Core::failure(status.error());
        }
    }
    for (const StaticMeshSubmeshDesc& submesh : desc.submeshes)
    {
        if (Core::Status status =
                validateSubmesh(submesh.firstIndex, submesh.indexCount, submesh.reserved, counts.indexCount);
            !status)
        {
            return Core::failure(status.error());
        }
    }
    if (Core::Status status = validateVertices(desc.vertices); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status =
            validateInfluences(desc.jointIndices, desc.jointWeights, counts.vertexCount, counts.jointCount);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = validateIndices(desc.indices, counts.vertexCount); !status)
    {
        return Core::failure(status.error());
    }

    try
    {
        std::vector<std::byte> bytes(static_cast<usize>(offsets->totalBytes), std::byte{0});
        writeU16(bytes, 0U, SkinnedMeshWire::SchemaVersion);
        writeU16(bytes, 2U, SkinnedMeshWire::VertexLayout);
        writeU16(bytes, 4U, static_cast<u16>(desc.indexType));
        writeU16(bytes, 6U, counts.submeshCount);
        writeU32(bytes, 8U, counts.vertexCount);
        writeU32(bytes, 12U, counts.indexCount);
        writeF32(bytes, 16U, desc.boundsCenterX);
        writeF32(bytes, 20U, desc.boundsCenterY);
        writeF32(bytes, 24U, desc.boundsCenterZ);
        writeF32(bytes, 28U, desc.boundsRadius);
        writeU16(bytes, 32U, counts.jointCount);
        writeU16(bytes, 34U, SkinnedMeshWire::InfluencesPerVertex);
        writeU32(bytes, 36U, 0U);
        writeU32(bytes, 40U, 0U);
        writeU32(bytes, 44U, 0U);

        usize offset = static_cast<usize>(offsets->inverseBindOffset);
        for (const float value : desc.inverseBindMatrices)
        {
            writeF32(bytes, offset, value);
            offset += sizeof(float);
        }

        offset = static_cast<usize>(offsets->jointOffset);
        for (const SkinnedMeshJointDesc& joint : desc.joints)
        {
            writeU16(bytes, offset + 0U, joint.parentJoint);
            writeU16(bytes, offset + 2U, joint.reserved);
            for (usize element = 0; element < 3U; ++element)
            {
                writeF32(bytes, offset + 4U + (element * 4U), joint.bindTranslation[element]);
            }
            for (usize element = 0; element < 4U; ++element)
            {
                writeF32(bytes, offset + 16U + (element * 4U), joint.bindRotation[element]);
            }
            for (usize element = 0; element < 3U; ++element)
            {
                writeF32(bytes, offset + 32U + (element * 4U), joint.bindScale[element]);
            }
            offset += SkinnedMeshWire::JointBytes;
        }

        offset = static_cast<usize>(offsets->submeshOffset);
        for (const StaticMeshSubmeshDesc& submesh : desc.submeshes)
        {
            writeU32(bytes, offset + 0U, submesh.firstIndex);
            writeU32(bytes, offset + 4U, submesh.indexCount);
            writeU32(bytes, offset + 8U, submesh.materialSlot);
            writeU32(bytes, offset + 12U, submesh.reserved);
            offset += SkinnedMeshWire::SubmeshBytes;
        }

        offset = static_cast<usize>(offsets->vertexOffset);
        for (const float value : desc.vertices)
        {
            writeF32(bytes, offset, value);
            offset += sizeof(float);
        }

        offset = static_cast<usize>(offsets->jointIndexOffset);
        for (const u16 value : desc.jointIndices)
        {
            writeU16(bytes, offset, value);
            offset += sizeof(u16);
        }

        offset = static_cast<usize>(offsets->jointWeightOffset);
        for (const u16 value : desc.jointWeights)
        {
            writeU16(bytes, offset, value);
            offset += sizeof(u16);
        }

        offset = static_cast<usize>(offsets->indexOffset);
        for (const u16 value : desc.indices)
        {
            writeU16(bytes, offset, value);
            offset += sizeof(u16);
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "skinned mesh payload allocation failed");
    }
}

Core::Result<SkinnedMeshPayloadView> parseSkinnedMeshPayload(std::span<const std::byte> payload)
{
    if (payload.size() < SkinnedMeshWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "skinned mesh payload too short");
    }

    SkinnedMeshPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .indexType = static_cast<StaticMeshIndexType>(readU16(payload, 4U)),
        .submeshCount = readU16(payload, 6U),
        .vertexCount = readU32(payload, 8U),
        .indexCount = readU32(payload, 12U),
        .boundsCenterX = readF32(payload, 16U),
        .boundsCenterY = readF32(payload, 20U),
        .boundsCenterZ = readF32(payload, 24U),
        .boundsRadius = readF32(payload, 28U),
        .jointCount = readU16(payload, 32U),
        .influencesPerVertex = readU16(payload, 34U),
    };
    if (view.schemaVersion != SkinnedMeshWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported skinned mesh payload schema");
    }
    if (readU16(payload, 2U) != SkinnedMeshWire::VertexLayout)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported skinned mesh vertex layout");
    }
    if (view.indexType != StaticMeshIndexType::U16)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported skinned mesh index type");
    }
    if (view.influencesPerVertex != SkinnedMeshWire::InfluencesPerVertex)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported skinned mesh influence count");
    }
    if (readU32(payload, 36U) != 0U || readU32(payload, 40U) != 0U ||
        readU32(payload, 44U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh reserved fields must be zero");
    }
    if (Core::Status status =
            validateBounds(view.boundsCenterX, view.boundsCenterY, view.boundsCenterZ, view.boundsRadius);
        !status)
    {
        return Core::failure(status.error());
    }

    auto offsets = validateLayout(LayoutCounts{
        .jointCount = view.jointCount,
        .submeshCount = view.submeshCount,
        .vertexCount = view.vertexCount,
        .indexCount = view.indexCount,
    });
    if (!offsets)
    {
        return Core::failure(offsets.error());
    }
    if (payload.size() != offsets->totalBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh payload size mismatch");
    }

    // Typed views below borrow the wire blocks directly.  A caller may provide a
    // subspan whose base address is not naturally aligned even though all wire
    // offsets are multiples of the required alignment, so reject that input before
    // any reinterpret_cast can form an invalid typed pointer.
    const auto isAligned = [&](u64 offset, std::size_t alignment) noexcept {
        return (reinterpret_cast<std::uintptr_t>(payload.data() + static_cast<usize>(offset)) % alignment) == 0U;
    };
    if (!isAligned(offsets->inverseBindOffset, alignof(float)) ||
        !isAligned(offsets->submeshOffset, alignof(StaticMeshSubmeshView)) ||
        !isAligned(offsets->vertexOffset, alignof(float)) ||
        !isAligned(offsets->jointIndexOffset, alignof(u16)) ||
        !isAligned(offsets->jointWeightOffset, alignof(u16)) ||
        !isAligned(offsets->indexOffset, alignof(u16)))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "skinned mesh typed block alignment invalid");
    }

    const usize inverseBindOffset = static_cast<usize>(offsets->inverseBindOffset);
    const usize inverseBindFloatCount =
        static_cast<usize>(view.jointCount) * SkinnedMeshWire::FloatsPerInverseBindMatrix;
    view.inverseBindMatrices = std::span<const float>{
        reinterpret_cast<const float*>(payload.data() + inverseBindOffset), inverseBindFloatCount};
    if (Core::Status status = validateInverseBindMatrices(view.inverseBindMatrices); !status)
    {
        return Core::failure(status.error());
    }

    const usize jointOffset = static_cast<usize>(offsets->jointOffset);
    view.jointsBytes = payload.subspan(jointOffset,
                                       static_cast<usize>(view.jointCount) * SkinnedMeshWire::JointBytes);
    for (u16 index = 0; index < view.jointCount; ++index)
    {
        const auto jointView = view.joint(index);
        if (!jointView)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "skinned mesh joint table is truncated");
        }
        const SkinnedMeshJointDesc jointDesc{
            .parentJoint = jointView->parentJoint,
            .reserved = jointView->reserved,
            .bindTranslation = {jointView->bindTranslation[0], jointView->bindTranslation[1],
                                jointView->bindTranslation[2]},
            .bindRotation = {jointView->bindRotation[0], jointView->bindRotation[1],
                             jointView->bindRotation[2], jointView->bindRotation[3]},
            .bindScale = {jointView->bindScale[0], jointView->bindScale[1],
                          jointView->bindScale[2]},
        };
        if (Core::Status status = validateJoint(jointDesc, index); !status)
        {
            return Core::failure(status.error());
        }
    }

    const usize submeshOffset = static_cast<usize>(offsets->submeshOffset);
    view.submeshes = std::span<const StaticMeshSubmeshView>{
        reinterpret_cast<const StaticMeshSubmeshView*>(payload.data() + submeshOffset), view.submeshCount};
    for (const StaticMeshSubmeshView& submesh : view.submeshes)
    {
        if (Core::Status status =
                validateSubmesh(submesh.firstIndex, submesh.indexCount, submesh.reserved, view.indexCount);
            !status)
        {
            return Core::failure(status.error());
        }
    }

    const usize vertexOffset = static_cast<usize>(offsets->vertexOffset);
    view.vertices = std::span<const float>{
        reinterpret_cast<const float*>(payload.data() + vertexOffset),
        static_cast<usize>(view.vertexCount) * SkinnedMeshWire::FloatsPerVertex};
    if (Core::Status status = validateVertices(view.vertices); !status)
    {
        return Core::failure(status.error());
    }

    const usize influenceCount =
        static_cast<usize>(view.vertexCount) * SkinnedMeshWire::InfluencesPerVertex;
    view.jointIndices = std::span<const u16>{
        reinterpret_cast<const u16*>(payload.data() + static_cast<usize>(offsets->jointIndexOffset)),
        influenceCount};
    view.jointWeights = std::span<const u16>{
        reinterpret_cast<const u16*>(payload.data() + static_cast<usize>(offsets->jointWeightOffset)),
        influenceCount};
    if (Core::Status status =
            validateInfluences(view.jointIndices, view.jointWeights, view.vertexCount, view.jointCount);
        !status)
    {
        return Core::failure(status.error());
    }

    view.indices = std::span<const u16>{
        reinterpret_cast<const u16*>(payload.data() + static_cast<usize>(offsets->indexOffset)), view.indexCount};
    if (Core::Status status = validateIndices(view.indices, view.vertexCount); !status)
    {
        return Core::failure(status.error());
    }
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedSkinnedMeshAsset(Core::AssetId assetId,
                                                                 const SkinnedMeshPayloadDesc& desc,
                                                                 TargetPlatform platform)
{
    auto payload = writeSkinnedMeshPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(payload.error());
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::SkinnedMesh,
        .assetTypeVersion = SkinnedMeshWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
