#pragma once

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Asset::TestSupport {

struct TestTileMapLayerDesc final {
    AssetFormat::TileMapLayerId stableLayerId = 0;
    AssetFormat::TileMapLayerKind kind = AssetFormat::TileMapLayerKind::Tile;
    bool visible = true;
    std::string_view name{};
    std::span<const Core::u16> cells{};
    std::span<const AssetFormat::TileMapObjectDesc> objects{};
};

[[nodiscard]] inline Core::Result<Core::AssetId> makeDerivedChunkId(Core::AssetId mapId,
                                                                   AssetFormat::TileMapLayerId stableLayerId,
                                                                   Core::u32 chunkX,
                                                                   Core::u32 chunkY)
{
    constexpr std::string_view Domain = "tina.test.tilemap-chunk-id";
    constexpr Core::u8 DerivationVersion = 1U;
    constexpr std::size_t ScalarBytes = sizeof(Core::u32);
    std::array<std::byte, Domain.size() + 1U + Core::AssetId::Bytes{}.size() + ScalarBytes * 3U> input{};

    std::size_t offset = 0;
    for (const char value : Domain)
    {
        input[offset++] = static_cast<std::byte>(static_cast<unsigned char>(value));
    }
    input[offset++] = static_cast<std::byte>(DerivationVersion);
    for (const std::byte value : mapId.bytes())
    {
        input[offset++] = value;
    }
    const auto appendU32LittleEndian = [&input, &offset](Core::u32 value) {
        for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
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
        return Core::failure(std::move(digest.error()).withContext("makeDerivedChunkId", "digest"));
    }
    auto chunkId = Core::AssetId::fromBytes(digest->bytes());
    if (!chunkId)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "test tilemap chunk AssetId derivation produced an invalid zero value");
    }
    return *chunkId;
}

[[nodiscard]] inline Core::Result<TileMapInstance> makeResidentTileMapInstance(
    Core::u32 widthCells, Core::u32 heightCells, Core::u16 chunkSizeCells, Core::AssetId mapId,
    Core::AssetId tilesetId, const AssetFormat::TilesetPayloadView& tileset,
    std::span<const TestTileMapLayerDesc> layers, std::pmr::memory_resource& memory)
{
    std::vector<std::vector<AssetFormat::TileMapChunkRefDesc>> ownedRefs;
    std::vector<std::vector<std::byte>> chunkPayloads;
    std::vector<std::pair<Core::AssetId, std::size_t>> chunkPayloadIndex;
    std::vector<AssetFormat::TileMapLayerDesc> rootLayers;
    ownedRefs.reserve(layers.size());
    rootLayers.reserve(layers.size());

    for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex)
    {
        const TestTileMapLayerDesc& layer = layers[layerIndex];
        ownedRefs.emplace_back();
        auto& refs = ownedRefs.back();
        if (layer.kind == AssetFormat::TileMapLayerKind::Tile)
        {
            if (layer.cells.size() != static_cast<std::size_t>(widthCells) * heightCells)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "test tile layer cell count mismatch");
            }
            const Core::u32 chunkCountX = (widthCells + chunkSizeCells - 1U) / chunkSizeCells;
            const Core::u32 chunkCountY = (heightCells + chunkSizeCells - 1U) / chunkSizeCells;
            for (Core::u32 chunkY = 0; chunkY < chunkCountY; ++chunkY)
            {
                for (Core::u32 chunkX = 0; chunkX < chunkCountX; ++chunkX)
                {
                    const Core::u32 originX = chunkX * chunkSizeCells;
                    const Core::u32 originY = chunkY * chunkSizeCells;
                    const Core::u16 chunkWidth = static_cast<Core::u16>(
                        (std::min)(static_cast<Core::u32>(chunkSizeCells), widthCells - originX));
                    const Core::u16 chunkHeight = static_cast<Core::u16>(
                        (std::min)(static_cast<Core::u32>(chunkSizeCells), heightCells - originY));
                    std::vector<Core::u16> chunkCells;
                    chunkCells.reserve(static_cast<std::size_t>(chunkWidth) * chunkHeight);
                    Core::u32 nonEmpty = 0;
                    for (Core::u16 y = 0; y < chunkHeight; ++y)
                    {
                        for (Core::u16 x = 0; x < chunkWidth; ++x)
                        {
                            const Core::u16 cell = layer.cells[(originY + y) * widthCells + originX + x];
                            chunkCells.push_back(cell);
                            nonEmpty += cell != AssetFormat::TileMapWire::EmptyTileId ? 1U : 0U;
                        }
                    }
                    if (nonEmpty == 0U)
                    {
                        continue;
                    }
                    auto chunkId = makeDerivedChunkId(mapId, layer.stableLayerId, chunkX, chunkY);
                    if (!chunkId)
                    {
                        return Core::failure(std::move(chunkId.error()));
                    }
                    auto payload = AssetFormat::writeTileMapChunkPayloadBytes(AssetFormat::TileMapChunkPayloadDesc{
                        .parentTileMapId = mapId,
                        .layerId = layer.stableLayerId,
                        .chunkX = chunkX,
                        .chunkY = chunkY,
                        .widthCells = chunkWidth,
                        .heightCells = chunkHeight,
                        .cells = chunkCells,
                    });
                    if (!payload)
                    {
                        return Core::failure(std::move(payload.error()));
                    }
                    refs.push_back(AssetFormat::TileMapChunkRefDesc{
                        .chunkX = chunkX,
                        .chunkY = chunkY,
                        .widthCells = chunkWidth,
                        .heightCells = chunkHeight,
                        .nonEmptyCount = nonEmpty,
                        .chunkAssetId = *chunkId,
                    });
                    chunkPayloadIndex.emplace_back(*chunkId, chunkPayloads.size());
                    chunkPayloads.push_back(std::move(*payload));
                }
            }
        }
        rootLayers.push_back(AssetFormat::TileMapLayerDesc{
            .stableLayerId = layer.stableLayerId,
            .kind = layer.kind,
            .visible = layer.visible,
            .name = layer.name,
            .chunkRefs = refs,
            .objects = layer.objects,
        });
    }

    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = widthCells,
        .heightCells = heightCells,
        .chunkSizeCells = chunkSizeCells,
        .layers = rootLayers,
        .tilesetId = tilesetId,
    });
    if (!mapBytes)
    {
        return Core::failure(std::move(mapBytes.error()));
    }
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    if (!map)
    {
        return Core::failure(std::move(map.error()));
    }
    auto instance = TileMapInstance::Create(
        *map, tileset, mapId, tilesetId,
        TileMapInstanceConfig{.residentChunkCapacity = (std::max)(Core::usize{64}, chunkPayloads.size()),
                              .memoryResource = &memory});
    if (!instance)
    {
        return Core::failure(std::move(instance.error()));
    }
    Core::u64 generation = 1U;
    for (const auto& [chunkId, payloadIndex] : chunkPayloadIndex)
    {
        auto chunk = AssetFormat::parseTileMapChunkPayload(chunkPayloads[payloadIndex]);
        if (!chunk)
        {
            return Core::failure(std::move(chunk.error()));
        }
        auto attached = instance->attachChunk(chunkId, *chunk, generation++);
        if (!attached)
        {
            return Core::failure(std::move(attached.error()));
        }
    }
    return std::move(*instance);
}

} // namespace Tina::Asset::TestSupport
