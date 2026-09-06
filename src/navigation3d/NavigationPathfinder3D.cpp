#include <tina/navigation3d/NavigationPathfinder3D.hpp>

#include <tina/navigation3d/NavigationErrors3D.hpp>

#include "NavigationTraversal3D.hpp"
#include "navigation/NavigationIndexHeap.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Navigation3D {

struct NavigationPathfinder3D::Storage final {
    Storage(Core::usize capacity, std::pmr::memory_resource& memory)
        : resource(&memory), records(capacity, &memory), openHeap(0U, &memory), path(0U, &memory)
    {
        openHeap.reserve(capacity);
        path.reserve(capacity);
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<NodeRecord> records;
    std::pmr::vector<Core::u32> openHeap;
    std::pmr::vector<NavigationCell3D> path;
};

void NavigationPathfinder3D::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

NavigationPathfinder3D::NavigationPathfinder3D(Core::usize cellCapacity, StorageOwner storage) noexcept
    : m_cellCapacity(cellCapacity), m_storage(std::move(storage))
{
}

NavigationPathfinder3D::NavigationPathfinder3D(NavigationPathfinder3D&& other) noexcept
    : m_cellCapacity(std::exchange(other.m_cellCapacity, 0)),
      m_storage(std::move(other.m_storage)), m_volume(std::exchange(other.m_volume, nullptr)),
      m_widthCells(std::exchange(other.m_widthCells, 0)),
      m_heightCells(std::exchange(other.m_heightCells, 0)),
      m_depthCells(std::exchange(other.m_depthCells, 0)),
      m_startIndex(std::exchange(other.m_startIndex, InvalidIndex)),
      m_goalIndex(std::exchange(other.m_goalIndex, InvalidIndex)),
      m_epoch(std::exchange(other.m_epoch, 0)),
      m_volumeRevision(std::exchange(other.m_volumeRevision, 0)),
      m_expandedNodes(std::exchange(other.m_expandedNodes, 0)),
      m_verticalTransitions(std::exchange(other.m_verticalTransitions, 0)),
      m_options(std::exchange(other.m_options, {})),
      m_pathCost(std::exchange(other.m_pathCost, 0)),
      m_state(std::exchange(other.m_state, NavigationPathQueryState3D::Idle))
{
}

Core::Result<NavigationPathfinder3D> NavigationPathfinder3D::Create(
    NavigationPathfinder3DConfig config, std::pmr::memory_resource& resource)
{
    if (config.cellCapacity == 0U ||
        config.cellCapacity > NavigationVolume3DContract::MaximumCellCount ||
        config.cellCapacity > static_cast<Core::usize>((std::numeric_limits<Core::u32>::max)()))
    {
        return Core::failure(Navigation3DErrorCode::CapacityExceeded,
                             "navigation pathfinder cell capacity is outside the supported range");
    }

    try
    {
        // The sized vector constructors are fallible even for zero elements. In MSVC Debug,
        // allocator-only/vector-move constructors are noexcept but allocate iterator
        // proxies. Keep vectors stationary inside this PMR owner.
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.cellCapacity, resource), &destroyStorage};
        return NavigationPathfinder3D(config.cellCapacity, std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation3DErrorCode::AllocationFailed,
                             "navigation pathfinder fixed storage allocation failed");
    }
}

void NavigationPathfinder3D::startNewEpoch() noexcept
{
    if (m_epoch == (std::numeric_limits<Core::u32>::max)())
    {
        std::fill(m_storage->records.begin(), m_storage->records.end(), NodeRecord{});
        m_epoch = 1U;
    }
    else
    {
        ++m_epoch;
    }
}

Core::u32 NavigationPathfinder3D::cellIndex(NavigationCell3D cell) const noexcept
{
    return cell.x + cell.z * m_widthCells + cell.y * m_widthCells * m_depthCells;
}

