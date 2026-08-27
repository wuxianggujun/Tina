#include <tina/asset/TileMapStream.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

struct DemandChunkRange final {
    Core::u32 minChunkX = 0;
    Core::u32 minChunkY = 0;
    Core::u32 maxChunkX = 0;
    Core::u32 maxChunkY = 0;
    bool intersectsMap = false;
};

[[nodiscard]] DemandChunkRange demandChunkRange(const TileMapInstance& map,
                                                const TileChunkCameraQuery& camera,
                                                Core::u16 margin) noexcept
{
    const float cellSize = map.cellSizeMeters();
    const float mapMaxX = static_cast<float>(map.widthCells()) * cellSize;
    const float mapMaxY = static_cast<float>(map.heightCells()) * cellSize;
    const float minWorldX = camera.centerX - camera.halfWidth;
    const float minWorldY = camera.centerY - camera.halfHeight;
    const float maxWorldX = camera.centerX + camera.halfWidth;
    const float maxWorldY = camera.centerY + camera.halfHeight;
    // The margin widens the window in world space, before the overlap test. Testing
    // the unwidened camera first made the retain window collapse to nothing the
    // instant the camera cleared the map rect, so a camera stepping just past the
    // edge unloaded the whole resident set and re-requested it on the way back.
    const float marginWorld = static_cast<float>(margin) * static_cast<float>(map.chunkSizeCells()) * cellSize;
    if (maxWorldX + marginWorld <= 0.0f || maxWorldY + marginWorld <= 0.0f ||
        minWorldX - marginWorld >= mapMaxX || minWorldY - marginWorld >= mapMaxY)
    {
        return {};
    }

    const auto chunkForWorld = [&](float world, Core::u32 count) -> Core::u32 {
        if (world <= 0.0f)
        {
            return 0U;
        }
        const auto cellIndex = static_cast<Core::u32>(world / cellSize);
        return (std::min)(cellIndex / map.chunkSizeCells(), count - 1U);
    };
    Core::u32 minChunkX = chunkForWorld((std::max)(minWorldX, 0.0f), map.chunkCountX());
    Core::u32 minChunkY = chunkForWorld((std::max)(minWorldY, 0.0f), map.chunkCountY());
    Core::u32 maxChunkX =
        chunkForWorld(std::nextafter((std::min)(maxWorldX, mapMaxX), 0.0f), map.chunkCountX());
    Core::u32 maxChunkY =
        chunkForWorld(std::nextafter((std::min)(maxWorldY, mapMaxY), 0.0f), map.chunkCountY());
    minChunkX = minChunkX > margin ? minChunkX - margin : 0U;
    minChunkY = minChunkY > margin ? minChunkY - margin : 0U;
    maxChunkX = (std::min)(maxChunkX + static_cast<Core::u32>(margin), map.chunkCountX() - 1U);
    maxChunkY = (std::min)(maxChunkY + static_cast<Core::u32>(margin), map.chunkCountY() - 1U);
    return DemandChunkRange{
        .minChunkX = minChunkX,
        .minChunkY = minChunkY,
        .maxChunkX = maxChunkX,
        .maxChunkY = maxChunkY,
        .intersectsMap = true,
    };
}

} // namespace

bool TileMapStream::keyLess(const ChunkKey& left, const ChunkKey& right) noexcept
{
    if (left.layerId != right.layerId)
    {
        return left.layerId < right.layerId;
    }
    if (left.coord.chunkY != right.coord.chunkY)
    {
        return left.coord.chunkY < right.coord.chunkY;
    }
    return left.coord.chunkX < right.coord.chunkX;
}

bool TileMapStream::desiredLess(const DesiredChunk& left, const DesiredChunk& right) noexcept
{
    if (left.priority != right.priority)
    {
        return left.priority > right.priority;
    }
    return keyLess(left.key, right.key);
}

