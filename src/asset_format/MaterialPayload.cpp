#include <tina/asset_format/MaterialPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <vector>

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
    for (usize index = 0; index < sizeof(u32); ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
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
    for (usize index = 0; index < sizeof(u32); ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    writeU32(bytes, offset, std::bit_cast<u32>(value));
}

[[nodiscard]] bool finiteColor(float r, float g, float b, float a) noexcept
{
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}

[[nodiscard]] bool unitInterval(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] bool supportedAlphaMode(MaterialAlphaMode alphaMode) noexcept
{
    return alphaMode == MaterialAlphaMode::Opaque || alphaMode == MaterialAlphaMode::Blend ||
           alphaMode == MaterialAlphaMode::Mask;
}

[[nodiscard]] Core::Status validateMaterialDesc(const MaterialPayloadDesc& desc) noexcept
{
    if (desc.model != MaterialModel::UnlitBaseColor)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "material model must be UnlitBaseColor");
    }
    if (!finiteColor(desc.baseColorR, desc.baseColorG, desc.baseColorB, desc.baseColorA))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material baseColor must be finite");
    }
    if (desc.baseColorR < 0.0F || desc.baseColorG < 0.0F || desc.baseColorB < 0.0F || desc.baseColorA < 0.0F ||
        desc.baseColorR > 1.0F || desc.baseColorG > 1.0F || desc.baseColorB > 1.0F || desc.baseColorA > 1.0F)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material baseColor components must be in [0,1]");
    }
    if (!unitInterval(desc.metallicFactor))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material metallicFactor must be in [0,1]");
    }
    if (!unitInterval(desc.roughnessFactor))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material roughnessFactor must be in [0,1]");
    }
    if (!supportedAlphaMode(desc.alphaMode))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "material alphaMode must be Opaque, Blend or Mask");
    }
    if (!std::isfinite(desc.alphaCutoff) || desc.alphaCutoff < 0.0F)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "material alphaCutoff must be finite and non-negative");
    }
    if (!std::isfinite(desc.emissiveFactorR) || desc.emissiveFactorR < 0.0F ||
        !std::isfinite(desc.emissiveFactorG) || desc.emissiveFactorG < 0.0F ||
        !std::isfinite(desc.emissiveFactorB) || desc.emissiveFactorB < 0.0F)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "material emissive factors must be finite and non-negative");
    }
    Core::AssetId previousTextureId{};
    const std::array textureIds{desc.baseColorTextureId, desc.metallicRoughnessTextureId,
                                desc.normalTextureId, desc.emissiveTextureId};
    for (const Core::AssetId textureId : textureIds)
    {
        if (!textureId)
        {
            continue;
        }
        if (previousTextureId && !(previousTextureId < textureId))
        {
            return Core::failure(
                AssetFormatErrorCode::InvalidLayout,
                "material texture role AssetIds must be unique and strictly increasing in role order");
        }
        previousTextureId = textureId;
    }
    return Core::success();
}

[[nodiscard]] u16 materialFlags(const MaterialPayloadDesc& desc) noexcept
{
    u16 flags = 0U;
    if (desc.baseColorTextureId)
    {
        flags = static_cast<u16>(flags | MaterialWire::FlagHasBaseColorTexture);
    }
    if (desc.metallicRoughnessTextureId)
    {
        flags = static_cast<u16>(flags | MaterialWire::FlagHasMetallicRoughnessTexture);
    }
    if (desc.normalTextureId)
    {
        flags = static_cast<u16>(flags | MaterialWire::FlagHasNormalTexture);
    }
    if (desc.emissiveTextureId)
    {
        flags = static_cast<u16>(flags | MaterialWire::FlagHasEmissiveTexture);
    }
    return flags;
}

} // namespace

