#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// NavigationGrid2D cooked payload schema v1 (little-endian).
// Layout: 32-byte header followed by row-major flags and traversal costs.
namespace NavigationGrid2DWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 32;
inline constexpr Core::u8 CellBlocked = 1U;
inline constexpr Core::u8 ValidCellFlags = CellBlocked;
inline constexpr Core::u8 MinimumTraversalCost = 1;
inline constexpr Core::u8 MaximumTraversalCost = 16;
inline constexpr Core::u32 MaximumDimension = 4096;
inline constexpr Core::usize MaximumCellCount = Core::usize{16} * 1024U * 1024U;
} // namespace NavigationGrid2DWire

struct NavigationGrid2DPayloadDesc final {
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float originXMeters = 0.0F;
    float originYMeters = 0.0F;
    float cellSizeMeters = 1.0F;
    std::span<const Core::u8> cellFlags{};
    std::span<const Core::u8> traversalCosts{};
};

struct NavigationGrid2DPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float originXMeters = 0.0F;
    float originYMeters = 0.0F;
    float cellSizeMeters = 0.0F;
    Core::u32 cellCount = 0;
    std::span<const Core::u8> cellFlags{};
    std::span<const Core::u8> traversalCosts{};
};

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeNavigationGrid2DPayloadBytes(const NavigationGrid2DPayloadDesc& desc);

[[nodiscard]] Core::Result<NavigationGrid2DPayloadView>
parseNavigationGrid2DPayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedNavigationGrid2DAsset(
    Core::AssetId assetId,
    const NavigationGrid2DPayloadDesc& desc,
    TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
