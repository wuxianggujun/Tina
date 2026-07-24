#include <tina/asset_format/MaterialPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <array>
#include <cmath>
#include <cstring>
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

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

[[nodiscard]] bool finiteColor(float r, float g, float b, float a) noexcept
{
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}

[[nodiscard]] bool unitInterval(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
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
    if (desc.alphaMode != MaterialAlphaMode::Opaque)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "material alphaMode must be Opaque in v2");
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
    return flags;
}

void appendTextureDependency(std::vector<CookedAssetWriteDependency>& deps, Core::AssetId textureId)
{
    if (!textureId)
    {
        return;
    }
    deps.push_back(CookedAssetWriteDependency{
        .assetId = textureId,
        .expectedKind = AssetKind::Texture2D,
        .flags = DependencyFlags::Required,
    });
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
        // bytes 32..39 reserved zero padding for 8-byte alignment / future factors
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
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material payload size must be 40 bytes");
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

    if (view.schemaVersion != MaterialWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported material schema version");
    }
    if (view.model != MaterialModel::UnlitBaseColor)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported material model");
    }
    if (doubleSided > 1U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material doubleSided must be 0 or 1");
    }
    view.doubleSided = doubleSided == 1U;
    if (view.alphaMode != MaterialAlphaMode::Opaque)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported material alphaMode");
    }
    if ((flags & ~MaterialWire::KnownFlags) != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material flags has unknown bits");
    }
    // Reserved tail must stay zero so future fields can be introduced carefully.
    for (usize offset = 32U; offset < MaterialWire::HeaderBytes; ++offset)
    {
        if (readU8(payload, offset) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "material reserved bytes must be zero");
        }
    }
    view.hasBaseColorTexture = (flags & MaterialWire::FlagHasBaseColorTexture) != 0;
    view.hasMetallicRoughnessTexture = (flags & MaterialWire::FlagHasMetallicRoughnessTexture) != 0;
    view.hasNormalTexture = (flags & MaterialWire::FlagHasNormalTexture) != 0;
    if (!finiteColor(view.baseColorR, view.baseColorG, view.baseColorB, view.baseColorA))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material baseColor must be finite");
    }
    if (view.baseColorR < 0.0F || view.baseColorG < 0.0F || view.baseColorB < 0.0F || view.baseColorA < 0.0F ||
        view.baseColorR > 1.0F || view.baseColorG > 1.0F || view.baseColorB > 1.0F || view.baseColorA > 1.0F)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material baseColor components must be in [0,1]");
    }
    if (!unitInterval(view.metallicFactor))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material metallicFactor must be in [0,1]");
    }
    if (!unitInterval(view.roughnessFactor))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "material roughnessFactor must be in [0,1]");
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
    std::vector<CookedAssetWriteDependency> deps;
    deps.reserve(3U);
    appendTextureDependency(deps, desc.baseColorTextureId);
    appendTextureDependency(deps, desc.metallicRoughnessTextureId);
    appendTextureDependency(deps, desc.normalTextureId);
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Material,
        .assetTypeVersion = MaterialWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .dependencies = deps,
        .payload = *payload,
        .payloadAlignment = 4,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
