#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Sprite cooked payload schema v1 (little-endian).
// Layout (40B):
//   u16 schemaVersion (=1)
//   u16 flags (=0 reserved)
//   f32 u0, v0, u1, v1     // UV rect in texture space [0,1]
//   f32 pivotX, pivotY     // normalized pivot
//   f32 pixelsPerUnit
// Texture dependency is declared on the CookedAsset dependency table (expectedKind Texture2D).
namespace SpriteWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 PayloadBytes = 40;
} // namespace SpriteWire

struct SpritePayloadDesc final {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float pivotX = 0.5f;
    float pivotY = 0.5f;
    float pixelsPerUnit = 100.0f;
    Core::AssetId textureId{}; // written as cooked dependency, not inside payload
};

struct SpritePayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 flags = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float pivotX = 0.5f;
    float pivotY = 0.5f;
    float pixelsPerUnit = 100.0f;
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeSpritePayloadBytes(const SpritePayloadDesc& desc);

[[nodiscard]] Core::Result<SpritePayloadView> parseSpritePayload(std::span<const std::byte> payload);

// Full cooked Sprite asset: one required Texture2D dependency + sprite payload.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedSpriteAsset(Core::AssetId spriteId, const SpritePayloadDesc& desc,
                       TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
