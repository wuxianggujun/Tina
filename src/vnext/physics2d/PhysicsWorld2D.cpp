#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Physics2D {

struct PhysicsWorld2D::Impl final {
    struct BodyRecord final {
        b2BodyId backend = b2_nullBodyId;
        PhysicsBodyId id{};
        PhysicsShapeId shape{};
    };

    struct ShapeRecord final {
        b2ShapeId backend = b2_nullShapeId;
        PhysicsShapeId id{};
        PhysicsBodyId body{};
    };

    struct ShapeTombstone final {
        b2ShapeId backend = b2_nullShapeId;
        PhysicsShapeId shape{};
        PhysicsBodyId body{};
        bool live = false;
    };

    using BodyPool = Core::GenerationPool<BodyRecord, Detail::PhysicsBodyRegistryTag>;
    using ShapePool = Core::GenerationPool<ShapeRecord, Detail::PhysicsShapeRegistryTag>;

    Impl(
        PhysicsWorld2DConfig worldConfig,
        BodyPool bodyPool,
        ShapePool shapePool,
        PhysicsContactBeginEvent2D* beginStorage,
        PhysicsContactEndEvent2D* endStorage,
        PhysicsContactHitEvent2D* hitStorage,
        ShapeTombstone* tombstoneStorage,
        b2WorldId backendWorld,
        std::pmr::memory_resource& memoryResource) noexcept
        : config(worldConfig),
          resource(&memoryResource),
          bodies(std::move(bodyPool)),
          shapes(std::move(shapePool)),
          beginContacts(beginStorage),
          endContacts(endStorage),
          hitContacts(hitStorage),
          shapeTombstones(tombstoneStorage),
          world(backendWorld)
    {
    }

    ~Impl() noexcept
    {
        releaseContactStorage();
    }

