#include <tina/scene/World.hpp>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Scene {

struct World::EntityRecord final {
    EntityMetadata metadata{};
    LocalTransform local{};
    WorldTransform world{};
    EntityId parent{};
    EntityId firstChild{};
    EntityId nextSibling{};
    bool hasCamera2D = false;
    Camera2D camera2D{};
    bool hasSpriteRenderer2D = false;
    SpriteRenderer2D spriteRenderer2D{};
    bool hasPointLight2D = false;
    PointLight2D pointLight2D{};
    bool hasPhysicsBody2D = false;
    PhysicsBody2D physicsBody2D{};
    bool hasPhysicsShape2D = false;
    PhysicsShape2D physicsShape2D{};
    bool hasResourceBinding2D = false;
    ResourceBinding2D resourceBinding2D{};
    bool hasShadowOccluder2D = false;
    ShadowOccluder2D shadowOccluder2D{};
    bool hasSpriteAnimationBinding2D = false;
    SpriteAnimationBinding2D spriteAnimationBinding2D{};
    bool hasPerspectiveCamera3D = false;
    PerspectiveCamera3D perspectiveCamera3D{};
    bool hasMeshRenderer3D = false;
    MeshRenderer3D meshRenderer3D{};
    bool hasSkinnedMeshRenderer3D = false;
    SkinnedMeshRenderer3D skinnedMeshRenderer3D{};
    bool hasDirectionalLight3D = false;
    DirectionalLight3D directionalLight3D{};
    bool hasPointLight3D = false;
    PointLight3D pointLight3D{};
    bool hasSpotLight3D = false;
    SpotLight3D spotLight3D{};
};

struct World::Impl final {
    using EntityPool = Core::GenerationPool<EntityRecord, Detail::EntityRegistryTag>;

    Impl(
        WorldConfig worldConfig,
        EntityPool entityPool,
        std::pmr::memory_resource& memoryResource)
        : config(worldConfig),
          resource(&memoryResource),
          entities(std::move(entityPool)),
          liveEntities(&memoryResource),
          liveIndexByEntityIndex(&memoryResource),
          traversalStack(&memoryResource),
          destroyStack(&memoryResource),
          visitedByIndex(&memoryResource),
          worldScratch(&memoryResource),
          computedByIndex(&memoryResource)
    {
        liveEntities.reserve(config.entityCapacity);
        liveIndexByEntityIndex.resize(config.entityCapacity, InvalidLiveIndex);
        traversalStack.reserve(config.entityCapacity);
        destroyStack.reserve(config.entityCapacity);
        visitedByIndex.resize(config.entityCapacity, 0);
        worldScratch.resize(config.entityCapacity);
        computedByIndex.resize(config.entityCapacity, 0);
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return ownerThread == std::this_thread::get_id();
    }

    WorldConfig config{};
    static constexpr u32 InvalidLiveIndex = (std::numeric_limits<u32>::max)();
    std::thread::id ownerThread = std::this_thread::get_id();
    std::pmr::memory_resource* resource = nullptr;
    EntityPool entities;
    std::pmr::vector<EntityId> liveEntities;
    std::pmr::vector<u32> liveIndexByEntityIndex;
    std::pmr::vector<EntityId> traversalStack;
    std::pmr::vector<EntityId> destroyStack;
    std::pmr::vector<u8> visitedByIndex;
    std::pmr::vector<WorldTransform> worldScratch;
    std::pmr::vector<u8> computedByIndex;
    bool worldDirty = false;
};

namespace {

[[nodiscard]] Core::Result<EntityName> makeEntityName(
    std::string_view name) noexcept
{
    if (name.size() > EntityNameMaximumBytes) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Scene runtime entity name exceeds fixed UTF-8 byte capacity");
    }
    if (!Core::isStrictUtf8WithoutNul(name)) {
        return Core::failure(
            SceneErrorCode::InvalidMetadata,
            "Scene runtime entity name must be strict UTF-8 without embedded NUL");
    }

    EntityName result{};
    std::copy(name.begin(), name.end(), result.bytes.begin());
    result.size = static_cast<u8>(name.size());
    return result;
}

[[nodiscard]] Core::Status invalidTransformStatus() noexcept
{
    return Core::failure(
        SceneErrorCode::InvalidTransform,
        "Scene transform contains non-finite values, overflow, or a zero quaternion");
}

[[nodiscard]] bool hasInvertibleScale(Math::Vec3 scale) noexcept
{
    constexpr float MinimumScaleMagnitude = 1.0e-12F;
    return std::abs(scale.x) > MinimumScaleMagnitude
        && std::abs(scale.y) > MinimumScaleMagnitude
        && std::abs(scale.z) > MinimumScaleMagnitude;
}

