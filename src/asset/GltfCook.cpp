#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

#include <mikktspace.h>

#include "DerivedAssetId.hpp"
#include "GltfFileSnapshot.hpp"
#include "Utf8Path.hpp"

#include <tina/asset/GltfCook.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

// Texture channels force Material dep AssetIds into baseColor < MR < normal order
// (CatalogCook requires strictly increasing deps; flag order must stay base/MR/normal).
enum class GltfTextureChannel : Core::u8 {
    BaseColor = 0,
    MetallicRoughness = 1,
    Normal = 2,
};

[[nodiscard]] Core::AssetId deriveTextureChannelId(std::string_view seed, GltfTextureChannel channel,
                                                   Core::u32 sequence)
{
    Core::u8 tag = Detail::GltfBaseColorTextureAssetIdTag;
    switch (channel)
    {
    case GltfTextureChannel::BaseColor:
        tag = Detail::GltfBaseColorTextureAssetIdTag;
        break;
    case GltfTextureChannel::MetallicRoughness:
        tag = Detail::GltfMetallicRoughnessTextureAssetIdTag;
        break;
    case GltfTextureChannel::Normal:
        tag = Detail::GltfNormalTextureAssetIdTag;
        break;
    }
    return Detail::deriveVersionedAssetId(seed, AssetFormat::AssetKind::Texture2D, tag, sequence,
                                          static_cast<Core::u32>(channel));
}

[[nodiscard]] Core::Status mapCgltfResult(cgltf_result result) noexcept
{
    switch (result)
    {
    case cgltf_result_success:
        return Core::success();
    case cgltf_result_file_not_found:
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "glTF file not found");
    case cgltf_result_io_error:
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "glTF IO error");
    case cgltf_result_out_of_memory:
        return Core::failure(AssetErrorCode::AllocationFailed, "glTF out of memory");
    case cgltf_result_legacy_gltf:
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "legacy glTF is not supported");
    default:
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF parse/validate failed");
    }
}

[[nodiscard]] const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type,
                                                  cgltf_int semanticIndex = 0)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
    {
        if (prim.attributes[i].type == type && prim.attributes[i].index == semanticIndex)
        {
            return prim.attributes[i].data;
        }
    }
    return nullptr;
}

[[nodiscard]] Core::Status validateGltfSkinAttributeContract(const cgltf_primitive& prim,
                                                             bool hasSkinBinding)
{
    bool hasJoints0 = false;
    bool hasWeights0 = false;
    for (cgltf_size attributeIndex = 0; attributeIndex < prim.attributes_count; ++attributeIndex)
    {
        const cgltf_attribute& attribute = prim.attributes[attributeIndex];
        if (attribute.type != cgltf_attribute_type_joints &&
            attribute.type != cgltf_attribute_type_weights)
        {
            continue;
        }
        if (attribute.index != 0)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "only JOINTS_0/WEIGHTS_0 skin attributes are supported");
        }

        bool& present = attribute.type == cgltf_attribute_type_joints ? hasJoints0 : hasWeights0;
        if (present || attribute.data == nullptr)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "JOINTS_0/WEIGHTS_0 skin attributes must be unique and valid");
        }
        present = true;
    }

    if (hasJoints0 != hasWeights0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "JOINTS_0 and WEIGHTS_0 skin attributes must be provided together");
    }
    if (hasSkinBinding && !hasJoints0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "a primitive bound to node.skin requires JOINTS_0 and WEIGHTS_0");
    }
    if (!hasSkinBinding && hasJoints0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "a primitive with JOINTS_0/WEIGHTS_0 must be bound to node.skin");
    }
    return Core::success();
}

struct CookedMeshPieces final {
    std::vector<float> vertices{};
    // Maps each cooked vertex back to its source POSITION/skin accessor vertex.
    std::vector<Core::u16> sourceVertexIndices{};
    std::vector<Core::u16> indices{};
    AssetFormat::StaticMeshSubmeshDesc submesh{};
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 1.0F;
    float baseR = 1.0F;
    float baseG = 1.0F;
    float baseB = 1.0F;
    float baseA = 1.0F;
    float metallicFactor = 1.0F;
    float roughnessFactor = 1.0F;
    AssetFormat::MaterialAlphaMode alphaMode = AssetFormat::MaterialAlphaMode::Opaque;
    bool doubleSided = false;
};

struct CookedAnimationEntry final {
    Core::AssetId assetId{};
    std::vector<std::byte> payload{};
};

struct GltfSkinCookInfo final {
    const cgltf_skin* skin = nullptr;
    std::vector<AssetFormat::SkinnedMeshJointDesc> joints{};
    std::vector<float> inverseBindMatrices{};
    std::vector<Core::u16> sourceJointToCookedJoint{};
    std::unordered_map<const cgltf_node*, Core::u16> jointIndexByNode{};
};

[[nodiscard]] Core::Result<GltfSkinCookInfo> cookGltfSkin(const cgltf_skin& skin);
[[nodiscard]] Core::Result<std::pair<std::vector<Core::u16>, std::vector<Core::u16>>>
readGltfSkinInfluences(const cgltf_primitive& prim, const GltfSkinCookInfo& skin,
                       std::span<const Core::u16> sourceVertexIndices);
[[nodiscard]] Core::Result<AssetFormat::AnimationClip3DPayloadDesc>
makeGltfAnimationDesc(const cgltf_animation& animation,
                      const std::vector<GltfSkinCookInfo>& skins,
                      std::vector<AssetFormat::AnimationTrackDesc>& trackStorage,
                      std::vector<std::vector<float>>& timeStorage,
                      std::vector<std::vector<float>>& valueStorage);

struct TangentGenerationData final {
    std::span<const float> vertices{};
    std::span<const Core::u16> indices{};
    std::span<float> cornerTangents{};
};

inline constexpr std::size_t MikkInputFloatsPerVertex = 8U;

[[nodiscard]] TangentGenerationData& tangentGenerationData(const SMikkTSpaceContext* context) noexcept
{
    return *static_cast<TangentGenerationData*>(context->m_pUserData);
}

[[nodiscard]] std::size_t tangentCornerVertexIndex(const TangentGenerationData& data, int face, int vertex) noexcept
{
    const std::size_t corner = static_cast<std::size_t>(face) * 3U + static_cast<std::size_t>(vertex);
    return data.indices[corner];
}

[[nodiscard]] int tangentFaceCount(const SMikkTSpaceContext* context)
{
    const auto& data = tangentGenerationData(context);
    return static_cast<int>(data.indices.size() / 3U);
}

[[nodiscard]] int tangentVerticesPerFace(const SMikkTSpaceContext*, int)
{
    return 3;
}

void tangentPosition(const SMikkTSpaceContext* context, float output[], int face, int vertex)
{
    const auto& data = tangentGenerationData(context);
    const std::size_t base = tangentCornerVertexIndex(data, face, vertex) *
                             MikkInputFloatsPerVertex;
    output[0] = data.vertices[base + 0U];
    output[1] = data.vertices[base + 1U];
    output[2] = data.vertices[base + 2U];
}

void tangentNormal(const SMikkTSpaceContext* context, float output[], int face, int vertex)
{
    const auto& data = tangentGenerationData(context);
    const std::size_t base = tangentCornerVertexIndex(data, face, vertex) *
                             MikkInputFloatsPerVertex;
    output[0] = data.vertices[base + 3U];
    output[1] = data.vertices[base + 4U];
    output[2] = data.vertices[base + 5U];
}

void tangentTexcoord(const SMikkTSpaceContext* context, float output[], int face, int vertex)
{
    const auto& data = tangentGenerationData(context);
    const std::size_t base = tangentCornerVertexIndex(data, face, vertex) *
                             MikkInputFloatsPerVertex;
    output[0] = data.vertices[base + 6U];
    output[1] = data.vertices[base + 7U];
}

void setGeneratedTangent(const SMikkTSpaceContext* context, const float tangent[], float sign, int face, int vertex)
{
    auto& data = tangentGenerationData(context);
    const std::size_t corner = static_cast<std::size_t>(face) * 3U + static_cast<std::size_t>(vertex);
    const std::size_t base = corner * 4U;
    data.cornerTangents[base + 0U] = tangent[0];
    data.cornerTangents[base + 1U] = tangent[1];
    data.cornerTangents[base + 2U] = tangent[2];
    data.cornerTangents[base + 3U] = sign;
}

[[nodiscard]] Core::Status normalizeTangent(float* tangent, bool authored)
{
    for (std::size_t component = 0; component < 4U; ++component)
    {
        if (!std::isfinite(tangent[component]))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 authored ? "authored TANGENT must be finite"
                                          : "MikkTSpace generated a non-finite tangent");
        }
    }
    const float lengthSquared =
        tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2];
    if (!std::isfinite(lengthSquared) || !(lengthSquared > 1.0e-12F))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             authored ? "authored TANGENT xyz must be normalizable"
                                      : "MikkTSpace generated a zero-length tangent");
    }
    if (authored)
    {
        constexpr float signTolerance = 1.0e-5F;
        if (std::abs(std::abs(tangent[3]) - 1.0F) > signTolerance)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "authored TANGENT w must be -1 or +1");
        }
    }
    else if (std::abs(tangent[3]) <= 1.0e-12F)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "MikkTSpace generated an invalid tangent handedness");
    }

    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    tangent[0] *= inverseLength;
    tangent[1] *= inverseLength;
    tangent[2] *= inverseLength;
    for (std::size_t component = 0; component < 3U; ++component)
    {
        if (tangent[component] == 0.0F)
        {
            tangent[component] = 0.0F; // canonicalize negative zero for vertex deduplication
        }
    }
    tangent[3] = tangent[3] < 0.0F ? -1.0F : 1.0F;
    return Core::success();
}

struct TangentMeshPieces final {
    std::vector<float> vertices{};
    std::vector<Core::u16> sourceVertexIndices{};
    std::vector<Core::u16> indices{};
};

struct TangentVertexKey final {
    Core::u16 sourceVertex = 0;
    Core::u32 tangentX = 0;
    Core::u32 tangentY = 0;
    Core::u32 tangentZ = 0;
    bool negativeHandedness = false;

    bool operator==(const TangentVertexKey&) const noexcept = default;
};

struct TangentVertexKeyHash final {
    [[nodiscard]] std::size_t operator()(const TangentVertexKey& key) const noexcept
    {
        std::size_t hash = key.sourceVertex;
        const auto mix = [&hash](Core::u32 value) {
            hash ^= static_cast<std::size_t>(value) + static_cast<std::size_t>(0x9E3779B9U) +
                    (hash << 6U) + (hash >> 2U);
        };
        mix(key.tangentX);
        mix(key.tangentY);
        mix(key.tangentZ);
        mix(key.negativeHandedness ? 1U : 0U);
        return hash;
    }
};

[[nodiscard]] TangentVertexKey makeTangentVertexKey(Core::u16 sourceVertex, const float* tangent) noexcept
{
    return TangentVertexKey{
        .sourceVertex = sourceVertex,
        .tangentX = std::bit_cast<Core::u32>(tangent[0]),
        .tangentY = std::bit_cast<Core::u32>(tangent[1]),
        .tangentZ = std::bit_cast<Core::u32>(tangent[2]),
        .negativeHandedness = tangent[3] < 0.0F,
    };
}