    void releaseContactStorage() noexcept
    {
        if (resource == nullptr) {
            return;
        }
        if (beginContacts != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(beginContacts, config.contactBeginCapacity);
            resource->deallocate(
                beginContacts,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
            beginContacts = nullptr;
        }
        if (endContacts != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(endContacts, config.contactEndCapacity);
            resource->deallocate(
                endContacts,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
            endContacts = nullptr;
        }
        if (hitContacts != nullptr && config.contactHitCapacity > 0) {
            std::destroy_n(hitContacts, config.contactHitCapacity);
            resource->deallocate(
                hitContacts,
                sizeof(PhysicsContactHitEvent2D) * config.contactHitCapacity,
                alignof(PhysicsContactHitEvent2D));
            hitContacts = nullptr;
        }
        if (shapeTombstones != nullptr && config.shapeCapacity > 0) {
            std::destroy_n(shapeTombstones, config.shapeCapacity);
            resource->deallocate(
                shapeTombstones,
                sizeof(ShapeTombstone) * config.shapeCapacity,
                alignof(ShapeTombstone));
            shapeTombstones = nullptr;
        }
    }

    void clearPublishedContacts() noexcept
    {
        beginCount = 0;
        endCount = 0;
        hitCount = 0;
        beginOverflow = false;
        endOverflow = false;
        hitOverflow = false;
    }

    void rememberDestroyedShape(const ShapeRecord& shapeRecord) noexcept
    {
        if (shapeTombstones == nullptr || config.shapeCapacity == 0 || B2_IS_NULL(shapeRecord.backend)) {
            return;
        }
        for (Core::usize index = 0; index < config.shapeCapacity; ++index) {
            ShapeTombstone& tombstone = shapeTombstones[index];
            if (tombstone.live && B2_ID_EQUALS(tombstone.backend, shapeRecord.backend)) {
                tombstone.shape = shapeRecord.id;
                tombstone.body = shapeRecord.body;
                return;
            }
        }
        for (Core::usize index = 0; index < config.shapeCapacity; ++index) {
            ShapeTombstone& tombstone = shapeTombstones[index];
            if (!tombstone.live) {
                tombstone.backend = shapeRecord.backend;
                tombstone.shape = shapeRecord.id;
                tombstone.body = shapeRecord.body;
                tombstone.live = true;
                return;
            }
        }
        ShapeTombstone& overwrite = shapeTombstones[tombstoneWriteIndex % config.shapeCapacity];
        overwrite.backend = shapeRecord.backend;
        overwrite.shape = shapeRecord.id;
        overwrite.body = shapeRecord.body;
        overwrite.live = true;
        ++tombstoneWriteIndex;
    }

    struct ShapeEndpoints final {
        PhysicsShapeId shape{};
        PhysicsBodyId body{};
        bool destroyed = false;
        bool resolved = false;
    };

    [[nodiscard]] ShapeEndpoints resolveShapeEndpoints(b2ShapeId backendShape) noexcept
    {
        ShapeEndpoints endpoints{};
        if (B2_IS_NULL(backendShape)) {
            endpoints.destroyed = true;
            return endpoints;
        }

        if (b2Shape_IsValid(backendShape)) {
            void* userData = b2Shape_GetUserData(backendShape);
            if (userData != nullptr) {
                auto* shapeRecord = static_cast<ShapeRecord*>(userData);
                if (shapeRecord != nullptr && shapeRecord->id.hasValue()) {
                    endpoints.shape = shapeRecord->id;
                    endpoints.body = shapeRecord->body;
                    endpoints.resolved = true;
                    endpoints.destroyed = !shapes.contains(shapeRecord->id);
                    return endpoints;
                }
            }
        }

        if (shapeTombstones != nullptr) {
            for (Core::usize index = 0; index < config.shapeCapacity; ++index) {
                const ShapeTombstone& tombstone = shapeTombstones[index];
                if (tombstone.live && B2_ID_EQUALS(tombstone.backend, backendShape)) {
                    endpoints.shape = tombstone.shape;
                    endpoints.body = tombstone.body;
                    endpoints.resolved = true;
                    endpoints.destroyed = true;
                    return endpoints;
                }
            }
        }

        endpoints.destroyed = true;
        return endpoints;
    }

    void publishContactEvents() noexcept
    {
        clearPublishedContacts();
        if (!b2World_IsValid(world)) {
            return;
        }

        const b2ContactEvents events = b2World_GetContactEvents(world);

        const int beginTotal = (std::max)(0, events.beginCount);
        const Core::usize beginCapacity = config.contactBeginCapacity;
        const Core::usize beginPublish =
            (std::min)(static_cast<Core::usize>(beginTotal), beginCapacity);
        for (Core::usize index = 0; index < beginPublish; ++index) {
            const b2ContactBeginTouchEvent& source = events.beginEvents[index];
            const ShapeEndpoints shapeA = resolveShapeEndpoints(source.shapeIdA);
            const ShapeEndpoints shapeB = resolveShapeEndpoints(source.shapeIdB);
            if (!shapeA.resolved || !shapeB.resolved || shapeA.destroyed || shapeB.destroyed) {
                continue;
            }
            if (beginContacts == nullptr) {
                break;
            }
            beginContacts[beginCount++] = PhysicsContactBeginEvent2D{
                shapeA.body,
                shapeB.body,
                shapeA.shape,
                shapeB.shape};
        }
        if (static_cast<Core::usize>(beginTotal) > beginCapacity) {
            beginOverflow = true;
            droppedBeginContactCount += static_cast<Core::u64>(beginTotal) - beginCapacity;
        }

        const int endTotal = (std::max)(0, events.endCount);
        const Core::usize endCapacity = config.contactEndCapacity;
        const Core::usize endPublish =
            (std::min)(static_cast<Core::usize>(endTotal), endCapacity);
        for (Core::usize index = 0; index < endPublish; ++index) {
            const b2ContactEndTouchEvent& source = events.endEvents[index];
            const ShapeEndpoints shapeA = resolveShapeEndpoints(source.shapeIdA);
            const ShapeEndpoints shapeB = resolveShapeEndpoints(source.shapeIdB);
            if (!shapeA.resolved && !shapeB.resolved) {
                continue;
            }
            if (endContacts == nullptr) {
                break;
            }
            endContacts[endCount++] = PhysicsContactEndEvent2D{
                shapeA.body,
                shapeB.body,
                shapeA.shape,
                shapeB.shape,
                shapeA.destroyed || !shapeA.resolved,
                shapeB.destroyed || !shapeB.resolved};
        }
        if (static_cast<Core::usize>(endTotal) > endCapacity) {
            endOverflow = true;
            droppedEndContactCount += static_cast<Core::u64>(endTotal) - endCapacity;
        }

        const int hitTotal = (std::max)(0, events.hitCount);
        const Core::usize hitCapacity = config.contactHitCapacity;
        const Core::usize hitPublish =
            (std::min)(static_cast<Core::usize>(hitTotal), hitCapacity);
        for (Core::usize index = 0; index < hitPublish; ++index) {
            const b2ContactHitEvent& source = events.hitEvents[index];
            const ShapeEndpoints shapeA = resolveShapeEndpoints(source.shapeIdA);
            const ShapeEndpoints shapeB = resolveShapeEndpoints(source.shapeIdB);
            if (!shapeA.resolved || !shapeB.resolved || shapeA.destroyed || shapeB.destroyed) {
                continue;
            }
            if (hitContacts == nullptr) {
                break;
            }
            hitContacts[hitCount++] = PhysicsContactHitEvent2D{
                shapeA.body,
                shapeB.body,
                shapeA.shape,
                shapeB.shape,
                {source.point.x, source.point.y},
                {source.normal.x, source.normal.y},
                source.approachSpeed};
        }
        if (static_cast<Core::usize>(hitTotal) > hitCapacity) {
            hitOverflow = true;
            droppedHitContactCount += static_cast<Core::u64>(hitTotal) - hitCapacity;
        }
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return ownerThread == std::this_thread::get_id();
    }

    [[nodiscard]] PhysicsContactEvents2DView contactView() const noexcept
    {
        return PhysicsContactEvents2DView{
            std::span<const PhysicsContactBeginEvent2D>(beginContacts, beginCount),
            std::span<const PhysicsContactEndEvent2D>(endContacts, endCount),
            std::span<const PhysicsContactHitEvent2D>(hitContacts, hitCount),
            beginOverflow,
            endOverflow,
            hitOverflow};
    }

    PhysicsWorld2DConfig config{};
    std::thread::id ownerThread = std::this_thread::get_id();
    std::pmr::memory_resource* resource = nullptr;
    BodyPool bodies;
    ShapePool shapes;
    PhysicsContactBeginEvent2D* beginContacts = nullptr;
    PhysicsContactEndEvent2D* endContacts = nullptr;
    PhysicsContactHitEvent2D* hitContacts = nullptr;
    ShapeTombstone* shapeTombstones = nullptr;
    Core::usize beginCount = 0;
    Core::usize endCount = 0;
    Core::usize hitCount = 0;
    Core::usize tombstoneWriteIndex = 0;
    bool beginOverflow = false;
    bool endOverflow = false;
    bool hitOverflow = false;
    Core::u64 droppedBeginContactCount = 0;
    Core::u64 droppedEndContactCount = 0;
    Core::u64 droppedHitContactCount = 0;
    b2WorldId world = b2_nullWorldId;
    Core::u64 completedStepCount = 0;
    bool open = true;
    bool inStep = false;
};

namespace {

[[nodiscard]] bool isFinite(PhysicsVec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool isKnownBodyType(PhysicsBodyType2D type) noexcept
{
    switch (type) {
    case PhysicsBodyType2D::Static:
    case PhysicsBodyType2D::Kinematic:
    case PhysicsBodyType2D::Dynamic:
        return true;
    }
    return false;
}

[[nodiscard]] b2BodyType toBackendBodyType(PhysicsBodyType2D type) noexcept
{
    switch (type) {
    case PhysicsBodyType2D::Static:
        return b2_staticBody;
    case PhysicsBodyType2D::Kinematic:
        return b2_kinematicBody;
    case PhysicsBodyType2D::Dynamic:
        return b2_dynamicBody;
    }
    return b2_staticBody;
}

[[nodiscard]] Core::Error mapPoolError(
    Core::Error error,
    std::string_view operation,
    std::string_view detail)
{
    if (error.code == Core::CoreErrorCode::CapacityExceeded) {
        error.code = Physics2DErrorCode::CapacityExceeded;
    }
    return std::move(error).withContext(operation, detail);
}

template <typename Value>
[[nodiscard]] Core::Result<Value*> allocateContactBuffer(
    std::pmr::memory_resource& resource,
    Core::usize capacity,
    std::string_view detail)
{
    if (capacity == 0) {
        return static_cast<Value*>(nullptr);
    }
    if (capacity > (std::numeric_limits<Core::usize>::max)() / sizeof(Value)) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D contact event capacity overflows storage size");
    }
    void* storage = nullptr;
    try {
        storage = resource.allocate(sizeof(Value) * capacity, alignof(Value));
        auto* values = static_cast<Value*>(storage);
        std::uninitialized_default_construct_n(values, capacity);
        return values;
    } catch (const std::bad_alloc&) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Value) * capacity, alignof(Value));
        }
        return Core::failure(
            Physics2DErrorCode::CapacityExceeded,
            detail);
    } catch (const std::exception& exception) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Value) * capacity, alignof(Value));
        }
        return Core::failure(Core::Error{
            Physics2DErrorCode::ConstructionFailed,
            exception.what()});
    } catch (...) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Value) * capacity, alignof(Value));
        }
        return Core::failure(
            Physics2DErrorCode::ConstructionFailed,
            detail);
    }
}

} // namespace

