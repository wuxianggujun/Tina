#include <tina/asset/PhysicsNavigationSync2D.hpp>

#include <tina/math/Geometry2D.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Tina::Asset {
namespace {

inline constexpr Core::usize InvalidIndex = (std::numeric_limits<Core::usize>::max)();

} // namespace

void PhysicsNavigationSync2D::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

PhysicsNavigationSync2D::PhysicsNavigationSync2D(StorageOwner storage) noexcept
    : m_storage(std::move(storage)) {}

Core::Result<PhysicsNavigationSync2D> PhysicsNavigationSync2D::Create(
    PhysicsNavigationSync2DConfig config)
{
    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();
    try {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{resource}.new_object<Storage>(*resource),
                             &destroyStorage};
        PhysicsNavigationSync2D sync{std::move(storage)};
        if (auto status = sync.reserveRegistrations(config.initialRegistrationReserve); !status) {
            return Core::failure(std::move(status.error()));
        }
        return sync;
    } catch (const std::bad_alloc&) {
        return Core::failure(
            AssetErrorCode::AllocationFailed,
            "PhysicsNavigationSync2D storage allocation failed");
    }
}

Core::Status PhysicsNavigationSync2D::reserveRegistrations(Core::usize minimum)
{
    auto& storage = *m_storage;
    if (minimum <= storage.records.size()) {
        return Core::success();
    }
    const Core::usize maximum = (std::min)({storage.records.max_size(), storage.plannedRects.max_size(),
                                          storage.plannedActions.max_size()});
    if (minimum > maximum) {
        return Core::failure(AssetErrorCode::PhysicsNavigationCapacityExceeded,
                             "PhysicsNavigationSync2D registration storage exceeds addressable size");
    }
    const Core::usize geometric = storage.records.size() > maximum / 2
        ? maximum : storage.records.size() * 2;
    const Core::usize size = (std::max)(minimum, geometric);
    try {
        // All throwing allocations precede the size/registration commit.
        storage.records.reserve(size);
        storage.plannedRects.reserve(size);
        storage.plannedActions.reserve(size);
        storage.records.resize(size);
        storage.plannedRects.resize(size);
        storage.plannedActions.resize(size);
        return Core::success();
    } catch (const std::bad_alloc&) {
        return Core::failure(AssetErrorCode::AllocationFailed, "PhysicsNavigationSync2D registration growth failed");
    } catch (const std::length_error&) {
        return Core::failure(AssetErrorCode::PhysicsNavigationCapacityExceeded,
                             "PhysicsNavigationSync2D registration storage exceeds addressable size");
    }
}

Core::usize PhysicsNavigationSync2D::findRecord(
    Physics2D::PhysicsBodyId body) const noexcept
{
    if (!m_storage) { return InvalidIndex; }
    for (Core::usize index = 0; index < m_storage->records.size(); ++index) {
        if (m_storage->records[index].occupied && m_storage->records[index].body == body) {
            return index;
        }
    }
    return InvalidIndex;
}

Core::usize PhysicsNavigationSync2D::findFreeRecord() const noexcept
{
    for (Core::usize index = 0; index < m_storage->records.size(); ++index) {
        if (!m_storage->records[index].occupied) {
            return index;
        }
    }
    return InvalidIndex;
}

Core::Status PhysicsNavigationSync2D::registerBody(
    const Physics2D::PhysicsWorld2D& world,
    const PhysicsNavigationBody2DDesc& body)
{
    if (!*this) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D is not initialized");
    }
    if (!world.isOpen()) {
        return Core::failure(
            Physics2D::Physics2DErrorCode::WorldClosed,
            "PhysicsNavigationSync2D requires an open Physics2D world");
    }
    if (m_world != nullptr && m_world != &world) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D cannot register bodies from another world");
    }
    if (const Core::Status status = Physics2D::validatePhysicsAabb2D(body.localBoundsMeters);
        !status) {
        return status;
    }
    if (findRecord(body.body) != InvalidIndex) {
        return Core::failure(
            Core::CoreErrorCode::AlreadyExists,
            "PhysicsNavigationSync2D body is already registered");
    }
    auto state = world.bodyState(body.body);
    if (!state) {
        return Core::failure(std::move(state.error()));
    }
    Core::usize index = findFreeRecord();
    if (index == InvalidIndex) {
        index = m_storage->records.size();
        if (auto status = reserveRegistrations(index + 1); !status) {
            return status;
        }
    }
    m_storage->records[index] = Record{
        .body = body.body,
        .localBoundsMeters = body.localBoundsMeters,
        .occupied = true};
    m_world = &world;
    refreshLiveStats();
    return Core::success();
}

