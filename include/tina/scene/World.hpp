#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/scene/Camera2D.hpp>
#include <tina/scene/DirectionalLight3D.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/EntityMetadata.hpp>
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

#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

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

template <typename Component>
concept WorldReadableComponent =
    std::same_as<std::remove_cvref_t<Component>, EntityMetadata>
    || std::same_as<std::remove_cvref_t<Component>, LocalTransform>
    || std::same_as<std::remove_cvref_t<Component>, WorldTransform>
    || std::same_as<std::remove_cvref_t<Component>, Camera2D>
    || std::same_as<std::remove_cvref_t<Component>, SpriteRenderer2D>
    || std::same_as<std::remove_cvref_t<Component>, PointLight2D>
    || std::same_as<std::remove_cvref_t<Component>, ShadowOccluder2D>
    || std::same_as<std::remove_cvref_t<Component>, SpriteAnimationBinding2D>
    || std::same_as<std::remove_cvref_t<Component>, PhysicsBody2D>
    || std::same_as<std::remove_cvref_t<Component>, PhysicsShape2D>
    || std::same_as<std::remove_cvref_t<Component>, ResourceBinding2D>
    || std::same_as<std::remove_cvref_t<Component>, PerspectiveCamera3D>
    || std::same_as<std::remove_cvref_t<Component>, MeshRenderer3D>
    || std::same_as<std::remove_cvref_t<Component>, SkinnedMeshRenderer3D>
    || std::same_as<std::remove_cvref_t<Component>, DirectionalLight3D>
    || std::same_as<std::remove_cvref_t<Component>, PointLight3D>
    || std::same_as<std::remove_cvref_t<Component>, SpotLight3D>;

template <WorldReadableComponent... Components>
class WorldView;

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

    // Metadata is runtime-only and never interpreted by Scene. Name is copied
    // as strict UTF-8; tag/layer/group are opaque game-defined values.
    [[nodiscard]] Core::Status setMetadata(
        EntityId entity,
        EntityMetadataDesc metadata) noexcept;
    [[nodiscard]] Core::Status setRuntimeName(
        EntityId entity,
        std::string_view name) noexcept;
    [[nodiscard]] Core::Status clearRuntimeName(EntityId entity) noexcept;
    [[nodiscard]] Core::Status setTag(EntityId entity, EntityTag tag) noexcept;
    [[nodiscard]] Core::Status setLayer(EntityId entity, EntityLayer layer) noexcept;
    [[nodiscard]] Core::Status setGroup(EntityId entity, EntityGroup group) noexcept;

    [[nodiscard]] Core::Status setName(EntityId entity, std::string_view name) noexcept
    {
        return setRuntimeName(entity, name);
    }

    [[nodiscard]] Core::Status clearName(EntityId entity) noexcept
    {
        return clearRuntimeName(entity);
    }

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
    [[nodiscard]] const EntityMetadata* metadata(EntityId entity) const noexcept;
    [[nodiscard]] std::string_view runtimeName(EntityId entity) const noexcept;
    [[nodiscard]] EntityTag tag(EntityId entity) const noexcept;
    [[nodiscard]] EntityLayer layer(EntityId entity) const noexcept;
    [[nodiscard]] EntityGroup group(EntityId entity) const noexcept;

    [[nodiscard]] std::string_view name(EntityId entity) const noexcept
    {
        return runtimeName(entity);
    }

    // Read-only generic access over the closed component set above. Mutation
    // remains on the explicit set*/clear* API so World does not become an
    // arbitrary dynamic registry.
    template <WorldReadableComponent Component>
    [[nodiscard]] const std::remove_cvref_t<Component>* get(
        EntityId entity) const noexcept;

    template <WorldReadableComponent Component>
    [[nodiscard]] const std::remove_cvref_t<Component>* component(
        EntityId entity) const noexcept
    {
        return get<Component>(entity);
    }

    template <WorldReadableComponent Component>
    [[nodiscard]] bool has(EntityId entity) const noexcept
    {
        return get<Component>(entity) != nullptr;
    }

    template <WorldReadableComponent... Components>
        requires(sizeof...(Components) > 0)
    [[nodiscard]] WorldView<std::remove_cvref_t<Components>...> view() const noexcept;

    template <WorldReadableComponent... Components>
        requires(sizeof...(Components) > 0)
    [[nodiscard]] WorldView<std::remove_cvref_t<Components>...> query() const noexcept
    {
        return view<Components...>();
    }

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

// Lazy borrowed range of entities that contain every requested component.
// Iteration and each() are read-only. Any World mutation, move, or destruction
// invalidates the view and its iterators.
template <WorldReadableComponent... Components>
class WorldView final {
    static_assert(sizeof...(Components) > 0);

public:
    class Iterator final {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = EntityId;
        using difference_type = std::ptrdiff_t;
        using reference = EntityId;

