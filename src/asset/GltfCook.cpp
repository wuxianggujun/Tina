#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <tina/asset/GltfCook.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
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

    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
    {
        cgltf_free(data);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF has no mesh primitives");
    }
    const cgltf_primitive& prim = data->meshes[0].primitives[0];
    if (prim.type != cgltf_primitive_type_triangles)
    {
        cgltf_free(data);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "only TRIANGLES primitives are supported");
    }
    const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
    if (positions == nullptr || positions->type != cgltf_type_vec3 ||
        positions->component_type != cgltf_component_type_r_32f || positions->count == 0)
    {
        cgltf_free(data);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "POSITION float3 accessor required");
    }
    if (positions->count > 65535U)
    {
        cgltf_free(data);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "vertex count exceeds u16 index range");
    }

    const cgltf_accessor* normals = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* texcoords = findAttribute(prim, cgltf_attribute_type_texcoord);

    std::vector<float> vertices(static_cast<std::size_t>(positions->count) * 8U);
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        float p[3]{};
        float n[3]{0.0F, 1.0F, 0.0F};
        float uv[2]{0.0F, 0.0F};
        if (!cgltf_accessor_read_float(positions, i, p, 3))
        {
            cgltf_free(data);
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
        vertices[base + 0] = p[0];
        vertices[base + 1] = p[1];
        vertices[base + 2] = p[2];
        vertices[base + 3] = n[0];
        vertices[base + 4] = n[1];
        vertices[base + 5] = n[2];
        vertices[base + 6] = uv[0];
        vertices[base + 7] = uv[1];
    }

    std::vector<Core::u16> indices;
    if (prim.indices != nullptr)
    {
        if (prim.indices->count % 3 != 0)
        {
            cgltf_free(data);
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "index count must be multiple of 3");
        }
        indices.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
        {
            const cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
            if (idx >= positions->count || idx > 65535U)
            {
                cgltf_free(data);
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "index out of range for u16 mesh");
            }
            indices[i] = static_cast<Core::u16>(idx);
        }
    }
    else
    {
        if (positions->count % 3 != 0)
        {
            cgltf_free(data);
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "non-indexed mesh vertex count invalid");
        }
        indices.resize(positions->count);
        for (cgltf_size i = 0; i < positions->count; ++i)
        {
            indices[i] = static_cast<Core::u16>(i);
        }
    }

    float minX = vertices[0], minY = vertices[1], minZ = vertices[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        const std::size_t base = static_cast<std::size_t>(i) * 8U;
        minX = (std::min)(minX, vertices[base + 0]);
        minY = (std::min)(minY, vertices[base + 1]);
        minZ = (std::min)(minZ, vertices[base + 2]);
        maxX = (std::max)(maxX, vertices[base + 0]);
        maxY = (std::max)(maxY, vertices[base + 1]);
        maxZ = (std::max)(maxZ, vertices[base + 2]);
    }
    const float cx = 0.5F * (minX + maxX);
    const float cy = 0.5F * (minY + maxY);
    const float cz = 0.5F * (minZ + maxZ);
    float radius = 0.0F;
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        const std::size_t base = static_cast<std::size_t>(i) * 8U;
        const float dx = vertices[base + 0] - cx;
        const float dy = vertices[base + 1] - cy;
        const float dz = vertices[base + 2] - cz;
        radius = (std::max)(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    if (!(radius > 0.0F))
    {
        radius = 1.0F;
    }

    AssetFormat::StaticMeshSubmeshDesc submesh{
        .firstIndex = 0,
        .indexCount = static_cast<Core::u32>(indices.size()),
        .materialSlot = 0,
    };
    AssetFormat::StaticMeshPayloadDesc meshDesc{
        .vertexLayout = AssetFormat::StaticMeshVertexLayout::P3N3UV2,
        .indexType = AssetFormat::StaticMeshIndexType::U16,
        .boundsCenterX = cx,
        .boundsCenterY = cy,
        .boundsCenterZ = cz,
        .boundsRadius = radius,
        .submeshes = std::span<const AssetFormat::StaticMeshSubmeshDesc>(&submesh, 1),
        .vertices = vertices,
        .indices = indices,
    };

    float baseR = 1.0F, baseG = 1.0F, baseB = 1.0F, baseA = 1.0F;
    bool doubleSided = false;
    if (prim.material != nullptr && prim.material->has_pbr_metallic_roughness)
    {
        baseR = prim.material->pbr_metallic_roughness.base_color_factor[0];
        baseG = prim.material->pbr_metallic_roughness.base_color_factor[1];
        baseB = prim.material->pbr_metallic_roughness.base_color_factor[2];
        baseA = prim.material->pbr_metallic_roughness.base_color_factor[3];
        doubleSided = prim.material->double_sided != 0;
    }
    AssetFormat::MaterialPayloadDesc materialDesc{
        .model = AssetFormat::MaterialModel::UnlitBaseColor,
        .baseColorR = baseR,
        .baseColorG = baseG,
        .baseColorB = baseB,
        .baseColorA = baseA,
        .doubleSided = doubleSided,
        .alphaMode = AssetFormat::MaterialAlphaMode::Opaque,
    };

    if (!static_cast<bool>(ids.meshId))
    {
        ids.meshId = deriveId(gltfUtf8Path, 0x71);
    }
    if (!static_cast<bool>(ids.materialId))
    {
        ids.materialId = deriveId(gltfUtf8Path, 0x72);
    }
    if (!static_cast<bool>(ids.prefabId))
    {
        ids.prefabId = deriveId(gltfUtf8Path, 0x73);
    }

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
        if (node->mesh == &data->meshes[0])
        {
            desc.meshId = ids.meshId;
            desc.materialId = ids.materialId;
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
    if (prefabNodes.empty())
    {
        prefabNodes.push_back(AssetFormat::PrefabNodeDesc{
            .stableNodeId = 1,
            .parentIndex = -1,
            .meshId = ids.meshId,
            .materialId = ids.materialId,
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
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::StaticMesh,
        .assetId = ids.meshId,
        .assetTypeVersion = AssetFormat::StaticMeshWire::SchemaVersion,
        .payload = std::move(*meshPayload),
    });
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Material,
        .assetId = ids.materialId,
        .assetTypeVersion = AssetFormat::MaterialWire::SchemaVersion,
        .payload = std::move(*materialPayload),
    });
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
