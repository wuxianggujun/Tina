#include <tina/editor/TileMapAuthoringDocument.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

using AssetFormat::TileMapChunkPayloadDesc;
using AssetFormat::TileMapChunkRefDesc;
using AssetFormat::TileMapLayerDesc;
using AssetFormat::TileMapLayerId;
using AssetFormat::TileMapLayerKind;
using AssetFormat::TileMapObjectDesc;
using AssetFormat::TileMapPayloadDesc;
using AssetFormat::TileMapPropertyDesc;

[[nodiscard]] Core::Status allocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "TileMap authoring document allocation failed");
}

[[nodiscard]] Core::u32 countNonEmpty(std::span<const Core::u16> cells) noexcept
{
    Core::u32 count = 0;
    for (const Core::u16 cell : cells)
    {
        count += cell != AssetFormat::TileMapWire::EmptyTileId ? 1U : 0U;
    }
    return count;
}

struct ChunkExtent final {
    Core::u16 width = 0;
    Core::u16 height = 0;
};

[[nodiscard]] Core::Result<ChunkExtent>
chunkExtent(const TileMapAuthoringDesc& desc, Core::u32 chunkX, Core::u32 chunkY)
{
    if (desc.widthCells == 0U || desc.heightCells == 0U || desc.chunkSizeCells == 0U)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "TileMap chunk extent requires valid map dimensions");
    }
    const Core::u32 chunkCountX =
        (desc.widthCells + desc.chunkSizeCells - 1U) / desc.chunkSizeCells;
    const Core::u32 chunkCountY =
        (desc.heightCells + desc.chunkSizeCells - 1U) / desc.chunkSizeCells;
    if (chunkX >= chunkCountX || chunkY >= chunkCountY)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "TileMap chunk coordinate lies outside the map");
    }
    const Core::u32 originX = chunkX * desc.chunkSizeCells;
    const Core::u32 originY = chunkY * desc.chunkSizeCells;
    return ChunkExtent{
        .width = static_cast<Core::u16>(
            (std::min)(static_cast<Core::u32>(desc.chunkSizeCells),
                       desc.widthCells - originX)),
        .height = static_cast<Core::u16>(
            (std::min)(static_cast<Core::u32>(desc.chunkSizeCells),
                       desc.heightCells - originY)),
    };
}

[[nodiscard]] bool coordinateLess(const TileMapAuthoringChunk* left,
                                  const TileMapAuthoringChunk* right) noexcept
{
    return left->chunkY < right->chunkY ||
           (left->chunkY == right->chunkY && left->chunkX < right->chunkX);
}

[[nodiscard]] bool sameChunkCoordinate(const TileMapAuthoringChunk& left,
                                       const TileMapAuthoringChunk& right) noexcept
{
    return left.chunkX == right.chunkX && left.chunkY == right.chunkY;
}

} // namespace

Core::Status validateTileMapAuthoringDocumentConfig(
    const TileMapAuthoringDocumentConfig& config) noexcept
{
    if (config.layerCapacity == 0U ||
        config.layerCapacity > AssetFormat::TileMapWire::MaxLayers)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap authoring layer capacity exceeds the current schema limit");
    }
    if (config.objectCapacity > AssetFormat::TileMapWire::MaxObjectsPerMap)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap authoring object capacity exceeds the current schema limit");
    }
    if (config.chunkCapacity > AssetFormat::TileMapWire::MaxChunkRefsPerMap)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap authoring chunk capacity exceeds the current schema limit");
    }
    if (config.historyEntryCapacity < 2U ||
        config.historyEntryCapacity > TileMapAuthoringLimits::MaximumHistoryEntries)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap authoring history must contain between 2 and 256 entries");
    }
    if (config.historyByteCapacity < AssetFormat::TileMapWire::HeaderBytes * 2U ||
        config.historyByteCapacity > TileMapAuthoringLimits::MaximumHistoryBytes)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap authoring history byte capacity is outside the supported range");
    }
    return Core::success();
}

Core::Result<TileMapAuthoringDocument>
TileMapAuthoringDocument::Create(const TileMapAuthoringDesc& initial,
                                 TileMapAuthoringDocumentConfig config)
{
    if (const Core::Status status = validateTileMapAuthoringDocumentConfig(config); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    try
    {
        std::vector<Revision> history;
        history.reserve(config.historyEntryCapacity);
        TileMapAuthoringDocument document{config, std::move(history)};
        auto initialRevision = document.buildRevision(initial);
        if (!initialRevision)
        {
            return Core::failure(std::move(initialRevision.error()));
        }
        if (initialRevision->byteCount > config.historyByteCapacity)
        {
            return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                                 "TileMap authoring baseline exceeds the configured history byte capacity");
        }
        document.m_history.push_back(std::move(*initialRevision));
        document.m_historyBytes = document.m_history.front().byteCount;
        return document;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "TileMap authoring history allocation failed");
    }
}

TileMapAuthoringDocument::TileMapAuthoringDocument(
    TileMapAuthoringDocumentConfig config, std::vector<Revision> history) noexcept
    : m_config(config), m_history(std::move(history))
{
}