[[nodiscard]] bool nearlyEqual(float left, float right) noexcept
{
    const float scale = std::max({1.0F, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= 2.0e-4F * scale;
}

[[nodiscard]] bool equivalent(WorldTransform left, WorldTransform right) noexcept
{
    const bool sameRotation = nearlyEqual(left.rotation.x, right.rotation.x)
        && nearlyEqual(left.rotation.y, right.rotation.y)
        && nearlyEqual(left.rotation.z, right.rotation.z)
        && nearlyEqual(left.rotation.w, right.rotation.w);
    const bool oppositeRotation = nearlyEqual(left.rotation.x, -right.rotation.x)
        && nearlyEqual(left.rotation.y, -right.rotation.y)
        && nearlyEqual(left.rotation.z, -right.rotation.z)
        && nearlyEqual(left.rotation.w, -right.rotation.w);
    return nearlyEqual(left.position.x, right.position.x)
        && nearlyEqual(left.position.y, right.position.y)
        && nearlyEqual(left.position.z, right.position.z)
        && (sameRotation || oppositeRotation)
        && nearlyEqual(left.scale.x, right.scale.x)
        && nearlyEqual(left.scale.y, right.scale.y)
        && nearlyEqual(left.scale.z, right.scale.z);
}

[[nodiscard]] Core::Result<LocalTransform> deriveLocalTransform(
    WorldTransform world,
    const WorldTransform* parent) noexcept
{
    if (!isValid(world)) {
        return Core::failure(
            SceneErrorCode::InvalidTransform,
            "Scene world transform is not finite or has a zero quaternion");
    }

    world.rotation = Math::normalized(world.rotation);
    if (parent == nullptr) {
        return LocalTransform{world.position, world.rotation, world.scale};
    }
    if (!isValid(*parent) || !hasInvertibleScale(parent->scale)) {
        return Core::failure(
            SceneErrorCode::InvalidTransform,
            "Scene parent transform cannot preserve a child world transform");
    }

    const Math::Vec3 relative = world.position - parent->position;
    const Math::Vec3 unrotated =
        Math::rotate(Math::conjugate(parent->rotation), relative);
    LocalTransform local{
        {unrotated.x / parent->scale.x,
            unrotated.y / parent->scale.y,
            unrotated.z / parent->scale.z},
        Math::normalized(Math::conjugate(parent->rotation) * world.rotation),
        {world.scale.x / parent->scale.x,
            world.scale.y / parent->scale.y,
            world.scale.z / parent->scale.z}};
    if (!isValid(local)) {
        return Core::failure(
            SceneErrorCode::InvalidTransform,
            "Scene reparent produced an invalid local transform");
    }
    if (!supportsTrsComposition(*parent, local)) {
        return Core::failure(
            SceneErrorCode::UnsupportedTransformComposition,
            "Scene reparent would require an affine transform for non-uniform scale and rotation");
    }
    WorldTransform recomposed{};
    if (!tryCompose(*parent, local, recomposed) || !equivalent(recomposed, world)) {
        return Core::failure(
            SceneErrorCode::UnsupportedTransformComposition,
            "Scene reparent cannot preserve the requested world transform as TRS");
    }
    return local;
}

} // namespace

Core::Status validateWorldConfig(const WorldConfig& config) noexcept
{
    if (config.entityCapacity == 0) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Scene World entity capacity must be greater than zero");
    }
    if (config.entityCapacity > WorldConfig::MaxEntityCapacity
        || config.entityCapacity > EntityId::InvalidIndex) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Scene World entity capacity exceeds the generation index range");
    }
    return Core::success();
}

Core::Result<World> World::Create(
    WorldConfig config,
    std::pmr::memory_resource& resource)
{
    if (const Core::Status status = validateWorldConfig(config); !status) {
        return Core::failure(status.error());
    }

    auto entityPoolResult = Impl::EntityPool::Create(config.entityCapacity, resource);
    if (!entityPoolResult) {
        return Core::failure(std::move(entityPoolResult.error()).withContext(
            "World::Create", "entity registry allocation"));
    }

    void* storage = nullptr;
    try {
        storage = resource.allocate(sizeof(Impl), alignof(Impl));
        auto* impl = std::construct_at(
            static_cast<Impl*>(storage),
            config,
            std::move(*entityPoolResult),
            resource);
        return World(impl);
    } catch (const std::bad_alloc&) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Scene World fixed storage allocation failed");
    } catch (const std::exception& exception) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        return Core::failure(Core::Error{
            SceneErrorCode::ConstructionFailed, exception.what()});
    } catch (...) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        return Core::failure(Core::Error{
            SceneErrorCode::ConstructionFailed,
            "Scene World construction failed with an unknown exception"});
    }
}

World::World(Impl* impl) noexcept
    : m_impl(impl)
{
}

World::~World() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    if (!m_impl->isOwnerThread()) {
        std::terminate();
    }
    std::pmr::memory_resource* resource = m_impl->resource;
    std::destroy_at(m_impl);
    resource->deallocate(m_impl, sizeof(Impl), alignof(Impl));
    m_impl = nullptr;
}

World::World(World&& other) noexcept
    : m_impl(std::exchange(other.m_impl, nullptr))
{
    if (m_impl != nullptr) {
        m_impl->ownerThread = std::this_thread::get_id();
    }
}

Core::Status World::validateEntity(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !entity.hasValue()) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene entity id is empty");
    }
    if (entity.owner() != m_impl->entities.owner()) {
        return Core::failure(
            SceneErrorCode::WrongWorld,
            "Scene entity belongs to another World");
    }
    if (!m_impl->entities.contains(entity)) {
        return Core::failure(
            SceneErrorCode::StaleEntity,
            "Scene entity generation is stale or already destroyed");
    }
    return Core::success();
}

