#include <tina/navigation2d/NavigationGrid2D.hpp>

#include <tina/navigation2d/NavigationErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Navigation2D {

NavigationGrid2DData::NavigationGrid2DData(Core::u32 widthCells, Core::u32 heightCells,
                                           float originXMeters, float originYMeters,
                                           float cellSizeMeters,
                                           std::pmr::vector<Core::u8> cellFlags,
                                           std::pmr::vector<Core::u8> traversalCosts,
                                           Core::u8 minimumTraversalCost) noexcept
    : m_widthCells(widthCells), m_heightCells(heightCells),
      m_originXMeters(originXMeters), m_originYMeters(originYMeters),
      m_cellSizeMeters(cellSizeMeters),
      m_cellFlags(std::move(cellFlags)),
      m_traversalCosts(std::move(traversalCosts)), m_minimumTraversalCost(minimumTraversalCost)
{
}

NavigationGrid2DData::NavigationGrid2DData(NavigationGrid2DData&& other) noexcept
    : m_widthCells(std::exchange(other.m_widthCells, 0)),
      m_heightCells(std::exchange(other.m_heightCells, 0)),
      m_originXMeters(std::exchange(other.m_originXMeters, 0.0F)),
      m_originYMeters(std::exchange(other.m_originYMeters, 0.0F)),
      m_cellSizeMeters(std::exchange(other.m_cellSizeMeters, 0.0F)),
      m_cellFlags(std::move(other.m_cellFlags)),
      m_traversalCosts(std::move(other.m_traversalCosts)),
      m_minimumTraversalCost(std::exchange(other.m_minimumTraversalCost, 0))
{
}

Core::Result<NavigationGrid2DData> NavigationGrid2DData::Create(
    const NavigationGrid2DDataDesc& desc, std::pmr::memory_resource& resource)
{
    if (desc.widthCells == 0U || desc.heightCells == 0U ||
        desc.widthCells > NavigationGrid2DContract::MaximumDimension ||
        desc.heightCells > NavigationGrid2DContract::MaximumDimension)
    {
        return Core::failure(Navigation2DErrorCode::InvalidData,
                             "navigation grid dimensions are outside the supported range");
    }
    if (!std::isfinite(desc.originXMeters) || !std::isfinite(desc.originYMeters) ||
        !std::isfinite(desc.cellSizeMeters) || !(desc.cellSizeMeters > 0.0F))
    {
        return Core::failure(Navigation2DErrorCode::InvalidData,
                             "navigation grid origin and cell size must be finite");
    }

    const Core::usize width = desc.widthCells;
    const Core::usize height = desc.heightCells;
    if (width > (std::numeric_limits<Core::usize>::max)() / height)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded,
                             "navigation grid cell count overflowed addressable storage");
    }
    const Core::usize cellCount = width * height;
    if (cellCount > NavigationGrid2DContract::MaximumCellCount || desc.cellFlags.size() != cellCount
        || desc.traversalCosts.size() != cellCount)
    {
        return Core::failure(Navigation2DErrorCode::InvalidData,
                             "navigation grid fields do not match the declared dimensions");
    }
    for (const Core::u8 flags : desc.cellFlags)
    {
        if ((flags & static_cast<Core::u8>(~NavigationGrid2DContract::ValidCellFlags)) != 0U)
        {
            return Core::failure(Navigation2DErrorCode::InvalidData,
                                 "navigation grid cell contains unsupported flags");
        }
    }
    Core::u8 minimumTraversalCost = NavigationGrid2DContract::MaximumTraversalCost;
    for (const Core::u8 traversalCost : desc.traversalCosts)
    {
        if (traversalCost < NavigationGrid2DContract::MinimumTraversalCost
            || traversalCost > NavigationGrid2DContract::MaximumTraversalCost)
        {
            return Core::failure(Navigation2DErrorCode::InvalidData,
                                 "navigation grid traversal cost is outside the supported range");
        }
        minimumTraversalCost = (std::min)(minimumTraversalCost, traversalCost);
    }

    try
    {
        std::pmr::vector<Core::u8> cellFlags{&resource};
        cellFlags.assign(desc.cellFlags.begin(), desc.cellFlags.end());
        std::pmr::vector<Core::u8> traversalCosts{&resource};
        traversalCosts.assign(desc.traversalCosts.begin(), desc.traversalCosts.end());
        return NavigationGrid2DData(desc.widthCells, desc.heightCells,
                                    desc.originXMeters, desc.originYMeters, desc.cellSizeMeters,
                                    std::move(cellFlags),
                                    std::move(traversalCosts), minimumTraversalCost);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2DErrorCode::AllocationFailed,
                             "navigation grid data allocation failed");
    }
}

