#include <tina/navigation2d/NavigationPathfinder2D.hpp>

#include <tina/navigation2d/NavigationErrors.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Navigation2D {

namespace {

[[nodiscard]] constexpr bool validDiagonalMode(NavigationDiagonalMode2D mode) noexcept
{
    switch (mode)
    {
    case NavigationDiagonalMode2D::Disabled:
    case NavigationDiagonalMode2D::RequireClearAdjacentCells:
    case NavigationDiagonalMode2D::AllowCornerCutting:
        return true;
    }
    return false;
}

} // namespace

NavigationPathfinder2D::NavigationPathfinder2D(Core::usize cellCapacity,
                                               std::pmr::vector<NodeRecord> records,
                                               std::pmr::vector<Core::u32> openHeap,
                                               std::pmr::vector<NavigationCell2D> path) noexcept
    : m_cellCapacity(cellCapacity), m_records(std::move(records)),
      m_openHeap(std::move(openHeap)), m_path(std::move(path))
{
}

NavigationPathfinder2D::NavigationPathfinder2D(NavigationPathfinder2D&& other) noexcept
    : m_cellCapacity(std::exchange(other.m_cellCapacity, 0)),
      m_records(std::move(other.m_records)), m_openHeap(std::move(other.m_openHeap)),
      m_path(std::move(other.m_path)), m_grid(std::exchange(other.m_grid, nullptr)),
      m_widthCells(std::exchange(other.m_widthCells, 0)),
      m_heightCells(std::exchange(other.m_heightCells, 0)),
      m_startIndex(std::exchange(other.m_startIndex, InvalidIndex)),
      m_goalIndex(std::exchange(other.m_goalIndex, InvalidIndex)),
      m_epoch(std::exchange(other.m_epoch, 0)),
      m_gridRevision(std::exchange(other.m_gridRevision, 0)),
      m_expandedNodes(std::exchange(other.m_expandedNodes, 0)),
      m_options(std::exchange(other.m_options, {})),
      m_pathCost(std::exchange(other.m_pathCost, 0)),
      m_state(std::exchange(other.m_state, NavigationPathQueryState::Idle))
{
}

Core::Result<NavigationPathfinder2D> NavigationPathfinder2D::Create(
    NavigationPathfinder2DConfig config, std::pmr::memory_resource& resource)
{
    if (config.cellCapacity == 0U || config.cellCapacity > NavigationGrid2DContract::MaximumCellCount ||
        config.cellCapacity > static_cast<Core::usize>((std::numeric_limits<Core::u32>::max)()))
    {
        return Core::failure(NavigationErrorCode::CapacityExceeded,
                             "navigation pathfinder cell capacity is outside the supported range");
    }

    try
    {
        std::pmr::vector<NodeRecord> records{&resource};
        records.resize(config.cellCapacity);
        std::pmr::vector<Core::u32> openHeap{&resource};
        openHeap.reserve(config.cellCapacity);
        std::pmr::vector<NavigationCell2D> path{&resource};
        path.reserve(config.cellCapacity);
        return NavigationPathfinder2D(config.cellCapacity, std::move(records),
                                      std::move(openHeap), std::move(path));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(NavigationErrorCode::AllocationFailed,
                             "navigation pathfinder fixed storage allocation failed");
    }
}

void NavigationPathfinder2D::startNewEpoch() noexcept
{
    if (m_epoch == (std::numeric_limits<Core::u32>::max)())
    {
        std::fill(m_records.begin(), m_records.end(), NodeRecord{});
        m_epoch = 1U;
    }
    else
    {
        ++m_epoch;
    }
}

Core::u32 NavigationPathfinder2D::cellIndex(NavigationCell2D cell) const noexcept
{
    return cell.y * m_widthCells + cell.x;
}

NavigationCell2D NavigationPathfinder2D::cellForIndex(Core::u32 index) const noexcept
{
    return NavigationCell2D{.x = index % m_widthCells, .y = index / m_widthCells};
}

