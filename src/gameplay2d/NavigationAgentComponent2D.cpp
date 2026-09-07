#include <tina/gameplay2d/NavigationAgentComponent2D.hpp>

#include <cmath>
#include <utility>

namespace Tina::Gameplay2D {
Core::Result<NavigationAgentComponent2D> NavigationAgentComponent2D::Create(
    Scene::World& world, Scene::EntityId entity, Navigation2D::NavigationGrid2D& grid,
    NavigationAgentComponent2DConfig config, std::pmr::memory_resource& resource)
{
    if (!world.contains(entity) || !grid)
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "navigation component requires a live entity and grid");
    auto agent = Navigation2D::NavigationAgent2D::Create(config.agent, resource);
    if (!agent) return Core::failure(std::move(agent.error()));
    return NavigationAgentComponent2D(world, entity, grid, std::move(*agent), config.authority);
}

Core::Result<Math::Vec2> NavigationAgentComponent2D::position() const noexcept
{
    if (m_world == nullptr || !m_world->contains(m_entity))
        return Core::failure(Scene::SceneErrorCode::InvalidEntity, "navigation component entity is stale");
    const auto* transform = m_world->worldTransform(m_entity);
    if (transform == nullptr || !Math::isFinite(transform->position))
        return Core::failure(Scene::SceneErrorCode::InvalidTransform, "navigation component requires a published finite transform");
    return Math::Vec2{transform->position.x, transform->position.y};
}

Core::Status NavigationAgentComponent2D::setGoal(Math::Vec2 goal,
                                                 Navigation2D::NavigationPathQueryOptions options)
{
    auto current = position();
    if (!current) return Core::failure(std::move(current.error()));
    return m_agent.setGoal(*m_grid, *current, goal, options);
}

Core::Result<Navigation2D::NavigationAgentSteering2D> NavigationAgentComponent2D::update(
    float deltaSeconds, Core::usize expansionBudget)
{
    auto current = position();
    if (!current) return Core::failure(std::move(current.error()));
    return m_agent.update(*m_grid, *current, deltaSeconds, expansionBudget);
}

Core::Status NavigationAgentComponent2D::apply(float deltaSeconds, Core::usize expansionBudget)
{
    auto steering = update(deltaSeconds, expansionBudget);
    if (!steering) return Core::failure(std::move(steering.error()));
    if (m_authority != NavigationTransformAuthority2D::Transform)
        return Core::success();
    if (m_world->physicsBody2D(m_entity) != nullptr)
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "navigation transform authority conflicts with PhysicsBody2D");
    if (m_world->parent(m_entity))
        return Core::failure(Scene::SceneErrorCode::UnsupportedTransformComposition,
                             "navigation transform authority requires a root entity");
    auto local = m_world->localTransform(m_entity);
    if (local == nullptr) return Core::failure(Scene::SceneErrorCode::InvalidTransform, "navigation component local transform is unavailable");
    auto next = *local;
    next.position.x += steering->desiredVelocity.x * deltaSeconds;
    next.position.y += steering->desiredVelocity.y * deltaSeconds;
    if (!Math::isFinite(next.position)) return Core::failure(Scene::SceneErrorCode::TransformOverflow, "navigation component transform overflow");
    if (auto status = m_world->setLocalTransform(m_entity, next); !status)
        return status;
    return m_world->updateWorldTransforms();
}
} // namespace Tina::Gameplay2D
