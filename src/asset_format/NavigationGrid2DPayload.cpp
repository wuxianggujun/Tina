#include <tina/asset_format/NavigationGrid2DPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

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
    for (usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    const u32 bits = readU32(bytes, offset);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value) noexcept
{
    for (usize index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value) noexcept
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(bytes, offset, bits);
}

[[nodiscard]] Core::Result<u32> validateLayout(const NavigationGrid2DPayloadDesc& desc)
{
    if (desc.widthCells == 0U || desc.heightCells == 0U ||
        desc.widthCells > NavigationGrid2DWire::MaximumDimension ||
        desc.heightCells > NavigationGrid2DWire::MaximumDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "navigation grid dimensions are outside the supported range");
    }
    if (!std::isfinite(desc.originXMeters) || !std::isfinite(desc.originYMeters) ||
        !std::isfinite(desc.cellSizeMeters) || !(desc.cellSizeMeters > 0.0F))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "navigation grid origin and cell size must be finite");
    }
    const auto cellsWide = static_cast<Core::u64>(desc.widthCells) * desc.heightCells;
    if (cellsWide > NavigationGrid2DWire::MaximumCellCount ||
        cellsWide > (std::numeric_limits<u32>::max)())
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "navigation grid cell count exceeds the schema limit");
    }
    const u32 cellCount = static_cast<u32>(cellsWide);
    if (desc.cellFlags.size() != cellCount || desc.traversalCosts.size() != cellCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "navigation grid table sizes do not match its dimensions");
    }
    if (std::ranges::any_of(desc.cellFlags, [](u8 flags) {
            return (flags & static_cast<u8>(~NavigationGrid2DWire::ValidCellFlags)) != 0U;
        }))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "navigation grid contains unsupported cell flags");
    }
    if (std::ranges::any_of(desc.traversalCosts, [](u8 cost) {
            return cost < NavigationGrid2DWire::MinimumTraversalCost ||
                   cost > NavigationGrid2DWire::MaximumTraversalCost;
        }))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "navigation grid traversal cost is outside the supported range");
    }
    return cellCount;
}

} // namespace

Core::Result<std::vector<std::byte>>
writeNavigationGrid2DPayloadBytes(const NavigationGrid2DPayloadDesc& desc)
{
    auto cellCount = validateLayout(desc);
    if (!cellCount)
    {
        return Core::failure(std::move(cellCount.error()));
    }
    const usize payloadBytes = NavigationGrid2DWire::HeaderBytes + static_cast<usize>(*cellCount) * 2U;
    try
    {
        std::vector<std::byte> bytes(payloadBytes, std::byte{0});
        writeU16(bytes, 0U, NavigationGrid2DWire::SchemaVersion);
        writeU16(bytes, 2U, 0U);
        writeU32(bytes, 4U, desc.widthCells);
        writeU32(bytes, 8U, desc.heightCells);
        writeF32(bytes, 12U, desc.originXMeters);
        writeF32(bytes, 16U, desc.originYMeters);
        writeF32(bytes, 20U, desc.cellSizeMeters);
        writeU32(bytes, 24U, *cellCount);
        writeU32(bytes, 28U, 0U);
        std::memcpy(bytes.data() + NavigationGrid2DWire::HeaderBytes,
                    desc.cellFlags.data(), *cellCount);
        std::memcpy(bytes.data() + NavigationGrid2DWire::HeaderBytes + *cellCount,
                    desc.traversalCosts.data(), *cellCount);
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "navigation grid payload allocation failed");
    }
}

Core::Result<NavigationGrid2DPayloadView>
parseNavigationGrid2DPayload(std::span<const std::byte> payload)
{
    if (payload.size() < NavigationGrid2DWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "navigation grid payload is smaller than its header");
    }
    NavigationGrid2DPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .widthCells = readU32(payload, 4U),
        .heightCells = readU32(payload, 8U),
        .originXMeters = readF32(payload, 12U),
        .originYMeters = readF32(payload, 16U),
        .cellSizeMeters = readF32(payload, 20U),
        .cellCount = readU32(payload, 24U),
    };
    if (view.schemaVersion != NavigationGrid2DWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported navigation grid payload schema");
    }
    if (readU16(payload, 2U) != 0U || readU32(payload, 28U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "navigation grid reserved fields must be zero");
    }
    const usize expectedBytes = NavigationGrid2DWire::HeaderBytes +
                                static_cast<usize>(view.cellCount) * 2U;
    if (expectedBytes < NavigationGrid2DWire::HeaderBytes || payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "navigation grid payload byte count is inconsistent");
    }
    view.cellFlags = std::span<const u8>{
        reinterpret_cast<const u8*>(payload.data() + NavigationGrid2DWire::HeaderBytes),
        view.cellCount,
    };
    view.traversalCosts = std::span<const u8>{
        reinterpret_cast<const u8*>(payload.data() + NavigationGrid2DWire::HeaderBytes + view.cellCount),
        view.cellCount,
    };
    const NavigationGrid2DPayloadDesc desc{
        .widthCells = view.widthCells,
        .heightCells = view.heightCells,
        .originXMeters = view.originXMeters,
        .originYMeters = view.originYMeters,
        .cellSizeMeters = view.cellSizeMeters,
        .cellFlags = view.cellFlags,
        .traversalCosts = view.traversalCosts,
    };
    auto validatedCount = validateLayout(desc);
    if (!validatedCount || *validatedCount != view.cellCount)
    {
        return validatedCount
                   ? Core::failure(AssetFormatErrorCode::InvalidLayout,
                                   "navigation grid header cell count is inconsistent")
                   : Core::failure(std::move(validatedCount.error()));
    }
    return view;
}

Core::Result<std::vector<std::byte>>
writeCookedNavigationGrid2DAsset(Core::AssetId assetId,
                                 const NavigationGrid2DPayloadDesc& desc,
                                 TargetPlatform platform)
{
    auto payload = writeNavigationGrid2DPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::NavigationGrid2D,
        .assetTypeVersion = NavigationGrid2DWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