NavigationCell3D NavigationPathfinder3D::cellForIndex(Core::u32 index) const noexcept
{
    const Core::u32 layerStride = m_widthCells * m_depthCells;
    const Core::u32 y = index / layerStride;
    const Core::u32 withinLayer = index % layerStride;
    return NavigationCell3D{
        .x = withinLayer % m_widthCells,
        .y = y,
        .z = withinLayer / m_widthCells,
    };
}

Core::u32 NavigationPathfinder3D::heuristic(Core::u32 index) const noexcept
{
    const NavigationCell3D cell = cellForIndex(index);
    const NavigationCell3D goal = cellForIndex(m_goalIndex);
    const Core::u32 dx = cell.x > goal.x ? cell.x - goal.x : goal.x - cell.x;
    const Core::u32 dz = cell.z > goal.z ? cell.z - goal.z : goal.z - cell.z;
    Core::u32 horizontalCost = 0;
    if (m_options.diagonalMode == NavigationDiagonalMode3D::Disabled)
    {
        horizontalCost = NavigationPathCost3D::Cardinal * (dx + dz);
    }
    else
    {
        const Core::u32 diagonalSteps = (std::min)(dx, dz);
        const Core::u32 cardinalSteps = (std::max)(dx, dz) - diagonalSteps;
        horizontalCost = NavigationPathCost3D::Diagonal * diagonalSteps +
                         NavigationPathCost3D::Cardinal * cardinalSteps;
    }
    // Vertical distance contributes at the CHEAPER of the two rates, and only that.
    // Charging the step-up rate for a climb would overestimate whenever the route reaches
    // the goal's height by falling, and an overestimating heuristic is no longer
    // admissible -- A* would stop returning the least-cost path.
    const Core::u32 dy = cell.y > goal.y ? cell.y - goal.y : goal.y - cell.y;
    const Core::u32 verticalCost = NavigationPathCost3D::FallPerCell * dy;
    return (horizontalCost + verticalCost) * m_volume->minimumTraversalCost();
}

bool NavigationPathfinder3D::higherPriority(Core::u32 left, Core::u32 right) const noexcept
{
    const NodeRecord& leftRecord = m_storage->records[left];
    const NodeRecord& rightRecord = m_storage->records[right];
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

void NavigationPathfinder3D::pushOpen(Core::u32 index) noexcept
{
    Navigation::Detail::navigationHeapPush(m_storage->openHeap, m_storage->records, index,
        [this](Core::u32 left, Core::u32 right) { return higherPriority(left, right); });
}

Core::u32 NavigationPathfinder3D::popOpen() noexcept
{
    return Navigation::Detail::navigationHeapPop(m_storage->openHeap, m_storage->records,
        [this](Core::u32 left, Core::u32 right) { return higherPriority(left, right); });
}

void NavigationPathfinder3D::updateOpenPriority(Core::u32 index) noexcept
{
    const Core::u32 heapIndex = m_storage->records[index].heapIndex;
    if (heapIndex != InvalidIndex)
    {
        Navigation::Detail::navigationHeapSiftUp(m_storage->openHeap, m_storage->records, heapIndex,
            [this](Core::u32 left, Core::u32 right) { return higherPriority(left, right); });
    }
}

NavigationPathfinder3D::NodeRecord& NavigationPathfinder3D::recordFor(Core::u32 index) noexcept
{
    NodeRecord& record = m_storage->records[index];
    if (record.epoch != m_epoch)
    {
        record = NodeRecord{.epoch = m_epoch};
    }
    return record;
}

Core::Status NavigationPathfinder3D::reconstructPath(Core::u32 goalIndex)
{
    m_storage->path.clear();
    m_verticalTransitions = 0;
    Core::u32 current = goalIndex;
    for (Core::usize count = 0; count < m_cellCapacity; ++count)
    {
        m_storage->path.push_back(cellForIndex(current));
        if (current == m_startIndex)
        {
            std::reverse(m_storage->path.begin(), m_storage->path.end());
            for (Core::usize step = 1; step < m_storage->path.size(); ++step)
            {
                if (m_storage->path[step].y != m_storage->path[step - 1].y)
                {
                    ++m_verticalTransitions;
                }
            }
            return Core::success();
        }
        const NodeRecord& record = m_storage->records[current];
        if (record.epoch != m_epoch || record.parentIndex == InvalidIndex)
        {
            m_storage->path.clear();
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "navigation path parent chain is incomplete");
        }
        current = record.parentIndex;
    }
    m_storage->path.clear();
    return Core::failure(Core::CoreErrorCode::Internal,
                         "navigation path parent chain exceeded fixed capacity");
}

