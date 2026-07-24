#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

#include <tina/asset/GltfCook.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
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

// Sequential suffix keeps multi-mesh Prefab deps strictly AssetId-sorted while
// remaining path-stable: bytes[12..15] = big-endian index after a seed hash prefix.
[[nodiscard]] Core::AssetId deriveIndexedId(std::string_view seed, Core::u8 tag, Core::u32 index)
{
    Core::AssetId id = deriveId(seed, tag == 0 ? 0x71 : tag);
    Core::AssetId::Bytes bytes = id.bytes();
    bytes[12] = static_cast<std::byte>(static_cast<Core::u8>((index >> 24) & 0xFFU));
    bytes[13] = static_cast<std::byte>(static_cast<Core::u8>((index >> 16) & 0xFFU));
    bytes[14] = static_cast<std::byte>(static_cast<Core::u8>((index >> 8) & 0xFFU));
    bytes[15] = static_cast<std::byte>(static_cast<Core::u8>(index & 0xFFU));
    return *Core::AssetId::fromBytes(bytes);
}

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
    // bytes[12]=channel, bytes[13..15]=sequence under shared path hash (tag 0x74).
    const Core::u32 packed =
        (static_cast<Core::u32>(channel) << 24) | (sequence & 0x00FFFFFFU);
    return deriveIndexedId(seed, 0x74, packed);
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
    float metallicFactor = 1.0F;
    float roughnessFactor = 1.0F;
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

    if (prim.material != nullptr)
    {
        out.doubleSided = prim.material->double_sided != 0;
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

// ASSET-001: external glTF file URIs must stay under the glTF parent directory.
// Reject absolute paths, URI schemes, and ".." escape. Max encoded file size 64 MiB.
inline constexpr std::uint64_t kMaxGltfExternalFileBytes = 64ULL * 1024ULL * 1024ULL;

[[nodiscard]] Core::Result<std::filesystem::path> resolveContainedGltfExternalPath(
    const std::filesystem::path& gltfFilePath,
    std::string_view uri) noexcept
{
    if (uri.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF external URI is empty");
    }
    if (uri.find(':') != std::string_view::npos)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI schemes are not supported (use relative paths under glTF root)");
    }
    if (uri.find("..") != std::string_view::npos)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI must not contain path traversal");
    }
    const std::filesystem::path relative{std::string{uri}};
    if (relative.is_absolute())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external URI must be relative to the glTF file");
    }
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(gltfFilePath.parent_path(), ec);
    if (ec)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "failed to resolve glTF parent directory");
    }
    const auto joined = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "failed to resolve glTF external path");
    }
    const auto rootText = root.generic_string();
    const auto joinedText = joined.generic_string();
    if (joinedText.size() < rootText.size() ||
        joinedText.compare(0, rootText.size(), rootText) != 0 ||
        (joinedText.size() > rootText.size() && joinedText[rootText.size()] != '/'))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "glTF external path escapes catalog root containment");
    }
    return joined;
}

[[nodiscard]] Core::Status validateGltfExternalBuffersContained(
    const cgltf_data* data,
    const std::filesystem::path& gltfFilePath) noexcept
{
    if (data == nullptr)
    {
        return Core::success();
    }
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
    {
        const cgltf_buffer& buffer = data->buffers[i];
        if (buffer.uri == nullptr || buffer.uri[0] == '\0')
        {
            continue;
        }
        if (std::strncmp(buffer.uri, "data:", 5) == 0)
        {
            continue;
        }
        auto path = resolveContainedGltfExternalPath(gltfFilePath, buffer.uri);
        if (!path)
        {
            return Core::failure(std::move(path.error()));
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(*path, ec))
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "glTF external buffer file not found");
        }
        const auto fileSize = std::filesystem::file_size(*path, ec);
        if (ec || fileSize == 0 || fileSize > kMaxGltfExternalFileBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF external buffer size invalid");
        }
    }
    return Core::success();
}