World::EntityRecord* World::record(EntityId entity) noexcept
{
    return m_impl == nullptr ? nullptr : m_impl->entities.tryGet(entity);
}

const World::EntityRecord* World::record(EntityId entity) const noexcept
{
    return m_impl == nullptr ? nullptr : m_impl->entities.tryGet(entity);
}

Core::Status World::detachFromParent(EntityId entity) noexcept
{
    EntityRecord* childRecord = record(entity);
    if (childRecord == nullptr || !childRecord->parent.hasValue()) {
        return Core::success();
    }

    EntityRecord* parentRecord = record(childRecord->parent);
    if (parentRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene child points at an invalid parent");
    }

    EntityId* link = &parentRecord->firstChild;
    usize steps = 0;
    while (link->hasValue() && *link != entity) {
        EntityRecord* siblingRecord = record(*link);
        if (siblingRecord == nullptr || ++steps > m_impl->config.entityCapacity) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene sibling chain is invalid");
        }
        link = &siblingRecord->nextSibling;
    }
    if (!link->hasValue()) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene parent does not contain its child");
    }
    *link = childRecord->nextSibling;
    childRecord->parent = {};
    childRecord->nextSibling = {};
    return Core::success();
}

Core::Status World::eraseEntity(EntityId entity) noexcept
{
    if (m_impl == nullptr || entity.index() >= m_impl->liveIndexByEntityIndex.size()) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity index is outside the live registry");
    }
    const u32 liveIndex = m_impl->liveIndexByEntityIndex[entity.index()];
    if (liveIndex == Impl::InvalidLiveIndex
        || liveIndex >= m_impl->liveEntities.size()
        || m_impl->liveEntities[liveIndex] != entity) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene live registry lost an entity");
    }

    if (m_impl->entities.erase(entity) != Core::GenerationEraseResult::Erased) {
        return Core::failure(
            SceneErrorCode::StaleEntity,
            "Scene entity could not be erased");
    }

    const EntityId moved = m_impl->liveEntities.back();
    m_impl->liveEntities[liveIndex] = moved;
    m_impl->liveEntities.pop_back();
    m_impl->liveIndexByEntityIndex[entity.index()] = Impl::InvalidLiveIndex;
    if (moved != entity) {
        m_impl->liveIndexByEntityIndex[moved.index()] = liveIndex;
    }
    return Core::success();
}

Core::Result<EntityId> World::createEntity(LocalTransform local)
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (!isValid(local)) {
        return Core::failure(
            SceneErrorCode::InvalidTransform,
            "Scene transform contains non-finite values or a zero quaternion");
    }
    local.rotation = Math::normalized(local.rotation);

    auto idResult = m_impl->entities.tryEmplace();
    if (!idResult) {
        Core::Error error = std::move(idResult.error()).withContext(
            "World::createEntity", "entity registry capacity");
        if (error.code == Core::CoreErrorCode::CapacityExceeded) {
            error.code = SceneErrorCode::CapacityExceeded;
        }
        return Core::failure(std::move(error));
    }
    const EntityId entity = *idResult;
    EntityRecord* entityRecord = m_impl->entities.tryGet(entity);
    if (entityRecord == nullptr || entity.index() >= m_impl->liveIndexByEntityIndex.size()) {
        (void)m_impl->entities.erase(entity);
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Newly created Scene entity could not be resolved");
    }
    entityRecord->local = local;
    entityRecord->world = toWorld(local);
    try {
        m_impl->liveEntities.push_back(entity);
    } catch (const std::bad_alloc&) {
        (void)m_impl->entities.erase(entity);
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Scene live entity storage is exhausted");
    }
    m_impl->liveIndexByEntityIndex[entity.index()] = static_cast<u32>(
        m_impl->liveEntities.size() - 1);
    m_impl->worldScratch[entity.index()] = entityRecord->world;
    m_impl->computedByIndex[entity.index()] = 1;
    return entity;
}

Core::Status World::setLocalTransform(
    EntityId entity,
    LocalTransform local) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(local)) {
        return invalidTransformStatus();
    }
    local.rotation = Math::normalized(local.rotation);
    record(entity)->local = local;
    m_impl->worldDirty = true;
    return Core::success();
}