Core::AssetId TileMapAuthoringDocument::tileMapId() const noexcept
{
    return current().tileMapId;
}

Core::AssetId TileMapAuthoringDocument::tilesetId() const noexcept
{
    return current().tilesetId;
}

Core::usize TileMapAuthoringDocument::layerCount() const noexcept
{
    return current().layerCount;
}

Core::usize TileMapAuthoringDocument::chunkCount() const noexcept
{
    return current().chunks.size();
}

Core::usize TileMapAuthoringDocument::nonEmptyCellCount() const noexcept
{
    return current().nonEmptyCellCount;
}

std::span<const std::byte> TileMapAuthoringDocument::rootPayloadBytes() const noexcept
{
    return current().rootBytes;
}

std::optional<TileMapAuthoringChunkPayloadView>
TileMapAuthoringDocument::chunkPayloadAt(Core::usize index) const noexcept
{
    if (index >= current().chunks.size())
    {
        return std::nullopt;
    }
    const ChunkRevision& chunk = current().chunks[index];
    return TileMapAuthoringChunkPayloadView{
        .assetId = chunk.assetId,
        .layerId = chunk.layerId,
        .chunkX = chunk.chunkX,
        .chunkY = chunk.chunkY,
        .widthCells = chunk.widthCells,
        .heightCells = chunk.heightCells,
        .nonEmptyCount = chunk.nonEmptyCount,
        .payloadBytes = chunk.bytes,
    };
}