Core::u32 NavigationPathfinder2D::heuristic(Core::u32 index) const noexcept
{
    const NavigationCell2D cell = cellForIndex(index);
    const NavigationCell2D goal = cellForIndex(m_goalIndex);
    const Core::u32 dx = cell.x > goal.x ? cell.x - goal.x : goal.x - cell.x;
    const Core::u32 dy = cell.y > goal.y ? cell.y - goal.y : goal.y - cell.y;
    Core::u32 distanceCost = 0;
    if (m_options.diagonalMode == NavigationDiagonalMode2D::Disabled)
    {
        distanceCost = NavigationPathCost2D::Cardinal * (dx + dy);
    }
    else
    {
        const Core::u32 diagonalSteps = (std::min)(dx, dy);
        const Core::u32 cardinalSteps = (std::max)(dx, dy) - diagonalSteps;
        distanceCost = NavigationPathCost2D::Diagonal * diagonalSteps +
                       NavigationPathCost2D::Cardinal * cardinalSteps;
    }
    return distanceCost * m_grid->minimumTraversalCost();
}

bool NavigationPathfinder2D::higherPriority(Core::u32 left, Core::u32 right) const noexcept
{
    const NodeRecord& leftRecord = m_records[left];
    const NodeRecord& rightRecord = m_records[right];
    const Core::u32 leftHeuristic = heuristic(left);
    const Core::u32 rightHeuristic = heuristic(right);
    const Core::u64 leftTotal = static_cast<Core::u64>(leftRecord.gCost) + leftHeuristic;
    const Core::u64 rightTotal = static_cast<Core::u64>(rightRecord.gCost) + rightHeuristic;
    if (leftTotal != rightTotal)
    {
        return leftTotal < rightTotal;
    }
    if (leftHeuristic != rightHeuristic)
    {
        return leftHeuristic < rightHeuristic;
    }
    return left < right;
}

void NavigationPathfinder2D::heapSwap(Core::u32 leftHeapIndex, Core::u32 rightHeapIndex) noexcept
{
    std::swap(m_openHeap[leftHeapIndex], m_openHeap[rightHeapIndex]);
    m_records[m_openHeap[leftHeapIndex]].heapIndex = leftHeapIndex;
    m_records[m_openHeap[rightHeapIndex]].heapIndex = rightHeapIndex;
}

void NavigationPathfinder2D::siftUp(Core::u32 heapIndex) noexcept
{
    while (heapIndex != 0U)
    {
        const Core::u32 parent = (heapIndex - 1U) / 2U;
        if (!higherPriority(m_openHeap[heapIndex], m_openHeap[parent]))
        {
            break;
        }
        heapSwap(heapIndex, parent);
        heapIndex = parent;
    }
}

void NavigationPathfinder2D::siftDown(Core::u32 heapIndex) noexcept
{
    for (;;)
    {
        const Core::u32 left = heapIndex * 2U + 1U;
        if (left >= m_openHeap.size())
        {
            return;
        }
        const Core::u32 right = left + 1U;
        Core::u32 best = left;
        if (right < m_openHeap.size() && higherPriority(m_openHeap[right], m_openHeap[left]))
        {
            best = right;
        }
        if (!higherPriority(m_openHeap[best], m_openHeap[heapIndex]))
        {
            return;
        }
        heapSwap(heapIndex, best);
        heapIndex = best;
    }
}

void NavigationPathfinder2D::pushOpen(Core::u32 index) noexcept
{
    NodeRecord& record = m_records[index];
    record.heapIndex = static_cast<Core::u32>(m_openHeap.size());
    m_openHeap.push_back(index);
    siftUp(record.heapIndex);
}

Core::u32 NavigationPathfinder2D::popOpen() noexcept
{
    const Core::u32 result = m_openHeap.front();
    m_records[result].heapIndex = InvalidIndex;
    if (m_openHeap.size() == 1U)
    {
        m_openHeap.pop_back();
        return result;
    }
    m_openHeap.front() = m_openHeap.back();
    m_openHeap.pop_back();
    m_records[m_openHeap.front()].heapIndex = 0U;
    siftDown(0U);
    return result;
}

void NavigationPathfinder2D::updateOpenPriority(Core::u32 index) noexcept
{
    const Core::u32 heapIndex = m_records[index].heapIndex;
    if (heapIndex != InvalidIndex)
    {
        siftUp(heapIndex);
    }
}

NavigationPathfinder2D::NodeRecord& NavigationPathfinder2D::recordFor(Core::u32 index) noexcept
{
    NodeRecord& record = m_records[index];
    if (record.epoch != m_epoch)
    {
        record = NodeRecord{.epoch = m_epoch};
    }
    return record;
}