Core::Status World::setParent(
    EntityId child,
    std::optional<EntityId> parentId,
    ReparentMode mode) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(child); !status) {
        return status;
    }
    EntityRecord* childRecord = record(child);
    if (childRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene child could not be resolved");
    }

    EntityRecord* newParentRecord = nullptr;
    if (parentId.has_value()) {
        if (const Core::Status status = validateEntity(*parentId); !status) {
            return status;
        }
        if (child == *parentId) {
            return Core::failure(
                SceneErrorCode::HierarchyCycle,
                "Scene entity cannot parent itself");
        }
        newParentRecord = record(*parentId);
        if (newParentRecord == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene new parent could not be resolved");
        }

        // Search the candidate child's subtree instead of walking the new
        // parent's ancestors. The walk is iterative and bounded by the fixed
        // World capacity.
        m_impl->traversalStack.clear();
        m_impl->traversalStack.push_back(child);
        usize visited = 0;
        while (!m_impl->traversalStack.empty()) {
            const EntityId current = m_impl->traversalStack.back();
            m_impl->traversalStack.pop_back();
            if (current == *parentId) {
                return Core::failure(
                    SceneErrorCode::HierarchyCycle,
                    "Scene reparent would create a hierarchy cycle");
            }
            const EntityRecord* currentRecord = record(current);
            if (currentRecord == nullptr) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene child subtree contains an invalid entity");
            }
            if (++visited > m_impl->config.entityCapacity) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene child subtree exceeded World capacity");
            }
            EntityId descendant = currentRecord->firstChild;
            usize siblingCount = 0;
            while (descendant.hasValue()) {
                if (++siblingCount > m_impl->config.entityCapacity
                    || m_impl->traversalStack.size() >= m_impl->config.entityCapacity) {
                    return Core::failure(
                        SceneErrorCode::CorruptHierarchy,
                        "Scene child subtree exceeded fixed storage");
                }
                const EntityRecord* descendantRecord = record(descendant);
                if (descendantRecord == nullptr || descendantRecord->parent != current) {
                    return Core::failure(
                        SceneErrorCode::CorruptHierarchy,
                        "Scene child subtree has an invalid sibling link");
                }
                m_impl->traversalStack.push_back(descendant);
                descendant = descendantRecord->nextSibling;
            }
        }
    }

    const EntityId oldParent = childRecord->parent;
    const EntityId newParent = parentId.value_or(EntityId{});
    if (oldParent == newParent) {
        return Core::success();
    }

    LocalTransform replacementLocal = childRecord->local;
    if (mode == ReparentMode::KeepWorld) {
        // Reparenting must preserve the current world pose, so publish pending
        // local edits before deriving the new local transform.
        if (m_impl->worldDirty) {
            if (const Core::Status status = updateWorldTransforms(); !status) {
                return status;
            }
        }
        childRecord = record(child);
        auto localResult = deriveLocalTransform(
            childRecord->world,
            newParentRecord == nullptr ? nullptr : &newParentRecord->world);
        if (!localResult) {
            return Core::failure(std::move(localResult.error()));
        }
        replacementLocal = *localResult;
    }

    if (const Core::Status status = detachFromParent(child); !status) {
        return status;
    }

    childRecord->parent = newParent;
    childRecord->nextSibling = {};
    if (newParent.hasValue()) {
        childRecord->nextSibling = newParentRecord->firstChild;
        newParentRecord->firstChild = child;
    }
    childRecord->local = replacementLocal;
    m_impl->worldDirty = mode == ReparentMode::KeepLocal;
    return Core::success();
}

Core::Status World::destroyEntity(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    // Keep-world destruction needs a current snapshot. The update is
    // transactional, so a malformed hierarchy or overflow leaves the World
    // unchanged and prevents a partial detach.
    if (m_impl->worldDirty) {
        if (const Core::Status status = updateWorldTransforms(); !status) {
            return status;
        }
    }

    EntityRecord* target = record(entity);
    if (target == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene destroy target could not be resolved");
    }

    // Validate every direct child and derive its root-local transform before
    // changing any links. The children remain alive and keep their world pose.
    EntityId child = target->firstChild;
    usize siblingCount = 0;
    while (child.hasValue()) {
        if (++siblingCount > m_impl->config.entityCapacity) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene destroy target has an invalid child chain");
        }
        const EntityRecord* childRecord = record(child);
        if (childRecord == nullptr || childRecord->parent != entity) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene destroy target has an invalid child link");
        }
        if (!deriveLocalTransform(childRecord->world, nullptr)) {
            return Core::failure(
                SceneErrorCode::InvalidTransform,
                "Scene child world transform cannot be preserved");
        }
        child = childRecord->nextSibling;
    }

    if (const Core::Status status = detachFromParent(entity); !status) {
        return status;
    }
    child = target->firstChild;
    target->firstChild = {};
    while (child.hasValue()) {
        EntityRecord* childRecord = record(child);
        const EntityId nextSibling = childRecord->nextSibling;
        childRecord->parent = {};
        childRecord->nextSibling = {};
        childRecord->local = *deriveLocalTransform(childRecord->world, nullptr);
        child = nextSibling;
    }

    return eraseEntity(entity);
}