Core::Result<TileMapAuthoringDocument::Revision>
TileMapAuthoringDocument::buildRevision(const TileMapAuthoringDesc& desc) const
{
    if (!desc.tileMapId || !desc.tilesetId || desc.tileMapId == desc.tilesetId)
    {
        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                             "TileMap authoring requires distinct map and tileset identities");
    }
    if (desc.layers.size() > m_config.layerCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "TileMap authoring layer capacity is exhausted");
    }

    struct WireLayer final {
        std::vector<TileMapPropertyDesc> properties{};
        std::vector<TileMapChunkRefDesc> chunkRefs{};
        std::vector<std::vector<TileMapPropertyDesc>> objectProperties{};
        std::vector<TileMapObjectDesc> objects{};
        TileMapLayerDesc desc{};
    };

    try
    {
        Revision candidate{
            .tileMapId = desc.tileMapId,
            .tilesetId = desc.tilesetId,
            .layerCount = static_cast<Core::u32>(desc.layers.size()),
        };
        std::vector<WireLayer> wireLayers(desc.layers.size());
        std::vector<TileMapLayerDesc> rootLayers;
        rootLayers.reserve(desc.layers.size());
        Core::usize objectCount = 0;
        Core::u64 nonEmptyCellCount = 0;

        for (Core::usize layerIndex = 0; layerIndex < desc.layers.size(); ++layerIndex)
        {
            const TileMapAuthoringLayer& authoredLayer = desc.layers[layerIndex];
            WireLayer& wire = wireLayers[layerIndex];
            wire.properties.reserve(authoredLayer.properties.size());
            for (const TileMapAuthoringProperty& property : authoredLayer.properties)
            {
                wire.properties.push_back({.key = property.key, .value = property.value});
            }

            if (authoredLayer.kind == TileMapLayerKind::Tile)
            {
                if (!authoredLayer.objects.empty())
                {
                    return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                         "TileMap tile layers cannot own objects");
                }
                if (candidate.chunks.size() + authoredLayer.chunks.size() >
                    m_config.chunkCapacity)
                {
                    return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                         "TileMap authoring chunk capacity is exhausted");
                }

                std::vector<const TileMapAuthoringChunk*> sortedChunks;
                sortedChunks.reserve(authoredLayer.chunks.size());
                for (const TileMapAuthoringChunk& chunk : authoredLayer.chunks)
                {
                    sortedChunks.push_back(&chunk);
                }
                std::sort(sortedChunks.begin(), sortedChunks.end(), coordinateLess);
                wire.chunkRefs.reserve(sortedChunks.size());
                for (Core::usize chunkIndex = 0; chunkIndex < sortedChunks.size(); ++chunkIndex)
                {
                    const TileMapAuthoringChunk& chunk = *sortedChunks[chunkIndex];
                    if (chunkIndex != 0U &&
                        sameChunkCoordinate(*sortedChunks[chunkIndex - 1U], chunk))
                    {
                        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                                             "TileMap authoring contains duplicate chunk coordinates");
                    }
                    auto extent = chunkExtent(desc, chunk.chunkX, chunk.chunkY);
                    if (!extent)
                    {
                        return Core::failure(std::move(extent.error()));
                    }
                    const Core::usize expectedCellCount =
                        static_cast<Core::usize>(extent->width) * extent->height;
                    if (chunk.cells.size() != expectedCellCount)
                    {
                        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                             "TileMap authoring chunk cell count does not match its extent");
                    }
                    const Core::u32 nonEmptyCount = countNonEmpty(chunk.cells);
                    if (nonEmptyCount == 0U)
                    {
                        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                             "TileMap authoring stores only non-empty chunks");
                    }
                    auto chunkId = AssetFormat::deriveTileMapChunkAssetId(
                        desc.tileMapId, authoredLayer.stableLayerId, chunk.chunkX, chunk.chunkY);
                    if (!chunkId)
                    {
                        return Core::failure(std::move(chunkId.error()));
                    }
                    auto payload = AssetFormat::writeTileMapChunkPayloadBytes(
                        TileMapChunkPayloadDesc{
                            .parentTileMapId = desc.tileMapId,
                            .layerId = authoredLayer.stableLayerId,
                            .chunkX = chunk.chunkX,
                            .chunkY = chunk.chunkY,
                            .widthCells = extent->width,
                            .heightCells = extent->height,
                            .cells = chunk.cells,
                        });
                    if (!payload)
                    {
                        return Core::failure(std::move(payload.error()));
                    }
                    candidate.chunks.push_back(ChunkRevision{
                        .assetId = *chunkId,
                        .layerId = authoredLayer.stableLayerId,
                        .chunkX = chunk.chunkX,
                        .chunkY = chunk.chunkY,
                        .widthCells = extent->width,
                        .heightCells = extent->height,
                        .nonEmptyCount = nonEmptyCount,
                        .bytes = std::move(*payload),
                    });
                    wire.chunkRefs.push_back(TileMapChunkRefDesc{
                        .chunkX = chunk.chunkX,
                        .chunkY = chunk.chunkY,
                        .widthCells = extent->width,
                        .heightCells = extent->height,
                        .nonEmptyCount = nonEmptyCount,
                        .chunkAssetId = *chunkId,
                    });
                    nonEmptyCellCount += nonEmptyCount;
                }
            }
            else if (authoredLayer.kind == TileMapLayerKind::Object)
            {
                if (!authoredLayer.chunks.empty())
                {
                    return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                         "TileMap object layers cannot own chunks");
                }
                if (objectCount + authoredLayer.objects.size() > m_config.objectCapacity)
                {
                    return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                         "TileMap authoring object capacity is exhausted");
                }
                objectCount += authoredLayer.objects.size();
                wire.objectProperties.resize(authoredLayer.objects.size());
                wire.objects.reserve(authoredLayer.objects.size());
                for (Core::usize objectIndex = 0;
                     objectIndex < authoredLayer.objects.size(); ++objectIndex)
                {
                    const TileMapAuthoringObject& authoredObject =
                        authoredLayer.objects[objectIndex];
                    auto& properties = wire.objectProperties[objectIndex];
                    properties.reserve(authoredObject.properties.size());
                    for (const TileMapAuthoringProperty& property : authoredObject.properties)
                    {
                        properties.push_back({.key = property.key, .value = property.value});
                    }
                    wire.objects.push_back(TileMapObjectDesc{
                        .stableObjectId = authoredObject.stableObjectId,
                        .kind = authoredObject.kind,
                        .visible = authoredObject.visible,
                        .name = authoredObject.name,
                        .x = authoredObject.x,
                        .y = authoredObject.y,
                        .width = authoredObject.width,
                        .height = authoredObject.height,
                        .properties = properties,
                    });
                }
            }
            else
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::UnsupportedValue,
                                     "TileMap authoring layer kind is unsupported");
            }

            wire.desc = TileMapLayerDesc{
                .stableLayerId = authoredLayer.stableLayerId,
                .kind = authoredLayer.kind,
                .visible = authoredLayer.visible,
                .name = authoredLayer.name,
                .properties = wire.properties,
                .chunkRefs = wire.chunkRefs,
                .objects = wire.objects,
            };
            rootLayers.push_back(wire.desc);
        }

        if (nonEmptyCellCount > (std::numeric_limits<Core::u32>::max)())
        {
            return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                 "TileMap authored non-empty cell count exceeds the supported range");
        }
        auto rootBytes = AssetFormat::writeTileMapPayloadBytes(TileMapPayloadDesc{
            .widthCells = desc.widthCells,
            .heightCells = desc.heightCells,
            .cellSizeMeters = desc.cellSizeMeters,
            .chunkSizeCells = desc.chunkSizeCells,
            .layers = rootLayers,
            .tilesetId = desc.tilesetId,
        });
        if (!rootBytes)
        {
            return Core::failure(std::move(rootBytes.error()));
        }
        candidate.rootBytes = std::move(*rootBytes);
        candidate.nonEmptyCellCount = static_cast<Core::u32>(nonEmptyCellCount);
        candidate.byteCount = candidate.rootBytes.size();
        for (const ChunkRevision& chunk : candidate.chunks)
        {
            if (candidate.byteCount > (std::numeric_limits<Core::usize>::max)() -
                                          chunk.bytes.size())
            {
                return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                     "TileMap authoring payload family byte count overflowed");
            }
            candidate.byteCount += chunk.bytes.size();
        }
        return candidate;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(allocationFailure().error());
    }
}

