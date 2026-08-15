#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Material cooked payload schema v2 (little-endian, after CookedAsset header/deps).
// Opaque/alpha-blended UnlitBaseColor product path.
// PBR metallic/roughness factors + optional MR/normal Texture2D deps are cooked
// data for RENDER-001; GPU PBR sampling is a separate branch.
// Layout (40B):
//   u16 schemaVersion (=2)
//   u16 materialModel  (1 = UnlitBaseColor)
//   f32 baseColorR, G, B, A   // linear RGBA
//   f32 metallicFactor        // glTF pbrMetallicRoughness; default 1
//   f32 roughnessFactor       // glTF pbrMetallicRoughness; default 1
//   u8  doubleSided           (0/1)
//   u8  alphaMode             (1 = Opaque, 2 = Blend)
//   u16 flags
//     bit0 = hasBaseColorTexture dependency
//     bit1 = hasMetallicRoughnessTexture dependency
//     bit2 = hasNormalTexture dependency
// Texture AssetIds live in CookedAsset dependencies (required Texture2D), in flag
// order: baseColor, metallicRoughness, normal.
namespace MaterialWire {
inline constexpr Core::u16 SchemaVersion = 2;
inline constexpr Core::u32 HeaderBytes = 40;
inline constexpr Core::u16 FlagHasBaseColorTexture = 1U << 0U;
inline constexpr Core::u16 FlagHasMetallicRoughnessTexture = 1U << 1U;
inline constexpr Core::u16 FlagHasNormalTexture = 1U << 2U;
inline constexpr Core::u16 KnownFlags = FlagHasBaseColorTexture | FlagHasMetallicRoughnessTexture |
                                        FlagHasNormalTexture;
} // namespace MaterialWire

enum class MaterialModel : Core::u16 {
    Invalid = 0,
    UnlitBaseColor = 1,
};

enum class MaterialAlphaMode : Core::u8 {
    Invalid = 0,
    Opaque = 1,
    Blend = 2,
};

struct MaterialPayloadDesc final {
    MaterialModel model = MaterialModel::UnlitBaseColor;
    float baseColorR = 1.0F;
    float baseColorG = 1.0F;
    float baseColorB = 1.0F;
    float baseColorA = 1.0F;
    float metallicFactor = 1.0F;
    float roughnessFactor = 1.0F;
    bool doubleSided = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    // Optional; when set, written as required Texture2D dependency (flag order).
    Core::AssetId baseColorTextureId{};
    Core::AssetId metallicRoughnessTextureId{};
    Core::AssetId normalTextureId{};
};

struct MaterialPayloadView final {
    Core::u16 schemaVersion = 0;
    MaterialModel model = MaterialModel::Invalid;
    float baseColorR = 0.0F;
    float baseColorG = 0.0F;
    float baseColorB = 0.0F;
    float baseColorA = 0.0F;
    float metallicFactor = 0.0F;
    float roughnessFactor = 0.0F;
    bool doubleSided = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Invalid;
    bool hasBaseColorTexture = false;          // resolve via CookedAsset deps
    bool hasMetallicRoughnessTexture = false;  // resolve via CookedAsset deps
    bool hasNormalTexture = false;             // resolve via CookedAsset deps

    [[nodiscard]] bool empty() const noexcept
    {
        return schemaVersion == 0 || model == MaterialModel::Invalid;
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeMaterialPayloadBytes(const MaterialPayloadDesc& desc);

[[nodiscard]] Core::Result<MaterialPayloadView> parseMaterialPayload(std::span<const std::byte> payload);

// Convenience: full cooked Material asset. Optional Texture2D dependencies when
// texture AssetIds are set (baseColor, metallicRoughness, normal).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedMaterialAsset(Core::AssetId assetId, const MaterialPayloadDesc& desc,
                         TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
