#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <tina/asset/GltfCook.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId deriveId(std::string_view seed, Core::u8 tag)
{
    Core::AssetId::Bytes bytes{};
    for (std::size_t i = 0; i < seed.size(); ++i)
    {
        bytes[i % 16] = static_cast<std::byte>(
            static_cast<Core::u8>(bytes[i % 16]) ^ static_cast<Core::u8>(seed[i]) ^ tag);
    }
    bytes[0] = static_cast<std::byte>(tag);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Core::AssetId deriveIndexedId(std::string_view seed, Core::u8 tag, Core::u32 index)
{
    Core::AssetId id = deriveId(seed, tag);
    Core::AssetId::Bytes bytes = id.bytes();
    bytes[1] = static_cast<std::byte>(static_cast<Core::u8>(bytes[1]) ^ static_cast<Core::u8>(index & 0xFFU));
    bytes[2] = static_cast<std::byte>(static_cast<Core::u8>(bytes[2]) ^ static_cast<Core::u8>((index >> 8) & 0xFFU));
    bytes[3] = static_cast<std::byte>(static_cast<Core::u8>(bytes[3]) ^ static_cast<Core::u8>((index >> 16) & 0xFFU));
    return *Core::AssetId::fromBytes(bytes);
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

[[nodiscard]] const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
    {
        if (prim.attributes[i].type == type)
        {
            return prim.attributes[i].data;
        }
    }
    return nullptr;
}

struct CookedMeshPieces final {
    std::vector<float> vertices{};
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
    bool doubleSided = false;
};

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

    CookedMeshPieces out{};
    out.vertices.resize(static_cast<std::size_t>(positions->count) * 8U);
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        float p[3]{};
        float n[3]{0.0F, 1.0F, 0.0F};
        float uv[2]{0.0F, 0.0F};
        if (!cgltf_accessor_read_float(positions, i, p, 3))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "failed to read POSITION");
        }
        if (normals != nullptr && normals->type == cgltf_type_vec3)
        {
            (void)cgltf_accessor_read_float(normals, i, n, 3);
        }
        if (texcoords != nullptr && texcoords->type == cgltf_type_vec2)
        {
            (void)cgltf_accessor_read_float(texcoords, i, uv, 2);
        }
        const std::size_t base = static_cast<std::size_t>(i) * 8U;
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
        if (prim.indices->count % 3 != 0)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "index count must be multiple of 3");
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

    float minX = out.vertices[0], minY = out.vertices[1], minZ = out.vertices[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        const std::size_t base = static_cast<std::size_t>(i) * 8U;
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
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        const std::size_t base = static_cast<std::size_t>(i) * 8U;
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

    if (prim.material != nullptr && prim.material->has_pbr_metallic_roughness)
    {
        out.baseR = prim.material->pbr_metallic_roughness.base_color_factor[0];
        out.baseG = prim.material->pbr_metallic_roughness.base_color_factor[1];
        out.baseB = prim.material->pbr_metallic_roughness.base_color_factor[2];
        out.baseA = prim.material->pbr_metallic_roughness.base_color_factor[3];
        out.doubleSided = prim.material->double_sided != 0;
    }
    return out;
}

} // namespace