Core::Result<TileMapAuthoringDesc>
TileMapAuthoringDocument::decode(const Revision& revision) const
{
    auto root = AssetFormat::parseTileMapPayload(revision.rootBytes);
    if (!root)
    {
        return Core::failure(std::move(root.error()));
    }

    try
    {
        TileMapAuthoringDesc result{
            .tileMapId = revision.tileMapId,
            .tilesetId = revision.tilesetId,
            .widthCells = root->widthCells,
            .heightCells = root->heightCells,
            .cellSizeMeters = root->cellSizeMeters,
            .chunkSizeCells = root->chunkSizeCells,
        };
        result.layers.reserve(root->layerCount);
        Core::usize referencedChunkCount = 0;
        for (Core::u16 layerIndex = 0; layerIndex < root->layerCount; ++layerIndex)
        {
            const auto layer = root->layerAt(layerIndex);
            if (!layer)
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                     "TileMap layer disappeared after root validation");
            }
            TileMapAuthoringLayer authoredLayer{
                .stableLayerId = layer->stableLayerId,
                .kind = layer->kind,
                .visible = layer->visible,
                .name = std::string(layer->name),
            };
            authoredLayer.properties.reserve(layer->propertyCount);
            for (Core::u16 propertyIndex = 0;
                 propertyIndex < layer->propertyCount; ++propertyIndex)
            {
                const auto property = layer->propertyAt(propertyIndex);
                if (!property)
                {
                    return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                         "TileMap layer property disappeared after validation");
                }
                authoredLayer.properties.push_back(
                    {.key = std::string(property->key), .value = std::string(property->value)});
            }

            if (layer->kind == TileMapLayerKind::Tile)
            {
                authoredLayer.chunks.reserve(layer->chunkRefCount);
                for (Core::u32 chunkIndex = 0;
                     chunkIndex < layer->chunkRefCount; ++chunkIndex)
                {
                    const auto chunkRef = layer->chunkRefAt(chunkIndex);
                    if (!chunkRef)
                    {
                        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                             "TileMap chunk reference disappeared after validation");
                    }
                    const auto stored = std::find_if(
                        revision.chunks.begin(), revision.chunks.end(),
                        [&chunkRef](const ChunkRevision& candidate) {
                            return candidate.assetId == chunkRef->chunkAssetId;
                        });
                    if (stored == revision.chunks.end())
                    {
                        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                             "TileMap root references a missing chunk payload");
                    }
                    auto derived = AssetFormat::deriveTileMapChunkAssetId(
                        revision.tileMapId, layer->stableLayerId,
                        chunkRef->chunkX, chunkRef->chunkY);
                    if (!derived || *derived != chunkRef->chunkAssetId)
                    {
                        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                                             "TileMap chunk reference does not use current stable identity");
                    }
                    auto chunk = AssetFormat::parseTileMapChunkPayload(stored->bytes);
                    if (!chunk)
                    {
                        return Core::failure(std::move(chunk.error()));
                    }
                    if (chunk->parentTileMapId != revision.tileMapId ||
                        chunk->layerId != layer->stableLayerId ||
                        chunk->chunkX != chunkRef->chunkX ||
                        chunk->chunkY != chunkRef->chunkY ||
                        chunk->widthCells != chunkRef->widthCells ||
                        chunk->heightCells != chunkRef->heightCells ||
                        chunk->nonEmptyCount != chunkRef->nonEmptyCount)
                    {
                        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                             "TileMap chunk payload does not match its root reference");
                    }
                    TileMapAuthoringChunk authoredChunk{
                        .chunkX = chunk->chunkX,
                        .chunkY = chunk->chunkY,
                    };
                    authoredChunk.cells.reserve(chunk->cellCount);
                    for (Core::u16 y = 0; y < chunk->heightCells; ++y)
                    {
                        for (Core::u16 x = 0; x < chunk->widthCells; ++x)
                        {
                            const auto cell = chunk->cellAt(x, y);
                            if (!cell)
                            {
                                return Core::failure(
                                    AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                    "TileMap chunk cell disappeared after validation");
                            }
                            authoredChunk.cells.push_back(*cell);
                        }
                    }
                    authoredLayer.chunks.push_back(std::move(authoredChunk));
                    ++referencedChunkCount;
                }
            }
            else
            {
                authoredLayer.objects.reserve(layer->objectCount);
                for (Core::u32 objectIndex = 0;
                     objectIndex < layer->objectCount; ++objectIndex)
                {
                    const auto object = layer->objectAt(objectIndex);
                    if (!object)
                    {
                        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                             "TileMap object disappeared after validation");
                    }
                    TileMapAuthoringObject authoredObject{
                        .stableObjectId = object->stableObjectId,
                        .kind = object->kind,
                        .visible = object->visible,
                        .name = std::string(object->name),
                        .x = object->x,
                        .y = object->y,
                        .width = object->width,
                        .height = object->height,
                    };
                    authoredObject.properties.reserve(object->propertyCount);
                    for (Core::u16 propertyIndex = 0;
                         propertyIndex < object->propertyCount; ++propertyIndex)
                    {
                        const auto property = object->propertyAt(propertyIndex);
                        if (!property)
                        {
                            return Core::failure(
                                AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                "TileMap object property disappeared after validation");
                        }
                        authoredObject.properties.push_back(
                            {.key = std::string(property->key),
                             .value = std::string(property->value)});
                    }
                    authoredLayer.objects.push_back(std::move(authoredObject));
                }
            }
            result.layers.push_back(std::move(authoredLayer));
        }
        if (referencedChunkCount != revision.chunks.size())
        {
            return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidLayout,
                                 "TileMap payload family contains an unreferenced chunk");
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(allocationFailure().error());
    }
}

