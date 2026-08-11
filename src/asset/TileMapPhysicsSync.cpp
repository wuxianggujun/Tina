#include <tina/asset/TileMapPhysicsSync.hpp>

#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

inline constexpr Core::usize InvalidIndex = (std::numeric_limits<Core::usize>::max)();
inline constexpr Core::usize MaximumChunkCapacity = 4096;

[[nodiscard]] bool isSolidMaterial(Core::u16 flags) noexcept
{
    return (flags & AssetFormat::TilesetWire::MaterialSolid) != 0;
}

[[nodiscard]] bool validMaterial(
    const Physics2D::PhysicsGridColliderMaterial2D& material) noexcept
{
    return material.filter.categoryBits != 0
        && std::isfinite(material.density) && material.density >= 0.0F
        && std::isfinite(material.friction) && material.friction >= 0.0F
        && material.friction <= 1.0F
        && std::isfinite(material.restitution) && material.restitution >= 0.0F
        && material.restitution <= 1.0F;
}

} // namespace

TileMapPhysicsSync2D::TileMapPhysicsSync2D(
    AssetFormat::TileMapLayerId layerId,
    Core::AssetId tileMapAssetId,
    Core::AssetId tilesetAssetId,
    Core::u32 widthCells,
    Core::u32 heightCells,
    Core::u16 chunkSizeCells,
    float cellSizeMeters,
    Core::usize chunkCapacity,
    std::pmr::vector<ChunkRecord> records,
    std::pmr::vector<ChunkRecord> nextRecords,
    std::pmr::vector<Physics2D::PhysicsGridSolidRect2D> rectangles,
    std::pmr::vector<Core::u8> occupancy,
    std::pmr::vector<Physics2D::PhysicsBodyId> stagedBodies,
    std::pmr::vector<Physics2D::PhysicsBodyId> retiredBodies,
    std::pmr::vector<Core::u8> seenRecords,
    TileMapPhysicsSync2DConfig config) noexcept
    : m_layerId(layerId),
      m_tileMapAssetId(tileMapAssetId),
      m_tilesetAssetId(tilesetAssetId),
      m_widthCells(widthCells),
      m_heightCells(heightCells),
      m_chunkSizeCells(chunkSizeCells),
      m_cellSizeMeters(cellSizeMeters),
      m_chunkCapacity(chunkCapacity),
      m_config(config),
      m_records(std::move(records)),
      m_nextRecords(std::move(nextRecords)),
      m_rectangles(std::move(rectangles)),
      m_occupancy(std::move(occupancy)),
      m_stagedBodies(std::move(stagedBodies)),
      m_retiredBodies(std::move(retiredBodies)),
      m_seenRecords(std::move(seenRecords))
{
}

