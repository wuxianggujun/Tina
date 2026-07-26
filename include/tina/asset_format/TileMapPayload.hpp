#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {

// TileMap stream-root cooked payload schema v3 (little-endian).
//
// Header:
//   u16 schemaVersion (=3)
//   u16 flags (=0 reserved)
//   u32 widthCells
//   u32 heightCells
//   f32 cellSizeMeters
//   u16 chunkSizeCells (power-of-two in [1, 64])
//   u16 layerCount
//   u32 reserved (=0)
//
// Each layer is serialized in authoring order:
//   u32 stableLayerId (non-zero)
//   u8  kind (1 tile, 2 object)
//   u8  flags (bit0 visible; rest zero)
//   u16 nameBytes
//   u16 propertyCount
//   u16 reserved (=0)
//   u32 contentCount (chunk-ref count or object count)
//   nameBytes UTF-8 bytes
//   propertyCount * { u16 keyBytes, u16 valueBytes, key UTF-8, value UTF-8 }
//   tile layer: contentCount * {
//       u32 chunkX, u32 chunkY, u16 widthCells, u16 heightCells,
//       u32 nonEmptyCount, byte[16] chunkAssetId
//   }
//   object layer: contentCount * {
//       u32 stableObjectId (non-zero), u8 kind (1 point, 2 rectangle),
//       u8 flags (bit0 visible; rest zero),
//       u16 nameBytes, u16 propertyCount, u16 reserved (=0),
//       f32 x, f32 y, f32 width, f32 height,
//       name UTF-8, properties...
//   }
namespace TileMapWire {
inline constexpr Core::u16 SchemaVersion = 3;
inline constexpr Core::u32 HeaderBytes = 24;
inline constexpr Core::u32 LayerHeaderBytes = 16;
inline constexpr Core::u32 ChunkRefBytes = 32;
inline constexpr Core::u32 ObjectHeaderBytes = 28;
inline constexpr Core::u32 MaxDimension = 1024;
inline constexpr Core::u16 MinChunkSizeCells = 1;
inline constexpr Core::u16 MaxChunkSizeCells = 64;
inline constexpr Core::u32 MaxChunkRefsPerLayer = Wire::MaxDependenciesPerAsset - 1U;
inline constexpr Core::u32 MaxChunkRefsPerMap = Wire::MaxDependenciesPerAsset - 1U;
inline constexpr Core::u16 MaxLayers = 256;
inline constexpr Core::u16 MaxPropertiesPerOwner = 64;
inline constexpr Core::u32 MaxObjectsPerLayer = 1024;
inline constexpr Core::u32 MaxObjectsPerMap = 4096;
inline constexpr Core::u16 MaxStringBytes = 1024;
inline constexpr Core::u16 EmptyTileId = 0;
inline constexpr Core::u8 LayerVisible = 1U << 0U;
inline constexpr Core::u8 ObjectVisible = 1U << 0U;
} // namespace TileMapWire

using TileMapLayerId = Core::u32;
using TileMapObjectId = Core::u32;

enum class TileMapLayerKind : Core::u8 {
    Tile = 1,
    Object = 2,
};

enum class TileMapObjectKind : Core::u8 {
    Point = 1,
    Rectangle = 2,
};

struct TileMapPropertyDesc final {
    std::string_view key{};
    std::string_view value{};
};

