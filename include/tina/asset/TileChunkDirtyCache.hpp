#pragma once

#include <tina/asset/TileChunkView.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Asset {

// CPU-side visible-chunk revision cache (game-2d dirty rebuild gate).
// Tracks last-seen TileChunkView.revision per chunk coord. Unchanged revisions
// are cache hits (no re-emit); setTile-bumped revisions force a rebuild entry.
// Does not own tile storage or GPU buffers; extraction still uses emit* helpers.

struct TileChunkDirtyCacheConfig final {
    // Max simultaneously tracked chunk coords. Create-time fixed; no growth.
    Core::usize capacity = 256;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TileChunkDirtyCacheStats final {
    Core::u64 framesSynced = 0;
    Core::u64 visibleChunkObservations = 0;
    Core::u64 rebuilds = 0;
    Core::u64 cacheHits = 0;
    Core::u64 capacityEvictions = 0;
    Core::usize trackedChunks = 0;
    Core::usize capacity = 0;
};

class TileChunkDirtyCache final {
  public:
    TileChunkDirtyCache() noexcept = default;
    ~TileChunkDirtyCache() noexcept = default;

    TileChunkDirtyCache(const TileChunkDirtyCache&) = delete;
    TileChunkDirtyCache& operator=(const TileChunkDirtyCache&) = delete;
    TileChunkDirtyCache(TileChunkDirtyCache&&) noexcept = default;
    TileChunkDirtyCache& operator=(TileChunkDirtyCache&&) noexcept = default;

    [[nodiscard]] static Core::Result<TileChunkDirtyCache>
    Create(TileChunkDirtyCacheConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_capacity != 0;
    }

    // extractVisibleTileChunks then classify. Clears rebuiltOut first.
    // rebuiltOut receives only chunks whose revision is new or changed.
    // Returns rebuild count this call.
    [[nodiscard]] Core::Result<Core::u32> syncVisible(const TileMapInstance& map,
                                                      const TileChunkCameraQuery& camera,
                                                      std::pmr::vector<TileChunkView>& rebuiltOut);

    // Classify a caller-supplied visible set (no extract). Clears rebuiltOut.
    [[nodiscard]] Core::Result<Core::u32> classifyVisible(std::span<const TileChunkView> visible,
                                                          std::pmr::vector<TileChunkView>& rebuiltOut);

    [[nodiscard]] TileChunkDirtyCacheStats stats() const noexcept;

    // Drop all tracked revisions (e.g. map replace). Stats counters are kept.
    void clear() noexcept;

  private:
    struct Entry final {
        Core::u32 chunkX = 0;
        Core::u32 chunkY = 0;
        Core::u32 revision = 0;
        Core::u64 lastFrame = 0;
        bool occupied = false;
    };

    explicit TileChunkDirtyCache(std::pmr::vector<Entry> entries, Core::usize capacity) noexcept;

    [[nodiscard]] Entry* findEntry(Core::u32 chunkX, Core::u32 chunkY) noexcept;
    [[nodiscard]] Entry* allocateEntry() noexcept;

    std::pmr::vector<Entry> m_entries{};
    Core::usize m_capacity = 0;
    Core::usize m_tracked = 0;
    Core::u64 m_framesSynced = 0;
    Core::u64 m_visibleChunkObservations = 0;
    Core::u64 m_rebuilds = 0;
    Core::u64 m_cacheHits = 0;
    Core::u64 m_capacityEvictions = 0;
};

} // namespace Tina::Asset