[[nodiscard]] Core::Result<TangentMeshPieces> generateTangentMeshWithMikkTSpace(
    std::span<const float> vertices,
    std::span<const Core::u16> indices)
{
    std::vector<float> cornerTangents(indices.size() * 4U, 0.0F);

    TangentGenerationData data{
        .vertices = vertices,
        .indices = indices,
        .cornerTangents = cornerTangents,
    };
    SMikkTSpaceInterface interface{
        .m_getNumFaces = &tangentFaceCount,
        .m_getNumVerticesOfFace = &tangentVerticesPerFace,
        .m_getPosition = &tangentPosition,
        .m_getNormal = &tangentNormal,
        .m_getTexCoord = &tangentTexcoord,
        .m_setTSpaceBasic = &setGeneratedTangent,
        .m_setTSpace = nullptr,
    };
    const SMikkTSpaceContext context{
        .m_pInterface = &interface,
        .m_pUserData = &data,
    };
    if (genTangSpaceDefault(&context) == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "MikkTSpace failed to generate mesh tangents");
    }

    constexpr std::size_t sourceStride = MikkInputFloatsPerVertex;
    constexpr std::size_t tangentStride = AssetFormat::StaticMeshWire::FloatsPerVertex;
    constexpr std::size_t maxProductVertices = (std::numeric_limits<Core::u16>::max)();
    TangentMeshPieces output{};
    output.vertices.reserve((std::min)(indices.size(), maxProductVertices) * tangentStride);
    output.sourceVertexIndices.reserve((std::min)(indices.size(), maxProductVertices));
    output.indices.resize(indices.size());
    std::unordered_map<TangentVertexKey, Core::u16, TangentVertexKeyHash> rebuiltVertexByKey;
    rebuiltVertexByKey.reserve((std::min)(indices.size(), maxProductVertices));

    for (std::size_t corner = 0; corner < indices.size(); ++corner)
    {
        float* tangent = cornerTangents.data() + corner * 4U;
        if (auto status = normalizeTangent(tangent, false); !status)
        {
            return Core::failure(status.error());
        }

        const Core::u16 sourceVertex = indices[corner];
        const TangentVertexKey key = makeTangentVertexKey(sourceVertex, tangent);
        if (const auto found = rebuiltVertexByKey.find(key); found != rebuiltVertexByKey.end())
        {
            output.indices[corner] = found->second;
            continue;
        }
        const std::size_t rebuiltVertexCount = output.vertices.size() / tangentStride;
        if (rebuiltVertexCount >= maxProductVertices)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "MikkTSpace tangent splits exceed the u16 product vertex limit");
        }

        const auto rebuiltVertex = static_cast<Core::u16>(rebuiltVertexCount);
        rebuiltVertexByKey.emplace(key, rebuiltVertex);
        output.indices[corner] = rebuiltVertex;
        const std::size_t source = static_cast<std::size_t>(sourceVertex) * sourceStride;
        output.sourceVertexIndices.push_back(sourceVertex);
        output.vertices.insert(output.vertices.end(), vertices.data() + source, vertices.data() + source + 6U);
        output.vertices.insert(output.vertices.end(), tangent, tangent + 4U);
        output.vertices.insert(output.vertices.end(), vertices.data() + source + 6U,
                               vertices.data() + source + 8U);
    }
    return output;
}