Core::Status NavigationPathfinder2D::reconstructPath(Core::u32 goalIndex)
{
    m_path.clear();
    Core::u32 current = goalIndex;
    for (Core::usize count = 0; count < m_cellCapacity; ++count)
    {
        m_path.push_back(cellForIndex(current));
        if (current == m_startIndex)
        {
            std::reverse(m_path.begin(), m_path.end());
            return Core::success();
        }
        const NodeRecord& record = m_records[current];
        if (record.epoch != m_epoch || record.parentIndex == InvalidIndex)
        {
            m_path.clear();
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "navigation path parent chain is incomplete");
        }
        current = record.parentIndex;
    }
    m_path.clear();
    return Core::failure(Core::CoreErrorCode::Internal,
                         "navigation path parent chain exceeded fixed capacity");
}

void NavigationPathfinder2D::setTerminal(NavigationPathQueryState state) noexcept
{
    m_state = state;
    m_openHeap.clear();
    if (state != NavigationPathQueryState::Reached)
    {
        m_path.clear();
        m_pathCost = 0;
    }
}

Core::Result<NavigationPathQueryResult>
NavigationPathfinder2D::begin(const NavigationGrid2D& grid, NavigationCell2D start,
                              NavigationCell2D goal, NavigationPathQueryOptions options)
{
    if (!grid)
    {
        return Core::failure(NavigationErrorCode::InvalidData,
                             "navigation path query requires a valid grid");
    }
    if (grid.cellCount() > m_cellCapacity)
    {
        return Core::failure(NavigationErrorCode::CapacityExceeded,
                             "navigation grid exceeds pathfinder fixed cell capacity");
    }
    if (!grid.inBounds(start) || !grid.inBounds(goal))
    {
        return Core::failure(NavigationErrorCode::InvalidCell,
                             "navigation path start or goal is outside the grid");
    }
    if (!validDiagonalMode(options.diagonalMode))
    {
        return Core::failure(NavigationErrorCode::InvalidData,
                             "navigation path query diagonal mode is invalid");
    }

    startNewEpoch();
    m_openHeap.clear();
    m_path.clear();
    m_grid = &grid;
    m_widthCells = grid.widthCells();
    m_heightCells = grid.heightCells();
    m_startIndex = cellIndex(start);
    m_goalIndex = cellIndex(goal);
    m_gridRevision = grid.revision();
    m_expandedNodes = 0;
    m_options = options;
    m_pathCost = 0;
    m_state = NavigationPathQueryState::Pending;

    if (grid.isBlocked(start) || grid.isBlocked(goal))
    {
        setTerminal(NavigationPathQueryState::Unreachable);
        return result();
    }
    if (start == goal)
    {
        m_path.push_back(start);
        setTerminal(NavigationPathQueryState::Reached);
        return result();
    }

    NodeRecord& startRecord = recordFor(m_startIndex);
    startRecord.parentIndex = InvalidIndex;
    startRecord.gCost = 0U;
    startRecord.closed = false;
    pushOpen(m_startIndex);
    return result();
}