Core::Status World::destroySubtree(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }

    m_impl->destroyStack.clear();
    for (const EntityId live : m_impl->liveEntities) {
        m_impl->visitedByIndex[live.index()] = 0;
    }
    m_impl->destroyStack.push_back(entity);
    for (usize cursor = 0; cursor < m_impl->destroyStack.size(); ++cursor) {
        const EntityId current = m_impl->destroyStack[cursor];
        if (current.index() >= m_impl->visitedByIndex.size()
            || m_impl->visitedByIndex[current.index()] != 0) {
            return Core::failure(
                SceneErrorCode::HierarchyCycle,
                "Scene destroy traversal visited an entity twice");
        }
        m_impl->visitedByIndex[current.index()] = 1;
        const EntityRecord* currentRecord = record(current);
        if (currentRecord == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene destroy traversal found a stale entity");
        }

        EntityId child = currentRecord->firstChild;
        usize siblingCount = 0;
        while (child.hasValue()) {
            if (++siblingCount > m_impl->config.entityCapacity
                || m_impl->destroyStack.size() >= m_impl->config.entityCapacity) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene destroy traversal exceeded fixed storage");
            }
            const EntityRecord* childRecord = record(child);
            if (childRecord == nullptr || childRecord->parent != current) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene destroy traversal found an invalid sibling");
            }
            m_impl->destroyStack.push_back(child);
            child = childRecord->nextSibling;
        }
    }

    // Every link and live-index entry is validated before the first erase;
    // the commit loop therefore has no fallible lookup in the normal path.
    for (const EntityId current : m_impl->destroyStack) {
        const EntityRecord* currentRecord = record(current);
        if (currentRecord == nullptr
            || current.index() >= m_impl->liveIndexByEntityIndex.size()
            || m_impl->liveIndexByEntityIndex[current.index()] == Impl::InvalidLiveIndex) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene destroy traversal found an entity outside the live registry");
        }
        if (currentRecord->parent.hasValue() && record(currentRecord->parent) == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene destroy traversal found a stale parent");
        }
    }

    // Only the subtree root has an external link. Detaching every descendant
    // would repeatedly scan sibling chains and turn a wide destroy into O(N^2).
    if (const Core::Status status = detachFromParent(entity); !status) {
        return status;
    }
    for (usize index = m_impl->destroyStack.size(); index > 0; --index) {
        if (const Core::Status status = eraseEntity(m_impl->destroyStack[index - 1]); !status) {
            return status;
        }
    }
    m_impl->destroyStack.clear();
    return Core::success();
}

Core::Status World::updateWorldTransforms() noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }

    for (const EntityId entity : m_impl->liveEntities) {
        if (entity.index() >= m_impl->visitedByIndex.size()
            || entity.index() >= m_impl->worldScratch.size()) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene entity index exceeds World storage");
        }
        m_impl->visitedByIndex[entity.index()] = 0;
        m_impl->computedByIndex[entity.index()] = 0;
    }

    m_impl->traversalStack.clear();
    for (const EntityId entity : m_impl->liveEntities) {
        const EntityRecord* entityRecord = record(entity);
        if (entityRecord == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene live registry contains a stale entity");
        }
        if (entityRecord->parent.hasValue() && record(entityRecord->parent) == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene entity parent is stale");
        }
        if (!entityRecord->parent.hasValue()) {
            m_impl->traversalStack.push_back(entity);
        }
    }

    usize visitedCount = 0;
    while (!m_impl->traversalStack.empty()) {
        const EntityId current = m_impl->traversalStack.back();
        m_impl->traversalStack.pop_back();
        const EntityRecord* currentRecord = record(current);
        if (currentRecord == nullptr || current.index() >= m_impl->visitedByIndex.size()) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene transform traversal found an invalid entity");
        }
        if (m_impl->visitedByIndex[current.index()] != 0) {
            return Core::failure(
                SceneErrorCode::HierarchyCycle,
                "Scene transform traversal visited an entity twice");
        }
        m_impl->visitedByIndex[current.index()] = 1;
        ++visitedCount;

        WorldTransform computedWorld{};
        if (currentRecord->parent.hasValue()) {
            const EntityRecord* parentRecord = record(currentRecord->parent);
            if (parentRecord == nullptr
                || currentRecord->parent.index() >= m_impl->computedByIndex.size()
                || m_impl->computedByIndex[currentRecord->parent.index()] == 0) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene transform traversal found an uncomputed parent");
            }
            const WorldTransform& parentWorld =
                m_impl->worldScratch[currentRecord->parent.index()];
            if (!isValid(currentRecord->local) || !isValid(parentWorld)) {
                return Core::failure(
                    SceneErrorCode::InvalidTransform,
                    "Scene transform traversal encountered invalid input");
            }
            if (!supportsTrsComposition(parentWorld, currentRecord->local)) {
                return Core::failure(
                    SceneErrorCode::UnsupportedTransformComposition,
                    "Scene hierarchy requires an affine transform for non-uniform scale and rotation");
            }
            if (!tryCompose(parentWorld, currentRecord->local, computedWorld)) {
                return Core::failure(
                    SceneErrorCode::TransformOverflow,
                    "Scene world transform overflowed during composition");
            }
        } else {
            computedWorld = toWorld(currentRecord->local);
            if (!isValid(computedWorld)) {
                return Core::failure(
                    SceneErrorCode::TransformOverflow,
                    "Scene root world transform is invalid");
            }
        }
        m_impl->worldScratch[current.index()] = computedWorld;
        m_impl->computedByIndex[current.index()] = 1;

        EntityId child = currentRecord->firstChild;
        usize siblingCount = 0;
        while (child.hasValue()) {
            if (++siblingCount > m_impl->config.entityCapacity) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene transform traversal exceeded sibling capacity");
            }
            const EntityRecord* childRecord = record(child);
            if (childRecord == nullptr || childRecord->parent != current) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene child link is inconsistent with its parent");
            }
            if (m_impl->traversalStack.size() >= m_impl->config.entityCapacity) {
                return Core::failure(
                    SceneErrorCode::CorruptHierarchy,
                    "Scene transform traversal exceeded fixed storage");
            }
            m_impl->traversalStack.push_back(child);
            child = childRecord->nextSibling;
        }
    }

    if (visitedCount != m_impl->liveEntities.size()) {
        return Core::failure(
            SceneErrorCode::HierarchyCycle,
            "Scene hierarchy contains a cycle or unreachable entity");
    }

    // Publish only after every node was validated. A bad transform or broken
    // hierarchy therefore cannot leave a partially updated World snapshot.
    for (const EntityId entity : m_impl->liveEntities) {
        EntityRecord* entityRecord = record(entity);
        if (entityRecord == nullptr
            || entity.index() >= m_impl->computedByIndex.size()
            || m_impl->computedByIndex[entity.index()] == 0) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene transform publication lost an entity");
        }
        entityRecord->world = m_impl->worldScratch[entity.index()];
    }
    m_impl->worldDirty = false;
    return Core::success();
}