        constexpr Iterator() noexcept = default;

        [[nodiscard]] EntityId operator*() const noexcept
        {
            return m_entities[m_index];
        }

        Iterator& operator++() noexcept
        {
            ++m_index;
            advanceToMatch();
            return *this;
        }

        Iterator operator++(int) noexcept
        {
            Iterator previous = *this;
            ++(*this);
            return previous;
        }

        friend bool operator==(const Iterator& left, const Iterator& right) noexcept
        {
            return left.m_world == right.m_world
                && left.m_entities.data() == right.m_entities.data()
                && left.m_entities.size() == right.m_entities.size()
                && left.m_index == right.m_index;
        }

    private:
        friend class WorldView;

        Iterator(
            const World* world,
            std::span<const EntityId> entities,
            usize index) noexcept
            : m_world(world), m_entities(entities), m_index(index)
        {
            advanceToMatch();
        }

        void advanceToMatch() noexcept
        {
            while (m_world != nullptr && m_index < m_entities.size()) {
                const EntityId entity = m_entities[m_index];
                if (((m_world->template get<Components>(entity) != nullptr) && ...)) {
                    return;
                }
                ++m_index;
            }
        }

        const World* m_world = nullptr;
        std::span<const EntityId> m_entities{};
        usize m_index = 0;
    };

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator{m_world, m_entities, 0};
    }

    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator{m_world, m_entities, m_entities.size()};
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return begin() == end();
    }

    [[nodiscard]] bool contains(EntityId entity) const noexcept
    {
        return m_world != nullptr && m_world->contains(entity)
            && ((m_world->template get<Components>(entity) != nullptr) && ...);
    }

    template <typename Function>
        requires std::invocable<Function&, EntityId, const Components&...>
    void each(Function&& function) const
    {
        for (const EntityId entity : *this) {
            std::invoke(
                function,
                entity,
                (*m_world->template get<Components>(entity))...);
        }
    }

private:
    friend class World;

    WorldView(const World& world, std::span<const EntityId> entities) noexcept
        : m_world(&world), m_entities(entities)
    {
    }

    const World* m_world = nullptr;
    std::span<const EntityId> m_entities{};
};

template <WorldReadableComponent Component>
const std::remove_cvref_t<Component>* World::get(EntityId entity) const noexcept
{
    using Value = std::remove_cvref_t<Component>;
    if constexpr (std::same_as<Value, EntityMetadata>) {
        return metadata(entity);
    } else if constexpr (std::same_as<Value, LocalTransform>) {
        return localTransform(entity);
    } else if constexpr (std::same_as<Value, WorldTransform>) {
        return worldTransform(entity);
    } else if constexpr (std::same_as<Value, Camera2D>) {
        return camera2D(entity);
    } else if constexpr (std::same_as<Value, SpriteRenderer2D>) {
        return spriteRenderer2D(entity);
    } else if constexpr (std::same_as<Value, PointLight2D>) {
        return pointLight2D(entity);
    } else if constexpr (std::same_as<Value, ShadowOccluder2D>) {
        return shadowOccluder2D(entity);
    } else if constexpr (std::same_as<Value, SpriteAnimationBinding2D>) {
        return spriteAnimationBinding2D(entity);
    } else if constexpr (std::same_as<Value, PhysicsBody2D>) {
        return physicsBody2D(entity);
    } else if constexpr (std::same_as<Value, PhysicsShape2D>) {
        return physicsShape2D(entity);
    } else if constexpr (std::same_as<Value, ResourceBinding2D>) {
        return resourceBinding2D(entity);
    } else if constexpr (std::same_as<Value, PerspectiveCamera3D>) {
        return perspectiveCamera3D(entity);
    } else if constexpr (std::same_as<Value, MeshRenderer3D>) {
        return meshRenderer3D(entity);
    } else if constexpr (std::same_as<Value, SkinnedMeshRenderer3D>) {
        return skinnedMeshRenderer3D(entity);
    } else if constexpr (std::same_as<Value, DirectionalLight3D>) {
        return directionalLight3D(entity);
    } else if constexpr (std::same_as<Value, PointLight3D>) {
        return pointLight3D(entity);
    } else if constexpr (std::same_as<Value, SpotLight3D>) {
        return spotLight3D(entity);
    }
}

template <WorldReadableComponent... Components>
    requires(sizeof...(Components) > 0)
WorldView<std::remove_cvref_t<Components>...> World::view() const noexcept
{
    return WorldView<std::remove_cvref_t<Components>...>{*this, liveEntities()};
}

} // namespace Tina::Scene