void NavigationPathfinder3D::setTerminal(NavigationPathQueryState3D state) noexcept
{
    m_state = state;
    m_storage->openHeap.clear();
    if (state != NavigationPathQueryState3D::Reached)
    {
        m_storage->path.clear();
        m_pathCost = 0;
        m_verticalTransitions = 0;
    }
}

Core::Result<NavigationPathQueryResult3D>
NavigationPathfinder3D::begin(const NavigationVolume3D& volume, NavigationCell3D start,
                              NavigationCell3D goal, NavigationPathQueryOptions3D options)
{
    if (!volume)
    {
        return Core::failure(Navigation3DErrorCode::InvalidData,
                             "navigation path query requires a valid volume");
    }
    if (volume.cellCount() > m_cellCapacity)
    {
        return Core::failure(Navigation3DErrorCode::CapacityExceeded,
                             "navigation volume exceeds pathfinder fixed cell capacity");
    }
    if (!Detail::validDiagonalMode(options.diagonalMode))
    {
        return Core::failure(Navigation3DErrorCode::InvalidData,
                             "navigation path query diagonal mode is invalid");
    }
    if (!isValidNavigationAgentProfile3D(options.agent))
    {
        return Core::failure(Navigation3DErrorCode::InvalidAgentProfile,
                             "navigation agent profile height, step-up or fall allowance is out of range");
    }
    if (options.agent.heightCells > volume.heightCells())
    {
        return Core::failure(Navigation3DErrorCode::InvalidAgentProfile,
                             "navigation agent is taller than the volume it must move through");
    }
    if (!volume.inBounds(start) || !volume.inBounds(goal))
    {
        return Core::failure(Navigation3DErrorCode::InvalidCell,
                             "navigation path start or goal is outside the volume");
    }
    // Standability is checked before publishing a query, and reported distinctly from a
    // coordinate error. See NavigationErrors3D.hpp: a mid-air sample is a caller bug that
    // "no route" would hide.
    if (!volume.isStandable(start, options.agent))
    {
        return Core::failure(Navigation3DErrorCode::NotStandable,
                             "navigation path start cannot hold an agent of this profile");
    }
    if (!volume.isStandable(goal, options.agent))
    {
        return Core::failure(Navigation3DErrorCode::NotStandable,
                             "navigation path goal cannot hold an agent of this profile");
    }

    startNewEpoch();
    m_storage->openHeap.clear();
    m_storage->path.clear();
    m_volume = &volume;
    m_widthCells = volume.widthCells();
    m_heightCells = volume.heightCells();
    m_depthCells = volume.depthCells();
    m_startIndex = cellIndex(start);
    m_goalIndex = cellIndex(goal);
    m_volumeRevision = volume.revision();
    m_expandedNodes = 0;
    m_verticalTransitions = 0;
    m_options = options;
    m_pathCost = 0;
    m_state = NavigationPathQueryState3D::Pending;

    if (start == goal)
    {
        m_storage->path.push_back(start);
        setTerminal(NavigationPathQueryState3D::Reached);
        return result();
    }

    NodeRecord& startRecord = recordFor(m_startIndex);
    startRecord.parentIndex = InvalidIndex;
    startRecord.gCost = 0U;
    startRecord.closed = false;
    pushOpen(m_startIndex);
    return result();
}