TileMapStream::TileMapStream(AssetSystem& assets, AssetLease rootLease, AssetLease tilesetLease,
                             TileMapInstance map, TileMapStreamConfig config,
                             std::pmr::vector<Slot> slots, std::pmr::vector<DesiredChunk> desired,
                             std::pmr::vector<RetainCandidate> retain) noexcept
    : m_assets(&assets), m_rootLease(std::move(rootLease)), m_tilesetLease(std::move(tilesetLease)),
      m_map(std::move(map)), m_config(config), m_slots(std::move(slots)),
      m_desired(std::move(desired)), m_retain(std::move(retain))
{
}

TileMapStream::~TileMapStream() noexcept
{
    static_cast<void>(shutdown());
}

TileMapStream::TileMapStream(TileMapStream&& other) noexcept
    : m_assets(std::exchange(other.m_assets, nullptr)), m_rootLease(std::move(other.m_rootLease)),
      m_tilesetLease(std::move(other.m_tilesetLease)), m_map(std::move(other.m_map)),
      m_config(other.m_config), m_slots(std::move(other.m_slots)), m_desired(std::move(other.m_desired)),
      m_retain(std::move(other.m_retain)), m_nextResidencyGeneration(other.m_nextResidencyGeneration),
      m_nextDemandGeneration(other.m_nextDemandGeneration),
      m_peakResidentSlots(other.m_peakResidentSlots), m_totalRequests(other.m_totalRequests),
      m_totalCommitted(other.m_totalCommitted), m_totalCancelled(other.m_totalCancelled),
      m_totalUnloaded(other.m_totalUnloaded), m_totalFailed(other.m_totalFailed)
{
}

Core::Result<TileMapStream> TileMapStream::Create(AssetSystem& assets, AssetLease rootLease,
                                                  AssetLease tilesetLease, TileMapStreamConfig config)
{
    if (!rootLease || !tilesetLease || rootLease.assetKind() != AssetFormat::AssetKind::TileMap ||
        tilesetLease.assetKind() != AssetFormat::AssetKind::Tileset)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "TileMapStream requires TileMap and Tileset leases");
    }
    if (config.residentCapacity == 0U ||
        config.residentCapacity > AssetFormat::TileMapWire::MaxChunkRefsPerMap ||
        config.requestBudgetPerUpdate == 0U || config.retainMarginChunks < config.loadMarginChunks)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "TileMapStream config is invalid");
    }
    if (config.memoryResource == nullptr)
    {
        config.memoryResource = std::pmr::get_default_resource();
    }
    const CookedAssetFile* rootFile = rootLease.get();
    const CookedAssetFile* tilesetFile = tilesetLease.get();
    if (rootFile == nullptr || tilesetFile == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "TileMapStream lease payload is unavailable");
    }
    auto root = parseTileMapFromCooked(*rootFile);
    auto tileset = parseTilesetFromCooked(*tilesetFile);
    if (!root || !tileset)
    {
        return Core::failure(root ? std::move(tileset.error()) : std::move(root.error()));
    }

    Core::u32 tilesetDependencyCount = 0;
    for (Core::u32 index = 0; index < rootFile->header().dependencyCount; ++index)
    {
        const auto dependency = rootFile->dependency(index);
        if (!dependency)
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "TileMapStream root dependency disappeared");
        }
        if (dependency->expectedKind == AssetFormat::AssetKind::Tileset &&
            dependency->flags == AssetFormat::DependencyFlags::Required &&
            dependency->assetId == tilesetLease.assetId())
        {
            ++tilesetDependencyCount;
        }
        else if (dependency->expectedKind != AssetFormat::AssetKind::TileMapChunk ||
                 dependency->flags != (AssetFormat::DependencyFlags::Required |
                                       AssetFormat::DependencyFlags::Deferred))
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "TileMapStream root dependency contract is invalid");
        }
    }
    Core::u32 rootChunkRefCount = 0;
    for (Core::u16 layerIndex = 0; layerIndex < root->layerCount; ++layerIndex)
    {
        const auto layer = root->layerAt(layerIndex);
        if (!layer)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "TileMapStream root layer disappeared after validation");
        }
        if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
        {
            continue;
        }
        rootChunkRefCount += layer->chunkRefCount;
        for (Core::u32 chunkIndex = 0; chunkIndex < layer->chunkRefCount; ++chunkIndex)
        {
            const auto chunkRef = layer->chunkRefAt(chunkIndex);
            if (!chunkRef)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "TileMapStream root chunk ref disappeared after validation");
            }
            Core::u32 matches = 0;
            for (Core::u32 dependencyIndex = 0; dependencyIndex < rootFile->header().dependencyCount;
                 ++dependencyIndex)
            {
                const auto dependency = rootFile->dependency(dependencyIndex);
                if (dependency && dependency->assetId == chunkRef->chunkAssetId &&
                    dependency->expectedKind == AssetFormat::AssetKind::TileMapChunk &&
                    dependency->flags == (AssetFormat::DependencyFlags::Required |
                                          AssetFormat::DependencyFlags::Deferred))
                {
                    ++matches;
                }
            }
            if (matches != 1U)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "TileMapStream chunk ref must have exactly one deferred dependency");
            }
        }
    }
    if (tilesetDependencyCount != 1U || rootFile->header().dependencyCount != rootChunkRefCount + 1U)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "TileMapStream root dependency counts do not match chunk refs");
    }

    auto map = TileMapInstance::Create(
        *root, *tileset, rootLease.assetId(), tilesetLease.assetId(),
        TileMapInstanceConfig{.residentChunkCapacity = config.residentCapacity,
                              .memoryResource = config.memoryResource});
    if (!map)
    {
        return Core::failure(std::move(map.error()));
    }
    try
    {
        std::pmr::vector<Slot> slots{config.memoryResource};
        std::pmr::vector<DesiredChunk> desired{config.memoryResource};
        std::pmr::vector<RetainCandidate> retain{config.memoryResource};
        slots.reserve(config.residentCapacity);
        desired.reserve(config.residentCapacity);
        retain.reserve(config.residentCapacity);
        return TileMapStream(assets, std::move(rootLease), std::move(tilesetLease), std::move(*map),
                             config, std::move(slots), std::move(desired), std::move(retain));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "TileMapStream allocation failed");
    }
}