struct TileMapObjectDesc final {
    // Non-zero and unique across the whole map, not merely within its layer.
    TileMapObjectId stableObjectId = 0;
    TileMapObjectKind kind = TileMapObjectKind::Point;
    bool visible = true;
    std::string_view name{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // Property keys are unique within this object.
    std::span<const TileMapPropertyDesc> properties{};
};

struct TileMapChunkRefDesc final {
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;
    Core::u16 widthCells = 0;
    Core::u16 heightCells = 0;
    Core::u32 nonEmptyCount = 0;
    Core::AssetId chunkAssetId{};
};

struct TileMapLayerDesc final {
    // Non-zero and unique across the whole map. Layer array order is authoritative.
    TileMapLayerId stableLayerId = 0;
    TileMapLayerKind kind = TileMapLayerKind::Tile;
    bool visible = true;
    std::string_view name{};
    // Property keys are unique within this layer.
    std::span<const TileMapPropertyDesc> properties{};
    // Tile layer only. Contains non-empty chunks in strict {chunkY, chunkX} order.
    // Missing coordinates are known-empty and have no asset.
    std::span<const TileMapChunkRefDesc> chunkRefs{};
    // Object layer only.
    std::span<const TileMapObjectDesc> objects{};
};

struct TileMapPayloadDesc final {
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 1.0f;
    Core::u16 chunkSizeCells = 16;
    // Authoring and rendering order. Must contain at least one layer.
    std::span<const TileMapLayerDesc> layers{};
    Core::AssetId tilesetId{}; // cooked dependency
};

struct TileMapPropertyView final {
    std::string_view key{};
    std::string_view value{};
};

struct TileMapObjectPayloadView final {
    TileMapObjectId stableObjectId = 0;
    TileMapObjectKind kind = TileMapObjectKind::Point;
    bool visible = false;
    std::string_view name{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    Core::u16 propertyCount = 0;
    // Internal validated wire bytes. Use propertyAt() rather than decoding directly.
    std::span<const std::byte> propertyBytes{};

    [[nodiscard]] std::optional<TileMapPropertyView> propertyAt(Core::u16 index) const noexcept;
    [[nodiscard]] std::optional<TileMapPropertyView> findProperty(std::string_view key) const noexcept;
};

struct TileMapChunkRefView final {
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;
    Core::u16 widthCells = 0;
    Core::u16 heightCells = 0;
    Core::u32 nonEmptyCount = 0;
    Core::AssetId chunkAssetId{};
};

struct TileMapLayerPayloadView final {
    TileMapLayerId stableLayerId = 0;
    TileMapLayerKind kind = TileMapLayerKind::Tile;
    bool visible = false;
    std::string_view name{};
    Core::u16 propertyCount = 0;
    Core::u32 chunkRefCount = 0;
    Core::u32 objectCount = 0;
    // Internal validated wire bytes. Use propertyAt(), chunkRefAt(), and objectAt().
    std::span<const std::byte> propertyBytes{};
    std::span<const std::byte> chunkRefBytes{};
    std::span<const std::byte> objectBytes{};
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    Core::u16 chunkSizeCells = 0;

    [[nodiscard]] std::optional<TileMapPropertyView> propertyAt(Core::u16 index) const noexcept;
    [[nodiscard]] std::optional<TileMapPropertyView> findProperty(std::string_view key) const noexcept;
    [[nodiscard]] std::optional<TileMapChunkRefView> chunkRefAt(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<TileMapChunkRefView> findChunkRef(Core::u32 chunkX, Core::u32 chunkY) const noexcept;
    [[nodiscard]] std::optional<TileMapObjectPayloadView> objectAt(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<TileMapObjectPayloadView>
    findObject(TileMapObjectId stableObjectId) const noexcept;
};

struct TileMapPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 flags = 0;
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 1.0f;
    Core::u16 chunkSizeCells = 0;
    Core::u16 layerCount = 0;
    // Full validated payload. Views returned by this type borrow from this span.
    std::span<const std::byte> payloadBytes{};
    // Internal validated layer sequence. Use layerAt() or findLayer().
    std::span<const std::byte> layerBytes{};

    [[nodiscard]] std::optional<TileMapLayerPayloadView> layerAt(Core::u16 index) const noexcept;
    [[nodiscard]] std::optional<TileMapLayerPayloadView> findLayer(TileMapLayerId stableLayerId) const noexcept;
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeTileMapPayloadBytes(const TileMapPayloadDesc& desc);
[[nodiscard]] Core::Result<TileMapPayloadView> parseTileMapPayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTileMapAsset(Core::AssetId tileMapId, const TileMapPayloadDesc& desc,
                        TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
