#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/math/Vec.hpp>
#include <tina/navigation2d/NavigationAgent2D.hpp>
#include <tina/scene/World.hpp>

namespace Tina::Gameplay2D {

enum class NavigationTransformAuthority2D : Core::u8 {
    ExternalVelocity,
    Transform,
};

struct NavigationAgentComponent2DConfig final {
    Navigation2D::NavigationAgent2DConfig agent{};
    NavigationTransformAuthority2D authority = NavigationTransformAuthority2D::ExternalVelocity;
};

// Gameplay-owned entity adapter. The navigation module remains Scene/Physics
// agnostic: this object reads the entity's published world position and returns
// steering, while Transform authority is only allowed when no PhysicsBody2D is
// present. The grid is borrowed and must outlive this component.
class NavigationAgentComponent2D final {
public:
    [[nodiscard]] static Core::Result<NavigationAgentComponent2D> Create(
        Scene::World& world, Scene::EntityId entity, Navigation2D::NavigationGrid2D& grid,
        NavigationAgentComponent2DConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~NavigationAgentComponent2D() noexcept = default;
    NavigationAgentComponent2D(const NavigationAgentComponent2D&) = delete;
    NavigationAgentComponent2D& operator=(const NavigationAgentComponent2D&) = delete;
    NavigationAgentComponent2D(NavigationAgentComponent2D&&) noexcept = default;
    NavigationAgentComponent2D& operator=(NavigationAgentComponent2D&&) = delete;

    [[nodiscard]] Core::Status setGoal(Math::Vec2 goal,
                                       Navigation2D::NavigationPathQueryOptions options = {});
    [[nodiscard]] Core::Result<Navigation2D::NavigationAgentSteering2D> update(
        float deltaSeconds, Core::usize expansionBudget);
    // Applies steering only for a root entity without PhysicsBody2D. A parented
    // entity or physics-owned entity returns Unsupported, avoiding world/local
    // feedback loops and ambiguous transform authority.
    [[nodiscard]] Core::Status apply(float deltaSeconds,
                                     Core::usize expansionBudget);
    void cancel() noexcept { m_agent.cancel(); }
    void reset() noexcept { m_agent.reset(); }

    [[nodiscard]] Scene::EntityId entity() const noexcept { return m_entity; }
    [[nodiscard]] Navigation2D::NavigationAgentState2D state() const noexcept { return m_agent.state(); }
    [[nodiscard]] std::span<const Math::Vec2> path() const noexcept { return m_agent.path(); }
    [[nodiscard]] const Navigation2D::NavigationAgent2D& agent() const noexcept { return m_agent; }

private:
    NavigationAgentComponent2D(Scene::World& world, Scene::EntityId entity,
                               Navigation2D::NavigationGrid2D& grid,
                               Navigation2D::NavigationAgent2D agent,
                               NavigationTransformAuthority2D authority) noexcept
        : m_world(&world), m_entity(entity), m_grid(&grid), m_agent(std::move(agent)), m_authority(authority) {}

    [[nodiscard]] Core::Result<Math::Vec2> position() const noexcept;
    Scene::World* m_world = nullptr;
    Scene::EntityId m_entity{};
    Navigation2D::NavigationGrid2D* m_grid = nullptr;
    Navigation2D::NavigationAgent2D m_agent;
    NavigationTransformAuthority2D m_authority = NavigationTransformAuthority2D::ExternalVelocity;
};

} // namespace Tina::Gameplay2D