Core::Status World::setMetadata(
    EntityId entity,
    EntityMetadataDesc metadataDesc) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }

    auto entityName = makeEntityName(metadataDesc.name);
    if (!entityName) {
        return Core::failure(std::move(entityName.error()));
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for metadata replacement");
    }

    entityRecord->metadata = EntityMetadata{
        .name = *entityName,
        .tag = metadataDesc.tag,
        .layer = metadataDesc.layer,
        .group = metadataDesc.group,
    };
    return Core::success();
}

Core::Status World::setRuntimeName(
    EntityId entity,
    std::string_view name) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }

    auto entityName = makeEntityName(name);
    if (!entityName) {
        return Core::failure(std::move(entityName.error()));
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for runtime name update");
    }
    entityRecord->metadata.name = *entityName;
    return Core::success();
}

Core::Status World::clearRuntimeName(EntityId entity) noexcept
{
    return setRuntimeName(entity, {});
}

Core::Status World::setTag(EntityId entity, EntityTag tagValue) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    record(entity)->metadata.tag = tagValue;
    return Core::success();
}

Core::Status World::setLayer(EntityId entity, EntityLayer layerValue) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    record(entity)->metadata.layer = layerValue;
    return Core::success();
}

Core::Status World::setGroup(EntityId entity, EntityGroup groupValue) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    record(entity)->metadata.group = groupValue;
    return Core::success();
}

bool World::contains(EntityId entity) const noexcept
{
    return m_impl != nullptr && m_impl->isOwnerThread()
        && m_impl->entities.contains(entity);
}

usize World::entityCount() const noexcept
{
    return m_impl == nullptr || !m_impl->isOwnerThread() ? 0 : m_impl->liveEntities.size();
}

usize World::entityCapacity() const noexcept
{
    return m_impl == nullptr || !m_impl->isOwnerThread() ? 0 : m_impl->config.entityCapacity;
}

EntityId World::parent(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return {};
    }
    const EntityRecord* entityRecord = record(entity);
    return entityRecord == nullptr ? EntityId{} : entityRecord->parent;
}

const LocalTransform* World::localTransform(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    return entityRecord == nullptr ? nullptr : &entityRecord->local;
}

const WorldTransform* World::worldTransform(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    return entityRecord == nullptr ? nullptr : &entityRecord->world;
}

Core::Status World::setCamera2D(EntityId entity, Camera2D camera) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(camera)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene Camera2D contains invalid projection or viewport values");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for Camera2D");
    }
    entityRecord->camera2D = camera;
    entityRecord->hasCamera2D = true;
    return Core::success();
}

Core::Status World::clearCamera2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for Camera2D clear");
    }
    entityRecord->hasCamera2D = false;
    entityRecord->camera2D = {};
    return Core::success();
}

Core::Status World::setSpriteRenderer2D(
    EntityId entity,
    SpriteRenderer2D sprite) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(sprite)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene SpriteRenderer2D has invalid render properties");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SpriteRenderer2D");
    }
    entityRecord->spriteRenderer2D = sprite;
    entityRecord->hasSpriteRenderer2D = true;
    return Core::success();
}

Core::Status World::clearSpriteRenderer2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SpriteRenderer2D clear");
    }
    entityRecord->hasSpriteRenderer2D = false;
    entityRecord->spriteRenderer2D = {};
    return Core::success();
}

Core::Status World::setPointLight2D(EntityId entity, PointLight2D light) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(light)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene PointLight2D has invalid color, intensity, influence radius, or source radius");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PointLight2D");
    }
    entityRecord->pointLight2D = light;
    entityRecord->hasPointLight2D = true;
    return Core::success();
}

Core::Status World::clearPointLight2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PointLight2D clear");
    }
    entityRecord->hasPointLight2D = false;
    entityRecord->pointLight2D = {};
    return Core::success();
}

Core::Status World::setPhysicsBody2D(EntityId entity, PhysicsBody2D body) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(body)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene PhysicsBody2D has an invalid kind, non-finite value, or negative damping");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PhysicsBody2D");
    }
    entityRecord->physicsBody2D = body;
    entityRecord->hasPhysicsBody2D = true;
    return Core::success();
}

