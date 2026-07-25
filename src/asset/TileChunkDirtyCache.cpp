#include <tina/asset/TileChunkDirtyCache.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <limits>
#include <utility>

namespace Tina::Asset {

TileChunkDirtyCache::TileChunkDirtyCache(std::pmr::vector<Entry> entries, Core::usize capacity) noexcept
    : m_entries(std::move(entries)), m_capacity(capacity)
{
}

Core::Result<TileChunkDirtyCache> TileChunkDirtyCache::Create(TileChunkDirtyCacheConfig config)
{
    if (config.capacity == 0 || config.capacity > 4096)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "TileChunkDirtyCache capacity must be in [1, 4096]");
    }
    std::pmr::memory_resource* resource =
        config.memoryResource != nullptr ? config.memoryResource : std::pmr::get_default_resource();
    try
    {
        std::pmr::vector<Entry> entries{resource};
        entries.resize(config.capacity);
        return TileChunkDirtyCache{std::move(entries), config.capacity};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "TileChunkDirtyCache allocation failed");
    }
}

TileChunkDirtyCache::Entry* TileChunkDirtyCache::findEntry(AssetFormat::TileMapLayerId layerId, Core::u32 chunkX,
                                                           Core::u32 chunkY) noexcept
{
    for (Entry& entry : m_entries)
    {
        if (entry.occupied && entry.layerId == layerId && entry.chunkX == chunkX && entry.chunkY == chunkY)
        {
            return &entry;
        }
    }
    return nullptr;
}

TileChunkDirtyCache::Entry* TileChunkDirtyCache::allocateEntry() noexcept
{
    for (Entry& entry : m_entries)
    {
        if (!entry.occupied)
        {
            entry.occupied = true;
            ++m_tracked;
            return &entry;
        }
    }
    // Evict least-recently-used occupied slot.
    Entry* victim = nullptr;
    Core::u64 oldest = (std::numeric_limits<Core::u64>::max)();
    for (Entry& entry : m_entries)
    {
        if (entry.occupied && entry.lastFrame < oldest)
        {
            oldest = entry.lastFrame;
            victim = &entry;
        }
    }
    if (victim != nullptr)
    {
        ++m_capacityEvictions;
        *victim = Entry{};
        victim->occupied = true;
        // tracked count unchanged (replace one with another)
        return victim;
    }
    return nullptr;
}

Core::Result<Core::u32> TileChunkDirtyCache::classifyVisible(std::span<const TileChunkView> visible,
                                                             std::pmr::vector<TileChunkView>& rebuiltOut)
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "TileChunkDirtyCache is empty");
    }
    try
    {
        rebuiltOut.clear();
        ++m_framesSynced;
        Core::u32 rebuildCount = 0;
        for (const TileChunkView& view : visible)
        {
            if (view.layerId == 0)
            {
                return Core::failure(AssetErrorCode::TileMapLayerNotFound, "tile chunk view has no selected layer");
            }
            ++m_visibleChunkObservations;
            Entry* entry = findEntry(view.layerId, view.coord.chunkX, view.coord.chunkY);
            if (entry != nullptr && entry->revision == view.revision)
            {
                ++m_cacheHits;
                entry->lastFrame = m_framesSynced;
                continue;
            }
            if (entry == nullptr)
            {
                entry = allocateEntry();
                if (entry == nullptr)
                {
                    return Core::failure(AssetErrorCode::CatalogCapacityExceeded,
                                         "TileChunkDirtyCache has no free slot");
                }
                entry->chunkX = view.coord.chunkX;
                entry->chunkY = view.coord.chunkY;
            }
            entry->revision = view.revision;
            entry->layerId = view.layerId;
            entry->lastFrame = m_framesSynced;
            ++m_rebuilds;
            ++rebuildCount;
            rebuiltOut.push_back(view);
        }
        return rebuildCount;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "TileChunkDirtyCache classify allocation failed");
    }
}

Core::Result<Core::u32> TileChunkDirtyCache::syncVisible(const TileMapInstance& map,
                                                         AssetFormat::TileMapLayerId layerId,
                                                         const TileChunkCameraQuery& camera,
                                                         std::pmr::vector<TileChunkView>& rebuiltOut)
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "TileChunkDirtyCache is empty");
    }
    if (!map)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    try
    {
        std::pmr::vector<TileChunkView> visible{rebuiltOut.get_allocator()};
        auto extracted = extractVisibleTileChunks(map, layerId, camera, visible);
        if (!extracted)
        {
            return Core::failure(std::move(extracted.error()));
        }
        return classifyVisible(visible, rebuiltOut);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "TileChunkDirtyCache sync allocation failed");
    }
}

TileChunkDirtyCacheStats TileChunkDirtyCache::stats() const noexcept
{
    return TileChunkDirtyCacheStats{
        .framesSynced = m_framesSynced,
        .visibleChunkObservations = m_visibleChunkObservations,
        .rebuilds = m_rebuilds,
        .cacheHits = m_cacheHits,
        .capacityEvictions = m_capacityEvictions,
        .trackedChunks = m_tracked,
        .capacity = m_capacity,
    };
}

void TileChunkDirtyCache::clear() noexcept
{
    for (Entry& entry : m_entries)
    {
        entry = Entry{};
    }
    m_tracked = 0;
}

} // namespace Tina::Asset