[[nodiscard]] Core::Status validateOptionalAttribute(const cgltf_accessor* accessor, cgltf_type type,
                                                     cgltf_size vertexCount, std::string_view name)
{
    if (accessor == nullptr)
    {
        return Core::success();
    }
    if (accessor->type != type || accessor->count != vertexCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             std::string{name} + " accessor shape/count does not match POSITION");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<CookedMeshPieces> extractTriangleMesh(const cgltf_primitive& prim)
{
    if (prim.type != cgltf_primitive_type_triangles)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "only TRIANGLES primitives are supported");
    }
    const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
    if (positions == nullptr || positions->type != cgltf_type_vec3 ||
        positions->component_type != cgltf_component_type_r_32f || positions->count == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "POSITION float3 accessor required");
    }
    if (positions->count > 65535U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "vertex count exceeds u16 index range");
    }

    const cgltf_accessor* normals = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* texcoords = findAttribute(prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor* authoredTangents = findAttribute(prim, cgltf_attribute_type_tangent);
    if (normals == nullptr || normals->type != cgltf_type_vec3 || normals->count != positions->count)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "NORMAL accessor matching POSITION is required");
    }
    if (texcoords == nullptr || texcoords->type != cgltf_type_vec2 || texcoords->count != positions->count)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "TEXCOORD_0 accessor matching POSITION is required");
    }
    if (auto status = validateOptionalAttribute(authoredTangents, cgltf_type_vec4, positions->count, "TANGENT");
        !status)
    {
        return Core::failure(status.error());
    }
    if (authoredTangents != nullptr && authoredTangents->component_type != cgltf_component_type_r_32f)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "authored TANGENT must use FLOAT components");
    }
    CookedMeshPieces out{};
    out.vertices.resize(static_cast<std::size_t>(positions->count) *
                        MikkInputFloatsPerVertex);
    out.sourceVertexIndices.resize(static_cast<std::size_t>(positions->count));
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        out.sourceVertexIndices[static_cast<std::size_t>(i)] = static_cast<Core::u16>(i);
        float p[3]{};
        float n[3]{};
        float uv[2]{};
        if (!cgltf_accessor_read_float(positions, i, p, 3))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "failed to read POSITION");
        }
        if (!cgltf_accessor_read_float(normals, i, n, 3))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "failed to read NORMAL");
        }
        if (!cgltf_accessor_read_float(texcoords, i, uv, 2))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "failed to read TEXCOORD_0");
        }
        const std::size_t base = static_cast<std::size_t>(i) * MikkInputFloatsPerVertex;
        out.vertices[base + 0] = p[0];
        out.vertices[base + 1] = p[1];
        out.vertices[base + 2] = p[2];
        out.vertices[base + 3] = n[0];
        out.vertices[base + 4] = n[1];
        out.vertices[base + 5] = n[2];
        out.vertices[base + 6] = uv[0];
        out.vertices[base + 7] = uv[1];
    }

    if (prim.indices != nullptr)
    {
        if (prim.indices->count == 0 || prim.indices->count > AssetFormat::StaticMeshWire::MaxIndexCount ||
            prim.indices->count % 3 != 0)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "index count must be a bounded positive multiple of 3");
        }
        out.indices.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
        {
            const cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
            if (idx >= positions->count || idx > 65535U)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "index out of range for u16 mesh");
            }
            out.indices[i] = static_cast<Core::u16>(idx);
        }
    }
    else
    {
        if (positions->count % 3 != 0)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "non-indexed mesh vertex count invalid");
        }
        out.indices.resize(positions->count);
        for (cgltf_size i = 0; i < positions->count; ++i)
        {
            out.indices[i] = static_cast<Core::u16>(i);
        }
    }

    std::vector<float> tangents;
    if (authoredTangents != nullptr)
    {
        tangents.resize(static_cast<std::size_t>(positions->count) * 4U);
        for (cgltf_size i = 0; i < positions->count; ++i)
        {
            if (!cgltf_accessor_read_float(authoredTangents, i,
                                           tangents.data() + static_cast<std::size_t>(i) * 4U, 4))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "failed to read TANGENT");
            }
            if (auto status = normalizeTangent(tangents.data() + static_cast<std::size_t>(i) * 4U, true);
                !status)
            {
                return Core::failure(status.error());
            }
        }
        std::vector<float> tangentVertices(static_cast<std::size_t>(positions->count) *
                                           AssetFormat::StaticMeshWire::FloatsPerVertex);
        for (cgltf_size i = 0; i < positions->count; ++i)
        {
            const std::size_t source = static_cast<std::size_t>(i) * MikkInputFloatsPerVertex;
            const std::size_t tangent = static_cast<std::size_t>(i) * 4U;
            const std::size_t target =
                static_cast<std::size_t>(i) * AssetFormat::StaticMeshWire::FloatsPerVertex;
            std::copy_n(out.vertices.data() + source, 6U, tangentVertices.data() + target);
            std::copy_n(tangents.data() + tangent, 4U, tangentVertices.data() + target + 6U);
            std::copy_n(out.vertices.data() + source + 6U, 2U, tangentVertices.data() + target + 10U);
        }
        out.vertices = std::move(tangentVertices);
    }
    else
    {
        auto generated = generateTangentMeshWithMikkTSpace(out.vertices, out.indices);
        if (!generated)
        {
            return Core::failure(std::move(generated.error()));
        }
        out.vertices = std::move(generated->vertices);
        out.sourceVertexIndices = std::move(generated->sourceVertexIndices);
        out.indices = std::move(generated->indices);
    }

    float minX = out.vertices[0], minY = out.vertices[1], minZ = out.vertices[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
    constexpr std::size_t vertexStride = AssetFormat::StaticMeshWire::FloatsPerVertex;
    const std::size_t outputVertexCount = out.vertices.size() / vertexStride;
    for (std::size_t i = 0; i < outputVertexCount; ++i)
    {
        const std::size_t base = static_cast<std::size_t>(i) * vertexStride;
        minX = (std::min)(minX, out.vertices[base + 0]);
        minY = (std::min)(minY, out.vertices[base + 1]);
        minZ = (std::min)(minZ, out.vertices[base + 2]);
        maxX = (std::max)(maxX, out.vertices[base + 0]);
        maxY = (std::max)(maxY, out.vertices[base + 1]);
        maxZ = (std::max)(maxZ, out.vertices[base + 2]);
    }
    out.boundsCenterX = 0.5F * (minX + maxX);
    out.boundsCenterY = 0.5F * (minY + maxY);
    out.boundsCenterZ = 0.5F * (minZ + maxZ);
    float radius = 0.0F;
    for (std::size_t i = 0; i < outputVertexCount; ++i)
    {
        const std::size_t base = static_cast<std::size_t>(i) * vertexStride;
        const float dx = out.vertices[base + 0] - out.boundsCenterX;
        const float dy = out.vertices[base + 1] - out.boundsCenterY;
        const float dz = out.vertices[base + 2] - out.boundsCenterZ;
        radius = (std::max)(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    out.boundsRadius = radius > 0.0F ? radius : 1.0F;
    out.submesh = AssetFormat::StaticMeshSubmeshDesc{
        .firstIndex = 0,
        .indexCount = static_cast<Core::u32>(out.indices.size()),
        .materialSlot = 0,
    };

    if (prim.material != nullptr)
    {
        out.doubleSided = prim.material->double_sided != 0;
        switch (prim.material->alpha_mode)
        {
        case cgltf_alpha_mode_opaque:
            out.alphaMode = AssetFormat::MaterialAlphaMode::Opaque;
            break;
        case cgltf_alpha_mode_blend:
            out.alphaMode = AssetFormat::MaterialAlphaMode::Blend;
            break;
        case cgltf_alpha_mode_mask:
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF MASK materials are not supported; use OPAQUE or BLEND");
        default:
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF material has an unsupported alphaMode");
        }
        if (prim.material->has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
            out.baseR = pbr.base_color_factor[0];
            out.baseG = pbr.base_color_factor[1];
            out.baseB = pbr.base_color_factor[2];
            out.baseA = pbr.base_color_factor[3];
            out.metallicFactor = pbr.metallic_factor;
            out.roughnessFactor = pbr.roughness_factor;
        }
    }
    return out;
}

inline constexpr std::uint64_t kMebibyte = 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxGltfSourceFileBytes = 64ULL * kMebibyte;
inline constexpr std::uint64_t kMaxGltfExternalFileBytes = 64ULL * kMebibyte;
inline constexpr std::uint64_t kMaxGltfTotalBufferBytes = 256ULL * kMebibyte;
inline constexpr std::uint64_t kMaxGltfAccessorLogicalBytes = 256ULL * kMebibyte;
inline constexpr std::uint64_t kMaxGltfDecodedImageBytes = 64ULL * kMebibyte;
inline constexpr std::uint64_t kMaxGltfTotalDecodedImageBytes = 256ULL * kMebibyte;
inline constexpr std::size_t kMaxCgltfLiveBytes = 384ULL * 1024ULL * 1024ULL;
inline constexpr cgltf_size kMaxGltfBuffers = 256;
inline constexpr cgltf_size kMaxGltfBufferViews = 16'384;
inline constexpr cgltf_size kMaxGltfAccessors = 16'384;
inline constexpr cgltf_size kMaxGltfMeshes = 2'048;
inline constexpr cgltf_size kMaxGltfPrimitives = 2'048;
inline constexpr cgltf_size kMaxGltfMaterials = 4'096;
inline constexpr cgltf_size kMaxGltfImages = 4'096;
inline constexpr cgltf_size kMaxGltfTextures = 4'096;
inline constexpr cgltf_size kMaxGltfScenes = 256;

struct GltfSourceCaptureContext final {
    const SourceImportCaptureConfig* config = nullptr;
    SourceImportCandidate* candidate = nullptr;
    std::filesystem::path documentSourcePath{};
};

[[nodiscard]] std::string pathToUtf8Bytes(const std::filesystem::path& path)
{
    const auto utf8 = path.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

[[nodiscard]] Core::Result<Core::u32> captureGltfSourceBytes(
    GltfSourceCaptureContext& capture,
    const std::filesystem::path& sourcePath,
    AssetFormat::SourceImportReadExtent readExtent,
    std::span<const std::byte> consumedBytes)
{
    if (capture.config == nullptr || capture.candidate == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF source capture context is incomplete");
    }
    const std::string sourceUtf8Path = pathToUtf8Bytes(sourcePath);
    return captureSourceImportBytes(*capture.candidate, *capture.config, sourceUtf8Path,
                                    readExtent, consumedBytes);
}

[[nodiscard]] bool checkedAdd(std::uint64_t left, std::uint64_t right,
                              std::uint64_t& result) noexcept
{
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left)
    {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(std::uint64_t left, std::uint64_t right,
                                   std::uint64_t& result) noexcept
{
    if (left != 0 && right > (std::numeric_limits<std::uint64_t>::max)() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

struct alignas(std::max_align_t) CgltfAllocationHeader final {
    std::size_t bytes = 0;
};

struct CgltfMemoryBudget final {
    std::size_t liveBytes = 0;
    bool limitExceeded = false;
};

[[nodiscard]] void* allocateCgltfMemory(void* user, cgltf_size size) noexcept
{
    auto* budget = static_cast<CgltfMemoryBudget*>(user);
    if (budget == nullptr || size == 0 || budget->liveBytes > kMaxCgltfLiveBytes ||
        size > kMaxCgltfLiveBytes - budget->liveBytes ||
        size > (std::numeric_limits<std::size_t>::max)() - sizeof(CgltfAllocationHeader))
    {
        if (budget != nullptr)
        {
            budget->limitExceeded = true;
        }
        return nullptr;
    }
    void* allocation = std::malloc(sizeof(CgltfAllocationHeader) + size);
    if (allocation == nullptr)
    {
        return nullptr;
    }
    auto* header = static_cast<CgltfAllocationHeader*>(allocation);
    header->bytes = size;
    budget->liveBytes += size;
    return header + 1;
}

void freeCgltfMemory(void* user, void* pointer) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }
    auto* budget = static_cast<CgltfMemoryBudget*>(user);
    auto* header = static_cast<CgltfAllocationHeader*>(pointer) - 1;
    if (budget != nullptr && header->bytes <= budget->liveBytes)
    {
        budget->liveBytes -= header->bytes;
    }
    std::free(header);
}

[[nodiscard]] Core::Status validateGltfResourceStructure(const cgltf_data* data) noexcept
{
    if (data == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF document is null");
    }
    if (data->buffers_count > kMaxGltfBuffers || data->buffer_views_count > kMaxGltfBufferViews ||
        data->accessors_count > kMaxGltfAccessors || data->meshes_count > kMaxGltfMeshes ||
        data->materials_count > kMaxGltfMaterials || data->images_count > kMaxGltfImages ||
        data->textures_count > kMaxGltfTextures || data->scenes_count > kMaxGltfScenes ||
        data->nodes_count > AssetFormat::PrefabWire::MaxNodes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF top-level object count exceeds product limit");
    }

    std::uint64_t nodeReferenceCount = 0;
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        if (!checkedAdd(nodeReferenceCount, data->nodes[i].children_count, nodeReferenceCount) ||
            nodeReferenceCount > AssetFormat::PrefabWire::MaxNodes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF node child-reference count exceeds product limit");
        }
    }
    for (cgltf_size i = 0; i < data->scenes_count; ++i)
    {
        if (data->scenes[i].nodes_count > AssetFormat::PrefabWire::MaxNodes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF scene root count exceeds product limit");
        }
    }

    std::uint64_t totalBufferBytes = 0;
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
    {
        const std::uint64_t size = data->buffers[i].size;
        if (size == 0 || size > kMaxGltfExternalFileBytes ||
            !checkedAdd(totalBufferBytes, size, totalBufferBytes) ||
            totalBufferBytes > kMaxGltfTotalBufferBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF buffer byte budget exceeded");
        }
    }

    for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
    {
        const cgltf_buffer_view& view = data->buffer_views[i];
        std::uint64_t end = 0;
        if (view.buffer == nullptr || view.size == 0 || view.size > kMaxGltfExternalFileBytes ||
            view.has_meshopt_compression || !checkedAdd(view.offset, view.size, end) ||
            end > view.buffer->size || view.stride > 252U)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF bufferView offset/size/stride is invalid");
        }
    }

    std::uint64_t totalAccessorBytes = 0;
    for (cgltf_size i = 0; i < data->accessors_count; ++i)
    {
        const cgltf_accessor& accessor = data->accessors[i];
        const std::uint64_t elementBytes = cgltf_calc_size(accessor.type, accessor.component_type);
        if (accessor.is_sparse || accessor.buffer_view == nullptr || accessor.count == 0 ||
            accessor.count > AssetFormat::StaticMeshWire::MaxIndexCount || elementBytes == 0 ||
            accessor.stride < elementBytes || accessor.stride > 252U)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF accessor count/type/stride is invalid");
        }

        std::uint64_t stridedBytes = 0;
        std::uint64_t requiredBytes = 0;
        std::uint64_t logicalBytes = 0;
        if (!checkedMultiply(accessor.count - 1U, accessor.stride, stridedBytes) ||
            !checkedAdd(stridedBytes, elementBytes, requiredBytes) ||
            !checkedAdd(accessor.offset, requiredBytes, requiredBytes) ||
            requiredBytes > accessor.buffer_view->size ||
            !checkedMultiply(accessor.count, elementBytes, logicalBytes) ||
            !checkedAdd(totalAccessorBytes, logicalBytes, totalAccessorBytes) ||
            totalAccessorBytes > kMaxGltfAccessorLogicalBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF accessor range or logical byte budget exceeded");
        }
    }

    std::uint64_t primitiveCount = 0;
    std::uint64_t vertexCount = 0;
    std::uint64_t indexCount = 0;
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& mesh = data->meshes[meshIndex];
        if (mesh.primitives_count == 0 || mesh.primitives_count > AssetFormat::StaticMeshWire::MaxSubmeshes ||
            !checkedAdd(primitiveCount, mesh.primitives_count, primitiveCount) ||
            primitiveCount > kMaxGltfPrimitives)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF primitive count exceeds product limit");
        }
        for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex)
        {
            const cgltf_primitive& prim = mesh.primitives[primIndex];
            const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
            const cgltf_accessor* normals = findAttribute(prim, cgltf_attribute_type_normal);
            const cgltf_accessor* texcoords = findAttribute(prim, cgltf_attribute_type_texcoord);
            if (prim.type != cgltf_primitive_type_triangles)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "only TRIANGLES primitives are supported");
            }
            if (prim.targets_count != 0 || positions == nullptr || positions->type != cgltf_type_vec3 ||
                positions->component_type != cgltf_component_type_r_32f || positions->count == 0 ||
                positions->count > 65'535U ||
                (normals != nullptr && (normals->type != cgltf_type_vec3 ||
                                        normals->count != positions->count)) ||
                (texcoords != nullptr && (texcoords->type != cgltf_type_vec2 ||
                                          texcoords->count != positions->count)))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF primitive attribute contract is invalid");
            }
            const std::uint64_t primitiveIndices =
                prim.indices != nullptr ? prim.indices->count : positions->count;
            if (primitiveIndices == 0 || primitiveIndices > AssetFormat::StaticMeshWire::MaxIndexCount ||
                (primitiveIndices % 3U) != 0 ||
                !checkedAdd(vertexCount, positions->count, vertexCount) ||
                vertexCount > AssetFormat::StaticMeshWire::MaxVertexCount ||
                !checkedAdd(indexCount, primitiveIndices, indexCount) ||
                indexCount > AssetFormat::StaticMeshWire::MaxIndexCount)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF primitive vertex/index budget exceeded");
            }
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Result<std::filesystem::path> resolveContainedGltfExternalPath(
    const std::filesystem::path& gltfFilePath,
    std::string_view uri)
{
    if (uri.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF external URI is empty");
    }

    std::string decoded{uri};
    const cgltf_size decodedSize = cgltf_decode_uri(decoded.data());
    if (decodedSize == 0 || std::find(decoded.begin(), decoded.begin() + decodedSize, '\0') !=
                                decoded.begin() + decodedSize)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI decoding is invalid");
    }
    decoded.resize(decodedSize);
    if (!Core::isStrictUtf8WithoutNul(decoded))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI is not strict UTF-8");
    }
    if (decoded.find(':') != std::string::npos)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI schemes are not supported (use relative paths under glTF root)");
    }
    const std::filesystem::path relative = Detail::pathFromUtf8Bytes(decoded);
    if (relative.is_absolute() || relative.has_root_path())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI must be relative to the glTF file");
    }
    for (const std::filesystem::path& component : relative)
    {
        if (component == "..")
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF external URI must not contain path traversal");
        }
    }
    return (gltfFilePath.parent_path() / relative).lexically_normal();
}

[[nodiscard]] Core::Result<Core::u32> captureGltfExternalSourceBytes(
    GltfSourceCaptureContext& capture,
    std::string_view uri,
    AssetFormat::SourceImportReadExtent readExtent,
    std::span<const std::byte> consumedBytes)
{
    auto sourcePath = resolveContainedGltfExternalPath(capture.documentSourcePath, uri);
    if (!sourcePath)
    {
        return Core::failure(std::move(sourcePath.error()));
    }
    return captureGltfSourceBytes(capture, *sourcePath, readExtent, consumedBytes);
}

