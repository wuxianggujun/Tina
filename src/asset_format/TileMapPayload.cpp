#include <tina/asset_format/TileMapPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <array>
#include <cstring>
#include <limits>

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
    float value = 0.0f;
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

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    for (usize index = 0; index < 4U; ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

} // namespace

std::optional<Core::u16> TileMapPayloadView::tileAt(Core::u32 x, Core::u32 y) const noexcept
{
    if (x >= widthCells || y >= heightCells)
    {
        return std::nullopt;
    }
    const usize index = static_cast<usize>(y) * widthCells + x;
    const usize offset = index * sizeof(u16);
    if (offset + sizeof(u16) > tilesBytes.size())
    {
        return std::nullopt;
    }
    return readU16(tilesBytes, offset);
}

Core::Result<std::vector<std::byte>> writeTileMapPayloadBytes(const TileMapPayloadDesc& desc)
{
    if (desc.widthCells == 0 || desc.heightCells == 0 || desc.widthCells > TileMapWire::MaxDimension ||
        desc.heightCells > TileMapWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap dimensions out of range");
    }
    if (!(desc.cellSizeMeters > 0.0f) || desc.cellSizeMeters != desc.cellSizeMeters)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "cellSizeMeters must be positive finite");
    }
    const std::uint64_t expected =
        static_cast<std::uint64_t>(desc.widthCells) * static_cast<std::uint64_t>(desc.heightCells);
    if (expected > (std::numeric_limits<u32>::max)() || desc.tiles.size() != expected)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap tile buffer size mismatch");
    }

    const u32 tileCount = static_cast<u32>(expected);
    const usize total =
        TileMapWire::HeaderBytes + TileMapWire::LayerHeaderBytes + static_cast<usize>(tileCount) * sizeof(u16);
    try
    {
        std::vector<std::byte> bytes(total, std::byte{0});
        writeU16(bytes, 0U, TileMapWire::SchemaVersion);
        writeU16(bytes, 2U, 0U);
        writeU32(bytes, 4U, desc.widthCells);
        writeU32(bytes, 8U, desc.heightCells);
        writeF32(bytes, 12U, desc.cellSizeMeters);
        writeU16(bytes, 16U, 1U); // layerCount
        writeU16(bytes, 18U, 0U);
        writeU16(bytes, 20U, desc.layerId);
        writeU16(bytes, 22U, desc.layerFlags);
        writeU32(bytes, 24U, tileCount);
        for (u32 index = 0; index < tileCount; ++index)
        {
            writeU16(bytes, TileMapWire::HeaderBytes + TileMapWire::LayerHeaderBytes + index * sizeof(u16),
                     desc.tiles[index]);
        }
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "tilemap payload allocation failed");
    }
}

Core::Result<TileMapPayloadView> parseTileMapPayload(std::span<const std::byte> payload)
{
    if (payload.size() < TileMapWire::HeaderBytes + TileMapWire::LayerHeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "tilemap payload too small");
    }
    TileMapPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.flags = readU16(payload, 2U);
    view.widthCells = readU32(payload, 4U);
    view.heightCells = readU32(payload, 8U);
    view.cellSizeMeters = readF32(payload, 12U);
    view.layerCount = readU16(payload, 16U);
    const u16 reserved = readU16(payload, 18U);
    view.layerId = readU16(payload, 20U);
    view.layerFlags = readU16(payload, 22U);
    view.tileCount = readU32(payload, 24U);

    if (view.schemaVersion != TileMapWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported tilemap payload schema");
    }
    if (view.flags != 0U || reserved != 0U || view.layerCount != 1U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported tilemap flags/layer count");
    }
    if (view.widthCells == 0 || view.heightCells == 0 || view.widthCells > TileMapWire::MaxDimension ||
        view.heightCells > TileMapWire::MaxDimension || !(view.cellSizeMeters > 0.0f) ||
        view.cellSizeMeters != view.cellSizeMeters)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap header fields invalid");
    }
    const std::uint64_t expected =
        static_cast<std::uint64_t>(view.widthCells) * static_cast<std::uint64_t>(view.heightCells);
    if (view.tileCount != expected)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap tileCount does not match dimensions");
    }
    const usize expectedBytes =
        TileMapWire::HeaderBytes + TileMapWire::LayerHeaderBytes + static_cast<usize>(view.tileCount) * sizeof(u16);
    if (payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap payload size mismatch");
    }
    view.tilesBytes = payload.subspan(TileMapWire::HeaderBytes + TileMapWire::LayerHeaderBytes);
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedTileMapAsset(Core::AssetId tileMapId, const TileMapPayloadDesc& desc,
                                                             TargetPlatform platform)
{
    if (!tileMapId || !desc.tilesetId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "tilemap requires map id and tileset id");
    }
    auto payload = writeTileMapPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    const std::array deps{CookedAssetWriteDependency{
        .assetId = desc.tilesetId,
        .expectedKind = AssetKind::Tileset,
        .flags = DependencyFlags::Required,
    }};
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::TileMap,
        .assetTypeVersion = TileMapWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = tileMapId,
        .dependencies = deps,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