Core::Result<std::vector<std::byte>> writeMaterialPayloadBytes(const MaterialPayloadDesc& desc)
{
    if (Core::Status status = validateMaterialDesc(desc); !status)
    {
        return Core::failure(status.error());
    }
    try
    {
        std::vector<std::byte> bytes(MaterialWire::HeaderBytes, std::byte{0});
        writeU16(bytes, 0U, MaterialWire::SchemaVersion);
        writeU16(bytes, 2U, static_cast<u16>(desc.model));
        writeF32(bytes, 4U, desc.baseColorR);
        writeF32(bytes, 8U, desc.baseColorG);
        writeF32(bytes, 12U, desc.baseColorB);
        writeF32(bytes, 16U, desc.baseColorA);
        writeF32(bytes, 20U, desc.metallicFactor);
        writeF32(bytes, 24U, desc.roughnessFactor);
        writeU8(bytes, 28U, desc.doubleSided ? 1U : 0U);
        writeU8(bytes, 29U, static_cast<u8>(desc.alphaMode));
        writeU16(bytes, 30U, materialFlags(desc));
        writeF32(bytes, 32U, desc.alphaCutoff);
        writeF32(bytes, 36U, desc.emissiveFactorR);
        writeF32(bytes, 40U, desc.emissiveFactorG);
        writeF32(bytes, 44U, desc.emissiveFactorB);
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "material payload allocation failed");
    }
}

Core::Result<MaterialPayloadView> parseMaterialPayload(std::span<const std::byte> payload)
{
    if (payload.size() != MaterialWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material payload size must be 48 bytes");
    }

    MaterialPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.model = static_cast<MaterialModel>(readU16(payload, 2U));
    view.baseColorR = readF32(payload, 4U);
    view.baseColorG = readF32(payload, 8U);
    view.baseColorB = readF32(payload, 12U);
    view.baseColorA = readF32(payload, 16U);
    view.metallicFactor = readF32(payload, 20U);
    view.roughnessFactor = readF32(payload, 24U);
    const u8 doubleSided = readU8(payload, 28U);
    view.alphaMode = static_cast<MaterialAlphaMode>(readU8(payload, 29U));
    const u16 flags = readU16(payload, 30U);
    view.alphaCutoff = readF32(payload, 32U);
    view.emissiveFactorR = readF32(payload, 36U);
    view.emissiveFactorG = readF32(payload, 40U);
    view.emissiveFactorB = readF32(payload, 44U);

    if (view.schemaVersion != MaterialWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported material schema version");
    }
    if (doubleSided > 1U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material doubleSided must be 0 or 1");
    }
    view.doubleSided = doubleSided == 1U;
    if ((flags & ~MaterialWire::KnownFlags) != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material flags has unknown bits");
    }
    view.hasBaseColorTexture = (flags & MaterialWire::FlagHasBaseColorTexture) != 0;
    view.hasMetallicRoughnessTexture = (flags & MaterialWire::FlagHasMetallicRoughnessTexture) != 0;
    view.hasNormalTexture = (flags & MaterialWire::FlagHasNormalTexture) != 0;
    view.hasEmissiveTexture = (flags & MaterialWire::FlagHasEmissiveTexture) != 0;
    // One value validator for producer and consumer; the cooked dependency table
    // is validated by parseMaterialFromCooked(), not by this payload-only reader.
    const MaterialPayloadDesc desc{
        .model = view.model,
        .baseColorR = view.baseColorR,
        .baseColorG = view.baseColorG,
        .baseColorB = view.baseColorB,
        .baseColorA = view.baseColorA,
        .metallicFactor = view.metallicFactor,
        .roughnessFactor = view.roughnessFactor,
        .doubleSided = view.doubleSided,
        .alphaMode = view.alphaMode,
        .alphaCutoff = view.alphaCutoff,
        .emissiveFactorR = view.emissiveFactorR,
        .emissiveFactorG = view.emissiveFactorG,
        .emissiveFactorB = view.emissiveFactorB,
    };
    if (auto status = validateMaterialDesc(desc); !status)
    {
        return Core::failure(status.error());
    }
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedMaterialAsset(Core::AssetId assetId, const MaterialPayloadDesc& desc,
                                                              TargetPlatform platform)
{
    auto payload = writeMaterialPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(payload.error());
    }
    std::array<CookedAssetWriteDependency, MaterialWire::TextureRoleCount> deps{};
    usize dependencyCount = 0;
    for (const Core::AssetId textureId : {desc.baseColorTextureId, desc.metallicRoughnessTextureId,
                                         desc.normalTextureId, desc.emissiveTextureId})
    {
        if (textureId)
        {
            deps[dependencyCount++] = CookedAssetWriteDependency{
                .assetId = textureId,
                .expectedKind = AssetKind::Texture2D,
                .flags = DependencyFlags::Required,
            };
        }
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Material,
        .assetTypeVersion = MaterialWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .dependencies = std::span<const CookedAssetWriteDependency>{deps}.first(dependencyCount),
        .payload = *payload,
        .payloadAlignment = 4,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