Core::Status PhysicsNavigationSync2D::setLocalBounds(
    Physics2D::PhysicsBodyId body,
    const Physics2D::PhysicsAabb2D& localBoundsMeters)
{
    if (const Core::Status status = Physics2D::validatePhysicsAabb2D(localBoundsMeters);
        !status) {
        return status;
    }
    const Core::usize index = findRecord(body);
    if (index == InvalidIndex) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationRegistrationNotFound,
            "PhysicsNavigationSync2D body registration was not found");
    }
    m_storage->records[index].localBoundsMeters = localBoundsMeters;
    return Core::success();
}

Core::Result<std::optional<Navigation2D::NavigationCellRect2D>>
PhysicsNavigationSync2D::projectBody(
    const Physics2D::PhysicsBodyState2D& bodyState,
    const Physics2D::PhysicsAabb2D& localBoundsMeters,
    const Navigation2D::NavigationGrid2D& grid) const
{
    if (!bodyState.enabled) {
        return std::optional<Navigation2D::NavigationCellRect2D>{};
    }
    // Conservative on purpose: the rasterized rect must cover the rotated body, so
    // a blocker is never missed. Math::rotatedBounds is the same |cos| / |sin|
    // projection this function used before Tina::Math existed.
    const Math::Aabb2 worldBounds = Math::rotatedBounds(
        Math::Aabb2{localBoundsMeters.lowerMeters, localBoundsMeters.upperMeters},
        bodyState.angleRadians,
        bodyState.positionMeters);
    const float lowerX = worldBounds.lower.x;
    const float lowerY = worldBounds.lower.y;
    const float upperX = worldBounds.upper.x;
    const float upperY = worldBounds.upper.y;
    if (!Math::isFinite(bodyState.positionMeters) || !std::isfinite(bodyState.angleRadians) ||
        !std::isfinite(lowerX) || !std::isfinite(lowerY) ||
        !std::isfinite(upperX) || !std::isfinite(upperY)) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D body projection is not finite");
    }

    const double originX = grid.originXMeters();
    const double originY = grid.originYMeters();
    const double cellSize = grid.cellSizeMeters();
    const double gridUpperX = originX + static_cast<double>(grid.widthCells()) * cellSize;
    const double gridUpperY = originY + static_cast<double>(grid.heightCells()) * cellSize;
    if (static_cast<double>(upperX) <= originX || static_cast<double>(upperY) <= originY ||
        static_cast<double>(lowerX) >= gridUpperX || static_cast<double>(lowerY) >= gridUpperY) {
        return std::optional<Navigation2D::NavigationCellRect2D>{};
    }

    const double clippedLowerX = (std::max)(static_cast<double>(lowerX), originX);
    const double clippedLowerY = (std::max)(static_cast<double>(lowerY), originY);
    const double clippedUpperX = (std::min)(static_cast<double>(upperX), gridUpperX);
    const double clippedUpperY = (std::min)(static_cast<double>(upperY), gridUpperY);
    const Core::u32 startX = static_cast<Core::u32>(
        (std::max)(0.0, std::floor((clippedLowerX - originX) / cellSize)));
    const Core::u32 startY = static_cast<Core::u32>(
        (std::max)(0.0, std::floor((clippedLowerY - originY) / cellSize)));
    const Core::u32 endX = (std::min)(
        grid.widthCells(),
        static_cast<Core::u32>(std::ceil((clippedUpperX - originX) / cellSize)));
    const Core::u32 endY = (std::min)(
        grid.heightCells(),
        static_cast<Core::u32>(std::ceil((clippedUpperY - originY) / cellSize)));
    if (endX <= startX || endY <= startY) {
        return std::optional<Navigation2D::NavigationCellRect2D>{};
    }
    return std::optional<Navigation2D::NavigationCellRect2D>{
        Navigation2D::NavigationCellRect2D{
            .x = startX,
            .y = startY,
            .width = endX - startX,
            .height = endY - startY}};
}