Core::Status World::clearPhysicsBody2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PhysicsBody2D clear");
    }
    entityRecord->hasPhysicsBody2D = false;
    entityRecord->physicsBody2D = {};
    return Core::success();
}

Core::Status World::setPhysicsShape2D(EntityId entity, PhysicsShape2D shape) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(shape)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene PhysicsShape2D has an invalid kind, non-finite value, or non-positive extent for its kind");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PhysicsShape2D");
    }
    entityRecord->physicsShape2D = shape;
    entityRecord->hasPhysicsShape2D = true;
    return Core::success();
}

Core::Status World::clearPhysicsShape2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PhysicsShape2D clear");
    }
    entityRecord->hasPhysicsShape2D = false;
    entityRecord->physicsShape2D = {};
    return Core::success();
}

Core::Status World::setResourceBinding2D(EntityId entity, ResourceBinding2D binding) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(binding)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene ResourceBinding2D requires a non-zero AssetId and a known kind");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for ResourceBinding2D");
    }
    entityRecord->resourceBinding2D = binding;
    entityRecord->hasResourceBinding2D = true;
    return Core::success();
}

Core::Status World::clearResourceBinding2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for ResourceBinding2D clear");
    }
    entityRecord->hasResourceBinding2D = false;
    entityRecord->resourceBinding2D = {};
    return Core::success();
}

Core::Status World::setShadowOccluder2D(
    EntityId entity,
    ShadowOccluder2D occluder) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(occluder)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene ShadowOccluder2D endpoints must be finite and non-degenerate");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for ShadowOccluder2D");
    }
    entityRecord->shadowOccluder2D = occluder;
    entityRecord->hasShadowOccluder2D = true;
    return Core::success();
}

Core::Status World::clearShadowOccluder2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for ShadowOccluder2D clear");
    }
    entityRecord->hasShadowOccluder2D = false;
    entityRecord->shadowOccluder2D = {};
    return Core::success();
}

const Camera2D* World::camera2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasCamera2D) {
        return nullptr;
    }
    return &entityRecord->camera2D;
}

const SpriteRenderer2D* World::spriteRenderer2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasSpriteRenderer2D) {
        return nullptr;
    }
    return &entityRecord->spriteRenderer2D;
}

const PointLight2D* World::pointLight2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasPointLight2D) {
        return nullptr;
    }
    return &entityRecord->pointLight2D;
}

const PhysicsBody2D* World::physicsBody2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasPhysicsBody2D) {
        return nullptr;
    }
    return &entityRecord->physicsBody2D;
}

const PhysicsShape2D* World::physicsShape2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasPhysicsShape2D) {
        return nullptr;
    }
    return &entityRecord->physicsShape2D;
}

const ResourceBinding2D* World::resourceBinding2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasResourceBinding2D) {
        return nullptr;
    }
    return &entityRecord->resourceBinding2D;
}

const ShadowOccluder2D* World::shadowOccluder2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasShadowOccluder2D) {
        return nullptr;
    }
    return &entityRecord->shadowOccluder2D;
}

Core::Status World::setSpriteAnimationBinding2D(
    EntityId entity,
    SpriteAnimationBinding2D binding) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(binding)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene SpriteAnimationBinding2D requires a clip handle and positive finite speed");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SpriteAnimationBinding2D");
    }
    entityRecord->spriteAnimationBinding2D = binding;
    entityRecord->hasSpriteAnimationBinding2D = true;
    return Core::success();
}

Core::Status World::clearSpriteAnimationBinding2D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on the owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SpriteAnimationBinding2D clear");
    }
    entityRecord->hasSpriteAnimationBinding2D = false;
    entityRecord->spriteAnimationBinding2D = {};
    return Core::success();
}

const SpriteAnimationBinding2D* World::spriteAnimationBinding2D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasSpriteAnimationBinding2D) {
        return nullptr;
    }
    return &entityRecord->spriteAnimationBinding2D;
}

Core::Status World::setPerspectiveCamera3D(EntityId entity, PerspectiveCamera3D camera) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(camera)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene PerspectiveCamera3D contains invalid projection or viewport values");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PerspectiveCamera3D");
    }
    entityRecord->perspectiveCamera3D = camera;
    entityRecord->hasPerspectiveCamera3D = true;
    return Core::success();
}

Core::Status World::clearPerspectiveCamera3D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PerspectiveCamera3D clear");
    }
    entityRecord->hasPerspectiveCamera3D = false;
    entityRecord->perspectiveCamera3D = {};
    return Core::success();
}

Core::Status World::setMeshRenderer3D(EntityId entity, MeshRenderer3D mesh) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(mesh)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene MeshRenderer3D has invalid bounds or material color");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for MeshRenderer3D");
    }
    entityRecord->meshRenderer3D = mesh;
    entityRecord->hasMeshRenderer3D = true;
    entityRecord->hasSkinnedMeshRenderer3D = false;
    entityRecord->skinnedMeshRenderer3D = {};
    return Core::success();
}

Core::Status World::clearMeshRenderer3D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for MeshRenderer3D clear");
    }
    entityRecord->hasMeshRenderer3D = false;
    entityRecord->meshRenderer3D = {};
    return Core::success();
}

