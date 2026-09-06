#include <tina/navigation3d/NavigationVolume3D.hpp>

#include <tina/navigation3d/NavigationErrors3D.hpp>

#include <limits>
#include <new>
#include <utility>

namespace Tina::Navigation3D {

struct NavigationVolume3D::CountStorage final {
    CountStorage(Core::usize cellCount, std::pmr::memory_resource& memory)
        : resource(&memory), counts(cellCount, Core::u16{0}, &memory)
    {
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<Core::u16> counts;
};

void NavigationVolume3D::destroyCountStorage(CountStorage* storage) noexcept
{
    std::pmr::polymorphic_allocator<CountStorage>{storage->resource}.delete_object(storage);
}

Core::u16& NavigationVolume3D::countAt(Core::usize index) noexcept
{
    return m_counts->counts[index];
}

Core::u16 NavigationVolume3D::countAt(Core::usize index) const noexcept
{
    return m_counts ? m_counts->counts[index] : Core::u16{0};
}

NavigationVolume3D::NavigationVolume3D(NavigationVolume3DData data, BlockerPool blockers,
                                       CountStorageOwner counts) noexcept
    : m_data(std::move(data)), m_blockers(std::move(blockers)), m_counts(std::move(counts))
{
}

NavigationVolume3D::NavigationVolume3D(NavigationVolume3D&& other) noexcept
    : m_data(std::move(other.m_data)), m_blockers(std::move(other.m_blockers)),
      m_counts(std::move(other.m_counts)),
      m_revision(std::exchange(other.m_revision, 0))
{
}

Core::Result<NavigationVolume3D> NavigationVolume3D::Create(
    NavigationVolume3DData data, NavigationVolume3DConfig config,
    std::pmr::memory_resource& resource)
{
    if (!data)
    {
        return Core::failure(Navigation3DErrorCode::InvalidData,
                             "navigation volume requires valid immutable volume data");
    }
    if (config.dynamicBlockerCapacity == 0U ||
        config.dynamicBlockerCapacity > NavigationVolume3DContract::MaximumDynamicBlockers)
    {
        return Core::failure(Navigation3DErrorCode::CapacityExceeded,
                             "navigation dynamic blocker capacity is outside the supported range");
    }

    auto blockers = BlockerPool::Create(config.dynamicBlockerCapacity, resource);
    if (!blockers)
    {
        return Core::failure(std::move(blockers.error()).withContext(
            "NavigationVolume3D::Create", "dynamic blocker storage"));
    }
    try
    {
        CountStorageOwner counts{
            std::pmr::polymorphic_allocator<CountStorage>{&resource}.new_object<CountStorage>(
                data.cellCount(), resource), &destroyCountStorage};
        return NavigationVolume3D(std::move(data), std::move(*blockers), std::move(counts));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation3DErrorCode::AllocationFailed,
                             "navigation dynamic blocker cell storage allocation failed");
    }
}

Core::usize NavigationVolume3D::cellIndex(NavigationCell3D cell) const noexcept
{
    return static_cast<Core::usize>(cell.x) +
           static_cast<Core::usize>(cell.z) * widthCells() +
           static_cast<Core::usize>(cell.y) * widthCells() * depthCells();
}

bool NavigationVolume3D::isSolid(NavigationCell3D cell) const noexcept
{
    if (!inBounds(cell))
    {
        return false;
    }
    return isBaseSolid(cell) || countAt(cellIndex(cell)) != 0U;
}

Core::u16 NavigationVolume3D::dynamicBlockerCountAt(NavigationCell3D cell) const noexcept
{
    return inBounds(cell) ? countAt(cellIndex(cell)) : Core::u16{0};
}

bool NavigationVolume3D::hasClearance(NavigationCell3D cell,
                                      const NavigationAgentProfile3D& profile) const noexcept
{
    if (!inBounds(cell) || !isValidNavigationAgentProfile3D(profile))
    {
        return false;
    }
    // The head cells may leave the volume upwards: that is open sky, so the top layer
    // stays usable rather than being unreachable for every agent taller than one cell.
    for (Core::u32 offset = 0; offset < profile.heightCells; ++offset)
    {
        if (isSolid({cell.x, cell.y + offset, cell.z}))
        {
            return false;
        }
    }
    return true;
}

bool NavigationVolume3D::isStandable(NavigationCell3D cell,
                                     const NavigationAgentProfile3D& profile) const noexcept
{
    if (!hasClearance(cell, profile))
    {
        return false;
    }
    // Support. At y = 0 the cell below is out of bounds and therefore not solid, so the
    // bottom layer is unstandable unless the world authored a floor there. That is the
    // point of ADR 0048 D4: an empty volume must not look like one walkable plane.
    if (cell.y == 0U)
    {
        return false;
    }
    return isSolid({cell.x, cell.y - 1U, cell.z});
}

Core::Status NavigationVolume3D::validateBox(NavigationCellBox3D box) const
{
    if (box.width == 0U || box.height == 0U || box.depth == 0U ||
        box.x >= widthCells() || box.y >= heightCells() || box.z >= depthCells() ||
        box.width > widthCells() - box.x || box.height > heightCells() - box.y ||
        box.depth > depthCells() - box.z)
    {
        return Core::failure(Navigation3DErrorCode::InvalidCell,
                             "navigation blocker box is empty or outside the volume");
    }
    return Core::success();
}

void NavigationVolume3D::addBoxCounts(NavigationCellBox3D box) noexcept
{
    const Core::u32 endX = box.x + box.width;
    const Core::u32 endY = box.y + box.height;
    const Core::u32 endZ = box.z + box.depth;
    for (Core::u32 y = box.y; y < endY; ++y)
    {
        for (Core::u32 z = box.z; z < endZ; ++z)
        {
            for (Core::u32 x = box.x; x < endX; ++x)
            {
                ++countAt(cellIndex({x, y, z}));
            }
        }
    }
}

void NavigationVolume3D::removeBoxCounts(NavigationCellBox3D box) noexcept
{
    const Core::u32 endX = box.x + box.width;
    const Core::u32 endY = box.y + box.height;
    const Core::u32 endZ = box.z + box.depth;
    for (Core::u32 y = box.y; y < endY; ++y)
    {
        for (Core::u32 z = box.z; z < endZ; ++z)
        {
            for (Core::u32 x = box.x; x < endX; ++x)
            {
                Core::u16& count = countAt(cellIndex({x, y, z}));
                if (count != 0U)
                {
                    --count;
                }
            }
        }
    }
}

void NavigationVolume3D::advanceRevision() noexcept
{
    if (m_revision == (std::numeric_limits<Core::u64>::max)())
    {
        m_revision = 1U;
    }
    else
    {
        ++m_revision;
    }
}

Core::Result<NavigationBlocker3DId> NavigationVolume3D::addBlocker(NavigationCellBox3D box)
{
    if (const Core::Status status = validateBox(box); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto blocker = m_blockers.tryEmplace(DynamicBlocker{.box = box});
    if (!blocker)
    {
        return Core::failure(Navigation3DErrorCode::CapacityExceeded,
                             "navigation dynamic blocker capacity is exhausted");
    }
    addBoxCounts(box);
    advanceRevision();
    return *blocker;
}

std::optional<NavigationCellBox3D> NavigationVolume3D::blockerBox(
    NavigationBlocker3DId blocker) const noexcept
{
    const DynamicBlocker* entry = m_blockers.tryGet(blocker);
    if (entry == nullptr)
    {
        return std::nullopt;
    }
    return entry->box;
}

Core::Status NavigationVolume3D::updateBlocker(NavigationBlocker3DId blocker,
                                               NavigationCellBox3D box)
{
    if (const Core::Status status = validateBox(box); !status)
    {
        return status;
    }
    DynamicBlocker* entry = m_blockers.tryGet(blocker);
    if (entry == nullptr)
    {
        return Core::failure(Navigation3DErrorCode::InvalidBlocker,
                             "navigation blocker id is invalid, stale, or belongs to another volume");
    }
    if (entry->box == box)
    {
        return Core::success();
    }
    removeBoxCounts(entry->box);
    addBoxCounts(box);
    entry->box = box;
    advanceRevision();
    return Core::success();
}

Core::Status NavigationVolume3D::removeBlocker(NavigationBlocker3DId blocker)
{
    DynamicBlocker* entry = m_blockers.tryGet(blocker);
    if (entry == nullptr)
    {
        return Core::failure(Navigation3DErrorCode::InvalidBlocker,
                             "navigation blocker id is invalid, stale, or belongs to another volume");
    }
    const NavigationCellBox3D box = entry->box;
    removeBoxCounts(box);
    if (m_blockers.erase(blocker) != Core::GenerationEraseResult::Erased)
    {
        addBoxCounts(box);
        return Core::failure(Navigation3DErrorCode::InvalidBlocker,
                             "navigation blocker could not be removed after validation");
    }
    advanceRevision();
    return Core::success();
}

} // namespace Tina::Navigation3D