bool TileMapStream::contains(std::span<const ChunkKey> keys, const ChunkKey& key) noexcept
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool TileMapStream::contains(std::span<const DesiredChunk> chunks, const ChunkKey& key) noexcept
{
    return std::find_if(chunks.begin(), chunks.end(),
                        [&key](const DesiredChunk& chunk) { return chunk.key == key; }) != chunks.end();
}

bool TileMapStream::contains(std::span<const RetainCandidate> candidates, const ChunkKey& key) noexcept
{
    return std::find_if(candidates.begin(), candidates.end(),
                        [&key](const RetainCandidate& candidate) { return candidate.key == key; }) !=
           candidates.end();
}

TileMapStream::Slot* TileMapStream::findSlot(const ChunkKey& key) noexcept
{
    const auto found = std::find_if(m_slots.begin(), m_slots.end(),
                                    [&key](const Slot& slot) { return slot.key == key; });
    return found == m_slots.end() ? nullptr : &*found;
}

const TileMapStream::Slot* TileMapStream::findSlot(const ChunkKey& key) const noexcept
{
    const auto found = std::find_if(m_slots.begin(), m_slots.end(),
                                    [&key](const Slot& slot) { return slot.key == key; });
    return found == m_slots.end() ? nullptr : &*found;
}