[[nodiscard]] Core::Status loadGltfBuffersFromSnapshots(const cgltf_options& options,
                                                        cgltf_data* data,
                                                        const std::filesystem::path& gltfFilePath,
                                                        GltfSourceCaptureContext* capture)
{
    if (data->buffers_count > 0 && data->buffers[0].data == nullptr &&
        data->buffers[0].uri == nullptr && data->bin != nullptr)
    {
        if (data->bin_size < data->buffers[0].size)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "GLB binary chunk is shorter than declared buffer");
        }
        data->buffers[0].data = const_cast<void*>(data->bin);
        data->buffers[0].data_free_method = cgltf_data_free_method_none;
    }

    std::uint64_t externalFileBytes = 0;
    const std::filesystem::path containmentRoot = gltfFilePath.parent_path();
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
    {
        cgltf_buffer& buffer = data->buffers[i];
        if (buffer.data != nullptr)
        {
            continue;
        }
        if (buffer.uri == nullptr || buffer.uri[0] == '\0')
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF buffer has no binary source");
        }
        if (std::strncmp(buffer.uri, "data:", 5) == 0)
        {
            const char* comma = std::strchr(buffer.uri, ',');
            if (comma == nullptr || comma - buffer.uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF buffer data URI must use base64");
            }
            const cgltf_result result =
                cgltf_load_buffer_base64(&options, buffer.size, comma + 1, &buffer.data);
            if (result != cgltf_result_success)
            {
                return mapCgltfResult(result);
            }
            buffer.data_free_method = cgltf_data_free_method_memory_free;
            continue;
        }

        auto externalPath = resolveContainedGltfExternalPath(gltfFilePath, buffer.uri);
        if (!externalPath)
        {
            return Core::failure(std::move(externalPath.error()));
        }
        auto snapshot = GltfDetail::readFileSnapshot(*externalPath, &containmentRoot,
                                                     kMaxGltfExternalFileBytes, buffer.size);
        if (!snapshot)
        {
            return Core::failure(std::move(snapshot.error()));
        }
        if (!checkedAdd(externalFileBytes, snapshot->fileSize, externalFileBytes) ||
            externalFileBytes > kMaxGltfTotalBufferBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF external buffer file budget exceeded");
        }
        if (capture != nullptr)
        {
            auto sourceIndex = captureGltfExternalSourceBytes(
                *capture, buffer.uri, AssetFormat::SourceImportReadExtent::Prefix, snapshot->bytes);
            if (!sourceIndex)
            {
                return Core::failure(std::move(sourceIndex.error())
                                         .withContext("loadGltfBuffersFromSnapshots", "source capture"));
            }
        }
        void* memory = options.memory.alloc_func(options.memory.user_data, buffer.size);
        if (memory == nullptr)
        {
            return Core::failure(AssetErrorCode::AllocationFailed,
                                 "glTF external buffer allocation failed");
        }
        std::memcpy(memory, snapshot->bytes.data(), snapshot->bytes.size());
        buffer.data = memory;
        buffer.data_free_method = cgltf_data_free_method_memory_free;
    }
    return Core::success();
}

// Decode PNG/JPEG (or other stb_image formats) to RGBA8 for Texture2D cook.
// Supports buffer-view embedded images and relative file URIs next to the glTF.
struct GltfImageDecodeBudget final {
    std::uint64_t externalFileBytes = 0;
    std::uint64_t decodedBytes = 0;
};

[[nodiscard]] Core::Result<std::pair<int, int>> decodeImageRgba8(
    const cgltf_image* image,
    const std::filesystem::path& gltfFilePath,
    std::vector<std::byte>& outRgba,
    GltfImageDecodeBudget& budget,
    GltfSourceCaptureContext* capture)
{
    if (image == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image is null");
    }
    const stbi_uc* encoded = nullptr;
    int encodedSize = 0;
    std::vector<std::byte> fileBytes;

    if (image->buffer_view != nullptr)
    {
        const cgltf_buffer_view* view = image->buffer_view;
        if (view->buffer == nullptr || view->buffer->data == nullptr)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image buffer_view has no data");
        }
        if (view->size == 0 || view->size > kMaxGltfExternalFileBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image buffer_view size invalid");
        }
        encoded = static_cast<const stbi_uc*>(view->buffer->data) + view->offset;
        encodedSize = static_cast<int>(view->size);
    }
    else if (image->uri != nullptr && image->uri[0] != '\0')
    {
        // data: URI
        if (std::strncmp(image->uri, "data:", 5) == 0)
        {
            // cgltf may not expand data URIs into buffer views for all images; fail clearly.
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF data-URI images must be bufferView-backed for cook (use GLB or buffer views)");
        }
        auto imagePath = resolveContainedGltfExternalPath(gltfFilePath, image->uri);
        if (!imagePath)
        {
            return Core::failure(std::move(imagePath.error()));
        }
        const std::filesystem::path root = gltfFilePath.parent_path();
        auto snapshot = GltfDetail::readFileSnapshot(*imagePath, &root, kMaxGltfExternalFileBytes);
        if (!snapshot)
        {
            return Core::failure(std::move(snapshot.error()));
        }
        if (!checkedAdd(budget.externalFileBytes, snapshot->fileSize, budget.externalFileBytes) ||
            budget.externalFileBytes > kMaxGltfTotalBufferBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF external image file budget exceeded");
        }
        if (capture != nullptr)
        {
            auto sourceIndex = captureGltfExternalSourceBytes(
                *capture, image->uri, AssetFormat::SourceImportReadExtent::WholeFile,
                snapshot->bytes);
            if (!sourceIndex)
            {
                return Core::failure(std::move(sourceIndex.error())
                                         .withContext("decodeImageRgba8", "source capture"));
            }
        }
        fileBytes = std::move(snapshot->bytes);
        encoded = reinterpret_cast<const stbi_uc*>(fileBytes.data());
        encodedSize = static_cast<int>(fileBytes.size());
    }
    else
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image has neither bufferView nor uri");
    }

    int headerWidth = 0;
    int headerHeight = 0;
    int headerComponents = 0;
    if (stbi_info_from_memory(encoded, encodedSize, &headerWidth, &headerHeight, &headerComponents) == 0 ||
        headerWidth <= 0 || headerHeight <= 0 ||
        static_cast<Core::u32>(headerWidth) > AssetFormat::Texture2DWire::MaxDimension ||
        static_cast<Core::u32>(headerHeight) > AssetFormat::Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF image header dimensions are invalid");
    }
    std::uint64_t pixelCount = 0;
    std::uint64_t byteCount = 0;
    if (!checkedMultiply(static_cast<std::uint64_t>(headerWidth),
                         static_cast<std::uint64_t>(headerHeight), pixelCount) ||
        !checkedMultiply(pixelCount, 4U, byteCount) || byteCount > kMaxGltfDecodedImageBytes ||
        !checkedAdd(budget.decodedBytes, byteCount, budget.decodedBytes) ||
        budget.decodedBytes > kMaxGltfTotalDecodedImageBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF decoded image byte budget exceeded");
    }

    int width = 0;
    int height = 0;
    int components = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded, encodedSize, &width, &height, &components, 4);
    if (pixels == nullptr || width != headerWidth || height != headerHeight)
    {
        stbi_image_free(pixels);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "stb_image failed to decode validated glTF image");
    }
    outRgba.resize(static_cast<std::size_t>(byteCount));
    std::memcpy(outRgba.data(), pixels, static_cast<std::size_t>(byteCount));
    stbi_image_free(pixels);
    return std::pair<int, int>{width, height};
}

[[nodiscard]] const cgltf_image* textureImage(const cgltf_texture* texture) noexcept
{
    return texture != nullptr ? texture->image : nullptr;
}

[[nodiscard]] const cgltf_image* baseColorImage(const cgltf_material* material) noexcept
{
    if (material == nullptr || !material->has_pbr_metallic_roughness)
    {
        return nullptr;
    }
    return textureImage(material->pbr_metallic_roughness.base_color_texture.texture);
}

[[nodiscard]] const cgltf_image* metallicRoughnessImage(const cgltf_material* material) noexcept
{
    if (material == nullptr || !material->has_pbr_metallic_roughness)
    {
        return nullptr;
    }
    return textureImage(material->pbr_metallic_roughness.metallic_roughness_texture.texture);
}

[[nodiscard]] const cgltf_image* normalImage(const cgltf_material* material) noexcept
{
    if (material == nullptr)
    {
        return nullptr;
    }
    return textureImage(material->normal_texture.texture);
}

} // namespace

namespace {

[[nodiscard]] Core::Status finalizeGltfSourceImports(CatalogCookSourceResult& result,
                                                     Core::u32 primarySourceIndex,
                                                     const GltfCookIds& requestedIds)
{
    if (primarySourceIndex >= result.sourceImports.sources.size() ||
        result.request.assets.size() > AssetFormat::SourceImportWire::MaxOutputs)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF source import contract exceeds current metadata limits");
    }

    const auto& primaryPath = result.sourceImports.sources[primarySourceIndex].path;
    auto contract = currentGltfSourceImportContract(primaryPath, requestedIds);
    if (!contract)
    {
        return Core::failure(std::move(contract.error())
                                 .withContext("finalizeGltfSourceImports", "currentContract"));
    }

    SourceImportCapturedUnit unit{
        .unitId = contract->unitId,
        .importerKind = contract->importerKind,
        .importerVersion = contract->importerVersion,
        .settingsHash = contract->settingsHash,
    };
    unit.inputs.reserve(result.sourceImports.sources.size());
    for (Core::u32 sourceIndex = 0; sourceIndex < result.sourceImports.sources.size(); ++sourceIndex)
    {
        unit.inputs.push_back(SourceImportCapturedInput{
            .sourceIndex = sourceIndex,
            .flags = sourceIndex == primarySourceIndex
                         ? AssetFormat::SourceImportInputFlags::Primary
                         : AssetFormat::SourceImportInputFlags::None,
        });
    }
    unit.outputs.reserve(result.request.assets.size());
    for (const auto& asset : result.request.assets)
    {
        unit.outputs.push_back(SourceImportCapturedOutput{
            .assetId = asset.assetId,
            .assetKind = asset.assetKind,
        });
    }
    result.sourceImports.units.push_back(std::move(unit));
    return Core::success();
}