NavigationGrid2DData::operator bool() const noexcept
{
    return m_widthCells != 0U && m_heightCells != 0U && !m_cellFlags.empty()
           && m_traversalCosts.size() == m_cellFlags.size()
           && m_minimumTraversalCost >= NavigationGrid2DContract::MinimumTraversalCost
           && m_minimumTraversalCost <= NavigationGrid2DContract::MaximumTraversalCost;
}

bool NavigationGrid2DData::inBounds(NavigationCell2D cell) const noexcept
{
    return cell.x < m_widthCells && cell.y < m_heightCells;
}

bool NavigationGrid2DData::blockedAt(NavigationCell2D cell) const noexcept
{
    if (!inBounds(cell))
    {
        return true;
    }
    const Core::usize index = static_cast<Core::usize>(cell.y) * m_widthCells + cell.x;
    return (m_cellFlags[index] & NavigationGrid2DContract::CellBlocked) != 0U;
}

Core::u8 NavigationGrid2DData::traversalCostAt(NavigationCell2D cell) const noexcept
{
    if (!inBounds(cell))
    {
        return 0;
    }
    const Core::usize index = static_cast<Core::usize>(cell.y) * m_widthCells + cell.x;
    return m_traversalCosts[index];
}

NavigationGrid2D::NavigationGrid2D(NavigationGrid2DData data, BlockerPool blockers,
                                   std::pmr::vector<Core::u16> blockerCounts) noexcept
    : m_data(std::move(data)), m_blockers(std::move(blockers)),
      m_blockerCounts(std::move(blockerCounts))
{
}

NavigationGrid2D::NavigationGrid2D(NavigationGrid2D&& other) noexcept
    : m_data(std::move(other.m_data)), m_blockers(std::move(other.m_blockers)),
      m_blockerCounts(std::move(other.m_blockerCounts)),
      m_revision(std::exchange(other.m_revision, 0))
{
}

Core::Result<NavigationGrid2D> NavigationGrid2D::Create(
    NavigationGrid2DData data, NavigationGrid2DConfig config, std::pmr::memory_resource& resource)
{
    if (!data)
    {
        return Core::failure(Navigation2DErrorCode::InvalidData,
                             "navigation grid requires valid immutable grid data");
    }
    if (config.dynamicBlockerCapacity == 0U ||
        config.dynamicBlockerCapacity > NavigationGrid2DContract::MaximumDynamicBlockers)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded,
                             "navigation dynamic blocker capacity is outside the supported range");
    }

    auto blockers = BlockerPool::Create(config.dynamicBlockerCapacity, resource);
    if (!blockers)
    {
        return Core::failure(std::move(blockers.error()).withContext(
            "NavigationGrid2D::Create", "dynamic blocker storage"));
    }
    try
    {
        std::pmr::vector<Core::u16> blockerCounts{&resource};
        blockerCounts.resize(data.cellCount(), Core::u16{0});
        return NavigationGrid2D(std::move(data), std::move(*blockers), std::move(blockerCounts));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2DErrorCode::AllocationFailed,
                             "navigation dynamic blocker cell storage allocation failed");
    }
}

Core::usize NavigationGrid2D::cellIndex(NavigationCell2D cell) const noexcept
{
    return static_cast<Core::usize>(cell.y) * widthCells() + cell.x;
}