Core::Result<TileMapPhysicsSync2D> TileMapPhysicsSync2D::Create(
    const TileMapInstance& map,
    TileMapPhysicsSync2DConfig config)
{
    if (!map || config.layerId == 0 || config.chunkCapacity == 0
        || config.chunkCapacity > MaximumChunkCapacity || !validMaterial(config.material)) {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "TileMapPhysicsSync2D config or source map is invalid");
    }

    auto layer = map.layer(config.layerId);
    if (!layer) {
        return Core::failure(std::move(layer.error()));
    }
    if (layer->kind != AssetFormat::TileMapLayerKind::Tile) {
        return Core::failure(
            AssetErrorCode::TileMapLayerTypeMismatch,
            "TileMapPhysicsSync2D requires a tile layer");
    }

    const Core::u64 chunkCellCount64 =
        static_cast<Core::u64>(map.chunkSizeCells()) * map.chunkSizeCells();
    if (chunkCellCount64 == 0
        || chunkCellCount64 > (std::numeric_limits<Core::usize>::max)()) {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "TileMapPhysicsSync2D chunk dimensions overflowed storage limits");
    }
    const Core::usize chunkCellCount = static_cast<Core::usize>(chunkCellCount64);
    const Core::usize rectangleCapacity = config.rectangleCapacityPerChunk == 0
        ? chunkCellCount
        : config.rectangleCapacityPerChunk;
    if (rectangleCapacity == 0 || rectangleCapacity > chunkCellCount) {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "TileMapPhysicsSync2D rectangle capacity must fit one source chunk");
    }
    config.rectangleCapacityPerChunk = rectangleCapacity;

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();
    try {
        std::pmr::vector<ChunkRecord> records{resource};
        std::pmr::vector<ChunkRecord> nextRecords{resource};
        std::pmr::vector<Physics2D::PhysicsGridSolidRect2D> rectangles{resource};
        std::pmr::vector<Core::u8> occupancy{resource};
        std::pmr::vector<Physics2D::PhysicsBodyId> stagedBodies{resource};
        std::pmr::vector<Physics2D::PhysicsBodyId> retiredBodies{resource};
        std::pmr::vector<Core::u8> seenRecords{resource};
        records.resize(config.chunkCapacity);
        nextRecords.resize(config.chunkCapacity);
        rectangles.resize(rectangleCapacity);
        occupancy.resize(chunkCellCount);
        stagedBodies.resize(config.chunkCapacity);
        retiredBodies.resize(config.chunkCapacity);
        seenRecords.resize(config.chunkCapacity);
        return TileMapPhysicsSync2D{
            config.layerId,
            map.tileMapAssetId(),
            map.tilesetAssetId(),
            map.widthCells(),
            map.heightCells(),
            map.chunkSizeCells(),
            map.cellSizeMeters(),
            config.chunkCapacity,
            std::move(records),
            std::move(nextRecords),
            std::move(rectangles),
            std::move(occupancy),
            std::move(stagedBodies),
            std::move(retiredBodies),
            std::move(seenRecords),
            config};
    } catch (const std::bad_alloc&) {
        return Core::failure(
            AssetErrorCode::AllocationFailed,
            "TileMapPhysicsSync2D fixed storage allocation failed");
    }
}

Core::Status TileMapPhysicsSync2D::validateMap(const TileMapInstance& map) const
{
    if (!*this || !map || map.tileMapAssetId() != m_tileMapAssetId
        || map.tilesetAssetId() != m_tilesetAssetId
        || map.widthCells() != m_widthCells || map.heightCells() != m_heightCells
        || map.chunkSizeCells() != m_chunkSizeCells
        || map.cellSizeMeters() != m_cellSizeMeters) {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "TileMapPhysicsSync2D received a map outside its bound contract");
    }
    auto layer = map.layer(m_layerId);
    if (!layer) {
        return Core::failure(std::move(layer.error()));
    }
    if (layer->kind != AssetFormat::TileMapLayerKind::Tile) {
        return Core::failure(
            AssetErrorCode::TileMapLayerTypeMismatch,
            "TileMapPhysicsSync2D bound layer is no longer a tile layer");
    }
    return Core::success();
}