Core::Status TileMapStream::buildDemandSet(std::span<const TileMapChunkDemand> demands, Core::u16 margin,
                                           std::pmr::vector<DesiredChunk>& out) const
{
    out.clear();
    for (const TileMapChunkDemand& demand : demands)
    {
        auto layer = m_map.layer(demand.layerId);
        if (!layer)
        {
            return Core::failure(std::move(layer.error()));
        }
        if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
        {
            return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch,
                                 "TileMapStream demand requires a tile layer");
        }
        if (!(demand.camera.halfWidth > 0.0f) || !(demand.camera.halfHeight > 0.0f) ||
            !std::isfinite(demand.camera.centerX) || !std::isfinite(demand.camera.centerY) ||
            !std::isfinite(demand.camera.halfWidth) || !std::isfinite(demand.camera.halfHeight))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "TileMapStream camera demand is invalid");
        }
        const DemandChunkRange range = demandChunkRange(m_map, demand.camera, margin);
        if (!range.intersectsMap)
        {
            continue;
        }

        for (Core::u32 chunkY = range.minChunkY; chunkY <= range.maxChunkY; ++chunkY)
        {
            for (Core::u32 chunkX = range.minChunkX; chunkX <= range.maxChunkX; ++chunkX)
            {
                const auto rootRef = layer->findChunkRef(chunkX, chunkY);
                if (!rootRef)
                {
                    continue;
                }
                const ChunkKey key{
                    .layerId = demand.layerId,
                    .coord = TileMapChunkCoord{.chunkX = chunkX, .chunkY = chunkY},
                    .assetId = rootRef->chunkAssetId,
                };
                const auto existing = std::find_if(
                    out.begin(), out.end(), [&key](const DesiredChunk& chunk) { return chunk.key == key; });
                if (existing != out.end())
                {
                    existing->priority = (std::max)(existing->priority, demand.priority);
                    continue;
                }
                if (out.size() == m_config.residentCapacity)
                {
                    out.clear();
                    return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                         "TileMapStream demand exceeds resident capacity");
                }
                out.push_back(DesiredChunk{.key = key, .priority = demand.priority});
            }
        }
    }
    std::sort(out.begin(), out.end(), desiredLess);
    return Core::success();
}

// Slots not desired this frame but still inside the wider retain window, most
// recently demanded first, truncated to the capacity left over after the desired set.
//
// Demands are the outer loop so each window is computed once. The previous shape
// looped slots outside and recomputed the range per (slot, demand) pair, running a
// ~30-float-op calculation slots x demands times per frame for a result that depends
// only on the camera, the margin and the map extent.
void TileMapStream::buildRetainSet(std::span<const TileMapChunkDemand> demands) noexcept
{
    m_retain.clear();
    const Core::usize optionalRetainCapacity =
        m_config.residentCapacity - (std::min)(m_config.residentCapacity, m_desired.size());
    if (optionalRetainCapacity == 0)
    {
        return;
    }
    for (const TileMapChunkDemand& demand : demands)
    {
        const DemandChunkRange range = demandChunkRange(m_map, demand.camera, m_config.retainMarginChunks);
        if (!range.intersectsMap)
        {
            continue;
        }
        for (const Slot& slot : m_slots)
        {
            if (slot.key.layerId != demand.layerId || slot.key.coord.chunkX < range.minChunkX ||
                slot.key.coord.chunkX > range.maxChunkX || slot.key.coord.chunkY < range.minChunkY ||
                slot.key.coord.chunkY > range.maxChunkY)
            {
                continue;
            }
            // Two demands may cover the same slot, so the union needs a dedup that the
            // old slot-outer shape got for free.
            if (contains(m_desired, slot.key) || contains(m_retain, slot.key))
            {
                continue;
            }
            m_retain.push_back(RetainCandidate{
                .key = slot.key,
                .lastDesiredDemand = slot.lastDesiredDemand,
            });
        }
    }
    std::sort(m_retain.begin(), m_retain.end(),
              [](const RetainCandidate& left, const RetainCandidate& right) {
                  if (left.lastDesiredDemand != right.lastDesiredDemand)
                  {
                      return left.lastDesiredDemand > right.lastDesiredDemand;
                  }
                  return keyLess(left.key, right.key);
              });
    if (m_retain.size() > optionalRetainCapacity)
    {
        m_retain.resize(optionalRetainCapacity);
    }
}

void TileMapStream::removeSlot(Core::usize index) noexcept
{
    if (index + 1U != m_slots.size())
    {
        m_slots[index] = std::move(m_slots.back());
    }
    m_slots.pop_back();
}