bool NavigationGrid2D::isBlocked(NavigationCell2D cell) const noexcept
{
    return !inBounds(cell) || isBaseBlocked(cell) || dynamicBlockerCountAt(cell) != 0U;
}

Core::u16 NavigationGrid2D::dynamicBlockerCountAt(NavigationCell2D cell) const noexcept
{
    return inBounds(cell) ? m_blockerCounts[cellIndex(cell)] : 0U;
}

Core::Status NavigationGrid2D::validateRect(NavigationCellRect2D rect) const
{
    if (rect.width == 0U || rect.height == 0U || rect.x >= widthCells() || rect.y >= heightCells() ||
        rect.width > widthCells() - rect.x || rect.height > heightCells() - rect.y)
    {
        return Core::failure(Navigation2DErrorCode::InvalidCell,
                             "navigation blocker rectangle is empty or outside the grid");
    }
    return Core::success();
}

void NavigationGrid2D::addRectCounts(NavigationCellRect2D rect) noexcept
{
    const Core::u32 endX = rect.x + rect.width;
    const Core::u32 endY = rect.y + rect.height;
    for (Core::u32 y = rect.y; y < endY; ++y)
    {
        for (Core::u32 x = rect.x; x < endX; ++x)
        {
            ++m_blockerCounts[cellIndex({x, y})];
        }
    }
}

void NavigationGrid2D::removeRectCounts(NavigationCellRect2D rect) noexcept
{
    const Core::u32 endX = rect.x + rect.width;
    const Core::u32 endY = rect.y + rect.height;
    for (Core::u32 y = rect.y; y < endY; ++y)
    {
        for (Core::u32 x = rect.x; x < endX; ++x)
        {
            Core::u16& count = m_blockerCounts[cellIndex({x, y})];
            if (count != 0U)
            {
                --count;
            }
        }
    }
}

void NavigationGrid2D::advanceRevision() noexcept
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

Core::Result<NavigationBlockerId> NavigationGrid2D::addBlocker(NavigationCellRect2D rect)
{
    if (const Core::Status status = validateRect(rect); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto blocker = m_blockers.tryEmplace(DynamicBlocker{.rect = rect});
    if (!blocker)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded,
                             "navigation dynamic blocker capacity is exhausted");
    }
    addRectCounts(rect);
    advanceRevision();
    return *blocker;
}

std::optional<NavigationCellRect2D> NavigationGrid2D::blockerRect(
    NavigationBlockerId blocker) const noexcept
{
    const DynamicBlocker* entry = m_blockers.tryGet(blocker);
    if (entry == nullptr)
    {
        return std::nullopt;
    }
    return entry->rect;
}

Core::Status NavigationGrid2D::updateBlocker(NavigationBlockerId blocker, NavigationCellRect2D rect)
{
    if (const Core::Status status = validateRect(rect); !status)
    {
        return status;
    }
    DynamicBlocker* entry = m_blockers.tryGet(blocker);
    if (entry == nullptr)
    {
        return Core::failure(Navigation2DErrorCode::InvalidBlocker,
                             "navigation blocker id is invalid, stale, or belongs to another grid");
    }
    if (entry->rect == rect)
    {
        return Core::success();
    }
    removeRectCounts(entry->rect);
    addRectCounts(rect);
    entry->rect = rect;
    advanceRevision();
    return Core::success();
}

Core::Status NavigationGrid2D::removeBlocker(NavigationBlockerId blocker)
{
    DynamicBlocker* entry = m_blockers.tryGet(blocker);
    if (entry == nullptr)
    {
        return Core::failure(Navigation2DErrorCode::InvalidBlocker,
                             "navigation blocker id is invalid, stale, or belongs to another grid");
    }
    const NavigationCellRect2D rect = entry->rect;
    removeRectCounts(rect);
    if (m_blockers.erase(blocker) != Core::GenerationEraseResult::Erased)
    {
        addRectCounts(rect);
        return Core::failure(Navigation2DErrorCode::InvalidBlocker,
                             "navigation blocker could not be removed after validation");
    }
    advanceRevision();
    return Core::success();
}

} // namespace Tina::Navigation2D
