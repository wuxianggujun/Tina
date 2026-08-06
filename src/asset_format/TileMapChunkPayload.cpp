#include <tina/asset_format/TileMapChunkPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <array>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string_view>

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
    for (usize index = 0; index < sizeof(u32); ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

void appendU8(std::vector<std::byte>& bytes, u8 value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void appendU16(std::vector<std::byte>& bytes, u16 value)
{
    appendU8(bytes, static_cast<u8>(value & 0xFFU));
    appendU8(bytes, static_cast<u8>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::byte>& bytes, u32 value)
{
    for (usize index = 0; index < sizeof(u32); ++index)
    {
        appendU8(bytes, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

[[nodiscard]] u32 countNonEmpty(std::span<const u16> cells) noexcept;

[[nodiscard]] Core::Status validateDesc(const TileMapChunkPayloadDesc& desc)
{
    if (!desc.parentTileMapId || desc.layerId == 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "tilemap chunk requires parent map and stable layer identities");
    }
    if (desc.chunkX > TileMapChunkWire::MaxChunkCoordinate ||
        desc.chunkY > TileMapChunkWire::MaxChunkCoordinate)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap chunk coordinate exceeds limit");
    }
    if (desc.widthCells == 0 || desc.heightCells == 0 || desc.widthCells > TileMapChunkWire::MaxDimension ||
        desc.heightCells > TileMapChunkWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap chunk dimensions out of range");
    }
    const u32 cellCount = static_cast<u32>(desc.widthCells) * desc.heightCells;
    if (cellCount > TileMapChunkWire::MaxCells || desc.cells.size() != cellCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap chunk cell count does not match dimensions");
    }
    if (countNonEmpty(desc.cells) == 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "tilemap chunk asset must contain at least one non-empty cell");
    }
    return Core::success();
}

[[nodiscard]] u32 countNonEmpty(std::span<const u16> cells) noexcept
{
    u32 count = 0;
    for (const u16 cell : cells)
    {
        count += cell != TileMapChunkWire::EmptyTileId ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] u32 countNonEmpty(std::span<const std::byte> cellBytes) noexcept
{
    u32 count = 0;
    for (usize offset = 0; offset < cellBytes.size(); offset += sizeof(u16))
    {
        count += readU16(cellBytes, offset) != TileMapChunkWire::EmptyTileId ? 1U : 0U;
    }
    return count;
}

} // namespace

Core::Result<Core::AssetId> deriveTileMapChunkAssetId(Core::AssetId parentTileMapId,
                                                       Core::u32 stableLayerId,
                                                       Core::u32 chunkX,
                                                       Core::u32 chunkY)
{
    if (!parentTileMapId || stableLayerId == 0U ||
        chunkX > TileMapChunkWire::MaxChunkCoordinate ||
        chunkY > TileMapChunkWire::MaxChunkCoordinate)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "tilemap chunk identity inputs are invalid");
    }

    constexpr std::string_view Domain = "tina.asset.tilemap-chunk-id";
    constexpr Core::u8 DerivationVersion = 1U;
    constexpr usize ScalarBytes = sizeof(Core::u32);
    std::array<std::byte, Domain.size() + 1U + Core::AssetId::Bytes{}.size() + ScalarBytes * 3U> input{};

    usize offset = 0;
    for (const char value : Domain)
    {
        input[offset++] = static_cast<std::byte>(static_cast<unsigned char>(value));
    }
    input[offset++] = static_cast<std::byte>(DerivationVersion);
    for (const std::byte value : parentTileMapId.bytes())
    {
        input[offset++] = value;
    }
    const auto appendU32LittleEndian = [&input, &offset](Core::u32 value) {
        for (usize byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
        {
            input[offset++] = static_cast<std::byte>((value >> (byteIndex * 8U)) & 0xFFU);
        }
    };
    appendU32LittleEndian(stableLayerId);
    appendU32LittleEndian(chunkX);
    appendU32LittleEndian(chunkY);

    auto digest = Core::digestContentHashV1(input);
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext("deriveTileMapChunkAssetId", "digest"));
    }
    auto chunkId = Core::AssetId::fromBytes(digest->bytes());
    if (!chunkId)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "tilemap chunk AssetId derivation produced an invalid zero value");
    }
    return *chunkId;
}

std::optional<Core::u16> TileMapChunkPayloadView::cellAt(Core::u16 x, Core::u16 y) const noexcept
{
    if (x >= widthCells || y >= heightCells)
    {
        return std::nullopt;
    }
    const usize index = static_cast<usize>(y) * widthCells + x;
    const usize offset = index * sizeof(u16);
    if (index >= cellCount || offset > cellBytes.size() || sizeof(u16) > cellBytes.size() - offset)
    {
        return std::nullopt;
    }
    return readU16(cellBytes, offset);
}

