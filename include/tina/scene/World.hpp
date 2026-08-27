#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/scene/Camera2D.hpp>
#include <tina/scene/DirectionalLight3D.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PhysicsBody2D.hpp>
#include <tina/scene/PhysicsShape2D.hpp>
#include <tina/scene/PointLight2D.hpp>
#include <tina/scene/PointLight3D.hpp>
#include <tina/scene/ResourceBinding2D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/SkinnedMeshRenderer3D.hpp>
#include <tina/scene/ShadowOccluder2D.hpp>
#include <tina/scene/SpotLight3D.hpp>
#include <tina/scene/SpriteAnimationBinding2D.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>
#include <tina/scene/Transform.hpp>

#include <memory_resource>
#include <optional>
#include <span>

namespace Tina::Scene {

struct WorldConfig final {
    static constexpr usize DefaultEntityCapacity = 4096;
    static constexpr usize MaxEntityCapacity = 1'048'576;

    usize entityCapacity = DefaultEntityCapacity;
};

enum class ReparentMode : u8 {
    KeepWorld,
    KeepLocal,
};

[[nodiscard]] Core::Status validateWorldConfig(const WorldConfig& config) noexcept;

class World final {
public:
    [[nodiscard]] static Core::Result<World> Create(
        WorldConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~World() noexcept;

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept;
    World& operator=(World&&) = delete;

    [[nodiscard]] Core::Result<EntityId> createEntity(
        LocalTransform local = {});
    // Destroy only this entity by default. Direct children are reparented to
    // the root and keep their last published world transform.
    [[nodiscard]] Core::Status destroyEntity(EntityId entity) noexcept;

    // Explicit recursive destruction for callers that own the whole subtree.
    [[nodiscard]] Core::Status destroySubtree(EntityId entity) noexcept;

    // Hierarchy edits are owner-thread mutations. This standalone foundation
    // applies them immediately; updateWorldTransforms() is still the explicit
    // publication barrier used by the Runtime phase integration.
    [[nodiscard]] Core::Status setParent(
        EntityId child,
        std::optional<EntityId> parent,
        ReparentMode mode = ReparentMode::KeepWorld) noexcept;

    [[nodiscard]] Core::Status setLocalTransform(
        EntityId entity,
        LocalTransform local) noexcept;

    [[nodiscard]] Core::Status updateWorldTransforms() noexcept;

    // Optional POD component storage shares the entity slot capacity. Setting
    // replaces any previous value on that entity; clear removes the component.
    [[nodiscard]] Core::Status setCamera2D(EntityId entity, Camera2D camera) noexcept;
    [[nodiscard]] Core::Status clearCamera2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setSpriteRenderer2D(
        EntityId entity,
        SpriteRenderer2D sprite) noexcept;
    [[nodiscard]] Core::Status clearSpriteRenderer2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setPointLight2D(EntityId entity, PointLight2D light) noexcept;
    [[nodiscard]] Core::Status clearPointLight2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setShadowOccluder2D(
        EntityId entity,
        ShadowOccluder2D occluder) noexcept;
    [[nodiscard]] Core::Status clearShadowOccluder2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setSpriteAnimationBinding2D(
        EntityId entity,
        SpriteAnimationBinding2D binding) noexcept;
    [[nodiscard]] Core::Status clearSpriteAnimationBinding2D(EntityId entity) noexcept;
    // Authored physics and resource data. Scene stores and round-trips these but
    // never acts on them: no body is created and no resource is instantiated
    // here. Turning a body into a live one is the Gameplay2D bridge's job.
    [[nodiscard]] Core::Status setPhysicsBody2D(EntityId entity, PhysicsBody2D body) noexcept;
    [[nodiscard]] Core::Status clearPhysicsBody2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setPhysicsShape2D(EntityId entity, PhysicsShape2D shape) noexcept;
    [[nodiscard]] Core::Status clearPhysicsShape2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setResourceBinding2D(
        EntityId entity,
        ResourceBinding2D binding) noexcept;
    [[nodiscard]] Core::Status clearResourceBinding2D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setPerspectiveCamera3D(
        EntityId entity,
        PerspectiveCamera3D camera) noexcept;
    [[nodiscard]] Core::Status clearPerspectiveCamera3D(EntityId entity) noexcept;
    // Static and skinned mesh renderers are mutually exclusive. Setting either
    // renderer replaces and clears the other renderer on the same entity.
    [[nodiscard]] Core::Status setMeshRenderer3D(EntityId entity, MeshRenderer3D mesh) noexcept;
    [[nodiscard]] Core::Status clearMeshRenderer3D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setSkinnedMeshRenderer3D(
        EntityId entity,
        SkinnedMeshRenderer3D mesh) noexcept;
    [[nodiscard]] Core::Status clearSkinnedMeshRenderer3D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setDirectionalLight3D(
        EntityId entity,
        DirectionalLight3D light) noexcept;
    [[nodiscard]] Core::Status clearDirectionalLight3D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setPointLight3D(EntityId entity, PointLight3D light) noexcept;
    [[nodiscard]] Core::Status clearPointLight3D(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setSpotLight3D(EntityId entity, SpotLight3D light) noexcept;
    [[nodiscard]] Core::Status clearSpotLight3D(EntityId entity) noexcept;

    [[nodiscard]] bool contains(EntityId entity) const noexcept;
    [[nodiscard]] usize entityCount() const noexcept;
    [[nodiscard]] usize entityCapacity() const noexcept;
    [[nodiscard]] EntityId parent(EntityId entity) const noexcept;
    [[nodiscard]] const LocalTransform* localTransform(EntityId entity) const noexcept;
    [[nodiscard]] const WorldTransform* worldTransform(EntityId entity) const noexcept;
    [[nodiscard]] const Camera2D* camera2D(EntityId entity) const noexcept;
    [[nodiscard]] const SpriteRenderer2D* spriteRenderer2D(EntityId entity) const noexcept;
    [[nodiscard]] const PointLight2D* pointLight2D(EntityId entity) const noexcept;
    [[nodiscard]] const ShadowOccluder2D* shadowOccluder2D(EntityId entity) const noexcept;
    [[nodiscard]] const SpriteAnimationBinding2D* spriteAnimationBinding2D(EntityId entity) const noexcept;
    [[nodiscard]] const PhysicsBody2D* physicsBody2D(EntityId entity) const noexcept;
    [[nodiscard]] const PhysicsShape2D* physicsShape2D(EntityId entity) const noexcept;
    [[nodiscard]] const ResourceBinding2D* resourceBinding2D(EntityId entity) const noexcept;
    [[nodiscard]] const PerspectiveCamera3D* perspectiveCamera3D(EntityId entity) const noexcept;
    [[nodiscard]] const MeshRenderer3D* meshRenderer3D(EntityId entity) const noexcept;
    [[nodiscard]] const SkinnedMeshRenderer3D* skinnedMeshRenderer3D(EntityId entity) const noexcept;
    [[nodiscard]] const DirectionalLight3D* directionalLight3D(EntityId entity) const noexcept;
    [[nodiscard]] const PointLight3D* pointLight3D(EntityId entity) const noexcept;
    [[nodiscard]] const SpotLight3D* spotLight3D(EntityId entity) const noexcept;

    // Live entity ids in create-order-independent dense storage. Valid only on
    // the owner thread until the next structural mutation (create/destroy).
    [[nodiscard]] std::span<const EntityId> liveEntities() const noexcept;

private:
    struct EntityRecord;
    struct Impl;

    explicit World(Impl* impl) noexcept;

    [[nodiscard]] Core::Status validateEntity(
        EntityId entity) const noexcept;
    [[nodiscard]] EntityRecord* record(EntityId entity) noexcept;
    [[nodiscard]] const EntityRecord* record(EntityId entity) const noexcept;
    [[nodiscard]] Core::Status detachFromParent(EntityId entity) noexcept;
    [[nodiscard]] Core::Status eraseEntity(EntityId entity) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Scene