Core::Status validatePhysicsWorld2DConfig(const PhysicsWorld2DConfig& config) noexcept
{
    if (config.bodyCapacity == 0 || config.shapeCapacity == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D body and shape capacities must be greater than zero");
    }
    if (config.bodyCapacity > PhysicsWorld2DConfig::MaxBodyCapacity
        || config.bodyCapacity > PhysicsBodyId::InvalidIndex
        || config.shapeCapacity > PhysicsWorld2DConfig::MaxShapeCapacity
        || config.shapeCapacity > PhysicsShapeId::InvalidIndex) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D body or shape capacity exceeds the supported generation range");
    }
    if (config.contactBeginCapacity > PhysicsWorld2DConfig::MaxContactEventCapacity
        || config.contactEndCapacity > PhysicsWorld2DConfig::MaxContactEventCapacity
        || config.contactHitCapacity > PhysicsWorld2DConfig::MaxContactEventCapacity) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D contact event capacity exceeds the supported range");
    }
    if (!isFinite(config.gravityMetersPerSecondSquared)
        || !std::isfinite(config.fixedDeltaSeconds)
        || config.fixedDeltaSeconds <= 0.0F) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D gravity and fixed delta must be finite, with a positive fixed delta");
    }
    if (config.solverSubStepCount == 0
        || config.solverSubStepCount > PhysicsWorld2DConfig::MaxSolverSubStepCount) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D solver sub-step count is outside the supported range");
    }
    return Core::success();
}

