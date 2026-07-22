#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Material cooked payload schema v1 (little-endian, after CookedAsset header/deps).
// M11-E4/E5: UnlitBaseColor solid factor; optional Texture2D is a cooked dependency
// (not embedded in payload), same pattern as Sprite→Texture2D.
// Layout (24B):
//   u16 schemaVersion (=1)
//   u16 materialModel  (1 = UnlitBaseColor)
//   f32 baseColorR, G, B, A   // linear RGBA
//   u8  doubleSided           (0/1)
//   u8  alphaMode             (1 = Opaque)
//   u16 flags                 (bit0 = hasBaseColorTexture dependency)
namespace MaterialWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 24;
inline constexpr Core::u16 FlagHasBaseColorTexture = 1U << 0U;
} // namespace MaterialWire

enum class MaterialModel : Core::u16 {
    Invalid = 0,
    UnlitBaseColor = 1,
};

enum class MaterialAlphaMode : Core::u8 {
    Invalid = 0,
    Opaque = 1,
};

struct MaterialPayloadDesc final {
    MaterialModel model = MaterialModel::UnlitBaseColor;
    float baseColorR = 1.0F;
    float baseColorG = 1.0F;
    float baseColorB = 1.0F;
    float baseColorA = 1.0F;
    bool doubleSided = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    // Optional; when set, written as required Texture2D dependency (M11-E5).
    Core::AssetId baseColorTextureId{};
};

struct MaterialPayloadView final {
    Core::u16 schemaVersion = 0;
    MaterialModel model = MaterialModel::Invalid;
    float baseColorR = 0.0F;
    float baseColorG = 0.0F;
    float baseColorB = 0.0F;
    float baseColorA = 0.0F;
    bool doubleSided = false;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Invalid;
    bool hasBaseColorTexture = false; // payload flag; resolve texture via CookedAsset deps

    [[nodiscard]] bool empty() const noexcept
    {
        return schemaVersion == 0 || model == MaterialModel::Invalid;
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeMaterialPayloadBytes(const MaterialPayloadDesc& desc);

[[nodiscard]] Core::Result<MaterialPayloadView> parseMaterialPayload(std::span<const std::byte> payload);

// Convenience: full cooked Material asset. Optional Texture2D dependency when
// baseColorTextureId is set.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedMaterialAsset(Core::AssetId assetId, const MaterialPayloadDesc& desc,
                         TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