Core::Result<TileMapAuthoringDesc> TileMapAuthoringDocument::snapshot() const
{
    return decode(current());
}

Core::Status TileMapAuthoringDocument::replace(const TileMapAuthoringDesc& desc)
{
    auto candidate = buildRevision(desc);
    if (!candidate)
    {
        return Core::failure(std::move(candidate.error()));
    }
    return commit(std::move(*candidate));
}

Core::Status TileMapAuthoringDocument::setCells(
    TileMapLayerId layerId, std::span<const TileMapAuthoringCellEdit> edits)
{
    if (edits.empty())
    {
        return Core::success();
    }
    try
    {
        for (Core::usize index = 0; index < edits.size(); ++index)
        {
            for (Core::usize prior = 0; prior < index; ++prior)
            {
                if (edits[index].x == edits[prior].x && edits[index].y == edits[prior].y)
                {
                    return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                         "TileMap brush batch contains duplicate cell coordinates");
                }
            }
        }

        auto authored = snapshot();
        if (!authored)
        {
            return Core::failure(std::move(authored.error()));
        }
        auto layer = std::find_if(
            authored->layers.begin(), authored->layers.end(),
            [layerId](const TileMapAuthoringLayer& candidate) {
                return candidate.stableLayerId == layerId;
            });
        if (layer == authored->layers.end())
        {
            return Core::failure(EditorErrorCode::LayerNotFound,
                                 "TileMap authoring layer does not exist");
        }
        if (layer->kind != TileMapLayerKind::Tile)
        {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "TileMap cells can only be edited on tile layers");
        }

        for (const TileMapAuthoringCellEdit& edit : edits)
        {
            if (edit.x >= authored->widthCells || edit.y >= authored->heightCells)
            {
                return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                     "TileMap brush cell lies outside the map");
            }
            const Core::u32 chunkX = edit.x / authored->chunkSizeCells;
            const Core::u32 chunkY = edit.y / authored->chunkSizeCells;
            auto chunk = std::find_if(
                layer->chunks.begin(), layer->chunks.end(),
                [chunkX, chunkY](const TileMapAuthoringChunk& candidate) {
                    return candidate.chunkX == chunkX && candidate.chunkY == chunkY;
                });
            if (chunk == layer->chunks.end())
            {
                if (edit.localTileId == AssetFormat::TileMapWire::EmptyTileId)
                {
                    continue;
                }
                if (current().chunks.size() + 1U > m_config.chunkCapacity)
                {
                    return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                         "TileMap authoring chunk capacity is exhausted");
                }
                auto extent = chunkExtent(*authored, chunkX, chunkY);
                if (!extent)
                {
                    return Core::failure(std::move(extent.error()));
                }
                layer->chunks.push_back(TileMapAuthoringChunk{
                    .chunkX = chunkX,
                    .chunkY = chunkY,
                    .cells = std::vector<Core::u16>(
                        static_cast<Core::usize>(extent->width) * extent->height,
                        AssetFormat::TileMapWire::EmptyTileId),
                });
                chunk = std::prev(layer->chunks.end());
            }
            auto extent = chunkExtent(*authored, chunkX, chunkY);
            if (!extent)
            {
                return Core::failure(std::move(extent.error()));
            }
            const Core::u32 localX = edit.x - chunkX * authored->chunkSizeCells;
            const Core::u32 localY = edit.y - chunkY * authored->chunkSizeCells;
            const Core::usize cellIndex =
                static_cast<Core::usize>(localY) * extent->width + localX;
            if (cellIndex >= chunk->cells.size())
            {
                return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                     "TileMap brush resolved an invalid chunk cell index");
            }
            chunk->cells[cellIndex] = edit.localTileId;
        }

        layer->chunks.erase(
            std::remove_if(layer->chunks.begin(), layer->chunks.end(),
                           [](const TileMapAuthoringChunk& chunk) {
                               return countNonEmpty(chunk.cells) == 0U;
                           }),
            layer->chunks.end());
        return replace(*authored);
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status TileMapAuthoringDocument::paintCell(TileMapLayerId layerId,
                                                  Core::u32 x, Core::u32 y,
                                                  Core::u16 localTileId)
{
    const std::array edit{
        TileMapAuthoringCellEdit{.x = x, .y = y, .localTileId = localTileId},
    };
    return setCells(layerId, edit);
}