Core::Result<NavigationPathQueryResult3D>
NavigationPathfinder3D::advance(const NavigationVolume3D& volume, Core::usize expansionBudget)
{
    if (m_state == NavigationPathQueryState3D::Idle)
    {
        return Core::failure(Navigation3DErrorCode::QueryNotStarted,
                             "navigation path query has not been started");
    }
    if (m_state != NavigationPathQueryState3D::Pending)
    {
        return result();
    }
    if (expansionBudget == 0U)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "navigation path expansion budget must be greater than zero");
    }
    if (&volume != m_volume || volume.revision() != m_volumeRevision)
    {
        setTerminal(NavigationPathQueryState3D::Invalidated);
        return result();
    }

    const auto visitMove = [this, &volume](Core::u32 currentIndex, Detail::NavigationMove3D move) {
        const Core::u32 neighborIndex = cellIndex(move.destination);
        NodeRecord& neighborRecord = recordFor(neighborIndex);
        if (neighborRecord.closed)
        {
            return;
        }
        const Core::u32 weightedMovementCost =
            move.movementCost * static_cast<Core::u32>(volume.traversalCostAt(move.destination));
        const Core::u32 tentativeCost = m_storage->records[currentIndex].gCost + weightedMovementCost;
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
    while (expandedThisCall < expansionBudget && !m_storage->openHeap.empty())
    {
        const Core::u32 currentIndex = popOpen();
        NodeRecord& currentRecord = m_storage->records[currentIndex];
        currentRecord.closed = true;
        ++expandedThisCall;
        ++m_expandedNodes;

        if (currentIndex == m_goalIndex)
        {
            const Core::u32 reachedCost = currentRecord.gCost;
            if (const Core::Status status = reconstructPath(currentIndex); !status)
            {
                reset();
                return Core::failure(std::move(status.error()));
            }
            m_pathCost = reachedCost;
            setTerminal(NavigationPathQueryState3D::Reached);
            return result();
        }

        const NavigationCell3D current = cellForIndex(currentIndex);
        Detail::visitNavigationNeighbors3D(volume, current, m_options.agent, m_options.diagonalMode,
            [&visitMove, currentIndex](Detail::NavigationMove3D move) {
                visitMove(currentIndex, move);
            });
    }

    if (m_storage->openHeap.empty())
    {
        setTerminal(NavigationPathQueryState3D::Unreachable);
    }
    return result();
}

Core::Result<NavigationPathQueryResult3D>
NavigationPathfinder3D::findPath(const NavigationVolume3D& volume, NavigationCell3D start,
                                 NavigationCell3D goal, NavigationPathQueryOptions3D options)
{
    auto started = begin(volume, start, goal, options);
    if (!started || started->state != NavigationPathQueryState3D::Pending)
    {
        return started;
    }
    return advance(volume, m_cellCapacity);
}

NavigationPathQueryResult3D NavigationPathfinder3D::cancel() noexcept
{
    if (m_state == NavigationPathQueryState3D::Pending)
    {
        setTerminal(NavigationPathQueryState3D::Cancelled);
    }
    return result();
}

void NavigationPathfinder3D::reset() noexcept
{
    if (m_storage)
    {
        m_storage->openHeap.clear();
        m_storage->path.clear();
    }
    m_volume = nullptr;
    m_widthCells = 0;
    m_heightCells = 0;
    m_depthCells = 0;
    m_startIndex = InvalidIndex;
    m_goalIndex = InvalidIndex;
    m_volumeRevision = 0;
    m_expandedNodes = 0;
    m_verticalTransitions = 0;
    m_options = {};
    m_pathCost = 0;
    m_state = NavigationPathQueryState3D::Idle;
}

NavigationPathQueryResult3D NavigationPathfinder3D::result() const noexcept
{
    return NavigationPathQueryResult3D{
        .state = m_state,
        .expandedNodes = m_expandedNodes,
        .pathCellCount = path().size(),
        .pathCost = m_pathCost,
        .volumeRevision = m_volumeRevision,
        .verticalTransitions = m_verticalTransitions,
    };
}

std::span<const NavigationCell3D> NavigationPathfinder3D::path() const noexcept
{
    return m_storage ? std::span<const NavigationCell3D>{m_storage->path}
                     : std::span<const NavigationCell3D>{};
}

} // namespace Tina::Navigation3D