// Decode PNG/JPEG (or other stb_image formats) to RGBA8 for Texture2D cook.
// Supports buffer-view embedded images and relative file URIs next to the glTF.
[[nodiscard]] Core::Result<std::pair<int, int>> decodeImageRgba8(
    const cgltf_data* data,
    const cgltf_image* image,
    const std::filesystem::path& gltfFilePath,
    std::vector<std::byte>& outRgba)
{
    if (image == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image is null");
    }
    const stbi_uc* encoded = nullptr;
    int encodedSize = 0;
    std::vector<unsigned char> fileBytes;

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
        std::error_code ec;
        if (!std::filesystem::is_regular_file(*imagePath, ec))
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "glTF image file not found");
        }
        const auto fileSize = std::filesystem::file_size(*imagePath, ec);
        if (ec || fileSize == 0 || fileSize > kMaxGltfExternalFileBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image file size invalid");
        }
        fileBytes.resize(static_cast<std::size_t>(fileSize));
        std::FILE* file = std::fopen(imagePath->string().c_str(), "rb");
        if (file == nullptr)
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "failed to open glTF image file");
        }
        const auto read = std::fread(fileBytes.data(), 1, fileBytes.size(), file);
        std::fclose(file);
        if (read != fileBytes.size())
        {
            return Core::failure(AssetErrorCode::CatalogFileLoadFailed, "failed to read glTF image file");
        }
        encoded = fileBytes.data();
        encodedSize = static_cast<int>(fileBytes.size());
    }
    else
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image has neither bufferView nor uri");
    }

    int width = 0;
    int height = 0;
    int components = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded, encodedSize, &width, &height, &components, 4);
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "stb_image failed to decode glTF image");
    }
    if (static_cast<Core::u32>(width) > AssetFormat::Texture2DWire::MaxDimension ||
        static_cast<Core::u32>(height) > AssetFormat::Texture2DWire::MaxDimension)
    {
        stbi_image_free(pixels);
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF image exceeds max dimension");
    }
    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    outRgba.resize(byteCount);
    std::memcpy(outRgba.data(), pixels, byteCount);
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
    // Reject path traversal / absolute external buffer URIs before cgltf loads them.
    if (const auto status = validateGltfExternalBuffersContained(data, std::filesystem::path{path}); !status)
    {
        cgltf_free(data);
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
        Core::AssetId baseColorTextureId{};
        Core::AssetId metallicRoughnessTextureId{};
        Core::AssetId normalTextureId{};
        std::vector<std::byte> meshPayload{};
        std::vector<std::byte> materialPayload{};
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
    const std::filesystem::path gltfFilePath{path};
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
        std::vector<std::byte> rgba;
        auto dims = decodeImageRgba8(data, image, gltfFilePath, rgba);
        if (!dims)
        {
            return Core::failure(std::move(dims.error()));
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
        const Core::AssetId textureId = deriveTextureChannelId(gltfUtf8Path, channel, sequence);
        auto texPayload = AssetFormat::writeTexture2DPayloadBytes(AssetFormat::Texture2DPayloadDesc{
            .width = static_cast<Core::u16>(dims->first),
            .height = static_cast<Core::u16>(dims->second),
            .pixelFormat = AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
            .pixels = rgba,
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
            cgltf_free(data);
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "glTF mesh has no primitives");
        }
        if (mesh.primitives_count > AssetFormat::StaticMeshWire::MaxSubmeshes)
        {
            cgltf_free(data);
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "glTF mesh primitive count exceeds product limit");
        }

        const std::size_t rangeFirst = meshes.size();
        for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex)
        {
            const cgltf_primitive& prim = mesh.primitives[primIndex];
            auto pieces = extractTriangleMesh(prim);
            if (!pieces)
            {
                cgltf_free(data);
                return Core::failure(std::move(pieces.error()));
            }

            // Optional fixed ids.meshId/materialId only apply to mesh 0 / prim 0; when both are
            // provided they must already satisfy meshId < materialId.
            Core::AssetId meshId{};
            Core::AssetId materialId{};
            if (meshIndex == 0 && primIndex == 0 && static_cast<bool>(ids.meshId) &&
                static_cast<bool>(ids.materialId))
            {
                if (!(ids.meshId < ids.materialId))
                {
                    cgltf_free(data);
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "fixed mesh AssetId must be strictly less than material AssetId");
                }
                meshId = ids.meshId;
                materialId = ids.materialId;
            }
            else
            {
                // Distinct tags so mesh/material never collide under dense sequential indexes.
                meshId = deriveIndexedId(gltfUtf8Path, 0x71, nextPrimSlot);
                materialId = deriveIndexedId(gltfUtf8Path, 0x72, nextPrimSlot);
            }
            ++nextPrimSlot;

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
            const cgltf_material* material = prim.material;
            auto baseColorTextureId =
                ensureTextureId(baseColorImage(material), GltfTextureChannel::BaseColor);
            if (!baseColorTextureId)
            {
                cgltf_free(data);
                return Core::failure(std::move(baseColorTextureId.error()));
            }
            auto metallicRoughnessTextureId =
                ensureTextureId(metallicRoughnessImage(material), GltfTextureChannel::MetallicRoughness);
            if (!metallicRoughnessTextureId)
            {
                cgltf_free(data);
                return Core::failure(std::move(metallicRoughnessTextureId.error()));
            }
            auto normalTextureId =
                ensureTextureId(normalImage(material), GltfTextureChannel::Normal);
            if (!normalTextureId)
            {
                cgltf_free(data);
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
                .alphaMode = AssetFormat::MaterialAlphaMode::Opaque,
                .baseColorTextureId = *baseColorTextureId,
                .metallicRoughnessTextureId = *metallicRoughnessTextureId,
                .normalTextureId = *normalTextureId,
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

            meshes.push_back(MeshEntry{
                .meshId = meshId,
                .materialId = materialId,
                .baseColorTextureId = *baseColorTextureId,
                .metallicRoughnessTextureId = *metallicRoughnessTextureId,
                .normalTextureId = *normalTextureId,
                .meshPayload = std::move(*meshPayload),
                .materialPayload = std::move(*materialPayload),
            });
        }

        meshRangeByPtr.emplace(&mesh,
                               MeshPrimRange{.firstEntry = rangeFirst, .entryCount = meshes.size() - rangeFirst});
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

        MeshPrimRange range{};
        bool hasMeshRange = false;
        if (node->mesh != nullptr)
        {
            const auto found = meshRangeByPtr.find(node->mesh);
            if (found != meshRangeByPtr.end())
            {
                range = found->second;
                hasMeshRange = range.entryCount > 0;
            }
        }

        if (hasMeshRange && range.entryCount == 1U)
        {
            // Single-prim path: mesh/material stay on the transform node (unchanged).
            const MeshEntry& entry = meshes[range.firstEntry];
            desc.meshId = entry.meshId;
            desc.materialId = entry.materialId;
            prefabNodes.push_back(desc);
        }
        else if (hasMeshRange && range.entryCount > 1U)
        {
            // Multi-prim SPLIT: transform parent (no draw) + identity children (one draw each).
            // Preserves per-prim materials and Prefab's 1 mesh / 1 material per node contract.
            prefabNodes.push_back(desc);
            for (std::size_t i = 0; i < range.entryCount; ++i)
            {
                const MeshEntry& entry = meshes[range.firstEntry + i];
                prefabNodes.push_back(AssetFormat::PrefabNodeDesc{
                    .stableNodeId = static_cast<Core::u32>(prefabNodes.size() + 1U),
                    .parentIndex = selfIndex,
                    .meshId = entry.meshId,
                    .materialId = entry.materialId,
                });
            }
        }
        else
        {
            prefabNodes.push_back(desc);
        }

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
            .assetKind = AssetFormat::AssetKind::StaticMesh,
            .assetId = entry.meshId,
            .assetTypeVersion = AssetFormat::StaticMeshWire::SchemaVersion,
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
    // Prefab node-order deps may not be AssetId-sorted when many meshes; CatalogCook requires
    // strictly increasing dependency streams — sort + unique here (kinds stay mesh/material pairs
    // only as ids; validation cares about id order, not pair adjacency).
    std::sort(prefabSpec.dependencies.begin(), prefabSpec.dependencies.end(),
              [](const AssetFormat::CookedAssetWriteDependency& a,
                 const AssetFormat::CookedAssetWriteDependency& b) { return a.assetId < b.assetId; });
    prefabSpec.dependencies.erase(std::unique(prefabSpec.dependencies.begin(), prefabSpec.dependencies.end(),
                                              [](const AssetFormat::CookedAssetWriteDependency& a,
                                                 const AssetFormat::CookedAssetWriteDependency& b) {
                                                  return a.assetId == b.assetId;
                                              }),
                                  prefabSpec.dependencies.end());
    request.assets.push_back(std::move(prefabSpec));
    return request;
}

} // namespace Tina::Asset