Core::u64 TileMapStream::nextResidencyGeneration() noexcept
{
    Core::u64 generation = m_nextResidencyGeneration++;
    if (generation == 0U)
    {
        generation = m_nextResidencyGeneration++;
    }
    return generation;
}

Core::u64 TileMapStream::nextDemandGeneration() noexcept
{
    Core::u64 generation = m_nextDemandGeneration++;
    if (generation == 0U)
    {
        generation = m_nextDemandGeneration++;
    }
    return generation;
}

Core::Status TileMapStream::updateDemand(std::span<const TileMapChunkDemand> demands)
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "TileMapStream is empty");
    }
    if (const auto status = buildDemandSet(demands, m_config.loadMarginChunks, m_desired); !status)
    {
        return status;
    }
    buildRetainSet(demands);

    for (Core::usize index = 0; index < m_slots.size();)
    {
        Slot& slot = m_slots[index];
        if (contains(m_desired, slot.key) || contains(m_retain, slot.key))
        {
            ++index;
            continue;
        }
        // Detach and lease release happen only once the unload is known to succeed:
        // returning in between would leave a slot the instance no longer holds cells
        // for while the slot still claims Resident, and nothing can repair that.
        const bool wasResident = slot.state == TileMapChunkResidencyState::Resident;
        if (wasResident)
        {
            if (auto detached = m_map.detachChunk(slot.key.layerId, slot.key.coord); !detached)
            {
                return detached;
            }
            slot.lease = AssetLease{};
        }
        if (auto status = m_assets->unload(slot.handle); !status)
        {
            // The chunk is gone from the map but the handle survives, so the slot must
            // not keep claiming residency. Marking it Requested puts it back on the
            // path that commitReady() can complete, instead of stranding it.
            if (wasResident)
            {
                slot.state = TileMapChunkResidencyState::Requested;
                ++m_totalUnloaded;
            }
            return status;
        }
        if (wasResident)
        {
            ++m_totalUnloaded;
        }
        else if (slot.state == TileMapChunkResidencyState::Requested)
        {
            // Counted here rather than before the unload, so an aborted eviction does
            // not report a cancellation that did not happen and then report it again
            // on the next pass.
            ++m_totalCancelled;
        }
        removeSlot(index);
    }

    Core::u32 issued = 0;
    for (const DesiredChunk& desired : m_desired)
    {
        const ChunkKey& key = desired.key;
        if (issued >= m_config.requestBudgetPerUpdate)
        {
            continue;
        }
        Slot* existing = findSlot(key);
        if (existing != nullptr)
        {
            // A Failed slot would otherwise be terminal: eviction keeps it because it
            // is still desired, this loop skipped it because a slot existed, and
            // commitReady() only looks at Requested. The chunk stayed blank forever
            // after one transient IO failure. Re-requesting is the retry.
            if (existing->state != TileMapChunkResidencyState::Failed)
            {
                continue;
            }
            if (auto status = m_assets->unload(existing->handle); !status)
            {
                return status;
            }
            auto handle = m_assets->requestOne(key.assetId);
            if (!handle)
            {
                // The old handle is already unloaded, so the slot cannot stay. Dropping
                // it lets the next frame request cleanly rather than leaving a slot
                // pointing at a released handle.
                removeSlot(static_cast<Core::usize>(existing - m_slots.data()));
                if (handle.error().code == AssetErrorCode::AssetQueueFull)
                {
                    break;
                }
                return Core::failure(std::move(handle.error()));
            }
            existing->handle = *handle;
            existing->state = TileMapChunkResidencyState::Requested;
            ++m_totalRequests;
            ++issued;
            continue;
        }
        auto handle = m_assets->requestOne(key.assetId);
        if (!handle)
        {
            if (handle.error().code == AssetErrorCode::AssetQueueFull)
            {
                break;
            }
            return Core::failure(std::move(handle.error()));
        }
        m_slots.push_back(Slot{.key = key, .handle = *handle});
        ++m_totalRequests;
        ++issued;
    }
    const Core::u64 demandGeneration = nextDemandGeneration();
    for (Slot& slot : m_slots)
    {
        if (contains(m_desired, slot.key))
        {
            slot.lastDesiredDemand = demandGeneration;
        }
    }
    std::sort(m_slots.begin(), m_slots.end(),
              [](const Slot& left, const Slot& right) { return keyLess(left.key, right.key); });
    return Core::success();
}