void PhysicsNavigationSync2D::refreshLiveStats() noexcept
{
    m_stats.registeredBodyCount = 0;
    m_stats.publishedBlockerCount = 0;
    for (const Record& record : m_storage->records) {
        if (!record.occupied) {
            continue;
        }
        ++m_stats.registeredBodyCount;
        m_stats.publishedBlockerCount += record.blocker.hasValue() ? 1U : 0U;
    }
}

void PhysicsNavigationSync2D::clearPlan() noexcept
{
    std::fill(m_storage->plannedRects.begin(), m_storage->plannedRects.end(), Navigation2D::NavigationCellRect2D{});
    std::fill(m_storage->plannedActions.begin(), m_storage->plannedActions.end(), PlannedAction::None);
}

Core::Result<PhysicsNavigationSync2DStats> PhysicsNavigationSync2D::synchronize(
    const Physics2D::PhysicsWorld2D& world,
    Navigation2D::NavigationGrid2D& grid)
{
    if (!*this || !world.isOpen() || !grid) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D requires initialized open world and grid objects");
    }
    if (m_world != nullptr && m_world != &world) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D received another Physics2D world");
    }
    if (m_grid != nullptr && m_grid != &grid) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D received another Navigation2D grid");
    }

    clearPlan();
    Core::usize additions = 0;
    Core::usize removals = 0;
    Core::usize updates = 0;
    Core::usize unchanged = 0;
    Core::usize outside = 0;
    Core::usize retired = 0;
    for (Core::usize index = 0; index < m_storage->records.size(); ++index) {
        const Record& record = m_storage->records[index];
        if (!record.occupied) {
            continue;
        }
        if (record.blocker.hasValue()) {
            const auto publishedRect = grid.blockerRect(record.blocker);
            if (!publishedRect || *publishedRect != record.publishedRect) {
                return Core::failure(
                    AssetErrorCode::PhysicsNavigationContractMismatch,
                    "PhysicsNavigationSync2D published blocker was modified externally");
            }
        }
        if (!world.contains(record.body)) {
            m_storage->plannedActions[index] = record.blocker.hasValue()
                ? PlannedAction::RetireAndRemove
                : PlannedAction::Retire;
            removals += record.blocker.hasValue() ? 1U : 0U;
            ++retired;
            continue;
        }
        auto bodyState = world.bodyState(record.body);
        if (!bodyState) {
            return Core::failure(std::move(bodyState.error()));
        }
        auto projected = projectBody(*bodyState, record.localBoundsMeters, grid);
        if (!projected) {
            return Core::failure(std::move(projected.error()));
        }
        if (!projected->has_value()) {
            ++outside;
            if (record.blocker.hasValue()) {
                m_storage->plannedActions[index] = PlannedAction::Remove;
                ++removals;
            }
            continue;
        }
        m_storage->plannedRects[index] = **projected;
        if (!record.blocker.hasValue()) {
            m_storage->plannedActions[index] = PlannedAction::Add;
            ++additions;
        } else if (record.publishedRect != **projected) {
            m_storage->plannedActions[index] = PlannedAction::Update;
            ++updates;
        } else {
            ++unchanged;
        }
    }

    // Reserve before removals as well: a generation-exhausted removed slot may
    // retire permanently, so it must not be counted as guaranteed reusable space.
    if (auto status = grid.reserveAdditionalBlockers(additions); !status) {
        return Core::failure(std::move(status.error()));
    }

    for (Core::usize index = 0; index < m_storage->records.size(); ++index) {
        Record& record = m_storage->records[index];
        const PlannedAction action = m_storage->plannedActions[index];
        if (action == PlannedAction::Remove || action == PlannedAction::RetireAndRemove) {
            if (const Core::Status status = grid.removeBlocker(record.blocker); !status) {
                return Core::failure(std::move(status.error()));
            }
            record.blocker = {};
            record.publishedRect = {};
        }
        if (action == PlannedAction::Retire || action == PlannedAction::RetireAndRemove) {
            record = {};
        }
    }
    for (Core::usize index = 0; index < m_storage->records.size(); ++index) {
        Record& record = m_storage->records[index];
        if (m_storage->plannedActions[index] != PlannedAction::Update) {
            continue;
        }
        if (const Core::Status status = grid.updateBlocker(record.blocker, m_storage->plannedRects[index]);
            !status) {
            return Core::failure(std::move(status.error()));
        }
        record.publishedRect = m_storage->plannedRects[index];
    }
    for (Core::usize index = 0; index < m_storage->records.size(); ++index) {
        Record& record = m_storage->records[index];
        if (m_storage->plannedActions[index] != PlannedAction::Add) {
            continue;
        }
        auto blocker = grid.addBlocker(m_storage->plannedRects[index]);
        if (!blocker) {
            return Core::failure(std::move(blocker.error()));
        }
        record.blocker = *blocker;
        record.publishedRect = m_storage->plannedRects[index];
    }

    m_world = &world;
    m_grid = &grid;
    m_stats.lastAddedBlockerCount = additions;
    m_stats.lastUpdatedBlockerCount = updates;
    m_stats.lastRemovedBlockerCount = removals;
    m_stats.lastUnchangedBlockerCount = unchanged;
    m_stats.lastOutsideGridCount = outside;
    m_stats.lastRetiredBodyCount = retired;
    ++m_stats.synchronizeCount;
    m_stats.totalAddedBlockerCount += additions;
    m_stats.totalUpdatedBlockerCount += updates;
    m_stats.totalRemovedBlockerCount += removals;
    m_stats.totalRetiredBodyCount += retired;
    refreshLiveStats();
    clearPlan();
    return m_stats;
}