Core::Status validatePhysicsBody2DDesc(const PhysicsBody2DDesc& desc) noexcept
{
    if (!isKnownBodyType(desc.type)) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D body type is not supported");
    }
    if (!isFinite(desc.positionMeters)
        || !std::isfinite(desc.angleRadians)
        || !isFinite(desc.linearVelocityMetersPerSecond)
        || !std::isfinite(desc.angularVelocityRadiansPerSecond)
        || !std::isfinite(desc.linearDamping)
        || !std::isfinite(desc.angularDamping)
        || !std::isfinite(desc.gravityScale)
        || desc.linearDamping < 0.0F
        || desc.angularDamping < 0.0F) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D body description contains non-finite values or negative damping");
    }
    return Core::success();
}

Core::Status validatePhysicsBoxShape2DDesc(const PhysicsBoxShape2DDesc& desc) noexcept
{
    if (!isFinite(desc.halfExtentsMeters)
        || desc.halfExtentsMeters.x <= 0.0F
        || desc.halfExtentsMeters.y <= 0.0F
        || !isFinite(desc.centerMeters)
        || !std::isfinite(desc.angleRadians)
        || !std::isfinite(desc.density)
        || !std::isfinite(desc.friction)
        || !std::isfinite(desc.restitution)
        || desc.density < 0.0F
        || desc.friction < 0.0F
        || desc.friction > 1.0F
        || desc.restitution < 0.0F
        || desc.restitution > 1.0F
        || desc.filter.categoryBits == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidShapeDescription,
            "Physics2D box shape contains invalid geometry, material, or collision filter values");
    }
    return Core::success();
}