Core::Result<Core::usize> TileMapPhysicsSync2D::bakeChunk(
    const TileMapInstance& map,
    TileMapChunkCoord coord)
{
    const Core::u64 startX64 = static_cast<Core::u64>(coord.chunkX) * m_chunkSizeCells;
    const Core::u64 startY64 = static_cast<Core::u64>(coord.chunkY) * m_chunkSizeCells;
    if (startX64 >= m_widthCells || startY64 >= m_heightCells) {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "TileMapPhysicsSync2D chunk coordinate is outside the map");
    }
    const Core::u32 startX = static_cast<Core::u32>(startX64);
    const Core::u32 startY = static_cast<Core::u32>(startY64);
    const Core::u32 width = (std::min)(
        static_cast<Core::u32>(m_chunkSizeCells),
        m_widthCells - startX);
    const Core::u32 height = (std::min)(
        static_cast<Core::u32>(m_chunkSizeCells),
        m_heightCells - startY);
    const Core::usize cellCount = static_cast<Core::usize>(width) * height;
    if (cellCount > m_occupancy.size()) {
        return Core::failure(
            AssetErrorCode::TileMapPhysicsCapacityExceeded,
            "TileMapPhysicsSync2D occupancy scratch is smaller than the source chunk");
    }

    std::fill_n(m_occupancy.begin(), cellCount, Core::u8{0});
    for (Core::u32 localY = 0; localY < height; ++localY) {
        for (Core::u32 localX = 0; localX < width; ++localX) {
            auto tile = map.tileInfoAt(m_layerId, startX + localX, startY + localY);
            if (!tile) {
                return Core::failure(std::move(tile.error()));
            }
            if (tile->has_value() && !tile->value().empty
                && isSolidMaterial(tile->value().materialFlags)) {
                m_occupancy[static_cast<Core::usize>(localY) * width + localX] = 1;
            }
        }
    }

    Core::usize rectangleCount = 0;
    for (Core::u32 localY = 0; localY < height; ++localY) {
        for (Core::u32 localX = 0; localX < width; ++localX) {
            const Core::usize origin = static_cast<Core::usize>(localY) * width + localX;
            if (m_occupancy[origin] == 0) {
                continue;
            }

            Core::u32 rectangleWidth = 1;
            while (localX + rectangleWidth < width
                   && m_occupancy[static_cast<Core::usize>(localY) * width
                                  + localX + rectangleWidth] != 0) {
                ++rectangleWidth;
            }

            Core::u32 rectangleHeight = 1;
            while (localY + rectangleHeight < height) {
                bool completeRow = true;
                for (Core::u32 x = 0; x < rectangleWidth; ++x) {
                    if (m_occupancy[static_cast<Core::usize>(localY + rectangleHeight) * width
                                    + localX + x] == 0) {
                        completeRow = false;
                        break;
                    }
                }
                if (!completeRow) {
                    break;
                }
                ++rectangleHeight;
            }

            if (rectangleCount >= m_rectangles.size()) {
                return Core::failure(
                    AssetErrorCode::TileMapPhysicsCapacityExceeded,
                    "TileMapPhysicsSync2D rectangle scratch capacity was exceeded");
            }
            m_rectangles[rectangleCount++] = Physics2D::PhysicsGridSolidRect2D{
                .cellX = startX + localX,
                .cellY = startY + localY,
                .widthCells = rectangleWidth,
                .heightCells = rectangleHeight};
            for (Core::u32 y = 0; y < rectangleHeight; ++y) {
                for (Core::u32 x = 0; x < rectangleWidth; ++x) {
                    m_occupancy[static_cast<Core::usize>(localY + y) * width
                                + localX + x] = 0;
                }
            }
        }
    }
    return rectangleCount;
}

Core::usize TileMapPhysicsSync2D::findRecord(TileMapChunkCoord coord) const noexcept
{
    for (Core::usize index = 0; index < m_records.size(); ++index) {
        if (m_records[index].occupied && m_records[index].coord == coord) {
            return index;
        }
    }
    return InvalidIndex;
}

Core::Status TileMapPhysicsSync2D::clearStagedBodies(
    Physics2D::PhysicsWorld2D& world) noexcept
{
    for (Physics2D::PhysicsBodyId& body : m_stagedBodies) {
        if (!body.hasValue()) {
            continue;
        }
        if (world.contains(body)) {
            auto destroyed = world.destroyBody(body);
            if (!destroyed) {
                return destroyed;
            }
        }
        body = {};
    }
    return Core::success();
}

void TileMapPhysicsSync2D::clearScratch() noexcept
{
    for (ChunkRecord& record : m_nextRecords) {
        record = {};
    }
    for (Physics2D::PhysicsBodyId& body : m_stagedBodies) {
        body = {};
    }
    for (Physics2D::PhysicsBodyId& body : m_retiredBodies) {
        body = {};
    }
    std::fill(m_seenRecords.begin(), m_seenRecords.end(), Core::u8{0});
}

