#include <tina/navigation3d/NavigationVolume3D.hpp>

#include <tina/navigation3d/NavigationErrors3D.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Navigation3D {

bool isValidNavigationAgentProfile3D(const NavigationAgentProfile3D& profile) noexcept
{
    return profile.heightCells >= NavigationVolume3DContract::MinimumAgentHeightCells
        && profile.heightCells <= NavigationVolume3DContract::MaximumAgentHeightCells
        && profile.maxStepUpCells <= NavigationVolume3DContract::MaximumAgentStepUpCells
        && profile.maxFallCells <= NavigationVolume3DContract::MaximumAgentFallCells;
}

struct NavigationVolume3DData::Storage final {
    Storage(std::span<const Core::u8> sourceFlags, std::span<const Core::u8> sourceCosts,
            std::pmr::memory_resource& memory)
        : resource(&memory), flags(sourceFlags.begin(), sourceFlags.end(), &memory),
          costs(sourceCosts.begin(), sourceCosts.end(), &memory)
    {
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<Core::u8> flags;
    std::pmr::vector<Core::u8> costs;
};

void NavigationVolume3DData::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

std::span<const Core::u8> NavigationVolume3DData::flagsSpan() const noexcept
{
    return m_storage ? std::span<const Core::u8>{m_storage->flags} : std::span<const Core::u8>{};
}

std::span<const Core::u8> NavigationVolume3DData::costsSpan() const noexcept
{
    return m_storage ? std::span<const Core::u8>{m_storage->costs} : std::span<const Core::u8>{};
}

NavigationVolume3DData::NavigationVolume3DData(
    Core::u32 widthCells, Core::u32 heightCells, Core::u32 depthCells,
    float originXMeters, float originYMeters, float originZMeters, float cellSizeMeters,
    StorageOwner storage, Core::u8 minimumTraversalCost) noexcept
    : m_widthCells(widthCells), m_heightCells(heightCells), m_depthCells(depthCells),
      m_originXMeters(originXMeters), m_originYMeters(originYMeters), m_originZMeters(originZMeters),
      m_cellSizeMeters(cellSizeMeters), m_storage(std::move(storage)),
      m_cellCount(m_storage ? m_storage->flags.size() : 0U),
      m_minimumTraversalCost(minimumTraversalCost)
{
}

NavigationVolume3DData::NavigationVolume3DData(NavigationVolume3DData&& other) noexcept
    : m_widthCells(std::exchange(other.m_widthCells, 0)),
      m_heightCells(std::exchange(other.m_heightCells, 0)),
      m_depthCells(std::exchange(other.m_depthCells, 0)),
      m_originXMeters(std::exchange(other.m_originXMeters, 0.0F)),
      m_originYMeters(std::exchange(other.m_originYMeters, 0.0F)),
      m_originZMeters(std::exchange(other.m_originZMeters, 0.0F)),
      m_cellSizeMeters(std::exchange(other.m_cellSizeMeters, 0.0F)),
      m_storage(std::move(other.m_storage)),
      m_cellCount(std::exchange(other.m_cellCount, 0)),
      m_minimumTraversalCost(std::exchange(other.m_minimumTraversalCost, 0))
{
}

Core::Result<NavigationVolume3DData> NavigationVolume3DData::Create(
    const NavigationVolume3DDataDesc& desc, std::pmr::memory_resource& resource)
{
    if (desc.widthCells == 0U || desc.heightCells == 0U || desc.depthCells == 0U ||
        desc.widthCells > NavigationVolume3DContract::MaximumDimension ||
        desc.heightCells > NavigationVolume3DContract::MaximumDimension ||
        desc.depthCells > NavigationVolume3DContract::MaximumDimension)
    {
        return Core::failure(Navigation3DErrorCode::InvalidData,
                             "navigation volume dimensions are outside the supported range");
    }
    if (!std::isfinite(desc.originXMeters) || !std::isfinite(desc.originYMeters) ||
        !std::isfinite(desc.originZMeters) || !std::isfinite(desc.cellSizeMeters) ||
        !(desc.cellSizeMeters > 0.0F))
    {
        return Core::failure(Navigation3DErrorCode::InvalidData,
                             "navigation volume origin and cell size must be finite");
    }

    const Core::usize width = desc.widthCells;
    const Core::usize height = desc.heightCells;
    const Core::usize depth = desc.depthCells;
    constexpr Core::usize addressableMaximum = (std::numeric_limits<Core::usize>::max)();
    if (width > addressableMaximum / depth || (width * depth) > addressableMaximum / height)
    {
        return Core::failure(Navigation3DErrorCode::CapacityExceeded,
                             "navigation volume cell count overflowed addressable storage");
    }
    const Core::usize cellCount = width * depth * height;
    if (cellCount > NavigationVolume3DContract::MaximumCellCount ||
        desc.cellFlags.size() != cellCount || desc.traversalCosts.size() != cellCount)
    {
        return Core::failure(Navigation3DErrorCode::InvalidData,
                             "navigation volume fields do not match the declared dimensions");
    }
    for (const Core::u8 flags : desc.cellFlags)
    {
        if ((flags & static_cast<Core::u8>(~NavigationVolume3DContract::ValidCellFlags)) != 0U)
        {
            return Core::failure(Navigation3DErrorCode::InvalidData,
                                 "navigation volume cell contains unsupported flags");
        }
    }
    Core::u8 minimumTraversalCost = NavigationVolume3DContract::MaximumTraversalCost;
    for (const Core::u8 traversalCost : desc.traversalCosts)
    {
        if (traversalCost < NavigationVolume3DContract::MinimumTraversalCost ||
            traversalCost > NavigationVolume3DContract::MaximumTraversalCost)
        {
            return Core::failure(Navigation3DErrorCode::InvalidData,
                                 "navigation volume traversal cost is outside the supported range");
        }
        minimumTraversalCost = (std::min)(minimumTraversalCost, traversalCost);
    }

    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 desc.cellFlags, desc.traversalCosts, resource), &destroyStorage};
        return NavigationVolume3DData(desc.widthCells, desc.heightCells, desc.depthCells,
                                      desc.originXMeters, desc.originYMeters, desc.originZMeters,
                                      desc.cellSizeMeters, std::move(storage), minimumTraversalCost);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation3DErrorCode::AllocationFailed,
                             "navigation volume data allocation failed");
    }
}

