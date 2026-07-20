#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Tileset cooked payload schema v1 (little-endian).
// Layout:
//   u16 schemaVersion (=1)
//   u16 flags (=0 reserved)
//   u16 tilePixelWidth
//   u16 tilePixelHeight
//   u32 tileCount
//   TileEntry[tileCount] (24B each, little-endian):
//     u16 localId
//     u16 materialFlags   (bit0 solid, bit1 oneWay, bit2 trigger; rest reserved)
//     f32 u0, v0, u1, v1  // UV rect in the Tileset Texture2D
// Dependency table: required Texture2D (atlas).
namespace TilesetWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 12;
inline constexpr Core::u32 EntryBytes = 24;
inline constexpr Core::u32 MaxTiles = 4096;
inline constexpr Core::u16 MaterialSolid = 1U << 0U;
inline constexpr Core::u16 MaterialOneWay = 1U << 1U;
inline constexpr Core::u16 MaterialTrigger = 1U << 2U;
} // namespace TilesetWire

struct TilesetTileDesc final {
    Core::u16 localId = 0;
    Core::u16 materialFlags = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct TilesetPayloadDesc final {
    Core::u16 tilePixelWidth = 16;
    Core::u16 tilePixelHeight = 16;
    std::span<const TilesetTileDesc> tiles{};
    Core::AssetId textureId{}; // cooked dependency
};

struct TilesetTileView final {
    Core::u16 localId = 0;
    Core::u16 materialFlags = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct TilesetPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 flags = 0;
    Core::u16 tilePixelWidth = 0;
    Core::u16 tilePixelHeight = 0;
    Core::u32 tileCount = 0;
    std::span<const std::byte> entriesBytes{};

    [[nodiscard]] std::optional<TilesetTileView> tile(Core::u32 index) const noexcept;
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeTilesetPayloadBytes(const TilesetPayloadDesc& desc);
[[nodiscard]] Core::Result<TilesetPayloadView> parseTilesetPayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTilesetAsset(Core::AssetId tilesetId, const TilesetPayloadDesc& desc,
                        TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
