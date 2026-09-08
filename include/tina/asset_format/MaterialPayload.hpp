#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Material cooked payload schema v3 (little-endian, after CookedAsset header/deps).
// Layout (48B):
//   u16 schemaVersion (=3)
//   u16 materialModel  (1 = UnlitBaseColor)
//   f32 baseColorR, G, B, A   // linear RGBA
//   f32 metallicFactor        // glTF pbrMetallicRoughness; default 1
//   f32 roughnessFactor       // glTF pbrMetallicRoughness; default 1
//   u8  doubleSided           (0/1)
//   u8  alphaMode             (1 = Opaque, 2 = Blend, 3 = Mask)
//   u16 flags
//     bit0 = hasBaseColorTexture dependency
//     bit1 = hasMetallicRoughnessTexture dependency
//     bit2 = hasNormalTexture dependency
//     bit3 = hasEmissiveTexture dependency
//   f32 alphaCutoff            // finite, >= 0; Mask discards alpha < cutoff
//   f32 emissiveFactorR, G, B  // linear radiance, finite and >= 0, not clamped to 1
// Texture AssetIds live in CookedAsset dependencies (required Texture2D), in flag
// order: baseColor, metallicRoughness, normal, emissive. IDs must be distinct and
// strictly increasing in that order. v1/v2 payloads are rejected, never upgraded.
namespace MaterialWire {
inline constexpr Core::u16 SchemaVersion = 3;
inline constexpr Core::u32 HeaderBytes = 48;
inline constexpr Core::u32 TextureRoleCount = 4;
inline constexpr Core::u16 FlagHasBaseColorTexture = 1U << 0U;
inline constexpr Core::u16 FlagHasMetallicRoughnessTexture = 1U << 1U;
inline constexpr Core::u16 FlagHasNormalTexture = 1U << 2U;
inline constexpr Core::u16 FlagHasEmissiveTexture = 1U << 3U;
inline constexpr Core::u16 KnownFlags = FlagHasBaseColorTexture | FlagHasMetallicRoughnessTexture |
                                        FlagHasNormalTexture | FlagHasEmissiveTexture;
} // namespace MaterialWire

enum class MaterialModel : Core::u16 {
    Invalid = 0,
    UnlitBaseColor = 1,
};

enum class MaterialAlphaMode : Core::u8 {
    Invalid = 0,
    Opaque = 1,
    Blend = 2,
    Mask = 3,
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
    // glTF permits a cutoff above 1 (all fragments discarded); only Mask uses it.
    float alphaCutoff = 0.5F;
    // Texture RGB (or white when absent) multiplies this linear radiance. Texture
    // alpha is ignored. glTF emissiveStrength is folded into these factors at cook.
    float emissiveFactorR = 0.0F;
    float emissiveFactorG = 0.0F;
    float emissiveFactorB = 0.0F;
    // Optional; when set, written as required Texture2D dependency (flag order).
    Core::AssetId baseColorTextureId{};
    Core::AssetId metallicRoughnessTextureId{};
    Core::AssetId normalTextureId{};
    Core::AssetId emissiveTextureId{};
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
    float alphaCutoff = 0.5F;
    float emissiveFactorR = 0.0F;
    float emissiveFactorG = 0.0F;
    float emissiveFactorB = 0.0F;
    bool hasBaseColorTexture = false;          // resolve via CookedAsset deps
    bool hasMetallicRoughnessTexture = false;  // resolve via CookedAsset deps
    bool hasNormalTexture = false;             // resolve via CookedAsset deps
    bool hasEmissiveTexture = false;           // resolve via CookedAsset deps

    [[nodiscard]] Core::u32 textureDependencyCount() const noexcept
    {
        return static_cast<Core::u32>(hasBaseColorTexture) +
               static_cast<Core::u32>(hasMetallicRoughnessTexture) +
               static_cast<Core::u32>(hasNormalTexture) +
               static_cast<Core::u32>(hasEmissiveTexture);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return schemaVersion == 0 || model == MaterialModel::Invalid;
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeMaterialPayloadBytes(const MaterialPayloadDesc& desc);

[[nodiscard]] Core::Result<MaterialPayloadView> parseMaterialPayload(std::span<const std::byte> payload);

// Convenience: full cooked Material asset. Optional Texture2D dependencies when
// texture AssetIds are set (baseColor, metallicRoughness, normal, emissive).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedMaterialAsset(Core::AssetId assetId, const MaterialPayloadDesc& desc,
                         TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