Core::Result<TileMapStreamStats> TileMapStream::commitReady()
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "TileMapStream is empty");
    }
    for (Slot& slot : m_slots)
    {
        if (slot.state != TileMapChunkResidencyState::Requested)
        {
            continue;
        }
        const AssetLogicalState state = m_assets->state(slot.handle);
        if (state == AssetLogicalState::Failed)
        {
            slot.state = TileMapChunkResidencyState::Failed;
            ++m_totalFailed;
            continue;
        }
        if (state != AssetLogicalState::ReadyCpu && state != AssetLogicalState::ReadyGpu)
        {
            continue;
        }
        auto lease = m_assets->acquire(slot.handle);
        if (!lease)
        {
            return Core::failure(std::move(lease.error()));
        }
        const CookedAssetFile* file = lease->get();
        if (file == nullptr || file->header().assetKind != AssetFormat::AssetKind::TileMapChunk ||
            lease->assetId() != slot.key.assetId)
        {
            slot.state = TileMapChunkResidencyState::Failed;
            ++m_totalFailed;
            continue;
        }
        auto chunk = AssetFormat::parseTileMapChunkPayload(file->payload());
        if (!chunk)
        {
            slot.state = TileMapChunkResidencyState::Failed;
            ++m_totalFailed;
            continue;
        }
        const Core::u64 generation = nextResidencyGeneration();
        if (auto status = m_map.attachChunk(slot.key.assetId, *chunk, generation); !status)
        {
            slot.state = TileMapChunkResidencyState::Failed;
            ++m_totalFailed;
            continue;
        }
        slot.lease = std::move(*lease);
        slot.state = TileMapChunkResidencyState::Resident;
        ++m_totalCommitted;
    }
    const TileMapStreamStats current = stats();
    m_peakResidentSlots = (std::max)(m_peakResidentSlots, current.residentSlots);
    return stats();
}

Core::Status TileMapStream::shutdown() noexcept
{
    if (m_assets == nullptr)
    {
        return Core::success();
    }
    Core::Status firstFailure = Core::success();
    for (Slot& slot : m_slots)
    {
        if (slot.state == TileMapChunkResidencyState::Resident)
        {
            if (auto detached = m_map.detachChunk(slot.key.layerId, slot.key.coord);
                !detached && firstFailure)
            {
                firstFailure = Core::failure(std::move(detached.error()));
            }
            slot.lease = AssetLease{};
        }
        if (auto unloaded = m_assets->unload(slot.handle); !unloaded && firstFailure)
        {
            firstFailure = Core::failure(std::move(unloaded.error()));
        }
    }
    m_slots.clear();
    m_desired.clear();
    m_retain.clear();
    m_rootLease = AssetLease{};
    m_tilesetLease = AssetLease{};
    m_assets = nullptr;
    return firstFailure;
}

TileMapStreamStats TileMapStream::stats() const noexcept
{
    TileMapStreamStats result{
        .activeSlots = m_slots.size(),
        .peakResidentSlots = m_peakResidentSlots,
        .totalRequests = m_totalRequests,
        .totalCommitted = m_totalCommitted,
        .totalCancelled = m_totalCancelled,
        .totalUnloaded = m_totalUnloaded,
        .totalFailed = m_totalFailed,
    };
    for (const Slot& slot : m_slots)
    {
        switch (slot.state)
        {
        case TileMapChunkResidencyState::Requested:
            ++result.requestedSlots;
            break;
        case TileMapChunkResidencyState::Resident:
            ++result.residentSlots;
            break;
        case TileMapChunkResidencyState::Failed:
            ++result.failedSlots;
            break;
        }
    }
    return result;
}

} // namespace Tina::Asset