Core::Result<PhysicsWorld2D> PhysicsWorld2D::Create(
    PhysicsWorld2DConfig config,
    std::pmr::memory_resource& resource)
{
    if (const Core::Status status = validatePhysicsWorld2DConfig(config); !status) {
        return Core::failure(status.error());
    }

    auto bodyPoolResult = Impl::BodyPool::Create(config.bodyCapacity, resource);
    if (!bodyPoolResult) {
        return Core::failure(mapPoolError(
            std::move(bodyPoolResult.error()),
            "PhysicsWorld2D::Create",
            "body registry allocation"));
    }
    auto shapePoolResult = Impl::ShapePool::Create(config.shapeCapacity, resource);
    if (!shapePoolResult) {
        return Core::failure(mapPoolError(
            std::move(shapePoolResult.error()),
            "PhysicsWorld2D::Create",
            "shape registry allocation"));
    }

    auto beginStorageResult = allocateContactBuffer<PhysicsContactBeginEvent2D>(
        resource,
        config.contactBeginCapacity,
        "Physics2D contact begin buffer allocation failed");
    if (!beginStorageResult) {
        return Core::failure(beginStorageResult.error());
    }
    auto endStorageResult = allocateContactBuffer<PhysicsContactEndEvent2D>(
        resource,
        config.contactEndCapacity,
        "Physics2D contact end buffer allocation failed");
    if (!endStorageResult) {
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        return Core::failure(endStorageResult.error());
    }
    auto hitStorageResult = allocateContactBuffer<PhysicsContactHitEvent2D>(
        resource,
        config.contactHitCapacity,
        "Physics2D contact hit buffer allocation failed");
    if (!hitStorageResult) {
        if (*endStorageResult != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(*endStorageResult, config.contactEndCapacity);
            resource.deallocate(
                *endStorageResult,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
        }
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        return Core::failure(hitStorageResult.error());
    }
    auto tombstoneStorageResult = allocateContactBuffer<Impl::ShapeTombstone>(
        resource,
        config.shapeCapacity,
        "Physics2D shape tombstone buffer allocation failed");
    if (!tombstoneStorageResult) {
        if (*hitStorageResult != nullptr && config.contactHitCapacity > 0) {
            std::destroy_n(*hitStorageResult, config.contactHitCapacity);
            resource.deallocate(
                *hitStorageResult,
                sizeof(PhysicsContactHitEvent2D) * config.contactHitCapacity,
                alignof(PhysicsContactHitEvent2D));
        }
        if (*endStorageResult != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(*endStorageResult, config.contactEndCapacity);
            resource.deallocate(
                *endStorageResult,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
        }
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        return Core::failure(tombstoneStorageResult.error());
    }

    b2WorldDef worldDefinition = b2DefaultWorldDef();
    worldDefinition.gravity = {
        config.gravityMetersPerSecondSquared.x,
        config.gravityMetersPerSecondSquared.y};
    worldDefinition.workerCount = 0;
    worldDefinition.enqueueTask = nullptr;
    worldDefinition.finishTask = nullptr;
    worldDefinition.userTaskContext = nullptr;
    const b2WorldId backendWorld = b2CreateWorld(&worldDefinition);
    if (B2_IS_NULL(backendWorld) || !b2World_IsValid(backendWorld)) {
        if (*tombstoneStorageResult != nullptr && config.shapeCapacity > 0) {
            std::destroy_n(*tombstoneStorageResult, config.shapeCapacity);
            resource.deallocate(
                *tombstoneStorageResult,
                sizeof(Impl::ShapeTombstone) * config.shapeCapacity,
                alignof(Impl::ShapeTombstone));
        }
        if (*hitStorageResult != nullptr && config.contactHitCapacity > 0) {
            std::destroy_n(*hitStorageResult, config.contactHitCapacity);
            resource.deallocate(
                *hitStorageResult,
                sizeof(PhysicsContactHitEvent2D) * config.contactHitCapacity,
                alignof(PhysicsContactHitEvent2D));
        }
        if (*endStorageResult != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(*endStorageResult, config.contactEndCapacity);
            resource.deallocate(
                *endStorageResult,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
        }
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Box2D failed to create a world");
    }

    void* storage = nullptr;
    try {
        storage = resource.allocate(sizeof(Impl), alignof(Impl));
        auto* impl = std::construct_at(
            static_cast<Impl*>(storage),
            config,
            std::move(*bodyPoolResult),
            std::move(*shapePoolResult),
            *beginStorageResult,
            *endStorageResult,
            *hitStorageResult,
            *tombstoneStorageResult,
            backendWorld,
            resource);
        b2World_SetUserData(backendWorld, impl);
        return PhysicsWorld2D(impl);
    } catch (const std::bad_alloc&) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        if (*tombstoneStorageResult != nullptr && config.shapeCapacity > 0) {
            std::destroy_n(*tombstoneStorageResult, config.shapeCapacity);
            resource.deallocate(
                *tombstoneStorageResult,
                sizeof(Impl::ShapeTombstone) * config.shapeCapacity,
                alignof(Impl::ShapeTombstone));
        }
        if (*hitStorageResult != nullptr && config.contactHitCapacity > 0) {
            std::destroy_n(*hitStorageResult, config.contactHitCapacity);
            resource.deallocate(
                *hitStorageResult,
                sizeof(PhysicsContactHitEvent2D) * config.contactHitCapacity,
                alignof(PhysicsContactHitEvent2D));
        }
        if (*endStorageResult != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(*endStorageResult, config.contactEndCapacity);
            resource.deallocate(
                *endStorageResult,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
        }
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        b2DestroyWorld(backendWorld);
        return Core::failure(
            Physics2DErrorCode::CapacityExceeded,
            "Physics2D fixed world storage allocation failed");
    } catch (const std::exception& exception) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        if (*tombstoneStorageResult != nullptr && config.shapeCapacity > 0) {
            std::destroy_n(*tombstoneStorageResult, config.shapeCapacity);
            resource.deallocate(
                *tombstoneStorageResult,
                sizeof(Impl::ShapeTombstone) * config.shapeCapacity,
                alignof(Impl::ShapeTombstone));
        }
        if (*hitStorageResult != nullptr && config.contactHitCapacity > 0) {
            std::destroy_n(*hitStorageResult, config.contactHitCapacity);
            resource.deallocate(
                *hitStorageResult,
                sizeof(PhysicsContactHitEvent2D) * config.contactHitCapacity,
                alignof(PhysicsContactHitEvent2D));
        }
        if (*endStorageResult != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(*endStorageResult, config.contactEndCapacity);
            resource.deallocate(
                *endStorageResult,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
        }
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        b2DestroyWorld(backendWorld);
        return Core::failure(Core::Error{
            Physics2DErrorCode::ConstructionFailed,
            exception.what()});
    } catch (...) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        if (*tombstoneStorageResult != nullptr && config.shapeCapacity > 0) {
            std::destroy_n(*tombstoneStorageResult, config.shapeCapacity);
            resource.deallocate(
                *tombstoneStorageResult,
                sizeof(Impl::ShapeTombstone) * config.shapeCapacity,
                alignof(Impl::ShapeTombstone));
        }
        if (*hitStorageResult != nullptr && config.contactHitCapacity > 0) {
            std::destroy_n(*hitStorageResult, config.contactHitCapacity);
            resource.deallocate(
                *hitStorageResult,
                sizeof(PhysicsContactHitEvent2D) * config.contactHitCapacity,
                alignof(PhysicsContactHitEvent2D));
        }
        if (*endStorageResult != nullptr && config.contactEndCapacity > 0) {
            std::destroy_n(*endStorageResult, config.contactEndCapacity);
            resource.deallocate(
                *endStorageResult,
                sizeof(PhysicsContactEndEvent2D) * config.contactEndCapacity,
                alignof(PhysicsContactEndEvent2D));
        }
        if (*beginStorageResult != nullptr && config.contactBeginCapacity > 0) {
            std::destroy_n(*beginStorageResult, config.contactBeginCapacity);
            resource.deallocate(
                *beginStorageResult,
                sizeof(PhysicsContactBeginEvent2D) * config.contactBeginCapacity,
                alignof(PhysicsContactBeginEvent2D));
        }
        b2DestroyWorld(backendWorld);
        return Core::failure(
            Physics2DErrorCode::ConstructionFailed,
            "Physics2D world construction failed with an unknown exception");
    }
}

PhysicsWorld2D::PhysicsWorld2D(Impl* impl) noexcept
    : m_impl(impl)
{
}

PhysicsWorld2D::~PhysicsWorld2D() noexcept
{
    if (m_impl == nullptr) {
        return;
    }
    if (!m_impl->isOwnerThread()) {
        std::terminate();
    }
    (void)shutdown();
    std::pmr::memory_resource* resource = m_impl->resource;
    std::destroy_at(m_impl);
    resource->deallocate(m_impl, sizeof(Impl), alignof(Impl));
    m_impl = nullptr;
}

PhysicsWorld2D::PhysicsWorld2D(PhysicsWorld2D&& other) noexcept
    : m_impl(std::exchange(other.m_impl, nullptr))
{
    if (m_impl != nullptr) {
        m_impl->ownerThread = std::this_thread::get_id();
    }
}

Core::Status PhysicsWorld2D::ensureUsable() const noexcept
{
    if (m_impl == nullptr || !m_impl->open) {
        return Core::failure(
            Physics2DErrorCode::WorldClosed,
            "Physics2D world is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            Physics2DErrorCode::WrongOwnerThread,
            "Physics2D world access must run on its owner thread");
    }
    if (m_impl->inStep) {
        return Core::failure(
            Physics2DErrorCode::ReentrantMutation,
            "Physics2D world does not allow access during a solver step");
    }
    return Core::success();
}

Core::Status PhysicsWorld2D::validateBody(PhysicsBodyId body) const noexcept
{
    if (!body.hasValue()) {
        return Core::failure(
            Physics2DErrorCode::InvalidBody,
            "Physics2D body handle is invalid");
    }
    if (body.owner() != m_impl->bodies.owner()) {
        return Core::failure(
            Physics2DErrorCode::WrongWorld,
            "Physics2D body belongs to another world");
    }
    if (!m_impl->bodies.contains(body)) {
        return Core::failure(
            Physics2DErrorCode::StaleBody,
            "Physics2D body handle is stale");
    }
    return Core::success();
}

Core::Status PhysicsWorld2D::validateShape(PhysicsShapeId shape) const noexcept
{
    if (!shape.hasValue()) {
        return Core::failure(
            Physics2DErrorCode::InvalidShape,
            "Physics2D shape handle is invalid");
    }
    if (shape.owner() != m_impl->shapes.owner()) {
        return Core::failure(
            Physics2DErrorCode::WrongWorld,
            "Physics2D shape belongs to another world");
    }
    if (!m_impl->shapes.contains(shape)) {
        return Core::failure(
            Physics2DErrorCode::StaleShape,
            "Physics2D shape handle is stale");
    }
    return Core::success();
}

Core::Result<PhysicsBodyShape2D> PhysicsWorld2D::createBoxBody(
    const PhysicsBody2DDesc& bodyDescription,
    const PhysicsBoxShape2DDesc& shapeDescription)
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsBody2DDesc(bodyDescription); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsBoxShape2DDesc(shapeDescription); !status) {
        return Core::failure(status.error());
    }

    auto bodyIdResult = m_impl->bodies.tryEmplace();
    if (!bodyIdResult) {
        return Core::failure(mapPoolError(
            std::move(bodyIdResult.error()),
            "PhysicsWorld2D::createBoxBody",
            "body registry capacity"));
    }
    const PhysicsBodyId bodyId = *bodyIdResult;

    auto shapeIdResult = m_impl->shapes.tryEmplace();
    if (!shapeIdResult) {
        (void)m_impl->bodies.erase(bodyId);
        return Core::failure(mapPoolError(
            std::move(shapeIdResult.error()),
            "PhysicsWorld2D::createBoxBody",
            "shape registry capacity"));
    }
    const PhysicsShapeId shapeId = *shapeIdResult;

    Impl::BodyRecord* bodyRecord = m_impl->bodies.tryGet(bodyId);
    Impl::ShapeRecord* shapeRecord = m_impl->shapes.tryGet(shapeId);
    if (bodyRecord == nullptr || shapeRecord == nullptr) {
        (void)m_impl->shapes.erase(shapeId);
        (void)m_impl->bodies.erase(bodyId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D could not resolve newly reserved registry slots");
    }
    bodyRecord->id = bodyId;
    bodyRecord->shape = shapeId;
    shapeRecord->id = shapeId;
    shapeRecord->body = bodyId;

    b2BodyDef bodyDefinition = b2DefaultBodyDef();
    bodyDefinition.type = toBackendBodyType(bodyDescription.type);
    bodyDefinition.position = {
        bodyDescription.positionMeters.x,
        bodyDescription.positionMeters.y};
    bodyDefinition.rotation = b2MakeRot(bodyDescription.angleRadians);
    bodyDefinition.linearVelocity = {
        bodyDescription.linearVelocityMetersPerSecond.x,
        bodyDescription.linearVelocityMetersPerSecond.y};
    bodyDefinition.angularVelocity = bodyDescription.angularVelocityRadiansPerSecond;
    bodyDefinition.linearDamping = bodyDescription.linearDamping;
    bodyDefinition.angularDamping = bodyDescription.angularDamping;
    bodyDefinition.gravityScale = bodyDescription.gravityScale;
    bodyDefinition.userData = bodyRecord;
    bodyDefinition.enableSleep = bodyDescription.enableSleep;
    bodyDefinition.isAwake = bodyDescription.initiallyAwake;
    bodyDefinition.fixedRotation = bodyDescription.fixedRotation;
    bodyDefinition.isBullet = bodyDescription.continuousCollision;
    bodyDefinition.isEnabled = bodyDescription.enabled;
    bodyRecord->backend = b2CreateBody(m_impl->world, &bodyDefinition);
    if (B2_IS_NULL(bodyRecord->backend) || !b2Body_IsValid(bodyRecord->backend)) {
        bodyRecord->backend = b2_nullBodyId;
        (void)m_impl->shapes.erase(shapeId);
        (void)m_impl->bodies.erase(bodyId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Box2D failed to create a body");
    }

    b2ShapeDef shapeDefinition = b2DefaultShapeDef();
    shapeDefinition.userData = shapeRecord;
    shapeDefinition.material.friction = shapeDescription.friction;
    shapeDefinition.material.restitution = shapeDescription.restitution;
    shapeDefinition.density = shapeDescription.density;
    shapeDefinition.filter.categoryBits = shapeDescription.filter.categoryBits;
    shapeDefinition.filter.maskBits = shapeDescription.filter.maskBits;
    shapeDefinition.filter.groupIndex = shapeDescription.filter.groupIndex;
    shapeDefinition.enableContactEvents = shapeDescription.enableContactEvents;
    shapeDefinition.enableHitEvents = shapeDescription.enableHitEvents;
    const b2Polygon polygon = b2MakeOffsetBox(
        shapeDescription.halfExtentsMeters.x,
        shapeDescription.halfExtentsMeters.y,
        {shapeDescription.centerMeters.x, shapeDescription.centerMeters.y},
        b2MakeRot(shapeDescription.angleRadians));
    shapeRecord->backend = b2CreatePolygonShape(
        bodyRecord->backend,
        &shapeDefinition,
        &polygon);
    if (B2_IS_NULL(shapeRecord->backend) || !b2Shape_IsValid(shapeRecord->backend)) {
        shapeRecord->backend = b2_nullShapeId;
        b2DestroyBody(bodyRecord->backend);
        bodyRecord->backend = b2_nullBodyId;
        (void)m_impl->shapes.erase(shapeId);
        (void)m_impl->bodies.erase(bodyId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Box2D failed to create a box shape");
    }

    return PhysicsBodyShape2D{bodyId, shapeId};
}

Core::Status PhysicsWorld2D::destroyBody(PhysicsBodyId body) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }

    Impl::BodyRecord* bodyRecord = m_impl->bodies.tryGet(body);
    if (bodyRecord == nullptr || !b2Body_IsValid(bodyRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D body registry lost its Box2D body");
    }
    const PhysicsShapeId shape = bodyRecord->shape;
    Impl::ShapeRecord* shapeRecord = m_impl->shapes.tryGet(shape);
    if (shapeRecord == nullptr || !b2Shape_IsValid(shapeRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D body registry lost its Box2D shape");
    }

    m_impl->rememberDestroyedShape(*shapeRecord);
    b2DestroyBody(bodyRecord->backend);
    bodyRecord->backend = b2_nullBodyId;
    shapeRecord->backend = b2_nullShapeId;
    const Core::GenerationEraseResult shapeEraseResult = m_impl->shapes.erase(shape);
    const Core::GenerationEraseResult bodyEraseResult = m_impl->bodies.erase(body);
    if (shapeEraseResult != Core::GenerationEraseResult::Erased
        || bodyEraseResult != Core::GenerationEraseResult::Erased) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D failed to retire destroyed registry slots");
    }
    return Core::success();
}

Core::Status PhysicsWorld2D::step() noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (!b2World_IsValid(m_impl->world)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D backend world is invalid");
    }

    m_impl->inStep = true;
    b2World_Step(
        m_impl->world,
        m_impl->config.fixedDeltaSeconds,
        static_cast<int>(m_impl->config.solverSubStepCount));
    m_impl->publishContactEvents();
    m_impl->inStep = false;
    ++m_impl->completedStepCount;
    return Core::success();
}

Core::Result<PhysicsContactEvents2DView> PhysicsWorld2D::contactEvents() const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    return m_impl->contactView();
}