Core::Status TileMapAuthoringDocument::setLayerVisibility(TileMapLayerId layerId,
                                                           bool visible)
{
    auto authored = snapshot();
    if (!authored)
    {
        return Core::failure(std::move(authored.error()));
    }
    const auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(),
        [layerId](const TileMapAuthoringLayer& candidate) {
            return candidate.stableLayerId == layerId;
        });
    if (layer == authored->layers.end())
    {
        return Core::failure(EditorErrorCode::LayerNotFound,
                             "TileMap authoring layer does not exist");
    }
    layer->visible = visible;
    return replace(*authored);
}

Core::Status TileMapAuthoringDocument::renameLayer(TileMapLayerId layerId,
                                                    std::string name)
{
    auto authored = snapshot();
    if (!authored)
    {
        return Core::failure(std::move(authored.error()));
    }
    const auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(),
        [layerId](const TileMapAuthoringLayer& candidate) {
            return candidate.stableLayerId == layerId;
        });
    if (layer == authored->layers.end())
    {
        return Core::failure(EditorErrorCode::LayerNotFound,
                             "TileMap authoring layer does not exist");
    }
    layer->name = std::move(name);
    return replace(*authored);
}

Core::Status TileMapAuthoringDocument::addTileLayer(TileMapLayerId layerId,
                                                     std::string name)
{
    try
    {
        auto authored = snapshot();
        if (!authored)
        {
            return Core::failure(std::move(authored.error()));
        }
        authored->layers.push_back(TileMapAuthoringLayer{
            .stableLayerId = layerId,
            .kind = TileMapLayerKind::Tile,
            .name = std::move(name),
        });
        return replace(*authored);
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status TileMapAuthoringDocument::addObjectLayer(TileMapLayerId layerId,
                                                       std::string name)
{
    try
    {
        auto authored = snapshot();
        if (!authored)
        {
            return Core::failure(std::move(authored.error()));
        }
        authored->layers.push_back(TileMapAuthoringLayer{
            .stableLayerId = layerId,
            .kind = TileMapLayerKind::Object,
            .name = std::move(name),
        });
        return replace(*authored);
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status TileMapAuthoringDocument::eraseLayer(TileMapLayerId layerId)
{
    auto authored = snapshot();
    if (!authored)
    {
        return Core::failure(std::move(authored.error()));
    }
    const auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(),
        [layerId](const TileMapAuthoringLayer& candidate) {
            return candidate.stableLayerId == layerId;
        });
    if (layer == authored->layers.end())
    {
        return Core::failure(EditorErrorCode::LayerNotFound,
                             "TileMap authoring layer does not exist");
    }
    if (authored->layers.size() == 1U)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "TileMap authoring document must retain at least one layer");
    }
    authored->layers.erase(layer);
    return replace(*authored);
}

Core::Status TileMapAuthoringDocument::upsertObject(TileMapLayerId layerId,
                                                     TileMapAuthoringObject object)
{
    try
    {
        auto authored = snapshot();
        if (!authored)
        {
            return Core::failure(std::move(authored.error()));
        }
        const auto layer = std::find_if(
            authored->layers.begin(), authored->layers.end(),
            [layerId](const TileMapAuthoringLayer& candidate) {
                return candidate.stableLayerId == layerId;
            });
        if (layer == authored->layers.end())
        {
            return Core::failure(EditorErrorCode::LayerNotFound,
                                 "TileMap authoring layer does not exist");
        }
        if (layer->kind != TileMapLayerKind::Object)
        {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "TileMap objects can only be edited on object layers");
        }
        const auto existing = std::find_if(
            layer->objects.begin(), layer->objects.end(),
            [&object](const TileMapAuthoringObject& candidate) {
                return candidate.stableObjectId == object.stableObjectId;
            });
        if (existing == layer->objects.end())
        {
            layer->objects.push_back(std::move(object));
        }
        else
        {
            *existing = std::move(object);
        }
        return replace(*authored);
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status TileMapAuthoringDocument::eraseObject(
    TileMapLayerId layerId, AssetFormat::TileMapObjectId objectId)
{
    auto authored = snapshot();
    if (!authored)
    {
        return Core::failure(std::move(authored.error()));
    }
    const auto layer = std::find_if(
        authored->layers.begin(), authored->layers.end(),
        [layerId](const TileMapAuthoringLayer& candidate) {
            return candidate.stableLayerId == layerId;
        });
    if (layer == authored->layers.end())
    {
        return Core::failure(EditorErrorCode::LayerNotFound,
                             "TileMap authoring layer does not exist");
    }
    if (layer->kind != TileMapLayerKind::Object)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "TileMap objects can only be edited on object layers");
    }
    const auto object = std::find_if(
        layer->objects.begin(), layer->objects.end(),
        [objectId](const TileMapAuthoringObject& candidate) {
            return candidate.stableObjectId == objectId;
        });
    if (object == layer->objects.end())
    {
        return Core::failure(EditorErrorCode::ObjectNotFound,
                             "TileMap authoring object does not exist");
    }
    layer->objects.erase(object);
    return replace(*authored);
}