Core::Result<NavigationPathQueryResult>
NavigationPathfinder2D::advance(const NavigationGrid2D& grid, Core::usize expansionBudget)
{
    if (m_state == NavigationPathQueryState::Idle)
    {
        return Core::failure(NavigationErrorCode::QueryNotStarted,
                             "navigation path query has not been started");
    }
    if (m_state != NavigationPathQueryState::Pending)
    {
        return result();
    }
    if (expansionBudget == 0U)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "navigation path expansion budget must be greater than zero");
    }
    if (&grid != m_grid || grid.revision() != m_gridRevision)
    {
        setTerminal(NavigationPathQueryState::Invalidated);
        return result();
    }

    const auto visitNeighbor = [this, &grid](Core::u32 currentIndex, NavigationCell2D neighbor,
                                             Core::u32 movementCost) {
        if (!grid.inBounds(neighbor) || grid.isBlocked(neighbor))
        {
            return;
        }
        const Core::u32 neighborIndex = cellIndex(neighbor);
        NodeRecord& neighborRecord = recordFor(neighborIndex);
        if (neighborRecord.closed)
        {
            return;
        }
        const Core::u32 weightedMovementCost =
            movementCost * static_cast<Core::u32>(grid.traversalCostAt(neighbor));
        const Core::u32 tentativeCost = m_records[currentIndex].gCost + weightedMovementCost;
        if (tentativeCost >= neighborRecord.gCost)
        {
            return;
        }
        neighborRecord.parentIndex = currentIndex;
        neighborRecord.gCost = tentativeCost;
        if (neighborRecord.heapIndex == InvalidIndex)
        {
            pushOpen(neighborIndex);
        }
        else
        {
            updateOpenPriority(neighborIndex);
        }
    };

    Core::usize expandedThisCall = 0;
    while (expandedThisCall < expansionBudget && !m_openHeap.empty())
    {
        const Core::u32 currentIndex = popOpen();
        NodeRecord& currentRecord = m_records[currentIndex];
        currentRecord.closed = true;
        ++expandedThisCall;
        ++m_expandedNodes;

        if (currentIndex == m_goalIndex)
        {
            if (const Core::Status status = reconstructPath(currentIndex); !status)
            {
                reset();
                return Core::failure(std::move(status.error()));
            }
            m_pathCost = currentRecord.gCost;
            setTerminal(NavigationPathQueryState::Reached);
            return result();
        }

        const NavigationCell2D current = cellForIndex(currentIndex);
        // Direction order is explicit; the heap tie-break remains authoritative.
        if (current.y + 1U < m_heightCells)
        {
            visitNeighbor(currentIndex, {current.x, current.y + 1U}, NavigationPathCost2D::Cardinal);
        }
        if (current.x != 0U)
        {
            visitNeighbor(currentIndex, {current.x - 1U, current.y}, NavigationPathCost2D::Cardinal);
        }
        if (current.x + 1U < m_widthCells)
        {
            visitNeighbor(currentIndex, {current.x + 1U, current.y}, NavigationPathCost2D::Cardinal);
        }
        if (current.y != 0U)
        {
            visitNeighbor(currentIndex, {current.x, current.y - 1U}, NavigationPathCost2D::Cardinal);
        }

        if (m_options.diagonalMode != NavigationDiagonalMode2D::Disabled)
        {
            const auto visitDiagonal = [this, &grid, &visitNeighbor, currentIndex, current](
                                           NavigationCell2D neighbor) {
                if (m_options.diagonalMode == NavigationDiagonalMode2D::RequireClearAdjacentCells)
                {
                    const NavigationCell2D horizontal{neighbor.x, current.y};
                    const NavigationCell2D vertical{current.x, neighbor.y};
                    if (grid.isBlocked(horizontal) || grid.isBlocked(vertical))
                    {
                        return;
                    }
                }
                visitNeighbor(currentIndex, neighbor, NavigationPathCost2D::Diagonal);
            };

            if (current.x != 0U && current.y != 0U)
            {
                visitDiagonal({current.x - 1U, current.y - 1U});
            }
            if (current.x + 1U < m_widthCells && current.y != 0U)
            {
                visitDiagonal({current.x + 1U, current.y - 1U});
            }
            if (current.x != 0U && current.y + 1U < m_heightCells)
            {
                visitDiagonal({current.x - 1U, current.y + 1U});
            }
            if (current.x + 1U < m_widthCells && current.y + 1U < m_heightCells)
            {
                visitDiagonal({current.x + 1U, current.y + 1U});
            }
        }
    }

    if (m_openHeap.empty())
    {
        setTerminal(NavigationPathQueryState::Unreachable);
    }
    return result();
}

Core::Result<NavigationPathQueryResult>
NavigationPathfinder2D::findPath(const NavigationGrid2D& grid, NavigationCell2D start,
                                 NavigationCell2D goal, NavigationPathQueryOptions options)
{
    auto started = begin(grid, start, goal, options);
    if (!started || started->state != NavigationPathQueryState::Pending)
    {
        return started;
    }
    return advance(grid, m_cellCapacity);
}

NavigationPathQueryResult NavigationPathfinder2D::cancel() noexcept
{
    if (m_state == NavigationPathQueryState::Pending)
    {
        setTerminal(NavigationPathQueryState::Cancelled);
    }
    return result();
}

void NavigationPathfinder2D::reset() noexcept
{
    m_openHeap.clear();
    m_path.clear();
    m_grid = nullptr;
    m_widthCells = 0;
    m_heightCells = 0;
    m_startIndex = InvalidIndex;
    m_goalIndex = InvalidIndex;
    m_gridRevision = 0;
    m_expandedNodes = 0;
    m_options = {};
    m_pathCost = 0;
    m_state = NavigationPathQueryState::Idle;
}

NavigationPathQueryResult NavigationPathfinder2D::result() const noexcept
{
    return NavigationPathQueryResult{
        .state = m_state,
        .expandedNodes = m_expandedNodes,
        .pathCellCount = m_path.size(),
        .pathCost = m_pathCost,
        .gridRevision = m_gridRevision,
    };
}

} // namespace Tina::Navigation2D