Core::Result<PhysicsBodyState2D> PhysicsWorld2D::bodyState(
    PhysicsBodyId body) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateBody(body); !status) {
        return Core::failure(status.error());
    }

    const Impl::BodyRecord* bodyRecord = m_impl->bodies.tryGet(body);
    if (bodyRecord == nullptr || !b2Body_IsValid(bodyRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D body registry lost its Box2D body");
    }
    const b2Transform transform = b2Body_GetTransform(bodyRecord->backend);
    const b2Vec2 linearVelocity = b2Body_GetLinearVelocity(bodyRecord->backend);
    return PhysicsBodyState2D{
        {transform.p.x, transform.p.y},
        b2Rot_GetAngle(transform.q),
        {linearVelocity.x, linearVelocity.y},
        b2Body_GetAngularVelocity(bodyRecord->backend),
        b2Body_IsAwake(bodyRecord->backend),
        b2Body_IsEnabled(bodyRecord->backend)};
}

Core::Result<PhysicsBodyId> PhysicsWorld2D::shapeBody(
    PhysicsShapeId shape) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateShape(shape); !status) {
        return Core::failure(status.error());
    }
    const Impl::ShapeRecord* shapeRecord = m_impl->shapes.tryGet(shape);
    if (shapeRecord == nullptr || !b2Shape_IsValid(shapeRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D shape registry lost its Box2D shape");
    }
    return shapeRecord->body;
}

