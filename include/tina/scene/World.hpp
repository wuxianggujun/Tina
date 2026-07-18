#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/Transform.hpp>

#include <memory_resource>
#include <optional>

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

    [[nodiscard]] bool contains(EntityId entity) const noexcept;
    [[nodiscard]] usize entityCount() const noexcept;
    [[nodiscard]] usize entityCapacity() const noexcept;
    [[nodiscard]] EntityId parent(EntityId entity) const noexcept;
    [[nodiscard]] const LocalTransform* localTransform(EntityId entity) const noexcept;
    [[nodiscard]] const WorldTransform* worldTransform(EntityId entity) const noexcept;

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
