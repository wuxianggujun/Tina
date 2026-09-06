#include <tina/navigation2d/NavigationAgent2D.hpp>
#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationFlowField2D.hpp>

#include <array>
#include <iostream>
#include <memory_resource>
#include <utility>

namespace {

using namespace Tina;
using namespace Tina::Navigation2D;

struct NavigationEvidence final {
    Core::usize pathCells = 0;
    Core::usize smoothedWaypoints = 0;
    Core::usize agentFrames = 0;
    Core::u32 flowCost = 0;
};

[[nodiscard]] Core::Status require(bool condition, const char* message)
{
    return condition ? Core::success() : Core::Status{Core::failure(Core::CoreErrorCode::Internal, message)};
}

[[nodiscard]] Core::Result<NavigationEvidence> exerciseInstalledNavigation()
{
    constexpr Core::u32 width = 7;
    constexpr Core::u32 height = 5;
    std::array<Core::u8, width * height> flags{};
    std::array<Core::u8, width * height> costs{};
    costs.fill(1);
    for (Core::u32 y = 0; y + 1U < height; ++y) { flags[y * width + 3U] = NavigationGrid2DContract::CellBlocked; }
    costs[width + 1U] = 8;
    std::pmr::unsynchronized_pool_resource memory;
    auto data = NavigationGrid2DData::Create(
        {.widthCells = width, .heightCells = height, .cellFlags = flags, .traversalCosts = costs}, memory);
    if (!data) { return Core::failure(std::move(data.error())); }
    auto grid = NavigationGrid2D::Create(std::move(*data), {.dynamicBlockerCapacity = 2}, memory);
    if (!grid) { return Core::failure(std::move(grid.error())); }

    const Math::Vec2 start{0.5F, 0.5F};
    const Math::Vec2 goal{6.25F, 0.75F};
    const auto startCell = grid->worldToCell(start);
    const auto goalCell = grid->worldToCell(goal);
    if (!startCell || !goalCell) { return Core::failure(Core::CoreErrorCode::Internal, "world conversion failed"); }
    auto search = NavigationPathfinder2D::Create({.cellCapacity = grid->cellCount()}, memory);
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = grid->cellCount()}, memory);
    auto field = NavigationFlowField2D::Create({.cellCapacity = grid->cellCount()}, memory);
    if (!search) { return Core::failure(std::move(search.error())); }
    if (!smoother) { return Core::failure(std::move(smoother.error())); }
    if (!field) { return Core::failure(std::move(field.error())); }
    auto query = search->findPath(*grid, *startCell, *goalCell);
    if (!query) { return Core::failure(std::move(query.error())); }
    if (auto status = require(query->state == NavigationPathQueryState::Reached, "installed A* failed to find the route"); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = smoother->smooth(*grid, search->path()); !status) { return Core::failure(std::move(status.error())); }
    if (auto result = field->build(*grid, *goalCell); !result) { return Core::failure(std::move(result.error())); }
    auto sampled = field->sample(*grid, *startCell);
    if (!sampled) { return Core::failure(std::move(sampled.error())); }
    if (auto status = require(sampled->reachable && sampled->cost == query->pathCost, "installed flow cost differs from forward A*"); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    NavigationEvidence evidence{search->path().size(), smoother->path().size(), 0, sampled->cost};

    auto follower = NavigationPathFollower2D::Create({.waypointCapacity = 2}, memory);
    if (!follower) { return Core::failure(std::move(follower.error())); }
    const std::array localPath{start, Math::Vec2{0.5F, 1.5F}};
    if (auto status = follower->setPath(localPath); !status) { return Core::failure(std::move(status.error())); }
    if (auto steering = follower->update(start, 1.0F); !steering) { return Core::failure(std::move(steering.error())); }

    auto agent = NavigationAgent2D::Create({.cellCapacity = grid->cellCount(), .speedMetersPerSecond = 3.0F}, memory);
    if (!agent) { return Core::failure(std::move(agent.error())); }
    if (auto status = agent->setGoal(*grid, start, goal); !status) { return Core::failure(std::move(status.error())); }
    Math::Vec2 position = start;
    constexpr float deltaSeconds = 1.0F / 30.0F;
    constexpr Core::usize maximumFrames = 1000;
    for (; evidence.agentFrames < maximumFrames; ++evidence.agentFrames)
    {
        auto steering = agent->update(*grid, position, deltaSeconds, 2);
        if (!steering) { return Core::failure(std::move(steering.error())); }
        if (steering->state == NavigationAgentState2D::Arrived) { break; }
        if (auto status = require(steering->state == NavigationAgentState2D::Planning ||
                                 steering->state == NavigationAgentState2D::Following,
                                 "installed agent entered an unexpected terminal state"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        const Math::Vec2 nextPosition = position + steering->desiredVelocity * deltaSeconds;
        auto visible = hasNavigationWorldLineOfSight2D(*grid, position, nextPosition);
        if (!visible) { return Core::failure(std::move(visible.error())); }
        if (auto status = require(*visible, "installed agent attempted to cross a blocked cell"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        position = nextPosition;
        if (evidence.agentFrames == 30)
        {
            auto blocker = grid->addBlocker({5, 1, 1, 1});
            if (!blocker) { return Core::failure(std::move(blocker.error())); }
            auto stale = field->sample(*grid, *startCell);
            if (auto status = require(!stale && stale.error().code == Navigation2DErrorCode::GridInvalidated,
                                      "installed flow field exposed a stale direction"); !status)
            {
                return Core::failure(std::move(status.error()));
            }
        }
    }
    if (auto status = require(agent->state() == NavigationAgentState2D::Arrived && Math::length(position - goal) <= 0.011F,
                              "installed agent did not reach its exact world goal"); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return evidence;
}

} // namespace

int main()
{
    auto result = exerciseInstalledNavigation();
    if (!result)
    {
        std::cerr << "navigation consumer failed: " << result.error().message << '\n';
        return 1;
    }
    std::cout << "{\"status\":\"ok\",\"consumer\":\"installed-tina-navigation2d\",\"pathCells\":"
              << result->pathCells << ",\"smoothedWaypoints\":" << result->smoothedWaypoints
              << ",\"agentFrames\":" << result->agentFrames << ",\"flowCost\":" << result->flowCost << "}\n";
    return 0;
}
