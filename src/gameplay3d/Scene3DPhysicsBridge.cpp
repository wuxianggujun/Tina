#include <tina/gameplay3d/Scene3DPhysicsBridge.hpp>
#include <tina/gameplay3d/World3DSceneIndex.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <cmath>
#include <exception>
#include <new>
#include <vector>

namespace Tina::Gameplay3D {
namespace {
bool unitScale(Math::Vec3 scale) noexcept
{
    return Math::largestComponent(Math::absolute(scale - Math::Vec3{1, 1, 1})) <= 0.00001F;
}
}

struct Scene3DPhysicsBridge::Impl final {
    struct Binding final {
        Scene::EntityId entity{};
        Core::i32 parent = -1;
        Physics3D::PhysicsBodyId body{};
        bool physicsAuthority = false;
        Scene::LocalTransform local{};
        Scene::WorldTransform projected{};
    };
    Physics3D::PhysicsWorld3D* physics = nullptr;
    std::vector<Binding> bindings;
    bool faulted = false;
    ~Impl() noexcept
    {
        if (physics == nullptr || !physics->isOpen()) return;
        for (auto it = bindings.rbegin(); it != bindings.rend(); ++it)
            if (it->body && !physics->destroyBody(it->body)) std::terminate();
    }
};

Scene3DPhysicsBridge::Scene3DPhysicsBridge() noexcept = default;
Scene3DPhysicsBridge::~Scene3DPhysicsBridge() noexcept = default;

Core::Status Scene3DPhysicsBridge::build(Scene::World& world, const AssetFormat::PrefabPayloadView& prefab,
    std::span<const Scene::EntityId> entities, Physics3D::PhysicsWorld3D& physics)
try
{
    if (m_impl) return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D physics bridge is already built");
    World3DSceneIndex validation;
    if (auto status = validation.build(world, prefab, entities); !status) return status;
    if (auto status = world.updateWorldTransforms(); !status) return status;
    auto candidate = std::make_unique<Impl>();
    candidate->physics = &physics;
    candidate->bindings.reserve(entities.size());
    for (Core::usize index = 0; index < entities.size(); ++index)
    {
        const auto& node = prefab.nodes[index];
        candidate->bindings.push_back({.entity = entities[index], .parent = node.parentIndex});
        auto& binding = candidate->bindings.back();
        if (!node.physics) continue;
        const auto& authored = *node.physics;
        const auto* transform = world.worldTransform(binding.entity);
        if (transform == nullptr || !unitScale(transform->scale))
            return Core::failure(Scene::SceneErrorCode::InvalidTransform, "Scene3D physics nodes require unit world scale");
        Core::Result<Physics3D::PhysicsBodyId> created = Core::failure(Scene::SceneErrorCode::InvalidComponent,
                                                                    "Scene3D invalid physics type");
        if (authored.type == AssetFormat::PrefabPhysicsBody3D::Character)
        {
            created = physics.createCharacter({.positionMeters = transform->position,
                .rotation = Math::normalized(transform->rotation),
                .radiusMeters = authored.radiusMeters, .halfHeightMeters = authored.halfHeightMeters,
                .maximumSlopeRadians = authored.maximumSlopeRadians, .stepHeightMeters = authored.stepHeightMeters,
                .floorSnapMeters = authored.floorSnapMeters, .massKilograms = authored.massKilograms});
        } else
        {
            Physics3D::PhysicsBody3DDesc desc{.shape = {
                .halfExtentsMeters = {authored.halfExtentX, authored.halfExtentY, authored.halfExtentZ},
                .radiusMeters = authored.radiusMeters, .halfHeightMeters = authored.halfHeightMeters},
                .positionMeters = transform->position, .rotation = Math::normalized(transform->rotation),
                .massKilograms = authored.massKilograms, .friction = authored.friction,
                .restitution = authored.restitution, .sensor = authored.sensor};
            switch (authored.type)
            {
            case AssetFormat::PrefabPhysicsBody3D::Static: desc.type = Physics3D::PhysicsBodyType3D::Static; break;
            case AssetFormat::PrefabPhysicsBody3D::Kinematic: desc.type = Physics3D::PhysicsBodyType3D::Kinematic; break;
            case AssetFormat::PrefabPhysicsBody3D::Dynamic: desc.type = Physics3D::PhysicsBodyType3D::Dynamic; break;
            default: return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D invalid body type");
            }
            switch (authored.shape)
            {
            case AssetFormat::PrefabPhysicsShape3D::Box: desc.shape.kind = Physics3D::PhysicsShapeKind3D::Box; break;
            case AssetFormat::PrefabPhysicsShape3D::Sphere: desc.shape.kind = Physics3D::PhysicsShapeKind3D::Sphere; break;
            case AssetFormat::PrefabPhysicsShape3D::Capsule: desc.shape.kind = Physics3D::PhysicsShapeKind3D::Capsule; break;
            default: return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D invalid shape kind");
            }
            created = physics.createBody(desc);
        }
        if (!created) return Core::failure(std::move(created.error()));
        binding.body = *created;
        binding.physicsAuthority = authored.type == AssetFormat::PrefabPhysicsBody3D::Dynamic ||
                                   authored.type == AssetFormat::PrefabPhysicsBody3D::Character;
    }
    m_impl = std::move(candidate);
    return Core::success();
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Scene3D physics bridge allocation failed");
}

Core::Status Scene3DPhysicsBridge::fixedUpdate(Scene::World& world)
{
    if (!m_impl || m_impl->faulted)
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D physics bridge is closed or faulted");
    if (auto status = world.updateWorldTransforms(); !status) return status;
    for (const auto& binding : m_impl->bindings)
    {
        const auto parent = binding.parent < 0 ? Scene::EntityId{} : m_impl->bindings[binding.parent].entity;
        if (!world.contains(binding.entity) || world.parent(binding.entity) != parent ||
            (binding.body && !unitScale(world.worldTransform(binding.entity)->scale)))
            return Core::failure(Scene::SceneErrorCode::CorruptHierarchy,
                                 "Scene3D physics instance was structurally edited; rebuild before stepping");
    }
    auto quarantine = Core::makeScopeExit([&]() noexcept { m_impl->faulted = true; });
    for (const auto& binding : m_impl->bindings)
    {
        if (!binding.body || binding.physicsAuthority) continue;
        const auto& transform = *world.worldTransform(binding.entity);
        if (auto status = m_impl->physics->setTransform(binding.body, transform.position,
                                                       Math::normalized(transform.rotation)); !status) return status;
    }
    if (auto status = m_impl->physics->step(); !status) return status;
    // Predict every parent first, including nonphysical intermediary nodes, before
    // writing any local transform. A dynamic parent never uses last frame's pose.
    for (auto& binding : m_impl->bindings)
    {
        binding.local = *world.localTransform(binding.entity);
        const auto parent = binding.parent < 0 ? Scene::WorldTransform{} : m_impl->bindings[binding.parent].projected;
        if (binding.body && binding.physicsAuthority)
        {
            const auto body = m_impl->physics->bodyState(binding.body);
            if (!body) return Core::failure(body.error());
            if (!Scene::isUniformScale(parent.scale) || parent.scale.x <= 0.00001F)
                return Core::failure(Scene::SceneErrorCode::InvalidTransform, "Scene3D physical parent requires positive uniform scale");
            const auto inverse = Math::conjugate(Math::normalized(parent.rotation));
            binding.local.position = Math::rotate(inverse, body->positionMeters - parent.position) / parent.scale.x;
            binding.local.rotation = Math::normalized(inverse * body->rotation);
        }
        if (!Scene::tryCompose(parent, binding.local, binding.projected))
            return Core::failure(Scene::SceneErrorCode::InvalidTransform, "Scene3D physics result cannot be represented as TRS");
    }
    for (const auto& binding : m_impl->bindings)
        if (binding.physicsAuthority)
            if (auto status = world.setLocalTransform(binding.entity, binding.local); !status) return status;
    if (auto status = world.updateWorldTransforms(); !status) return status;
    quarantine.release();
    return Core::success();
}

Physics3D::PhysicsBodyId Scene3DPhysicsBridge::body(Scene::EntityId entity) const noexcept
{
    if (m_impl) for (const auto& binding : m_impl->bindings) if (binding.entity == entity) return binding.body;
    return {};
}
Scene::EntityId Scene3DPhysicsBridge::entity(Physics3D::PhysicsBodyId body) const noexcept
{
    if (body && m_impl) for (const auto& binding : m_impl->bindings) if (binding.body == body) return binding.entity;
    return {};
}
Core::Status Scene3DPhysicsBridge::shutdown() noexcept
{
    if (!m_impl) return Core::success();
    if (m_impl->physics->isOpen())
        for (auto it = m_impl->bindings.rbegin(); it != m_impl->bindings.rend(); ++it)
            if (it->body)
            {
                if (auto status = m_impl->physics->destroyBody(it->body); !status) return status;
                it->body = {};
            }
    m_impl.reset();
    return Core::success();
}

} // namespace Tina::Gameplay3D
