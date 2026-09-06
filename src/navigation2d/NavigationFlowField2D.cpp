#include <tina/navigation2d/NavigationFlowField2D.hpp>

#include <tina/navigation2d/NavigationErrors.hpp>

#include "NavigationIndexHeap2D.hpp"
#include "NavigationTraversal2D.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Navigation2D {

struct NavigationFlowField2D::Storage final {
    Storage(Core::usize capacity, std::pmr::memory_resource& memory)
        : resource(&memory), records(capacity, &memory), heap(0U, &memory)
    {
        heap.reserve(capacity);
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<CellRecord> records;
    std::pmr::vector<Core::u32> heap;
};

void NavigationFlowField2D::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

NavigationFlowField2D::NavigationFlowField2D(Core::usize capacity, StorageOwner storage) noexcept
    : m_capacity(capacity), m_storage(std::move(storage))
{
}

NavigationFlowField2D::NavigationFlowField2D(NavigationFlowField2D&& other) noexcept
    : m_capacity(std::exchange(other.m_capacity, 0)), m_storage(std::move(other.m_storage)),
      m_grid(std::exchange(other.m_grid, nullptr)),
      m_gridRevision(std::exchange(other.m_gridRevision, 0)), m_epoch(std::exchange(other.m_epoch, 0)),
      m_width(std::exchange(other.m_width, 0)), m_expanded(std::exchange(other.m_expanded, 0)),
      m_options(std::exchange(other.m_options, {})),
      m_state(std::exchange(other.m_state, NavigationFlowFieldState2D::Idle))
{
}

Core::Result<NavigationFlowField2D> NavigationFlowField2D::Create(
    NavigationFlowField2DConfig config, std::pmr::memory_resource& resource)
{
    if (config.cellCapacity == 0 || config.cellCapacity > NavigationGrid2DContract::MaximumCellCount)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded, "navigation flow field capacity is outside the supported range");
    }
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.cellCapacity, resource), &destroyStorage};
        return NavigationFlowField2D(config.cellCapacity, std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2DErrorCode::AllocationFailed, "navigation flow field fixed storage allocation failed");
    }
}

void NavigationFlowField2D::startNewEpoch() noexcept
{
    if (m_epoch == (std::numeric_limits<Core::u32>::max)())
    {
        std::fill(m_storage->records.begin(), m_storage->records.end(), CellRecord{});
        m_epoch = 1;
    }
    else { ++m_epoch; }
}

NavigationFlowField2D::CellRecord& NavigationFlowField2D::recordFor(Core::u32 index) noexcept
{
    CellRecord& record = m_storage->records[index];
    if (record.epoch != m_epoch) { record = CellRecord{.epoch = m_epoch}; }
    return record;
}

bool NavigationFlowField2D::higherPriority(Core::u32 left, Core::u32 right) const noexcept
{
    return m_storage->records[left].cost != m_storage->records[right].cost ?
        m_storage->records[left].cost < m_storage->records[right].cost : left < right;
}

void NavigationFlowField2D::setTerminal(NavigationFlowFieldState2D state) noexcept
{
    m_state = state;
    m_storage->heap.clear();
}

Core::Result<NavigationFlowFieldResult2D> NavigationFlowField2D::begin(
    const NavigationGrid2D& grid, NavigationCell2D goal, NavigationPathQueryOptions options)
{
    if (!grid || !Detail::validDiagonalMode(options.diagonalMode))
    {
        return Core::failure(Navigation2DErrorCode::InvalidData, "navigation flow field requires a valid grid and corner policy");
    }
    if (grid.cellCount() > m_capacity)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded, "navigation grid exceeds flow field capacity");
    }
    if (!grid.inBounds(goal))
    {
        return Core::failure(Navigation2DErrorCode::InvalidCell, "navigation flow field goal is outside the grid");
    }
    startNewEpoch();
    m_storage->heap.clear();
    m_grid = &grid;
    m_gridRevision = grid.revision();
    m_width = grid.widthCells();
    m_expanded = 0;
    m_options = options;
    m_state = NavigationFlowFieldState2D::Pending;
    if (grid.isBlocked(goal))
    {
        setTerminal(NavigationFlowFieldState2D::Ready);
        return result();
    }
    const Core::u32 goalIndex = goal.y * m_width + goal.x;
    recordFor(goalIndex).cost = 0;
    Detail::navigationHeapPush(m_storage->heap, m_storage->records, goalIndex,
        [this](Core::u32 left, Core::u32 right) { return higherPriority(left, right); });
    return result();
}