Core::Result<std::vector<std::byte>> writeTileMapChunkPayloadBytes(const TileMapChunkPayloadDesc& desc)
{
    if (const auto status = validateDesc(desc); !status)
    {
        return Core::failure(status.error());
    }

    try
    {
        const u32 cellCount = static_cast<u32>(desc.cells.size());
        std::vector<std::byte> bytes;
        bytes.reserve(TileMapChunkWire::HeaderBytes + static_cast<usize>(cellCount) * sizeof(u16));
        appendU16(bytes, TileMapChunkWire::SchemaVersion);
        appendU16(bytes, 0U);
        bytes.insert(bytes.end(), desc.parentTileMapId.bytes().begin(), desc.parentTileMapId.bytes().end());
        appendU32(bytes, desc.layerId);
        appendU32(bytes, desc.chunkX);
        appendU32(bytes, desc.chunkY);
        appendU16(bytes, desc.widthCells);
        appendU16(bytes, desc.heightCells);
        appendU32(bytes, cellCount);
        appendU32(bytes, countNonEmpty(desc.cells));
        appendU32(bytes, 0U);
        for (const u16 cell : desc.cells)
        {
            appendU16(bytes, cell);
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "tilemap chunk payload allocation failed");
    }
    catch (const std::length_error&)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "tilemap chunk payload exceeds vector size limit");
    }
}

Core::Result<TileMapChunkPayloadView> parseTileMapChunkPayload(std::span<const std::byte> payload)
{
    if (payload.size() < TileMapChunkWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "tilemap chunk payload too small");
    }

    const u16 schemaVersion = readU16(payload, 0U);
    if (schemaVersion != TileMapChunkWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported tilemap chunk payload schema");
    }
    if (readU16(payload, 2U) != 0U || readU32(payload, 44U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "tilemap chunk reserved fields must be zero");
    }

    Core::AssetId::Bytes parentBytes{};
    std::memcpy(parentBytes.data(), payload.data() + 4U, parentBytes.size());
    const auto parentTileMapId = Core::AssetId::fromBytes(parentBytes);
    const u32 layerId = readU32(payload, 20U);
    if (!parentTileMapId || layerId == 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "tilemap chunk parent map or stable layer identity is zero");
    }

    const u32 chunkX = readU32(payload, 24U);
    const u32 chunkY = readU32(payload, 28U);
    if (chunkX > TileMapChunkWire::MaxChunkCoordinate || chunkY > TileMapChunkWire::MaxChunkCoordinate)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "tilemap chunk coordinate exceeds limit");
    }

    const u16 widthCells = readU16(payload, 32U);
    const u16 heightCells = readU16(payload, 34U);
    const u32 cellCount = readU32(payload, 36U);
    const u32 nonEmptyCount = readU32(payload, 40U);
    if (widthCells == 0 || heightCells == 0 || widthCells > TileMapChunkWire::MaxDimension ||
        heightCells > TileMapChunkWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap chunk dimensions out of range");
    }
    const u32 expectedCellCount = static_cast<u32>(widthCells) * heightCells;
    if (cellCount != expectedCellCount || cellCount > TileMapChunkWire::MaxCells || nonEmptyCount == 0U ||
        nonEmptyCount > cellCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap chunk stored counts are invalid");
    }

    const usize expectedBytes = TileMapChunkWire::HeaderBytes + static_cast<usize>(cellCount) * sizeof(u16);
    if (payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap chunk payload size or trailing bytes invalid");
    }
    const auto cellBytes = payload.subspan(TileMapChunkWire::HeaderBytes);
    if (countNonEmpty(cellBytes) != nonEmptyCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tilemap chunk non-empty count mismatch");
    }

    return TileMapChunkPayloadView{
        .schemaVersion = schemaVersion,
        .parentTileMapId = *parentTileMapId,
        .layerId = layerId,
        .chunkX = chunkX,
        .chunkY = chunkY,
        .widthCells = widthCells,
        .heightCells = heightCells,
        .cellCount = cellCount,
        .nonEmptyCount = nonEmptyCount,
        .cellBytes = cellBytes,
    };
}

Core::Result<std::vector<std::byte>> writeCookedTileMapChunkAsset(Core::AssetId chunkAssetId,
                                                                  const TileMapChunkPayloadDesc& desc,
                                                                  TargetPlatform platform)
{
    if (!chunkAssetId || chunkAssetId == desc.parentTileMapId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "tilemap chunk asset id must be non-zero and differ from its parent map id");
    }
    auto payload = writeTileMapChunkPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::TileMapChunk,
        .assetTypeVersion = TileMapChunkWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = chunkAssetId,
        .dependencies = {},
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