[[nodiscard]] Core::Result<CatalogCookSourceResult> cookGltfFileToCatalogSourceResultImpl(
    std::string_view gltfUtf8Path,
    AssetFormat::TargetPlatform targetPlatform,
    const SourceImportCaptureConfig* captureConfig,
    GltfCookIds ids)
{
    if (gltfUtf8Path.empty() || !Core::isStrictUtf8WithoutNul(gltfUtf8Path))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF path must be strict UTF-8 without NUL");
    }
    if (targetPlatform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF cook target platform must be valid");
    }
    if (static_cast<bool>(ids.meshId) != static_cast<bool>(ids.materialId))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "fixed glTF mesh and material AssetIds must be provided together");
    }
    if (ids.meshId && ids.meshId == ids.materialId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "fixed glTF mesh and material AssetIds must be distinct");
    }
    if (ids.prefabId && (ids.prefabId == ids.meshId || ids.prefabId == ids.materialId))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "fixed glTF Prefab AssetId must be distinct from mesh and material IDs");
    }

    const GltfCookIds requestedIds = ids;
    CatalogCookSourceResult result{};
    result.sourceImports.targetPlatform = targetPlatform;
    GltfSourceCaptureContext capture{
        .config = captureConfig,
        .candidate = captureConfig != nullptr ? &result.sourceImports : nullptr,
    };
    GltfSourceCaptureContext* activeCapture = captureConfig != nullptr ? &capture : nullptr;
    Core::u32 primarySourceIndex = 0;
    std::string identityLocator;
    const std::filesystem::path requestedPath = Detail::pathFromUtf8Bytes(gltfUtf8Path);

    if (captureConfig != nullptr)
    {
        auto primaryPath = normalizeSourceImportPath(*captureConfig, gltfUtf8Path);
        if (!primaryPath)
        {
            return Core::failure(std::move(primaryPath.error())
                                     .withContext("cookGltfFileToCatalogSourceResult", "root preflight"));
        }
        identityLocator = std::move(*primaryPath);
    }
    else
    {
        const auto generic = requestedPath.lexically_normal().generic_u8string();
        identityLocator.assign(generic.begin(), generic.end());
        if (identityLocator.empty())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF path cannot produce an identity locator");
        }
    }
    if (activeCapture != nullptr)
    {
        capture.documentSourcePath = requestedPath;
    }
    auto source = GltfDetail::readFileSnapshot(requestedPath, nullptr, kMaxGltfSourceFileBytes);
    if (!source)
    {
        return Core::failure(std::move(source.error()));
    }
    const std::filesystem::path gltfFilePath = source->finalPath;
    if (activeCapture != nullptr)
    {
        auto sourceIndex = captureSourceImportBytes(result.sourceImports, *captureConfig,
                                                    gltfUtf8Path,
                                                    AssetFormat::SourceImportReadExtent::WholeFile,
                                                    source->bytes);
        if (!sourceIndex)
        {
            return Core::failure(std::move(sourceIndex.error())
                                     .withContext("cookGltfFileToCatalogSourceResult", "primary source"));
        }
        primarySourceIndex = *sourceIndex;
    }

    CgltfMemoryBudget memoryBudget{};
    cgltf_options options{};
    options.memory.alloc_func = &allocateCgltfMemory;
    options.memory.free_func = &freeCgltfMemory;
    options.memory.user_data = &memoryBudget;

    cgltf_data* rawData = nullptr;
    const cgltf_result parseResult =
        cgltf_parse(&options, source->bytes.data(), source->bytes.size(), &rawData);
    if (parseResult != cgltf_result_success)
    {
        if (parseResult == cgltf_result_out_of_memory && memoryBudget.limitExceeded)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF parser memory budget exceeded");
        }
        return Core::failure(mapCgltfResult(parseResult).error());
    }
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> dataOwner{rawData, &cgltf_free};
    cgltf_data* data = dataOwner.get();

    if (const auto status = validateGltfResourceStructure(data); !status)
    {
        return Core::failure(status.error());
    }
    if (const auto status = mapCgltfResult(cgltf_validate(data)); !status)
    {
        return Core::failure(status.error());
    }
    if (const auto status = loadGltfBuffersFromSnapshots(options, data, gltfFilePath, activeCapture); !status)
    {
        return Core::failure(status.error());
    }

    if (data->meshes_count == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF has no meshes");
    }

    if (!static_cast<bool>(ids.prefabId))
    {
        ids.prefabId = Detail::deriveVersionedAssetId(
            identityLocator, AssetFormat::AssetKind::Prefab,
            Detail::GltfPrefabAssetIdTag, 0U);
    }

    struct MeshEntry final {
        Core::AssetId meshId{};
        Core::AssetId materialId{};
        Core::AssetId baseColorTextureId{};
        Core::AssetId metallicRoughnessTextureId{};
        Core::AssetId normalTextureId{};
        std::vector<std::byte> meshPayload{};
        std::vector<std::byte> materialPayload{};
        AssetFormat::AssetKind meshKind = AssetFormat::AssetKind::StaticMesh;
    };
    // One glTF mesh may expand to N StaticMesh/Material pairs (one per TRIANGLES prim).
    struct MeshPrimRange final {
        std::size_t firstEntry = 0;
        std::size_t entryCount = 0;
    };
    struct TextureEntry final {
        Core::AssetId textureId{};
        std::vector<std::byte> payload{};
    };
    std::vector<MeshEntry> meshes;
    meshes.reserve(data->meshes_count);
    std::vector<TextureEntry> textures;
    // pointer equality for cgltf_mesh* → contiguous MeshEntry range
    std::unordered_map<const cgltf_mesh*, MeshPrimRange> meshRangeByPtr;
    std::unordered_map<const cgltf_mesh*, const cgltf_skin*> skinByMesh;
    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex)
    {
        const cgltf_node& node = data->nodes[nodeIndex];
        if (node.mesh == nullptr)
        {
            continue;
        }
        const auto found = skinByMesh.find(node.mesh);
        if (found != skinByMesh.end() && found->second != node.skin)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "one glTF mesh cannot mix skin bindings in A1");
        }
        skinByMesh.emplace(node.mesh, node.skin);
    }
    std::vector<GltfSkinCookInfo> cookedSkins;
    cookedSkins.reserve(data->skins_count);
    for (cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex)
    {
        auto cookedSkin = cookGltfSkin(data->skins[skinIndex]);
        if (!cookedSkin)
        {
            return Core::failure(std::move(cookedSkin.error()));
        }
        cookedSkins.push_back(std::move(*cookedSkin));
    }
    // Per-channel sequence so first baseColor is always < first MR < first normal (CatalogCook).
    Core::u32 nextTextureSeqBase = 0;
    Core::u32 nextTextureSeqMr = 0;
    Core::u32 nextTextureSeqNormal = 0;
    // Sequential slot across all prims: mesh at 2*slot, material at 2*slot+1 (AssetId-sorted deps).
    Core::u32 nextPrimSlot = 0;

    // Key: image pointer + channel (same image reused as base and MR gets two ids if needed... 
    // actually same image for different channels is rare; still key by image only and first channel wins?
    // Prefer key (image, channel) so shared image across materials reuses id per channel role.
    struct ImageChannelKey final {
        const cgltf_image* image = nullptr;
        GltfTextureChannel channel = GltfTextureChannel::BaseColor;
        bool operator==(const ImageChannelKey& o) const noexcept
        {
            return image == o.image && channel == o.channel;
        }
    };
    struct ImageChannelKeyHash final {
        std::size_t operator()(const ImageChannelKey& k) const noexcept
        {
            return std::hash<const void*>{}(k.image) ^ (static_cast<std::size_t>(k.channel) << 1);
        }
    };
    std::unordered_map<ImageChannelKey, Core::AssetId, ImageChannelKeyHash> imageChannelToTextureId;
    struct DecodedImage final {
        int width = 0;
        int height = 0;
        std::vector<std::byte> rgba{};
    };
    std::unordered_map<const cgltf_image*, DecodedImage> decodedImages;
    GltfImageDecodeBudget imageBudget{};
    std::uint64_t emittedTexturePixelBytes = 0;

    auto ensureTextureId = [&](const cgltf_image* image,
                               GltfTextureChannel channel) -> Core::Result<Core::AssetId> {
        if (image == nullptr)
        {
            return Core::AssetId{};
        }
        const ImageChannelKey key{.image = image, .channel = channel};
        const auto existing = imageChannelToTextureId.find(key);
        if (existing != imageChannelToTextureId.end())
        {
            return existing->second;
        }
        auto decoded = decodedImages.find(image);
        if (decoded == decodedImages.end())
        {
            DecodedImage candidate{};
            auto dims = decodeImageRgba8(image, gltfFilePath, candidate.rgba, imageBudget,
                                         activeCapture);
            if (!dims)
            {
                return Core::failure(std::move(dims.error()));
            }
            candidate.width = dims->first;
            candidate.height = dims->second;
            decoded = decodedImages.emplace(image, std::move(candidate)).first;
        }
        Core::u32 sequence = 0;
        switch (channel)
        {
        case GltfTextureChannel::BaseColor:
            sequence = nextTextureSeqBase++;
            break;
        case GltfTextureChannel::MetallicRoughness:
            sequence = nextTextureSeqMr++;
            break;
        case GltfTextureChannel::Normal:
            sequence = nextTextureSeqNormal++;
            break;
        }
        if (textures.size() >= kMaxGltfTextures ||
            !checkedAdd(emittedTexturePixelBytes, decoded->second.rgba.size(),
                        emittedTexturePixelBytes) ||
            emittedTexturePixelBytes > kMaxGltfTotalDecodedImageBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF cooked texture output budget exceeded");
        }
        const Core::AssetId textureId = deriveTextureChannelId(identityLocator, channel, sequence);
        auto texPayload = AssetFormat::writeTexture2DPayloadBytes(AssetFormat::Texture2DPayloadDesc{
            .width = static_cast<Core::u16>(decoded->second.width),
            .height = static_cast<Core::u16>(decoded->second.height),
            .pixelFormat = AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
            .pixels = decoded->second.rgba,
        });
        if (!texPayload)
        {
            return Core::failure(std::move(texPayload.error()));
        }
        imageChannelToTextureId.emplace(key, textureId);
        textures.push_back(TextureEntry{
            .textureId = textureId,
            .payload = std::move(*texPayload),
        });
        return textureId;
    };

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& mesh = data->meshes[meshIndex];
        if (mesh.primitives_count == 0)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF mesh has no primitives");
        }
        if (mesh.primitives_count > AssetFormat::StaticMeshWire::MaxSubmeshes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF mesh primitive count exceeds product limit");
        }
        const auto meshSkin = skinByMesh.find(&mesh);
        const bool hasSkinBinding = meshSkin != skinByMesh.end() && meshSkin->second != nullptr;

        const std::size_t rangeFirst = meshes.size();
        for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex)
        {
            const cgltf_primitive& prim = mesh.primitives[primIndex];
            if (auto status = validateGltfSkinAttributeContract(prim, hasSkinBinding); !status)
            {
                return Core::failure(std::move(status.error()));
            }
            auto pieces = extractTriangleMesh(prim);
            if (!pieces)
            {
                return Core::failure(std::move(pieces.error()));
            }

            // Optional fixed ids.meshId/materialId only apply to mesh 0 / primitive 0.
            Core::AssetId meshId{};
            Core::AssetId materialId{};
            if (meshIndex == 0 && primIndex == 0 && static_cast<bool>(ids.meshId) &&
                static_cast<bool>(ids.materialId))
            {
                meshId = ids.meshId;
                materialId = ids.materialId;
            }
            else
            {
                // Distinct tags so mesh/material never collide under dense sequential indexes.
                meshId = Detail::deriveVersionedAssetId(
                    identityLocator,
                    hasSkinBinding ? AssetFormat::AssetKind::SkinnedMesh
                                   : AssetFormat::AssetKind::StaticMesh,
                    Detail::GltfMeshAssetIdTag, nextPrimSlot);
                materialId = Detail::deriveVersionedAssetId(
                    identityLocator, AssetFormat::AssetKind::Material,
                    Detail::GltfMaterialAssetIdTag, nextPrimSlot);
            }
            ++nextPrimSlot;

            AssetFormat::StaticMeshPayloadDesc meshDesc{
                .indexType = AssetFormat::StaticMeshIndexType::U16,
                .boundsCenterX = pieces->boundsCenterX,
                .boundsCenterY = pieces->boundsCenterY,
                .boundsCenterZ = pieces->boundsCenterZ,
                .boundsRadius = pieces->boundsRadius,
                .submeshes = std::span<const AssetFormat::StaticMeshSubmeshDesc>(&pieces->submesh, 1),
                .vertices = pieces->vertices,
                .indices = pieces->indices,
            };
            const cgltf_material* material = prim.material;
            auto baseColorTextureId =
                ensureTextureId(baseColorImage(material), GltfTextureChannel::BaseColor);
            if (!baseColorTextureId)
            {
                return Core::failure(std::move(baseColorTextureId.error()));
            }
            auto metallicRoughnessTextureId =
                ensureTextureId(metallicRoughnessImage(material), GltfTextureChannel::MetallicRoughness);
            if (!metallicRoughnessTextureId)
            {
                return Core::failure(std::move(metallicRoughnessTextureId.error()));
            }
            auto normalTextureId =
                ensureTextureId(normalImage(material), GltfTextureChannel::Normal);
            if (!normalTextureId)
            {
                return Core::failure(std::move(normalTextureId.error()));
            }

            AssetFormat::MaterialPayloadDesc materialDesc{
                .model = AssetFormat::MaterialModel::UnlitBaseColor,
                .baseColorR = pieces->baseR,
                .baseColorG = pieces->baseG,
                .baseColorB = pieces->baseB,
                .baseColorA = pieces->baseA,
                .metallicFactor = pieces->metallicFactor,
                .roughnessFactor = pieces->roughnessFactor,
                .doubleSided = pieces->doubleSided,
                .alphaMode = pieces->alphaMode,
                .baseColorTextureId = *baseColorTextureId,
                .metallicRoughnessTextureId = *metallicRoughnessTextureId,
                .normalTextureId = *normalTextureId,
            };

            std::vector<std::byte> cookedMeshPayload;
            AssetFormat::AssetKind meshKind = AssetFormat::AssetKind::StaticMesh;
            if (hasSkinBinding)
            {
                const auto skinIt = std::find_if(cookedSkins.begin(), cookedSkins.end(),
                                                 [&](const GltfSkinCookInfo& candidate) {
                                                     return candidate.skin == meshSkin->second;
                                                 });
                if (skinIt == cookedSkins.end())
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "glTF mesh skin binding lookup failed");
                }
                auto influences = readGltfSkinInfluences(
                    prim, *skinIt, pieces->sourceVertexIndices);
                if (!influences)
                {
                    return Core::failure(std::move(influences.error()));
                }
                auto skinnedPayload = AssetFormat::writeSkinnedMeshPayloadBytes(
                    AssetFormat::SkinnedMeshPayloadDesc{
                        .indexType = AssetFormat::StaticMeshIndexType::U16,
                        .boundsCenterX = pieces->boundsCenterX,
                        .boundsCenterY = pieces->boundsCenterY,
                        .boundsCenterZ = pieces->boundsCenterZ,
                        .boundsRadius = pieces->boundsRadius,
                        .joints = skinIt->joints,
                        .inverseBindMatrices = skinIt->inverseBindMatrices,
                        .submeshes = std::span<const AssetFormat::StaticMeshSubmeshDesc>(&pieces->submesh, 1),
                        .vertices = pieces->vertices,
                        .jointIndices = influences->first,
                        .jointWeights = influences->second,
                        .indices = pieces->indices,
                    });
                if (!skinnedPayload)
                {
                    return Core::failure(std::move(skinnedPayload.error()));
                }
                cookedMeshPayload = std::move(*skinnedPayload);
                meshKind = AssetFormat::AssetKind::SkinnedMesh;
            }
            else
            {
                auto staticPayload = AssetFormat::writeStaticMeshPayloadBytes(meshDesc);
                if (!staticPayload)
                {
                    return Core::failure(std::move(staticPayload.error()));
                }
                cookedMeshPayload = std::move(*staticPayload);
            }
            auto materialPayload = AssetFormat::writeMaterialPayloadBytes(materialDesc);
            if (!materialPayload)
            {
                return Core::failure(std::move(materialPayload.error()));
            }

            meshes.push_back(MeshEntry{
                .meshId = meshId,
                .materialId = materialId,
                .baseColorTextureId = *baseColorTextureId,
                .metallicRoughnessTextureId = *metallicRoughnessTextureId,
                .normalTextureId = *normalTextureId,
                .meshPayload = std::move(cookedMeshPayload),
                .materialPayload = std::move(*materialPayload),
                .meshKind = meshKind,
            });
        }

        meshRangeByPtr.emplace(&mesh,
                               MeshPrimRange{.firstEntry = rangeFirst, .entryCount = meshes.size() - rangeFirst});
    }

    struct PendingNode final {
        const cgltf_node* node = nullptr;
        int parentIndex = -1;
    };
    std::vector<AssetFormat::PrefabNodeDesc> prefabNodes;
    prefabNodes.reserve(data->nodes_count);
    std::vector<PendingNode> pendingNodes;
    pendingNodes.reserve(data->nodes_count);
    if (data->scenes_count > 0)
    {
        for (cgltf_size i = data->scenes[0].nodes_count; i > 0; --i)
        {
            pendingNodes.push_back(PendingNode{.node = data->scenes[0].nodes[i - 1U]});
        }
    }
    else
    {
        for (cgltf_size i = data->nodes_count; i > 0; --i)
        {
            if (data->nodes[i - 1U].parent == nullptr)
            {
                pendingNodes.push_back(PendingNode{.node = &data->nodes[i - 1U]});
            }
        }
    }

    std::vector<bool> visitedNodes(data->nodes_count, false);
    while (!pendingNodes.empty())
    {
        const PendingNode pending = pendingNodes.back();
        pendingNodes.pop_back();
        if (pending.node == nullptr)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF scene contains a null node");
        }
        const cgltf_size nodeIndex = cgltf_node_index(data, pending.node);
        if (nodeIndex >= data->nodes_count || &data->nodes[nodeIndex] != pending.node ||
            visitedNodes[nodeIndex])
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF scene node hierarchy is invalid or repeated");
        }
        visitedNodes[nodeIndex] = true;

        MeshPrimRange range{};
        bool hasMeshRange = false;
        if (pending.node->mesh != nullptr)
        {
            const auto found = meshRangeByPtr.find(pending.node->mesh);
            if (found != meshRangeByPtr.end())
            {
                range = found->second;
                hasMeshRange = range.entryCount > 0;
            }
        }
        const std::size_t addedNodes =
            hasMeshRange && range.entryCount > 1U ? 1U + range.entryCount : 1U;
        if (addedNodes > AssetFormat::PrefabWire::MaxNodes - prefabNodes.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF expanded Prefab node count exceeds product limit");
        }

        const int selfIndex = static_cast<int>(prefabNodes.size());
        AssetFormat::PrefabNodeDesc desc{};
        desc.stableNodeId = static_cast<Core::u32>(prefabNodes.size() + 1U);
        desc.parentIndex = pending.parentIndex;
        if (pending.node->has_translation)
        {
            desc.positionX = pending.node->translation[0];
            desc.positionY = pending.node->translation[1];
            desc.positionZ = pending.node->translation[2];
        }
        if (pending.node->has_rotation)
        {
            desc.rotationX = pending.node->rotation[0];
            desc.rotationY = pending.node->rotation[1];
            desc.rotationZ = pending.node->rotation[2];
            desc.rotationW = pending.node->rotation[3];
        }
        if (pending.node->has_scale)
        {
            desc.scaleX = pending.node->scale[0];
            desc.scaleY = pending.node->scale[1];
            desc.scaleZ = pending.node->scale[2];
        }

        if (hasMeshRange && range.entryCount == 1U)
        {
            const MeshEntry& entry = meshes[range.firstEntry];
            desc.nodeKind = entry.meshKind == AssetFormat::AssetKind::SkinnedMesh
                                ? AssetFormat::PrefabNodeKind::SkinnedMesh3D
                                : AssetFormat::PrefabNodeKind::Mesh3D;
            desc.meshId = entry.meshId;
            desc.materialId = entry.materialId;
        }
        prefabNodes.push_back(desc);
        if (hasMeshRange && range.entryCount > 1U)
        {
            for (std::size_t i = 0; i < range.entryCount; ++i)
            {
                const MeshEntry& entry = meshes[range.firstEntry + i];
                prefabNodes.push_back(AssetFormat::PrefabNodeDesc{
                    .stableNodeId = static_cast<Core::u32>(prefabNodes.size() + 1U),
                    .parentIndex = selfIndex,
                    .nodeKind = entry.meshKind == AssetFormat::AssetKind::SkinnedMesh
                                    ? AssetFormat::PrefabNodeKind::SkinnedMesh3D
                                    : AssetFormat::PrefabNodeKind::Mesh3D,
                    .meshId = entry.meshId,
                    .materialId = entry.materialId,
                });
            }
        }

        for (cgltf_size child = pending.node->children_count; child > 0; --child)
        {
            pendingNodes.push_back(PendingNode{
                .node = pending.node->children[child - 1U],
                .parentIndex = selfIndex,
            });
        }
    }
    if (prefabNodes.empty() && !meshes.empty())
    {
        prefabNodes.push_back(AssetFormat::PrefabNodeDesc{
            .stableNodeId = 1,
            .parentIndex = -1,
            .nodeKind = meshes[0].meshKind == AssetFormat::AssetKind::SkinnedMesh
                            ? AssetFormat::PrefabNodeKind::SkinnedMesh3D
                            : AssetFormat::PrefabNodeKind::Mesh3D,
            .meshId = meshes[0].meshId,
            .materialId = meshes[0].materialId,
        });
    }

    auto prefabPayload =
        AssetFormat::writePrefabPayloadBytes(AssetFormat::PrefabPayloadDesc{.nodes = prefabNodes});
    if (!prefabPayload)
    {
        return Core::failure(std::move(prefabPayload.error()));
    }

    std::vector<CookedAnimationEntry> animations;
    animations.reserve(data->animations_count);
    for (cgltf_size animationIndex = 0; animationIndex < data->animations_count; ++animationIndex)
    {
        std::vector<AssetFormat::AnimationTrackDesc> trackStorage;
        std::vector<std::vector<float>> timeStorage;
        std::vector<std::vector<float>> valueStorage;
        auto animationDesc = makeGltfAnimationDesc(data->animations[animationIndex], cookedSkins,
                                                   trackStorage, timeStorage, valueStorage);
        if (!animationDesc)
        {
            return Core::failure(std::move(animationDesc.error()));
        }
        auto animationPayload = AssetFormat::writeAnimationClip3DPayloadBytes(*animationDesc);
        if (!animationPayload)
        {
            return Core::failure(std::move(animationPayload.error()));
        }
        animations.push_back(CookedAnimationEntry{
            .assetId = Detail::deriveVersionedAssetId(
                identityLocator, AssetFormat::AssetKind::AnimationClip3D,
                Detail::GltfAnimationAssetIdTag, static_cast<Core::u32>(animationIndex)),
            .payload = std::move(*animationPayload),
        });
    }

    CatalogCookRequest& request = result.request;
    request.targetPlatform = targetPlatform;
    // Dependencies-first: textures before materials that reference them.
    for (auto& tex : textures)
    {
        request.assets.push_back(CatalogCookAssetSpec{
            .assetKind = AssetFormat::AssetKind::Texture2D,
            .assetId = tex.textureId,
            .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion,
            .payload = std::move(tex.payload),
        });
    }
    for (auto& entry : meshes)
    {
        request.assets.push_back(CatalogCookAssetSpec{
            .assetKind = entry.meshKind,
            .assetId = entry.meshId,
            .assetTypeVersion = entry.meshKind == AssetFormat::AssetKind::SkinnedMesh
                                    ? AssetFormat::SkinnedMeshWire::SchemaVersion
                                    : AssetFormat::StaticMeshWire::SchemaVersion,
            .payload = std::move(entry.meshPayload),
        });
        CatalogCookAssetSpec materialSpec{
            .assetKind = AssetFormat::AssetKind::Material,
            .assetId = entry.materialId,
            .assetTypeVersion = AssetFormat::MaterialWire::SchemaVersion,
            .payload = std::move(entry.materialPayload),
        };
        // Flag order: baseColor, metallicRoughness, normal (matches MaterialPayload flags).
        // Channel-tagged texture ids guarantee base < MR < normal when all three present.
        const std::array textureDeps{entry.baseColorTextureId, entry.metallicRoughnessTextureId,
                                     entry.normalTextureId};
        for (const Core::AssetId textureId : textureDeps)
        {
            if (static_cast<bool>(textureId))
            {
                materialSpec.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = AssetFormat::AssetKind::Texture2D,
                    .flags = AssetFormat::DependencyFlags::Required,
                });
            }
        }
        request.assets.push_back(std::move(materialSpec));
    }
    for (auto& animation : animations)
    {
        request.assets.push_back(CatalogCookAssetSpec{
            .assetKind = AssetFormat::AssetKind::AnimationClip3D,
            .assetId = animation.assetId,
            .assetTypeVersion = AssetFormat::AnimationClip3DWire::SchemaVersion,
            .payload = std::move(animation.payload),
        });
    }
    CatalogCookAssetSpec prefabSpec{
        .assetKind = AssetFormat::AssetKind::Prefab,
        .assetId = ids.prefabId,
        .assetTypeVersion = AssetFormat::PrefabWire::SchemaVersion,
        .payload = std::move(*prefabPayload),
    };
    for (const auto& node : prefabNodes)
    {
        if (static_cast<bool>(node.meshId))
        {
                prefabSpec.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                    .assetId = node.meshId,
                    .expectedKind = [&]() {
                        const auto mesh = std::find_if(meshes.begin(), meshes.end(),
                                                       [&](const MeshEntry& candidate) {
                                                           return candidate.meshId == node.meshId;
                                                       });
                        return mesh == meshes.end() ? AssetFormat::AssetKind::Invalid : mesh->meshKind;
                    }(),
                    .flags = AssetFormat::DependencyFlags::Required,
                });
            prefabSpec.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                .assetId = node.materialId,
                .expectedKind = AssetFormat::AssetKind::Material,
                .flags = AssetFormat::DependencyFlags::Required,
            });
        }
    }
    // Payload nodes own reference identity; the Catalog dependency set is canonical and deduplicated.
    std::sort(prefabSpec.dependencies.begin(), prefabSpec.dependencies.end(),
              [](const AssetFormat::CookedAssetWriteDependency& a,
                 const AssetFormat::CookedAssetWriteDependency& b) { return a.assetId < b.assetId; });
    const auto conflictingDependency = std::adjacent_find(
        prefabSpec.dependencies.begin(), prefabSpec.dependencies.end(),
        [](const AssetFormat::CookedAssetWriteDependency& left,
           const AssetFormat::CookedAssetWriteDependency& right) {
            return left.assetId == right.assetId && left.expectedKind != right.expectedKind;
        });
    if (conflictingDependency != prefabSpec.dependencies.end())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF output AssetId has conflicting dependency kinds");
    }
    prefabSpec.dependencies.erase(std::unique(prefabSpec.dependencies.begin(), prefabSpec.dependencies.end(),
                                              [](const AssetFormat::CookedAssetWriteDependency& a,
                                                 const AssetFormat::CookedAssetWriteDependency& b) {
                                                  return a.assetId == b.assetId;
                                              }),
                                  prefabSpec.dependencies.end());
    request.assets.push_back(std::move(prefabSpec));
    if (activeCapture != nullptr)
    {
        if (auto status = finalizeGltfSourceImports(result, primarySourceIndex, requestedIds); !status)
        {
            return Core::failure(std::move(status.error()));
        }
    }
    return result;
}