Core::Result<NavigationFlowFieldResult2D> NavigationFlowField2D::advance(
    const NavigationGrid2D& grid, Core::usize expansionBudget)
{
    if (m_state == NavigationFlowFieldState2D::Idle)
    {
        return Core::failure(Navigation2DErrorCode::QueryNotStarted, "navigation flow field build has not started");
    }
    if (m_state == NavigationFlowFieldState2D::Cancelled || m_state == NavigationFlowFieldState2D::Invalidated)
    {
        return result();
    }
    if (&grid != m_grid || grid.revision() != m_gridRevision)
    {
        setTerminal(NavigationFlowFieldState2D::Invalidated);
        return result();
    }
    if (m_state == NavigationFlowFieldState2D::Ready) { return result(); }
    if (expansionBudget == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "navigation flow field expansion budget must be positive");
    }
    const auto priority = [this](Core::u32 left, Core::u32 right) { return higherPriority(left, right); };
    Core::usize expandedThisCall = 0;
    while (expandedThisCall < expansionBudget && !m_storage->heap.empty())
    {
        const Core::u32 currentIndex = Detail::navigationHeapPop(m_storage->heap, m_storage->records, priority);
        CellRecord& currentRecord = m_storage->records[currentIndex];
        currentRecord.closed = true;
        ++expandedThisCall;
        ++m_expanded;
        const NavigationCell2D current{currentIndex % m_width, currentIndex / m_width};
        Detail::visitNavigationNeighbors2D(grid, current, m_options.diagonalMode,
            [this, &grid, current, currentIndex, &currentRecord, &priority](NavigationCell2D predecessor, Core::u32 baseCost) {
                const Core::u32 predecessorIndex = predecessor.y * m_width + predecessor.x;
                CellRecord& predecessorRecord = recordFor(predecessorIndex);
                if (predecessorRecord.closed) { return; }
                // Reverse relaxation represents predecessor -> current in the forward graph.
                const Core::u32 candidateCost = currentRecord.cost + baseCost * grid.traversalCostAt(current);
                if (candidateCost > predecessorRecord.cost) { return; }
                if (candidateCost == predecessorRecord.cost)
                {
                    predecessorRecord.nextIndex = (std::min)(predecessorRecord.nextIndex, currentIndex);
                    return;
                }
                predecessorRecord.cost = candidateCost;
                predecessorRecord.nextIndex = currentIndex;
                if (predecessorRecord.heapIndex == InvalidIndex)
                {
                    Detail::navigationHeapPush(m_storage->heap, m_storage->records, predecessorIndex, priority);
                }
                else
                {
                    Detail::navigationHeapSiftUp(m_storage->heap, m_storage->records, predecessorRecord.heapIndex, priority);
                }
            });
    }
    if (m_storage->heap.empty()) { setTerminal(NavigationFlowFieldState2D::Ready); }
    return result();
}

Core::Result<NavigationFlowFieldResult2D> NavigationFlowField2D::build(
    const NavigationGrid2D& grid, NavigationCell2D goal, NavigationPathQueryOptions options)
{
    auto started = begin(grid, goal, options);
    if (!started || started->state != NavigationFlowFieldState2D::Pending) { return started; }
    return advance(grid, m_capacity);
}

NavigationFlowFieldResult2D NavigationFlowField2D::cancel() noexcept
{
    if (m_state == NavigationFlowFieldState2D::Pending) { setTerminal(NavigationFlowFieldState2D::Cancelled); }
    return result();
}

void NavigationFlowField2D::reset() noexcept
{
    if (m_storage) { m_storage->heap.clear(); }
    m_grid = nullptr;
    m_gridRevision = 0;
    m_width = 0;
    m_expanded = 0;
    m_options = {};
    m_state = NavigationFlowFieldState2D::Idle;
}

NavigationFlowFieldResult2D NavigationFlowField2D::result() const noexcept
{
    return {m_state, m_expanded, m_state == NavigationFlowFieldState2D::Ready ? m_expanded : 0U, m_gridRevision};
}

bool NavigationFlowField2D::isCurrent(const NavigationGrid2D& grid) const noexcept
{
    return m_state == NavigationFlowFieldState2D::Ready && &grid == m_grid && grid.revision() == m_gridRevision;
}

Core::Result<NavigationFlowFieldCell2D> NavigationFlowField2D::sample(
    const NavigationGrid2D& grid, NavigationCell2D cell) const
{
    if (m_state != NavigationFlowFieldState2D::Ready)
    {
        return Core::failure(Navigation2DErrorCode::FieldNotReady, "navigation flow field has no completed publication");
    }
    if (!isCurrent(grid))
    {
        return Core::failure(Navigation2DErrorCode::GridInvalidated, "navigation flow field grid address or revision changed");
    }
    if (!grid.inBounds(cell))
    {
        return Core::failure(Navigation2DErrorCode::InvalidCell, "navigation flow field sample is outside the grid");
    }
    const CellRecord& record = m_storage->records[cell.y * m_width + cell.x];
    if (record.epoch != m_epoch || !record.closed) { return NavigationFlowFieldCell2D{}; }
    NavigationFlowFieldCell2D value{.reachable = true, .cost = record.cost};
    if (record.nextIndex != InvalidIndex)
    {
        const NavigationCell2D next{record.nextIndex % m_width, record.nextIndex / m_width};
        value.nextCell = next;
        const float dx = static_cast<float>(static_cast<Core::i32>(next.x) - static_cast<Core::i32>(cell.x));
        const float dy = static_cast<float>(static_cast<Core::i32>(next.y) - static_cast<Core::i32>(cell.y));
        const float length = std::sqrt(dx * dx + dy * dy);
        value.direction = {dx / length, dy / length};
    }
    return value;
}

} // namespace Tina::Navigation2D