NavigationVolume3DData::operator bool() const noexcept
{
    return m_storage != nullptr && m_widthCells != 0U && m_heightCells != 0U && m_depthCells != 0U
        && !m_storage->flags.empty() && m_storage->costs.size() == m_storage->flags.size()
        && m_minimumTraversalCost >= NavigationVolume3DContract::MinimumTraversalCost
        && m_minimumTraversalCost <= NavigationVolume3DContract::MaximumTraversalCost;
}

bool NavigationVolume3DData::inBounds(NavigationCell3D cell) const noexcept
{
    return cell.x < m_widthCells && cell.y < m_heightCells && cell.z < m_depthCells;
}

namespace {

[[nodiscard]] constexpr Core::usize volumeCellIndex(
    NavigationCell3D cell, Core::u32 widthCells, Core::u32 depthCells) noexcept
{
    return static_cast<Core::usize>(cell.x) +
           static_cast<Core::usize>(cell.z) * widthCells +
           static_cast<Core::usize>(cell.y) * widthCells * depthCells;
}

} // namespace

bool NavigationVolume3DData::solidAt(NavigationCell3D cell) const noexcept
{
    // Out of bounds is NOT solid: above the volume that is open sky, and below y = 0 it
    // is the absence of a floor. See ADR 0048 D4.
    if (m_storage == nullptr || !inBounds(cell))
    {
        return false;
    }
    return (m_storage->flags[volumeCellIndex(cell, m_widthCells, m_depthCells)] &
            NavigationVolume3DContract::CellSolid) != 0U;
}

Core::u8 NavigationVolume3DData::traversalCostAt(NavigationCell3D cell) const noexcept
{
    if (m_storage == nullptr || !inBounds(cell))
    {
        return 0;
    }
    return m_storage->costs[volumeCellIndex(cell, m_widthCells, m_depthCells)];
}

std::optional<NavigationCell3D> NavigationVolume3DData::worldToCell(
    Math::Vec3 positionMeters) const noexcept
{
    if (!*this || !std::isfinite(positionMeters.x) || !std::isfinite(positionMeters.y) ||
        !std::isfinite(positionMeters.z))
    {
        return std::nullopt;
    }
    // Subtraction and division stay in double: casting a negative or huge float to an
    // unsigned cell index before checking the bounds would be undefined.
    const double x = (static_cast<double>(positionMeters.x) - m_originXMeters) / m_cellSizeMeters;
    const double y = (static_cast<double>(positionMeters.y) - m_originYMeters) / m_cellSizeMeters;
    const double z = (static_cast<double>(positionMeters.z) - m_originZMeters) / m_cellSizeMeters;
    if (!(x >= 0.0 && x < m_widthCells && y >= 0.0 && y < m_heightCells &&
          z >= 0.0 && z < m_depthCells))
    {
        return std::nullopt;
    }
    return NavigationCell3D{static_cast<Core::u32>(x), static_cast<Core::u32>(y),
                            static_cast<Core::u32>(z)};
}

std::optional<Math::Vec3> NavigationVolume3DData::cellCenter(NavigationCell3D cell) const noexcept
{
    if (!inBounds(cell))
    {
        return std::nullopt;
    }
    const double x = static_cast<double>(m_originXMeters) +
                     (static_cast<double>(cell.x) + 0.5) * m_cellSizeMeters;
    const double y = static_cast<double>(m_originYMeters) +
                     (static_cast<double>(cell.y) + 0.5) * m_cellSizeMeters;
    const double z = static_cast<double>(m_originZMeters) +
                     (static_cast<double>(cell.z) + 0.5) * m_cellSizeMeters;
    constexpr double maximum = (std::numeric_limits<float>::max)();
    if (x < -maximum || x > maximum || y < -maximum || y > maximum || z < -maximum || z > maximum)
    {
        return std::nullopt;
    }
    const Math::Vec3 center{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    if (worldToCell(center) != cell)
    {
        return std::nullopt;
    }
    return center;
}

} // namespace Tina::Navigation3D