Core::Status PhysicsNavigationSync2D::unregisterBody(
    Physics2D::PhysicsBodyId body,
    Navigation2D::NavigationGrid2D& grid)
{
    const Core::usize index = findRecord(body);
    if (index == InvalidIndex) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationRegistrationNotFound,
            "PhysicsNavigationSync2D body registration was not found");
    }
    if (m_grid != nullptr && m_grid != &grid) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D unregister received another grid");
    }
    Record& record = m_storage->records[index];
    if (record.blocker.hasValue()) {
        const auto publishedRect = grid.blockerRect(record.blocker);
        if (!publishedRect || *publishedRect != record.publishedRect) {
            return Core::failure(
                AssetErrorCode::PhysicsNavigationContractMismatch,
                "PhysicsNavigationSync2D published blocker was modified externally");
        }
        if (const Core::Status status = grid.removeBlocker(record.blocker); !status) {
            return status;
        }
        ++m_stats.totalRemovedBlockerCount;
    }
    record = {};
    refreshLiveStats();
    return Core::success();
}

Core::Status PhysicsNavigationSync2D::shutdown(
    Navigation2D::NavigationGrid2D& grid) noexcept
{
    if (!*this) {
        return Core::success();
    }
    if (m_grid != nullptr && m_grid != &grid) {
        return Core::failure(
            AssetErrorCode::PhysicsNavigationContractMismatch,
            "PhysicsNavigationSync2D shutdown received another grid");
    }
    for (const Record& record : m_storage->records) {
        if (record.occupied && record.blocker.hasValue()) {
            const auto publishedRect = grid.blockerRect(record.blocker);
            if (!publishedRect || *publishedRect != record.publishedRect) {
                return Core::failure(
                    AssetErrorCode::PhysicsNavigationContractMismatch,
                    "PhysicsNavigationSync2D published blocker was modified externally");
            }
        }
    }
    for (Record& record : m_storage->records) {
        if (record.occupied && record.blocker.hasValue()) {
            if (const Core::Status status = grid.removeBlocker(record.blocker); !status) {
                return status;
            }
            ++m_stats.totalRemovedBlockerCount;
        }
        record = {};
    }
    m_world = nullptr;
    m_grid = nullptr;
    refreshLiveStats();
    clearPlan();
    return Core::success();
}

bool PhysicsNavigationSync2D::contains(
    Physics2D::PhysicsBodyId body) const noexcept
{
    return findRecord(body) != InvalidIndex;
}

} // namespace Tina::Asset