Core::Result<TileMapPhysicsSync2DStats> TileMapPhysicsSync2D::synchronize(
    const TileMapInstance& map,
    Physics2D::PhysicsWorld2D& world)
{
    if (auto status = validateMap(map); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (!world.isOpen()) {
        return Core::failure(
            Physics2D::Physics2DErrorCode::WorldClosed,
            "TileMapPhysicsSync2D requires an open Physics2D world");
    }
    for (const ChunkRecord& record : m_records) {
        if (!record.occupied || !record.body.hasValue()) {
            continue;
        }
        auto state = world.bodyState(record.body);
        if (!state) {
            return Core::failure(std::move(state.error()));
        }
    }

    clearScratch();
    Core::usize nextRecordCount = 0;
    Core::usize stagedBodyCount = 0;
    Core::usize retiredBodyCount = 0;
    Core::usize addedChunkCount = 0;
    Core::usize rebuiltChunkCount = 0;
    Core::usize removedChunkCount = 0;
    Core::usize unchangedChunkCount = 0;
    Core::usize bakedRectangleCount = 0;

    const auto rollback = [&](Core::Error error)
        -> Core::Result<TileMapPhysicsSync2DStats> {
        auto cleanup = clearStagedBodies(world);
        clearScratch();
        if (!cleanup) {
            return Core::failure(std::move(cleanup.error()));
        }
        return Core::failure(std::move(error));
    };

    for (Core::u32 chunkY = 0; chunkY < map.chunkCountY(); ++chunkY) {
        for (Core::u32 chunkX = 0; chunkX < map.chunkCountX(); ++chunkX) {
            const TileMapChunkCoord coord{chunkX, chunkY};
            if (!map.isChunkResident(m_layerId, coord)) {
                continue;
            }
            if (nextRecordCount >= m_chunkCapacity) {
                return rollback(Core::Error{
                    AssetErrorCode::TileMapPhysicsCapacityExceeded,
                    "TileMapPhysicsSync2D resident chunk capacity was exceeded"});
            }

            auto state = map.chunkState(m_layerId, chunkX, chunkY);
            if (!state) {
                return rollback(std::move(state.error()));
            }
            const Core::usize oldIndex = findRecord(coord);
            if (oldIndex != InvalidIndex) {
                m_seenRecords[oldIndex] = 1;
            }

            const bool changed = oldIndex == InvalidIndex
                || m_records[oldIndex].residencyGeneration != state->residencyGeneration
                || m_records[oldIndex].contentRevision != state->contentRevision;
            if (!changed) {
                m_nextRecords[nextRecordCount++] = m_records[oldIndex];
                ++unchangedChunkCount;
                continue;
            }

            auto baked = bakeChunk(map, coord);
            if (!baked) {
                return rollback(std::move(baked.error()));
            }
            bakedRectangleCount += *baked;
            auto created = Physics2D::createStaticBodyForSolidRectangles(
                world,
                std::span<const Physics2D::PhysicsGridSolidRect2D>{m_rectangles.data(), *baked},
                m_cellSizeMeters,
                m_config.material);
            if (!created) {
                return rollback(std::move(created.error()));
            }
            if (created->body.hasValue()) {
                m_stagedBodies[stagedBodyCount++] = created->body;
            }
            if (oldIndex != InvalidIndex && m_records[oldIndex].body.hasValue()) {
                m_retiredBodies[retiredBodyCount++] = m_records[oldIndex].body;
            }

            m_nextRecords[nextRecordCount++] = ChunkRecord{
                .coord = coord,
                .residencyGeneration = state->residencyGeneration,
                .contentRevision = state->contentRevision,
                .body = created->body,
                .shapeCount = created->shapeCount,
                .occupied = true};
            if (oldIndex == InvalidIndex) {
                ++addedChunkCount;
            } else {
                ++rebuiltChunkCount;
            }
        }
    }

    for (Core::usize index = 0; index < m_records.size(); ++index) {
        const ChunkRecord& record = m_records[index];
        if (!record.occupied || m_seenRecords[index] != 0) {
            continue;
        }
        if (record.body.hasValue()) {
            m_retiredBodies[retiredBodyCount++] = record.body;
        }
        ++removedChunkCount;
    }

    for (Core::usize index = 0; index < retiredBodyCount; ++index) {
        auto destroyed = world.destroyBody(m_retiredBodies[index]);
        if (!destroyed) {
            return rollback(std::move(destroyed.error()));
        }
    }

    for (Core::usize index = 0; index < m_records.size(); ++index) {
        m_records[index] = index < nextRecordCount ? m_nextRecords[index] : ChunkRecord{};
    }
    m_activeRecordCount = nextRecordCount;
    for (Core::usize index = 0; index < stagedBodyCount; ++index) {
        m_stagedBodies[index] = {};
    }

    TileMapPhysicsSync2DStats nextStats = m_stats;
    nextStats.residentChunkCount = nextRecordCount;
    nextStats.colliderBodyCount = 0;
    nextStats.colliderShapeCount = 0;
    for (Core::usize index = 0; index < nextRecordCount; ++index) {
        nextStats.colliderBodyCount += m_records[index].body.hasValue() ? 1U : 0U;
        nextStats.colliderShapeCount += m_records[index].shapeCount;
    }
    nextStats.lastAddedChunkCount = addedChunkCount;
    nextStats.lastRebuiltChunkCount = rebuiltChunkCount;
    nextStats.lastRemovedChunkCount = removedChunkCount;
    nextStats.lastUnchangedChunkCount = unchangedChunkCount;
    nextStats.lastBakedRectangleCount = bakedRectangleCount;
    ++nextStats.synchronizeCount;
    nextStats.totalAddedChunkCount += addedChunkCount;
    nextStats.totalRebuiltChunkCount += rebuiltChunkCount;
    nextStats.totalRemovedChunkCount += removedChunkCount;
    nextStats.totalBakedRectangleCount += bakedRectangleCount;
    m_stats = nextStats;
    clearScratch();
    return m_stats;
}

Core::Status TileMapPhysicsSync2D::shutdown(
    Physics2D::PhysicsWorld2D& world) noexcept
{
    if (!*this) {
        return Core::success();
    }
    if (!world.isOpen()) {
        return Core::failure(
            Physics2D::Physics2DErrorCode::WorldClosed,
            "TileMapPhysicsSync2D cannot retire colliders from a closed world");
    }
    for (const ChunkRecord& record : m_records) {
        if (!record.occupied || !record.body.hasValue()) {
            continue;
        }
        auto state = world.bodyState(record.body);
        if (!state) {
            return Core::failure(std::move(state.error()));
        }
    }

    const Core::usize removedChunkCount = m_activeRecordCount;
    for (ChunkRecord& record : m_records) {
        if (record.occupied && record.body.hasValue()) {
            auto destroyed = world.destroyBody(record.body);
            if (!destroyed) {
                return destroyed;
            }
        }
        record = {};
    }
    m_activeRecordCount = 0;
    m_stats.residentChunkCount = 0;
    m_stats.colliderBodyCount = 0;
    m_stats.colliderShapeCount = 0;
    m_stats.lastAddedChunkCount = 0;
    m_stats.lastRebuiltChunkCount = 0;
    m_stats.lastRemovedChunkCount = removedChunkCount;
    m_stats.lastUnchangedChunkCount = 0;
    m_stats.lastBakedRectangleCount = 0;
    m_stats.totalRemovedChunkCount += removedChunkCount;
    clearScratch();
    return Core::success();
}

} // namespace Tina::Asset