Core::Status TileMapAuthoringDocument::loadPayloadFamily(
    Core::AssetId tileMapId, Core::AssetId tilesetId,
    std::span<const std::byte> rootPayload,
    std::span<const TileMapAuthoringChunkSource> chunks)
{
    if (!tileMapId || !tilesetId || tileMapId == tilesetId)
    {
        return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                             "TileMap payload family requires distinct map and tileset identities");
    }
    if (chunks.size() > m_config.chunkCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "TileMap payload family exceeds the configured chunk capacity");
    }
    auto root = AssetFormat::parseTileMapPayload(rootPayload);
    if (!root)
    {
        return Core::failure(std::move(root.error()));
    }
    if (root->layerCount > m_config.layerCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "TileMap payload family exceeds the configured layer capacity");
    }

    try
    {
        Revision raw{
            .tileMapId = tileMapId,
            .tilesetId = tilesetId,
            .rootBytes = std::vector<std::byte>(rootPayload.begin(), rootPayload.end()),
            .layerCount = root->layerCount,
        };
        raw.chunks.reserve(chunks.size());
        for (Core::usize index = 0; index < chunks.size(); ++index)
        {
            const TileMapAuthoringChunkSource& source = chunks[index];
            if (!source.assetId)
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                                     "TileMap chunk source has a zero asset identity");
            }
            if (std::any_of(raw.chunks.begin(), raw.chunks.end(),
                            [&source](const ChunkRevision& candidate) {
                                return candidate.assetId == source.assetId;
                            }))
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                                     "TileMap payload family contains duplicate chunk identities");
            }
            auto parsed = AssetFormat::parseTileMapChunkPayload(source.payloadBytes);
            if (!parsed)
            {
                return Core::failure(std::move(parsed.error()));
            }
            if (parsed->parentTileMapId != tileMapId)
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                                     "TileMap chunk source belongs to another map");
            }
            auto derived = AssetFormat::deriveTileMapChunkAssetId(
                tileMapId, parsed->layerId, parsed->chunkX, parsed->chunkY);
            if (!derived || *derived != source.assetId)
            {
                return Core::failure(AssetFormat::AssetFormatErrorCode::InvalidIdentity,
                                     "TileMap chunk source does not use current stable identity");
            }
            raw.chunks.push_back(ChunkRevision{
                .assetId = source.assetId,
                .layerId = parsed->layerId,
                .chunkX = parsed->chunkX,
                .chunkY = parsed->chunkY,
                .widthCells = parsed->widthCells,
                .heightCells = parsed->heightCells,
                .nonEmptyCount = parsed->nonEmptyCount,
                .bytes = std::vector<std::byte>(source.payloadBytes.begin(),
                                                source.payloadBytes.end()),
            });
            raw.nonEmptyCellCount += parsed->nonEmptyCount;
        }
        raw.byteCount = raw.rootBytes.size();
        for (const ChunkRevision& chunk : raw.chunks)
        {
            raw.byteCount += chunk.bytes.size();
        }
        auto authored = decode(raw);
        if (!authored)
        {
            return Core::failure(std::move(authored.error()));
        }
        auto canonical = buildRevision(*authored);
        if (!canonical)
        {
            return Core::failure(std::move(canonical.error()));
        }
        return resetBaseline(std::move(*canonical));
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Result<TileMapCookPreview>
TileMapAuthoringDocument::cookPreview(AssetFormat::TargetPlatform platform) const
{
    try
    {
        // Reconstructing the complete root descriptor is intentionally delegated
        // to buildRevision(); the stored root is already canonical, so wrap it with
        // the exact current dependency set here.
        std::vector<AssetFormat::CookedAssetWriteDependency> dependencies;
        dependencies.reserve(1U + current().chunks.size());
        dependencies.push_back({
            .assetId = current().tilesetId,
            .expectedKind = AssetFormat::AssetKind::Tileset,
            .flags = AssetFormat::DependencyFlags::Required,
        });
        for (const ChunkRevision& chunk : current().chunks)
        {
            dependencies.push_back({
                .assetId = chunk.assetId,
                .expectedKind = AssetFormat::AssetKind::TileMapChunk,
                .flags = AssetFormat::DependencyFlags::Required |
                         AssetFormat::DependencyFlags::Deferred,
            });
        }
        std::sort(dependencies.begin(), dependencies.end(),
                  [](const auto& left, const auto& right) {
                      return left.assetId < right.assetId;
                  });
        auto exactRoot = AssetFormat::writeCookedAssetBytes({
            .assetKind = AssetFormat::AssetKind::TileMap,
            .assetTypeVersion = AssetFormat::TileMapWire::SchemaVersion,
            .targetPlatform = platform,
            .assetId = current().tileMapId,
            .dependencies = dependencies,
            .payload = current().rootBytes,
            .payloadAlignment = 16,
            .computeContentHash = true,
        });
        if (!exactRoot)
        {
            return Core::failure(std::move(exactRoot.error()));
        }

        TileMapCookPreview preview{
            .documentRevision = revision(),
            .targetPlatform = platform,
        };
        preview.artifacts.reserve(1U + current().chunks.size());
        auto rootPath = AssetFormat::makeCookedArtifactPath(
            AssetFormat::AssetKind::TileMap, current().tileMapId);
        if (!rootPath)
        {
            return Core::failure(std::move(rootPath.error()));
        }
        preview.artifacts.push_back({
            .assetKind = AssetFormat::AssetKind::TileMap,
            .assetId = current().tileMapId,
            .path = *rootPath,
            .cookedBytes = std::move(*exactRoot),
        });
        for (const ChunkRevision& chunk : current().chunks)
        {
            auto cooked = AssetFormat::writeCookedAssetBytes({
                .assetKind = AssetFormat::AssetKind::TileMapChunk,
                .assetTypeVersion = AssetFormat::TileMapChunkWire::SchemaVersion,
                .targetPlatform = platform,
                .assetId = chunk.assetId,
                .dependencies = {},
                .payload = chunk.bytes,
                .payloadAlignment = 16,
                .computeContentHash = true,
            });
            if (!cooked)
            {
                return Core::failure(std::move(cooked.error()));
            }
            auto path = AssetFormat::makeCookedArtifactPath(
                AssetFormat::AssetKind::TileMapChunk, chunk.assetId);
            if (!path)
            {
                return Core::failure(std::move(path.error()));
            }
            preview.artifacts.push_back({
                .assetKind = AssetFormat::AssetKind::TileMapChunk,
                .assetId = chunk.assetId,
                .path = *path,
                .cookedBytes = std::move(*cooked),
            });
        }
        std::sort(preview.artifacts.begin(), preview.artifacts.end(),
                  [](const TileMapCookPreviewArtifact& left,
                     const TileMapCookPreviewArtifact& right) {
                      return left.assetId < right.assetId;
                  });
        return preview;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(allocationFailure().error());
    }
}