Core::Result<CatalogCookRequest> cookGltfFileToCatalogRequest(std::string_view gltfUtf8Path,
                                                              GltfCookIds ids) noexcept
{
    if (gltfUtf8Path.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF path is empty");
    }

    const std::string path{gltfUtf8Path};
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (const auto status = mapCgltfResult(cgltf_parse_file(&options, path.c_str(), &data)); !status)
    {
        return Core::failure(status.error());
    }
    if (const auto status = mapCgltfResult(cgltf_load_buffers(&options, data, path.c_str())); !status)
    {
        cgltf_free(data);
        return Core::failure(status.error());
    }
    if (const auto status = mapCgltfResult(cgltf_validate(data)); !status)
    {
        cgltf_free(data);
        return Core::failure(status.error());
    }

    if (data->meshes_count == 0)
    {
        cgltf_free(data);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF has no meshes");
    }

    if (!static_cast<bool>(ids.prefabId))
    {
        ids.prefabId = deriveId(gltfUtf8Path, 0x73);
    }

    struct MeshEntry final {
        Core::AssetId meshId{};
        Core::AssetId materialId{};
        std::vector<std::byte> meshPayload{};
        std::vector<std::byte> materialPayload{};
    };
    std::vector<MeshEntry> meshes;
    meshes.reserve(data->meshes_count);
    // pointer equality for cgltf_mesh* → entry index
    std::unordered_map<const cgltf_mesh*, std::size_t> meshIndexByPtr;

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& mesh = data->meshes[meshIndex];
        if (mesh.primitives_count == 0)
        {
            cgltf_free(data);
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF mesh has no primitives");
        }
        if (mesh.primitives_count > 1)
        {
            cgltf_free(data);
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "multi-primitive meshes are not supported (use one TRIANGLES prim per mesh)");
        }
        auto pieces = extractTriangleMesh(mesh.primitives[0]);
        if (!pieces)
        {
            cgltf_free(data);
            return Core::failure(std::move(pieces.error()));
        }

        Core::AssetId meshId{};
        Core::AssetId materialId{};
        if (meshIndex == 0 && static_cast<bool>(ids.meshId))
        {
            meshId = ids.meshId;
        }
        else
        {
            meshId = deriveIndexedId(gltfUtf8Path, 0x71, static_cast<Core::u32>(meshIndex));
        }
        if (meshIndex == 0 && static_cast<bool>(ids.materialId))
        {
            materialId = ids.materialId;
        }
        else
        {
            materialId = deriveIndexedId(gltfUtf8Path, 0x72, static_cast<Core::u32>(meshIndex));
        }

        AssetFormat::StaticMeshPayloadDesc meshDesc{
            .vertexLayout = AssetFormat::StaticMeshVertexLayout::P3N3UV2,
            .indexType = AssetFormat::StaticMeshIndexType::U16,
            .boundsCenterX = pieces->boundsCenterX,
            .boundsCenterY = pieces->boundsCenterY,
            .boundsCenterZ = pieces->boundsCenterZ,
            .boundsRadius = pieces->boundsRadius,
            .submeshes = std::span<const AssetFormat::StaticMeshSubmeshDesc>(&pieces->submesh, 1),
            .vertices = pieces->vertices,
            .indices = pieces->indices,
        };
        AssetFormat::MaterialPayloadDesc materialDesc{
            .model = AssetFormat::MaterialModel::UnlitBaseColor,
            .baseColorR = pieces->baseR,
            .baseColorG = pieces->baseG,
            .baseColorB = pieces->baseB,
            .baseColorA = pieces->baseA,
            .doubleSided = pieces->doubleSided,
            .alphaMode = AssetFormat::MaterialAlphaMode::Opaque,
        };

        auto meshPayload = AssetFormat::writeStaticMeshPayloadBytes(meshDesc);
        if (!meshPayload)
        {
            cgltf_free(data);
            return Core::failure(std::move(meshPayload.error()));
        }
        auto materialPayload = AssetFormat::writeMaterialPayloadBytes(materialDesc);
        if (!materialPayload)
        {
            cgltf_free(data);
            return Core::failure(std::move(materialPayload.error()));
        }

        meshIndexByPtr.emplace(&mesh, meshes.size());
        meshes.push_back(MeshEntry{
            .meshId = meshId,
            .materialId = materialId,
            .meshPayload = std::move(*meshPayload),
            .materialPayload = std::move(*materialPayload),
        });
    }

    std::vector<AssetFormat::PrefabNodeDesc> prefabNodes;
    auto pushNode = [&](auto&& self, const cgltf_node* node, int parentIndex) -> void {
        if (node == nullptr)
        {
            return;
        }
        const int selfIndex = static_cast<int>(prefabNodes.size());
        AssetFormat::PrefabNodeDesc desc{};
        desc.stableNodeId = static_cast<Core::u32>(prefabNodes.size() + 1U);
        desc.parentIndex = parentIndex;
        if (node->has_translation)
        {
            desc.positionX = node->translation[0];
            desc.positionY = node->translation[1];
            desc.positionZ = node->translation[2];
        }
        if (node->has_rotation)
        {
            desc.rotationX = node->rotation[0];
            desc.rotationY = node->rotation[1];
            desc.rotationZ = node->rotation[2];
            desc.rotationW = node->rotation[3];
        }
        if (node->has_scale)
        {
            desc.scaleX = node->scale[0];
            desc.scaleY = node->scale[1];
            desc.scaleZ = node->scale[2];
        }
        if (node->mesh != nullptr)
        {
            const auto found = meshIndexByPtr.find(node->mesh);
            if (found != meshIndexByPtr.end())
            {
                const MeshEntry& entry = meshes[found->second];
                desc.meshId = entry.meshId;
                desc.materialId = entry.materialId;
            }
        }
        prefabNodes.push_back(desc);
        for (cgltf_size c = 0; c < node->children_count; ++c)
        {
            self(self, node->children[c], selfIndex);
        }
    };
    if (data->scenes_count > 0)
    {
        for (cgltf_size i = 0; i < data->scenes[0].nodes_count; ++i)
        {
            pushNode(pushNode, data->scenes[0].nodes[i], -1);
        }
    }
    else
    {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            if (data->nodes[i].parent == nullptr)
            {
                pushNode(pushNode, &data->nodes[i], -1);
            }
        }
    }
    if (prefabNodes.empty() && !meshes.empty())
    {
        prefabNodes.push_back(AssetFormat::PrefabNodeDesc{
            .stableNodeId = 1,
            .parentIndex = -1,
            .meshId = meshes[0].meshId,
            .materialId = meshes[0].materialId,
        });
    }

    auto prefabPayload =
        AssetFormat::writePrefabPayloadBytes(AssetFormat::PrefabPayloadDesc{.nodes = prefabNodes});
    cgltf_free(data);
    if (!prefabPayload)
    {
        return Core::failure(std::move(prefabPayload.error()));
    }

    CatalogCookRequest request{};
    request.targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    for (auto& entry : meshes)
    {
        request.assets.push_back(CatalogCookAssetSpec{
            .assetKind = AssetFormat::AssetKind::StaticMesh,
            .assetId = entry.meshId,
            .assetTypeVersion = AssetFormat::StaticMeshWire::SchemaVersion,
            .payload = std::move(entry.meshPayload),
        });
        request.assets.push_back(CatalogCookAssetSpec{
            .assetKind = AssetFormat::AssetKind::Material,
            .assetId = entry.materialId,
            .assetTypeVersion = AssetFormat::MaterialWire::SchemaVersion,
            .payload = std::move(entry.materialPayload),
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
                .expectedKind = AssetFormat::AssetKind::StaticMesh,
                .flags = AssetFormat::DependencyFlags::Required,
            });
            prefabSpec.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                .assetId = node.materialId,
                .expectedKind = AssetFormat::AssetKind::Material,
                .flags = AssetFormat::DependencyFlags::Required,
            });
        }
    }
    request.assets.push_back(std::move(prefabSpec));
    return request;
}

} // namespace Tina::Asset