bool PhysicsWorld2D::contains(PhysicsBodyId body) const noexcept
{
    return m_impl != nullptr && m_impl->open && m_impl->bodies.contains(body);
}

bool PhysicsWorld2D::contains(PhysicsShapeId shape) const noexcept
{
    return m_impl != nullptr && m_impl->open && m_impl->shapes.contains(shape);
}

PhysicsWorld2DStats PhysicsWorld2D::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return PhysicsWorld2DStats{
        m_impl->bodies.activeCount(),
        m_impl->shapes.activeCount(),
        m_impl->bodies.capacity(),
        m_impl->shapes.capacity(),
        m_impl->config.contactBeginCapacity,
        m_impl->config.contactEndCapacity,
        m_impl->config.contactHitCapacity,
        m_impl->completedStepCount,
        m_impl->droppedBeginContactCount,
        m_impl->droppedEndContactCount,
        m_impl->droppedHitContactCount,
        m_impl->open};
}

bool PhysicsWorld2D::isOpen() const noexcept
{
    return m_impl != nullptr && m_impl->open;
}

Core::Status PhysicsWorld2D::shutdown() noexcept
{
    if (m_impl == nullptr) {
        return Core::success();
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            Physics2DErrorCode::WrongOwnerThread,
            "Physics2D shutdown must run on the owner thread");
    }
    if (!m_impl->open) {
        return Core::success();
    }
    if (m_impl->inStep) {
        return Core::failure(
            Physics2DErrorCode::ReentrantMutation,
            "Physics2D shutdown cannot run during a solver step");
    }

    const bool backendWasValid = b2World_IsValid(m_impl->world);
    if (backendWasValid) {
        b2DestroyWorld(m_impl->world);
    }
    m_impl->world = b2_nullWorldId;
    m_impl->shapes.clear();
    m_impl->bodies.clear();
    m_impl->clearPublishedContacts();
    if (m_impl->shapeTombstones != nullptr) {
        for (Core::usize index = 0; index < m_impl->config.shapeCapacity; ++index) {
            m_impl->shapeTombstones[index] = {};
        }
    }
    m_impl->open = false;
    if (!backendWasValid) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D backend world was already invalid during shutdown");
    }
    return Core::success();
}

} // namespace Tina::Physics2D