[[nodiscard]] Core::Result<CatalogCookSourceResult> cookGltfFileToCatalogSourceResultBoundary(
    std::string_view gltfUtf8Path,
    AssetFormat::TargetPlatform targetPlatform,
    const SourceImportCaptureConfig* captureConfig,
    GltfCookIds ids) noexcept
{
    try
    {
        return cookGltfFileToCatalogSourceResultImpl(gltfUtf8Path, targetPlatform,
                                                     captureConfig, ids);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "glTF cook allocation failed within bounded input policy");
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "glTF cook filesystem operation failed");
    }
    catch (...)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "unexpected glTF cook failure");
    }
}

} // namespace

Core::Result<CatalogCookRequest> cookGltfFileToCatalogRequest(
    std::string_view gltfUtf8Path,
    AssetFormat::TargetPlatform targetPlatform,
    GltfCookIds ids) noexcept
{
    auto result = cookGltfFileToCatalogSourceResultBoundary(gltfUtf8Path, targetPlatform,
                                                            nullptr, ids);
    if (!result)
    {
        return Core::failure(std::move(result.error()));
    }
    return std::move(result->request);
}

Core::Result<CatalogCookSourceResult> cookGltfFileToCatalogSourceResult(
    std::string_view gltfUtf8Path,
    AssetFormat::TargetPlatform targetPlatform,
    SourceImportCaptureConfig captureConfig,
    GltfCookIds ids) noexcept
{
    return cookGltfFileToCatalogSourceResultBoundary(gltfUtf8Path, targetPlatform,
                                                     &captureConfig, ids);
}

