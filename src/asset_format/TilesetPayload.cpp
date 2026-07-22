#include <tina/asset_format/TilesetPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <array>
#include <cstring>

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

[[nodiscard]] bool finiteUv(float value) noexcept
{
    return value == value && value >= 0.0f && value <= 1.0f;
}

} // namespace

std::optional<TilesetTileView> TilesetPayloadView::tile(Core::u32 index) const noexcept
{
    if (index >= tileCount)
    {
        return std::nullopt;
    }
    const usize offset = static_cast<usize>(index) * TilesetWire::EntryBytes;
    if (offset + TilesetWire::EntryBytes > entriesBytes.size())
    {
        return std::nullopt;
    }
    TilesetTileView view{};
    view.localId = readU16(entriesBytes, offset);
    view.materialFlags = readU16(entriesBytes, offset + 2U);
    view.u0 = readF32(entriesBytes, offset + 4U);
    view.v0 = readF32(entriesBytes, offset + 8U);
    view.u1 = readF32(entriesBytes, offset + 12U);
    view.v1 = readF32(entriesBytes, offset + 16U);
    return view;
}

Core::Result<std::vector<std::byte>> writeTilesetPayloadBytes(const TilesetPayloadDesc& desc)
{
    if (desc.tilePixelWidth == 0 || desc.tilePixelHeight == 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tileset tile pixel size must be non-zero");
    }
    if (desc.tiles.empty() || desc.tiles.size() > TilesetWire::MaxTiles)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tileset tile count out of range");
    }
    for (const auto& tile : desc.tiles)
    {
        if (!finiteUv(tile.u0) || !finiteUv(tile.v0) || !finiteUv(tile.u1) || !finiteUv(tile.v1) ||
            !(tile.u0 < tile.u1) || !(tile.v0 < tile.v1))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "tileset tile UV invalid");
        }
        constexpr u16 Allowed = TilesetWire::MaterialSolid | TilesetWire::MaterialOneWay | TilesetWire::MaterialTrigger;
        if ((tile.materialFlags & ~Allowed) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unknown tileset material flags");
        }
    }

    const u32 tileCount = static_cast<u32>(desc.tiles.size());
    const usize total = TilesetWire::HeaderBytes + static_cast<usize>(tileCount) * TilesetWire::EntryBytes;
    try
    {
        std::vector<std::byte> bytes(total, std::byte{0});
        writeU16(bytes, 0U, TilesetWire::SchemaVersion);
        writeU16(bytes, 2U, 0U);
        writeU16(bytes, 4U, desc.tilePixelWidth);
        writeU16(bytes, 6U, desc.tilePixelHeight);
        writeU32(bytes, 8U, tileCount);
        for (u32 index = 0; index < tileCount; ++index)
        {
            const auto& tile = desc.tiles[index];
            const usize offset = TilesetWire::HeaderBytes + static_cast<usize>(index) * TilesetWire::EntryBytes;
            writeU16(bytes, offset, tile.localId);
            writeU16(bytes, offset + 2U, tile.materialFlags);
            writeF32(bytes, offset + 4U, tile.u0);
            writeF32(bytes, offset + 8U, tile.v0);
            writeF32(bytes, offset + 12U, tile.u1);
            writeF32(bytes, offset + 16U, tile.v1);
        }
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "tileset payload allocation failed");
    }
}

Core::Result<TilesetPayloadView> parseTilesetPayload(std::span<const std::byte> payload)
{
    if (payload.size() < TilesetWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "tileset payload too small");
    }
    TilesetPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.flags = readU16(payload, 2U);
    view.tilePixelWidth = readU16(payload, 4U);
    view.tilePixelHeight = readU16(payload, 6U);
    view.tileCount = readU32(payload, 8U);

    if (view.schemaVersion != TilesetWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported tileset payload schema");
    }
    if (view.flags != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unknown tileset flags");
    }
    if (view.tilePixelWidth == 0 || view.tilePixelHeight == 0 || view.tileCount == 0 ||
        view.tileCount > TilesetWire::MaxTiles)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tileset header fields invalid");
    }
    const usize expected = TilesetWire::HeaderBytes + static_cast<usize>(view.tileCount) * TilesetWire::EntryBytes;
    if (payload.size() != expected)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "tileset payload size mismatch");
    }
    view.entriesBytes = payload.subspan(TilesetWire::HeaderBytes);
    for (u32 index = 0; index < view.tileCount; ++index)
    {
        auto tile = view.tile(index);
        if (!tile || !finiteUv(tile->u0) || !finiteUv(tile->v0) || !finiteUv(tile->u1) || !finiteUv(tile->v1) ||
            !(tile->u0 < tile->u1) || !(tile->v0 < tile->v1))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "tileset entry UV invalid");
        }
    }
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedTilesetAsset(Core::AssetId tilesetId, const TilesetPayloadDesc& desc,
                                                             TargetPlatform platform)
{
    if (!tilesetId || !desc.textureId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "tileset requires tileset id and texture id");
    }
    auto payload = writeTilesetPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    const std::array deps{CookedAssetWriteDependency{
        .assetId = desc.textureId,
        .expectedKind = AssetKind::Texture2D,
        .flags = DependencyFlags::Required,
    }};
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Tileset,
        .assetTypeVersion = TilesetWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = tilesetId,
        .dependencies = deps,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