Core::Status World::setSkinnedMeshRenderer3D(
    EntityId entity,
    SkinnedMeshRenderer3D mesh) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(mesh)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene SkinnedMeshRenderer3D has invalid bounds or material color");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SkinnedMeshRenderer3D");
    }
    entityRecord->skinnedMeshRenderer3D = mesh;
    entityRecord->hasSkinnedMeshRenderer3D = true;
    entityRecord->hasMeshRenderer3D = false;
    entityRecord->meshRenderer3D = {};
    return Core::success();
}

Core::Status World::clearSkinnedMeshRenderer3D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SkinnedMeshRenderer3D clear");
    }
    entityRecord->hasSkinnedMeshRenderer3D = false;
    entityRecord->skinnedMeshRenderer3D = {};
    return Core::success();
}

Core::Status World::setDirectionalLight3D(
    EntityId entity,
    DirectionalLight3D light) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(light)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene DirectionalLight3D has invalid color or intensity");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for DirectionalLight3D");
    }
    entityRecord->directionalLight3D = light;
    entityRecord->hasDirectionalLight3D = true;
    return Core::success();
}

Core::Status World::clearDirectionalLight3D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for DirectionalLight3D clear");
    }
    entityRecord->hasDirectionalLight3D = false;
    entityRecord->directionalLight3D = {};
    return Core::success();
}

Core::Status World::setPointLight3D(EntityId entity, PointLight3D light) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(light)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene PointLight3D has invalid color, intensity, or influence radius");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PointLight3D");
    }
    entityRecord->pointLight3D = light;
    entityRecord->hasPointLight3D = true;
    return Core::success();
}

Core::Status World::clearPointLight3D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for PointLight3D clear");
    }
    entityRecord->hasPointLight3D = false;
    entityRecord->pointLight3D = {};
    return Core::success();
}

Core::Status World::setSpotLight3D(EntityId entity, SpotLight3D light) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    if (!isValid(light)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene SpotLight3D has invalid color, intensity, influence radius, cone angles, or shadow");
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SpotLight3D");
    }
    entityRecord->spotLight3D = light;
    entityRecord->hasSpotLight3D = true;
    return Core::success();
}

Core::Status World::clearSpotLight3D(EntityId entity) noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(
            SceneErrorCode::InvalidEntity,
            "Scene World is not initialized");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            SceneErrorCode::WrongOwnerThread,
            "Scene World mutation must run on its owner thread");
    }
    if (const Core::Status status = validateEntity(entity); !status) {
        return status;
    }
    EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "Scene entity could not be resolved for SpotLight3D clear");
    }
    entityRecord->hasSpotLight3D = false;
    entityRecord->spotLight3D = {};
    return Core::success();
}

const PerspectiveCamera3D* World::perspectiveCamera3D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasPerspectiveCamera3D) {
        return nullptr;
    }
    return &entityRecord->perspectiveCamera3D;
}

const MeshRenderer3D* World::meshRenderer3D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasMeshRenderer3D) {
        return nullptr;
    }
    return &entityRecord->meshRenderer3D;
}

const SkinnedMeshRenderer3D* World::skinnedMeshRenderer3D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasSkinnedMeshRenderer3D) {
        return nullptr;
    }
    return &entityRecord->skinnedMeshRenderer3D;
}

const DirectionalLight3D* World::directionalLight3D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasDirectionalLight3D) {
        return nullptr;
    }
    return &entityRecord->directionalLight3D;
}

const PointLight3D* World::pointLight3D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasPointLight3D) {
        return nullptr;
    }
    return &entityRecord->pointLight3D;
}

const SpotLight3D* World::spotLight3D(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    if (entityRecord == nullptr || !entityRecord->hasSpotLight3D) {
        return nullptr;
    }
    return &entityRecord->spotLight3D;
}

const EntityMetadata* World::metadata(EntityId entity) const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return nullptr;
    }
    const EntityRecord* entityRecord = record(entity);
    return entityRecord == nullptr ? nullptr : &entityRecord->metadata;
}

std::string_view World::runtimeName(EntityId entity) const noexcept
{
    const EntityMetadata* entityMetadata = metadata(entity);
    return entityMetadata == nullptr ? std::string_view{} : entityMetadata->name.view();
}

EntityTag World::tag(EntityId entity) const noexcept
{
    const EntityMetadata* entityMetadata = metadata(entity);
    return entityMetadata == nullptr ? NoEntityTag : entityMetadata->tag;
}

EntityLayer World::layer(EntityId entity) const noexcept
{
    const EntityMetadata* entityMetadata = metadata(entity);
    return entityMetadata == nullptr ? DefaultEntityLayer : entityMetadata->layer;
}

EntityGroup World::group(EntityId entity) const noexcept
{
    const EntityMetadata* entityMetadata = metadata(entity);
    return entityMetadata == nullptr ? NoEntityGroup : entityMetadata->group;
}

std::span<const EntityId> World::liveEntities() const noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return {};
    }
    return std::span<const EntityId>{
        m_impl->liveEntities.data(), m_impl->liveEntities.size()};
}

} // namespace Tina::Scene