namespace {

[[nodiscard]] Core::Result<GltfSkinCookInfo> cookGltfSkin(const cgltf_skin& skin)
{
    if (skin.joints_count == 0U || skin.joints_count > AssetFormat::SkinnedMeshWire::MaxJointCount ||
        skin.joints == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF skin joint count exceeds SkinnedMesh v1 limit");
    }
    GltfSkinCookInfo out{.skin = &skin};
    out.joints.resize(skin.joints_count);
    out.inverseBindMatrices.resize(skin.joints_count * AssetFormat::SkinnedMeshWire::FloatsPerInverseBindMatrix,
                                   0.0F);
    out.sourceJointToCookedJoint.resize(skin.joints_count);
    std::unordered_map<const cgltf_node*, Core::u16> sourceJointIndexByNode;
    sourceJointIndexByNode.reserve(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i)
    {
        const cgltf_node* node = skin.joints[i];
        if (node == nullptr || sourceJointIndexByNode.contains(node))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF skin joints must be unique and non-null");
        }
        sourceJointIndexByNode.emplace(node, static_cast<Core::u16>(i));
    }

    std::vector<Core::u16> sourceParents(skin.joints_count,
                                         AssetFormat::SkinnedMeshWire::JointIndexNone);
    for (cgltf_size sourceIndex = 0; sourceIndex < skin.joints_count; ++sourceIndex)
    {
        const cgltf_node* node = skin.joints[sourceIndex];
        if (node->has_matrix)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF skin joints with matrix transforms are not supported in v1");
        }
        if (node->parent != nullptr)
        {
            const auto parent = sourceJointIndexByNode.find(node->parent);
            if (parent == sourceJointIndexByNode.end())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF skin joint parent outside the skin is unsupported in v1");
            }
            sourceParents[sourceIndex] = parent->second;
        }
    }

    std::vector<Core::u16> hierarchyDepths(skin.joints_count, 0U);
    std::vector<Core::u16> cookedSourceOrder(skin.joints_count, 0U);
    for (cgltf_size sourceIndex = 0; sourceIndex < skin.joints_count; ++sourceIndex)
    {
        cookedSourceOrder[sourceIndex] = static_cast<Core::u16>(sourceIndex);
        Core::u16 cursor = static_cast<Core::u16>(sourceIndex);
        Core::u32 depth = 0;
        while (sourceParents[cursor] != AssetFormat::SkinnedMeshWire::JointIndexNone)
        {
            cursor = sourceParents[cursor];
            ++depth;
            if (depth >= skin.joints_count)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF skin joint hierarchy contains a cycle");
            }
        }
        hierarchyDepths[sourceIndex] = static_cast<Core::u16>(depth);
    }
    std::sort(cookedSourceOrder.begin(), cookedSourceOrder.end(),
              [&](Core::u16 left, Core::u16 right) {
                  if (hierarchyDepths[left] != hierarchyDepths[right])
                  {
                      return hierarchyDepths[left] < hierarchyDepths[right];
                  }
                  return left < right;
              });
    out.jointIndexByNode.reserve(skin.joints_count);
    for (cgltf_size cookedIndex = 0; cookedIndex < skin.joints_count; ++cookedIndex)
    {
        const Core::u16 sourceIndex = cookedSourceOrder[cookedIndex];
        out.sourceJointToCookedJoint[sourceIndex] = static_cast<Core::u16>(cookedIndex);
        out.jointIndexByNode.emplace(skin.joints[sourceIndex],
                                     static_cast<Core::u16>(cookedIndex));
    }

    for (cgltf_size cookedIndex = 0; cookedIndex < skin.joints_count; ++cookedIndex)
    {
        const Core::u16 sourceIndex = cookedSourceOrder[cookedIndex];
        const cgltf_node* node = skin.joints[sourceIndex];
        auto& joint = out.joints[cookedIndex];
        if (sourceParents[sourceIndex] != AssetFormat::SkinnedMeshWire::JointIndexNone)
        {
            joint.parentJoint = out.sourceJointToCookedJoint[sourceParents[sourceIndex]];
            if (joint.parentJoint >= cookedIndex)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF skin topology remap did not place parent before child");
            }
        }
        if (node->has_translation)
        {
            std::copy_n(node->translation, 3U, joint.bindTranslation);
        }
        if (node->has_rotation)
        {
            std::copy_n(node->rotation, 4U, joint.bindRotation);
        }
        if (node->has_scale)
        {
            std::copy_n(node->scale, 3U, joint.bindScale);
        }
    }

    if (skin.inverse_bind_matrices == nullptr)
    {
        for (cgltf_size i = 0; i < skin.joints_count; ++i)
        {
            float* matrix = out.inverseBindMatrices.data() + i * 16U;
            matrix[0] = 1.0F;
            matrix[5] = 1.0F;
            matrix[10] = 1.0F;
            matrix[15] = 1.0F;
        }
    }
    else
    {
        const cgltf_accessor* accessor = skin.inverse_bind_matrices;
        if (accessor->type != cgltf_type_mat4 || accessor->component_type != cgltf_component_type_r_32f ||
            accessor->count != skin.joints_count)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF inverseBindMatrices must be a FLOAT MAT4 per joint");
        }
        for (cgltf_size sourceIndex = 0; sourceIndex < accessor->count; ++sourceIndex)
        {
            const Core::u16 cookedIndex = out.sourceJointToCookedJoint[sourceIndex];
            if (!cgltf_accessor_read_float(
                    accessor, sourceIndex,
                    out.inverseBindMatrices.data() + static_cast<std::size_t>(cookedIndex) * 16U,
                    16U))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "failed to read glTF inverseBindMatrices");
            }
        }
    }
    for (const float value : out.inverseBindMatrices)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF inverseBindMatrices must be finite");
        }
    }
    return out;
}