Core::Status TileMapAuthoringDocument::undo() noexcept
{
    if (!canUndo())
    {
        return Core::failure(EditorErrorCode::UndoUnavailable,
                             "TileMap authoring document has no undo revision");
    }
    --m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status TileMapAuthoringDocument::redo() noexcept
{
    if (!canRedo())
    {
        return Core::failure(EditorErrorCode::RedoUnavailable,
                             "TileMap authoring document has no redo revision");
    }
    ++m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status TileMapAuthoringDocument::commit(Revision candidate)
{
    const auto equalRevision = [](const Revision& left, const Revision& right) {
        if (left.tileMapId != right.tileMapId || left.tilesetId != right.tilesetId ||
            left.rootBytes != right.rootBytes || left.chunks.size() != right.chunks.size())
        {
            return false;
        }
        for (Core::usize index = 0; index < left.chunks.size(); ++index)
        {
            if (left.chunks[index].assetId != right.chunks[index].assetId ||
                left.chunks[index].bytes != right.chunks[index].bytes)
            {
                return false;
            }
        }
        return true;
    };
    if (equalRevision(candidate, current()))
    {
        return Core::success();
    }
    if (candidate.byteCount > m_config.historyByteCapacity ||
        current().byteCount > m_config.historyByteCapacity - candidate.byteCount)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "TileMap authoring history cannot retain an undoable edit");
    }

    const Core::usize retainedEnd = m_historyCursor + 1U;
    for (Core::usize index = retainedEnd; index < m_history.size(); ++index)
    {
        m_historyBytes -= m_history[index].byteCount;
    }
    m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(retainedEnd),
                    m_history.end());

    while (m_history.size() > 1U &&
           (m_history.size() >= m_config.historyEntryCapacity ||
            candidate.byteCount > m_config.historyByteCapacity - m_historyBytes))
    {
        m_historyBytes -= m_history.front().byteCount;
        m_history.erase(m_history.begin());
        --m_historyCursor;
    }
    if (candidate.byteCount > m_config.historyByteCapacity - m_historyBytes)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "TileMap authoring history cannot retain an undoable edit");
    }

    m_historyBytes += candidate.byteCount;
    m_history.push_back(std::move(candidate));
    m_historyCursor = m_history.size() - 1U;
    advanceRevision();
    return Core::success();
}

Core::Status TileMapAuthoringDocument::resetBaseline(Revision candidate)
{
    if (candidate.byteCount > m_config.historyByteCapacity)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "TileMap authoring baseline exceeds the configured history byte capacity");
    }
    if (m_history.size() == 1U && candidate.tileMapId == current().tileMapId &&
        candidate.tilesetId == current().tilesetId &&
        candidate.rootBytes == current().rootBytes &&
        candidate.chunks.size() == current().chunks.size() &&
        std::equal(candidate.chunks.begin(), candidate.chunks.end(),
                   current().chunks.begin(), [](const ChunkRevision& left,
                                                const ChunkRevision& right) {
                       return left.assetId == right.assetId && left.bytes == right.bytes;
                   }))
    {
        return Core::success();
    }

    m_history.clear();
    m_history.push_back(std::move(candidate));
    m_historyCursor = 0;
    m_historyBytes = m_history.front().byteCount;
    advanceRevision();
    return Core::success();
}

void TileMapAuthoringDocument::advanceRevision() noexcept
{
    if (m_revision != (std::numeric_limits<Core::u64>::max)())
    {
        ++m_revision;
    }
}

} // namespace Tina::Editor