[[nodiscard]] Core::Result<std::pair<std::vector<Core::u16>, std::vector<Core::u16>>>
readGltfSkinInfluences(const cgltf_primitive& prim, const GltfSkinCookInfo& skin,
                       std::span<const Core::u16> sourceVertexIndices)
{
    const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* joints = findAttribute(prim, cgltf_attribute_type_joints);
    const cgltf_accessor* weights = findAttribute(prim, cgltf_attribute_type_weights);
    const bool supportedWeights =
        weights != nullptr &&
        (weights->component_type == cgltf_component_type_r_32f ||
         (weights->normalized &&
          (weights->component_type == cgltf_component_type_r_8u ||
           weights->component_type == cgltf_component_type_r_16u)));
    if (positions == nullptr || joints == nullptr || weights == nullptr ||
        joints->type != cgltf_type_vec4 ||
        weights->type != cgltf_type_vec4 || joints->count != weights->count ||
        joints->count != positions->count || sourceVertexIndices.empty() ||
        joints->normalized ||
        (joints->component_type != cgltf_component_type_r_8u &&
         joints->component_type != cgltf_component_type_r_16u) ||
        !supportedWeights)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "skinned primitive requires matching JOINTS_0/WEIGHTS_0 VEC4 accessors");
    }
    std::vector<Core::u16> indices(sourceVertexIndices.size() *
                                   AssetFormat::SkinnedMeshWire::InfluencesPerVertex);
    std::vector<Core::u16> quantized(indices.size());
    for (std::size_t vertex = 0; vertex < sourceVertexIndices.size(); ++vertex)
    {
        const cgltf_size sourceVertex = sourceVertexIndices[vertex];
        if (sourceVertex >= joints->count)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "skinned primitive source vertex mapping is out of range");
        }
        cgltf_uint jointValues[4]{};
        float weightValues[4]{};
        if (!cgltf_accessor_read_uint(joints, sourceVertex, jointValues, 4U) ||
            !cgltf_accessor_read_float(weights, sourceVertex, weightValues, 4U))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "failed to read skinned primitive influences");
        }
        struct Influence final {
            Core::u16 joint = 0;
            double weight = 0.0;
        } values[4]{};
        std::size_t valueCount = 0;
        double total = 0.0;
        for (int slot = 0; slot < 4; ++slot)
        {
            if (jointValues[slot] >= skin.sourceJointToCookedJoint.size() ||
                !std::isfinite(weightValues[slot]) ||
                weightValues[slot] < 0.0F)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF skin influence joint or weight is invalid");
            }
            const Core::u16 cookedJoint =
                skin.sourceJointToCookedJoint[jointValues[slot]];
            const double weight = static_cast<double>(weightValues[slot]);
            total += weight;
            if (weight == 0.0)
            {
                continue;
            }
            const auto existing = std::find_if(
                values, values + valueCount,
                [cookedJoint](const Influence& influence) { return influence.joint == cookedJoint; });
            if (existing != values + valueCount)
            {
                existing->weight += weight;
            }
            else
            {
                values[valueCount++] = Influence{.joint = cookedJoint, .weight = weight};
            }
        }
        if (!std::isfinite(total) || !(total > 1.0e-12))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF skin influence weights must have a positive sum");
        }
        std::sort(values, values + valueCount,
                  [](const Influence& left, const Influence& right) {
                      return left.joint < right.joint;
                  });
        const std::size_t base = static_cast<std::size_t>(vertex) * 4U;
        Core::u32 assigned = 0;
        Core::u32 encodedWeights[4]{};
        double fractions[4]{};
        std::size_t remainderOrder[4]{0U, 1U, 2U, 3U};
        for (std::size_t slot = 0; slot < valueCount; ++slot)
        {
            const double exact = values[slot].weight / total *
                                 static_cast<double>(AssetFormat::SkinnedMeshWire::WeightScale);
            const auto floorValue = static_cast<Core::u32>(std::floor(exact));
            encodedWeights[slot] = floorValue;
            assigned += floorValue;
            fractions[slot] = exact - std::floor(exact);
        }
        if (assigned > AssetFormat::SkinnedMeshWire::WeightScale)
        {
            std::sort(remainderOrder, remainderOrder + valueCount,
                      [&](std::size_t left, std::size_t right) {
                          if (fractions[left] != fractions[right])
                          {
                              return fractions[left] < fractions[right];
                          }
                          return values[left].joint > values[right].joint;
                      });
            Core::u32 excess = assigned - AssetFormat::SkinnedMeshWire::WeightScale;
            for (std::size_t cursor = 0; excess != 0U; ++cursor)
            {
                const std::size_t slot = remainderOrder[cursor % valueCount];
                if (encodedWeights[slot] != 0U)
                {
                    --encodedWeights[slot];
                    --excess;
                }
            }
        }
        else if (assigned < AssetFormat::SkinnedMeshWire::WeightScale)
        {
            std::sort(remainderOrder, remainderOrder + valueCount,
                      [&](std::size_t left, std::size_t right) {
                          if (fractions[left] != fractions[right])
                          {
                              return fractions[left] > fractions[right];
                          }
                          return values[left].joint < values[right].joint;
                      });
            Core::u32 remainder = AssetFormat::SkinnedMeshWire::WeightScale - assigned;
            for (std::size_t cursor = 0; remainder != 0U; ++cursor)
            {
                const std::size_t slot = remainderOrder[cursor % valueCount];
                if (encodedWeights[slot] < AssetFormat::SkinnedMeshWire::WeightScale)
                {
                    ++encodedWeights[slot];
                    --remainder;
                }
            }
        }
        struct QuantizedInfluence final {
            Core::u16 joint = 0;
            Core::u16 weight = 0;
        } encoded[4]{};
        // u16-typed rather than 0U: mixing 0U with a u16 makes the conditional's common
        // type int, and narrowing int back to u16 in an initializer list is ill-formed.
        // Clang rejects it; MSVC accepts it silently.
        constexpr Core::u16 unusedJoint = 0U;
        for (std::size_t slot = 0; slot < valueCount; ++slot)
        {
            encoded[slot] = QuantizedInfluence{
                .joint = encodedWeights[slot] == 0U ? unusedJoint : values[slot].joint,
                .weight = static_cast<Core::u16>(encodedWeights[slot]),
            };
        }
        std::sort(std::begin(encoded), std::end(encoded),
                  [](const QuantizedInfluence& left, const QuantizedInfluence& right) {
                      if (left.weight != right.weight)
                      {
                          return left.weight > right.weight;
                      }
                      return left.joint < right.joint;
                  });
        for (int slot = 0; slot < 4; ++slot)
        {
            indices[base + slot] = encoded[slot].joint;
            quantized[base + slot] = encoded[slot].weight;
        }
    }
    return std::pair{std::move(indices), std::move(quantized)};
}

[[nodiscard]] Core::Result<AssetFormat::AnimationClip3DPayloadDesc>
makeGltfAnimationDesc(const cgltf_animation& animation,
                      const std::vector<GltfSkinCookInfo>& skins,
                      std::vector<AssetFormat::AnimationTrackDesc>& trackStorage,
                      std::vector<std::vector<float>>& timeStorage,
                      std::vector<std::vector<float>>& valueStorage)
{
    if (animation.channels_count == 0U || animation.channels_count > AssetFormat::AnimationClip3DWire::MaxTracks)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF animation channel count exceeds AnimationClip3D v1 limit");
    }
    const GltfSkinCookInfo* selectedSkin = nullptr;
    trackStorage.clear();
    timeStorage.clear();
    valueStorage.clear();
    trackStorage.reserve(animation.channels_count);
    timeStorage.reserve(animation.channels_count);
    valueStorage.reserve(animation.channels_count);
    Core::u32 totalKeyframes = 0;
    Core::u32 totalValueFloats = 0;
    for (cgltf_size channelIndex = 0; channelIndex < animation.channels_count; ++channelIndex)
    {
        const cgltf_animation_channel& channel = animation.channels[channelIndex];
        if (channel.sampler == nullptr || channel.target_node == nullptr ||
            (channel.target_path != cgltf_animation_path_type_translation &&
             channel.target_path != cgltf_animation_path_type_rotation &&
             channel.target_path != cgltf_animation_path_type_scale) ||
            (channel.sampler->interpolation != cgltf_interpolation_type_linear &&
             channel.sampler->interpolation != cgltf_interpolation_type_step))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF animation channel target or interpolation is unsupported");
        }
        const GltfSkinCookInfo* channelSkin = nullptr;
        Core::u16 jointIndex = 0;
        for (const auto& skin : skins)
        {
            const auto found = skin.jointIndexByNode.find(channel.target_node);
            if (found != skin.jointIndexByNode.end())
            {
                if (channelSkin != nullptr)
                {
                    return Core::failure(
                        AssetErrorCode::InvalidCatalogConfig,
                        "glTF animation target joint belongs to multiple skins");
                }
                channelSkin = &skin;
                jointIndex = found->second;
            }
        }
        if (channelSkin == nullptr || (selectedSkin != nullptr && selectedSkin != channelSkin))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF animation must target joints of one skin");
        }
        selectedSkin = channelSkin;
        const cgltf_accessor* input = channel.sampler->input;
        const cgltf_accessor* output = channel.sampler->output;
        const auto path = channel.target_path;
        const auto tinaChannel = path == cgltf_animation_path_type_translation
                                     ? AssetFormat::AnimationChannel::Translation
                                     : path == cgltf_animation_path_type_rotation
                                           ? AssetFormat::AnimationChannel::Rotation
                                           : AssetFormat::AnimationChannel::Scale;
        const Core::u16 components = AssetFormat::animationChannelComponentCount(tinaChannel);
        if (input == nullptr || output == nullptr || input->type != cgltf_type_scalar ||
            input->component_type != cgltf_component_type_r_32f || output->component_type != cgltf_component_type_r_32f ||
            output->type != (components == 4U ? cgltf_type_vec4 : cgltf_type_vec3) ||
            input->count == 0U || input->count > AssetFormat::AnimationClip3DWire::MaxKeyframesPerTrack ||
            output->count != input->count)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF animation sampler accessor shape is invalid");
        }
        if (input->count > AssetFormat::AnimationClip3DWire::MaxTotalKeyframes - totalKeyframes ||
            input->count * components >
                AssetFormat::AnimationClip3DWire::MaxTotalValueFloats - totalValueFloats)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF animation aggregate key budget exceeded");
        }
        totalKeyframes += static_cast<Core::u32>(input->count);
        totalValueFloats += static_cast<Core::u32>(input->count * components);
        timeStorage.emplace_back(input->count);
        valueStorage.emplace_back(input->count * components);
        for (cgltf_size key = 0; key < input->count; ++key)
        {
            float time = 0.0F;
            if (!cgltf_accessor_read_float(input, key, &time, 1U) || !std::isfinite(time) || time < 0.0F ||
                time > AssetFormat::AnimationClip3DWire::MaxDurationSeconds)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "glTF animation key time is invalid");
            }
            timeStorage.back()[key] = time;
            if (!cgltf_accessor_read_float(output, key, valueStorage.back().data() + key * components, components))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "failed to read glTF animation sampler output");
            }
        }
        trackStorage.push_back(AssetFormat::AnimationTrackDesc{
            .jointIndex = jointIndex,
            .channel = tinaChannel,
            .interpolation = channel.sampler->interpolation == cgltf_interpolation_type_step
                                 ? AssetFormat::AnimationInterpolation::Step
                                 : AssetFormat::AnimationInterpolation::Linear,
            .times = timeStorage.back(),
            .values = valueStorage.back(),
        });
    }
    if (selectedSkin == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF animation has no skin joint targets");
    }
    float duration = 0.0F;
    for (const auto& times : timeStorage) duration = (std::max)(duration, times.back());
    std::sort(trackStorage.begin(), trackStorage.end(),
              [](const AssetFormat::AnimationTrackDesc& left,
                 const AssetFormat::AnimationTrackDesc& right) {
                  if (left.jointIndex != right.jointIndex)
                  {
                      return left.jointIndex < right.jointIndex;
                  }
                  return left.channel < right.channel;
              });
    const auto duplicate = std::adjacent_find(
        trackStorage.begin(), trackStorage.end(),
        [](const AssetFormat::AnimationTrackDesc& left,
           const AssetFormat::AnimationTrackDesc& right) {
            return left.jointIndex == right.jointIndex && left.channel == right.channel;
        });
    if (duplicate != trackStorage.end())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF animation contains duplicate joint/channel tracks");
    }
    return AssetFormat::AnimationClip3DPayloadDesc{
        .playbackMode = AssetFormat::AnimationClip3DPlaybackMode::Loop,
        .jointCount = static_cast<Core::u16>(selectedSkin->joints.size()),
        .durationSeconds = duration,
        .tracks = trackStorage,
    };
}

} // namespace

} // namespace Tina::Asset
