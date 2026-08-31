#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>

#include <box2d/box2d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Physics2D {

struct PhysicsWorld2D::Impl final {
    struct BodyRecord final {
        b2BodyId backend = b2_nullBodyId;
        PhysicsBodyId id{};
        PhysicsShapeId firstShape{};
        PhysicsBodyType2D type = PhysicsBodyType2D::Static;
    };

    struct ShapeRecord final {
        b2ShapeId backend = b2_nullShapeId;
        b2ChainId backendChain = b2_nullChainId;
        PhysicsShapeId id{};
        PhysicsBodyId body{};
        PhysicsShapeKind2D kind = PhysicsShapeKind2D::Box;
        bool isSensor = false;
        PhysicsCollisionFilter2D filter{};
        PhysicsShapeId previousOnBody{};
        PhysicsShapeId nextOnBody{};
        Core::u64 queryEpoch = 0;
        Core::usize queryOutputIndex = 0;
    };

    struct JointRecord final {
        b2JointId backend = b2_nullJointId;
        PhysicsJointId id{};
        PhysicsBodyId bodyA{};
        PhysicsBodyId bodyB{};
        PhysicsJointId previous{};
        PhysicsJointId next{};
    };

    struct ShapeTombstone final {
        std::array<b2ShapeId, MaximumChainVertices2D> backends{};
        Core::u32 backendCount = 0;
        PhysicsShapeId shape{};
        PhysicsBodyId body{};
        bool live = false;
    };

    using BodyPool = Core::GenerationPool<BodyRecord, Detail::PhysicsBodyRegistryTag>;
    using ShapePool = Core::GenerationPool<ShapeRecord, Detail::PhysicsShapeRegistryTag>;
    using JointPool = Core::GenerationPool<JointRecord, Detail::PhysicsJointRegistryTag>;

    Impl(
        PhysicsWorld2DConfig worldConfig,
        BodyPool bodyPool,
        ShapePool shapePool,
        JointPool jointPool,
        PhysicsContactBeginEvent2D* beginStorage,
        PhysicsContactEndEvent2D* endStorage,
        PhysicsContactHitEvent2D* hitStorage,
        ShapeTombstone* tombstoneStorage,
        PhysicsCommand2D* commandStorage,
        b2WorldId backendWorld,
        std::pmr::memory_resource& memoryResource) noexcept
        : config(worldConfig),
          resource(&memoryResource),
          bodies(std::move(bodyPool)),
          shapes(std::move(shapePool)),
          joints(std::move(jointPool)),
          beginContacts(beginStorage),
          endContacts(endStorage),
          hitContacts(hitStorage),
          shapeTombstones(tombstoneStorage),
          commands(commandStorage),
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
        if (commands != nullptr && config.commandCapacity > 0) {
            std::destroy_n(commands, config.commandCapacity);
            resource->deallocate(
                commands,
                sizeof(PhysicsCommand2D) * config.commandCapacity,
                alignof(PhysicsCommand2D));
            commands = nullptr;
        }
        commandCount = 0;
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
        if (shapeTombstones == nullptr || config.shapeCapacity == 0) {
            return;
        }
        std::array<b2ShapeId, MaximumChainVertices2D> destroyedBackends{};
        Core::u32 destroyedBackendCount = 0;
        if (B2_IS_NON_NULL(shapeRecord.backendChain) &&
            b2Chain_IsValid(shapeRecord.backendChain)) {
            const int segmentCount = b2Chain_GetSegments(
                shapeRecord.backendChain, destroyedBackends.data(),
                static_cast<int>(destroyedBackends.size()));
            if (segmentCount > 0) {
                destroyedBackendCount = static_cast<Core::u32>(segmentCount);
            }
        } else if (B2_IS_NON_NULL(shapeRecord.backend)) {
            destroyedBackends[0] = shapeRecord.backend;
            destroyedBackendCount = 1U;
        }
        if (destroyedBackendCount == 0U) {
            return;
        }

        const auto populate = [&](ShapeTombstone& tombstone) noexcept {
            tombstone.backends = destroyedBackends;
            tombstone.backendCount = destroyedBackendCount;
            tombstone.shape = shapeRecord.id;
            tombstone.body = shapeRecord.body;
            tombstone.live = true;
        };
        for (Core::usize index = 0; index < config.shapeCapacity; ++index) {
            ShapeTombstone& tombstone = shapeTombstones[index];
            if (!tombstone.live) {
                populate(tombstone);
                return;
            }
        }
        ShapeTombstone& overwrite = shapeTombstones[tombstoneWriteIndex % config.shapeCapacity];
        populate(overwrite);
        ++tombstoneWriteIndex;
    }

    [[nodiscard]] bool linkShapeToBody(BodyRecord& bodyRecord, ShapeRecord& shapeRecord) noexcept
    {
        shapeRecord.previousOnBody = {};
        shapeRecord.nextOnBody = bodyRecord.firstShape;
        if (bodyRecord.firstShape.hasValue()) {
            ShapeRecord* previousFirst = shapes.tryGet(bodyRecord.firstShape);
            if (previousFirst == nullptr) {
                shapeRecord.nextOnBody = {};
                return false;
            }
            previousFirst->previousOnBody = shapeRecord.id;
        }
        bodyRecord.firstShape = shapeRecord.id;
        return true;
    }

    [[nodiscard]] bool unlinkShapeFromBody(ShapeRecord& shapeRecord) noexcept
    {
        BodyRecord* bodyRecord = bodies.tryGet(shapeRecord.body);
        if (bodyRecord == nullptr) {
            return false;
        }
        if (shapeRecord.previousOnBody.hasValue()) {
            ShapeRecord* previous = shapes.tryGet(shapeRecord.previousOnBody);
            if (previous == nullptr) {
                return false;
            }
            previous->nextOnBody = shapeRecord.nextOnBody;
        } else {
            bodyRecord->firstShape = shapeRecord.nextOnBody;
        }
        if (shapeRecord.nextOnBody.hasValue()) {
            ShapeRecord* next = shapes.tryGet(shapeRecord.nextOnBody);
            if (next == nullptr) {
                return false;
            }
            next->previousOnBody = shapeRecord.previousOnBody;
        }
        shapeRecord.previousOnBody = {};
        shapeRecord.nextOnBody = {};
        return true;
    }

    [[nodiscard]] bool linkJoint(JointRecord& jointRecord) noexcept
    {
        jointRecord.previous = {};
        jointRecord.next = firstJoint;
        if (firstJoint.hasValue()) {
            JointRecord* previousFirst = joints.tryGet(firstJoint);
            if (previousFirst == nullptr) {
                jointRecord.next = {};
                return false;
            }
            previousFirst->previous = jointRecord.id;
        }
        firstJoint = jointRecord.id;
        return true;
    }

    [[nodiscard]] bool unlinkJoint(JointRecord& jointRecord) noexcept
    {
        if (jointRecord.previous.hasValue()) {
            JointRecord* previous = joints.tryGet(jointRecord.previous);
            if (previous == nullptr) {
                return false;
            }
            previous->next = jointRecord.next;
        } else {
            firstJoint = jointRecord.next;
        }
        if (jointRecord.next.hasValue()) {
            JointRecord* next = joints.tryGet(jointRecord.next);
            if (next == nullptr) {
                return false;
            }
            next->previous = jointRecord.previous;
        }
        jointRecord.previous = {};
        jointRecord.next = {};
        return true;
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
                if (!tombstone.live) {
                    continue;
                }
                for (Core::u32 backendIndex = 0;
                     backendIndex < tombstone.backendCount; ++backendIndex) {
                    if (B2_ID_EQUALS(tombstone.backends[backendIndex], backendShape)) {
                        endpoints.shape = tombstone.shape;
                        endpoints.body = tombstone.body;
                        endpoints.resolved = true;
                        endpoints.destroyed = true;
                        return endpoints;
                    }
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

        const auto publishBegin = [this](const ShapeEndpoints& shapeA,
                                         const ShapeEndpoints& shapeB,
                                         bool isSensor) noexcept {
            if (!shapeA.resolved || !shapeB.resolved || shapeA.destroyed || shapeB.destroyed) {
                return;
            }
            if (beginContacts == nullptr || beginCount >= config.contactBeginCapacity) {
                beginOverflow = true;
                ++droppedBeginContactCount;
                return;
            }
            beginContacts[beginCount++] = PhysicsContactBeginEvent2D{
                .bodyA = shapeA.body,
                .bodyB = shapeB.body,
                .shapeA = shapeA.shape,
                .shapeB = shapeB.shape,
                .isSensor = isSensor,
            };
        };
        const auto publishEnd = [this](const ShapeEndpoints& shapeA,
                                       const ShapeEndpoints& shapeB,
                                       bool isSensor) noexcept {
            if (!shapeA.resolved && !shapeB.resolved) {
                return;
            }
            if (endContacts == nullptr || endCount >= config.contactEndCapacity) {
                endOverflow = true;
                ++droppedEndContactCount;
                return;
            }
            endContacts[endCount++] = PhysicsContactEndEvent2D{
                .bodyA = shapeA.body,
                .bodyB = shapeB.body,
                .shapeA = shapeA.shape,
                .shapeB = shapeB.shape,
                .shapeADestroyed = shapeA.destroyed || !shapeA.resolved,
                .shapeBDestroyed = shapeB.destroyed || !shapeB.resolved,
                .isSensor = isSensor,
            };
        };

        const b2ContactEvents contactEvents = b2World_GetContactEvents(world);
        for (int index = 0; index < (std::max)(0, contactEvents.beginCount); ++index) {
            const b2ContactBeginTouchEvent& source = contactEvents.beginEvents[index];
            publishBegin(resolveShapeEndpoints(source.shapeIdA), resolveShapeEndpoints(source.shapeIdB), false);
        }
        for (int index = 0; index < (std::max)(0, contactEvents.endCount); ++index) {
            const b2ContactEndTouchEvent& source = contactEvents.endEvents[index];
            publishEnd(resolveShapeEndpoints(source.shapeIdA), resolveShapeEndpoints(source.shapeIdB), false);
        }

        const b2SensorEvents sensorEvents = b2World_GetSensorEvents(world);
        for (int index = 0; index < (std::max)(0, sensorEvents.beginCount); ++index) {
            const b2SensorBeginTouchEvent& source = sensorEvents.beginEvents[index];
            publishBegin(resolveShapeEndpoints(source.sensorShapeId),
                         resolveShapeEndpoints(source.visitorShapeId),
                         true);
        }
        for (int index = 0; index < (std::max)(0, sensorEvents.endCount); ++index) {
            const b2SensorEndTouchEvent& source = sensorEvents.endEvents[index];
            publishEnd(resolveShapeEndpoints(source.sensorShapeId),
                       resolveShapeEndpoints(source.visitorShapeId),
                       true);
        }

        for (int index = 0; index < (std::max)(0, contactEvents.hitCount); ++index) {
            const b2ContactHitEvent& source = contactEvents.hitEvents[index];
            const ShapeEndpoints shapeA = resolveShapeEndpoints(source.shapeIdA);
            const ShapeEndpoints shapeB = resolveShapeEndpoints(source.shapeIdB);
            if (!shapeA.resolved || !shapeB.resolved || shapeA.destroyed || shapeB.destroyed) {
                continue;
            }
            if (hitContacts == nullptr || hitCount >= config.contactHitCapacity) {
                hitOverflow = true;
                ++droppedHitContactCount;
                continue;
            }
            hitContacts[hitCount++] = PhysicsContactHitEvent2D{
                .bodyA = shapeA.body,
                .bodyB = shapeB.body,
                .shapeA = shapeA.shape,
                .shapeB = shapeB.shape,
                .pointMeters = {source.point.x, source.point.y},
                .normalFromAToB = {source.normal.x, source.normal.y},
                .approachSpeedMetersPerSecond = source.approachSpeed,
            };
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

    struct OverlapCollectContext final {
        Impl* world = nullptr;
        PhysicsOverlapHit2D* out = nullptr;
        Core::usize capacity = 0;
        Core::usize written = 0;
        Core::usize totalFound = 0;
        Core::u64 queryEpoch = 0;
    };

    struct CastCollectContext final {
        Impl* world = nullptr;
        PhysicsCastHit2D* out = nullptr;
        Core::usize capacity = 0;
        Core::usize written = 0;
        Core::usize totalFound = 0;
        Core::u64 queryEpoch = 0;
    };

    static bool collectOverlapHit(b2ShapeId shapeId, void* userContext)
    {
        auto* context = static_cast<OverlapCollectContext*>(userContext);
        if (context == nullptr || context->world == nullptr) {
            return false;
        }
        if (B2_IS_NULL(shapeId) || !b2Shape_IsValid(shapeId)) {
            return true;
        }
        void* userData = b2Shape_GetUserData(shapeId);
        if (userData == nullptr) {
            return true;
        }
        auto* shapeRecord = static_cast<ShapeRecord*>(userData);
        if (shapeRecord == nullptr
            || !shapeRecord->id.hasValue()
            || !context->world->shapes.contains(shapeRecord->id)
            || !context->world->bodies.contains(shapeRecord->body)) {
            return true;
        }

        if (shapeRecord->queryEpoch == context->queryEpoch) {
            return true;
        }
        shapeRecord->queryEpoch = context->queryEpoch;
        shapeRecord->queryOutputIndex = (std::numeric_limits<Core::usize>::max)();
        ++context->totalFound;
        if (context->written < context->capacity && context->out != nullptr) {
            shapeRecord->queryOutputIndex = context->written;
            context->out[context->written++] = PhysicsOverlapHit2D{
                shapeRecord->body,
                shapeRecord->id};
        }
        return true;
    }

    static float collectCastHit(
        b2ShapeId shapeId,
        b2Vec2 point,
        b2Vec2 normal,
        float fraction,
        void* userContext)
    {
        auto* context = static_cast<CastCollectContext*>(userContext);
        if (context == nullptr || context->world == nullptr) {
            return 0.0F;
        }
        if (B2_IS_NULL(shapeId) || !b2Shape_IsValid(shapeId)) {
            return -1.0F;
        }
        void* userData = b2Shape_GetUserData(shapeId);
        if (userData == nullptr) {
            return -1.0F;
        }
        auto* shapeRecord = static_cast<ShapeRecord*>(userData);
        if (shapeRecord == nullptr
            || !shapeRecord->id.hasValue()
            || !context->world->shapes.contains(shapeRecord->id)
            || !context->world->bodies.contains(shapeRecord->body)) {
            return -1.0F;
        }

        if (shapeRecord->queryEpoch == context->queryEpoch) {
            if (context->out != nullptr &&
                shapeRecord->queryOutputIndex < context->written &&
                fraction < context->out[shapeRecord->queryOutputIndex].fraction) {
                context->out[shapeRecord->queryOutputIndex] = PhysicsCastHit2D{
                    shapeRecord->body,
                    shapeRecord->id,
                    {point.x, point.y},
                    {normal.x, normal.y},
                    fraction};
            }
            return 1.0F;
        }
        shapeRecord->queryEpoch = context->queryEpoch;
        shapeRecord->queryOutputIndex = (std::numeric_limits<Core::usize>::max)();
        ++context->totalFound;
        if (context->written < context->capacity && context->out != nullptr) {
            shapeRecord->queryOutputIndex = context->written;
            context->out[context->written++] = PhysicsCastHit2D{
                shapeRecord->body,
                shapeRecord->id,
                {point.x, point.y},
                {normal.x, normal.y},
                fraction};
        }
        return 1.0F;
    }

    [[nodiscard]] Core::Status enqueueCommand(const PhysicsCommand2D& command) noexcept
    {
        if (commands == nullptr || commandCount >= config.commandCapacity) {
            return Core::failure(
                Physics2DErrorCode::CapacityExceeded,
                "Physics2D deferred command queue is full");
        }
        commands[commandCount++] = command;
        return Core::success();
    }

    void clearCommands() noexcept
    {
        commandCount = 0;
    }

    // Applies FIFO deferred commands immediately before the fixed Box2D step.
    // Stale body targets are skipped and counted; destroy uses the same path as
    // immediate destroyBody so generation handles retire consistently.
    void flushCommands(PhysicsWorld2D& owner) noexcept
    {
        if (commands == nullptr || commandCount == 0) {
            return;
        }
        const Core::usize total = commandCount;
        commandCount = 0;
        for (Core::usize index = 0; index < total; ++index) {
            const PhysicsCommand2D command = commands[index];
            if (!bodies.contains(command.body)) {
                ++skippedStaleCommandCount;
                continue;
            }
            BodyRecord* bodyRecord = bodies.tryGet(command.body);
            if (bodyRecord == nullptr || !b2Body_IsValid(bodyRecord->backend)) {
                ++skippedStaleCommandCount;
                continue;
            }

            switch (command.kind) {
            case PhysicsCommandKind2D::DestroyBody: {
                const Core::Status status = owner.destroyBody(command.body);
                if (status) {
                    ++appliedCommandCount;
                } else {
                    ++skippedStaleCommandCount;
                }
                break;
            }
            case PhysicsCommandKind2D::SetTransform:
                b2Body_SetTransform(
                    bodyRecord->backend,
                    {command.vectorMeters.x, command.vectorMeters.y},
                    b2MakeRot(command.scalar));
                ++appliedCommandCount;
                break;
            case PhysicsCommandKind2D::SetLinearVelocity:
                b2Body_SetLinearVelocity(
                    bodyRecord->backend,
                    {command.vectorMeters.x, command.vectorMeters.y});
                ++appliedCommandCount;
                break;
            case PhysicsCommandKind2D::SetAngularVelocity:
                b2Body_SetAngularVelocity(bodyRecord->backend, command.scalar);
                ++appliedCommandCount;
                break;
            case PhysicsCommandKind2D::ApplyForceToCenter:
                b2Body_ApplyForceToCenter(
                    bodyRecord->backend,
                    {command.vectorMeters.x, command.vectorMeters.y},
                    command.flag);
                ++appliedCommandCount;
                break;
            case PhysicsCommandKind2D::ApplyLinearImpulseToCenter:
                b2Body_ApplyLinearImpulseToCenter(
                    bodyRecord->backend,
                    {command.vectorMeters.x, command.vectorMeters.y},
                    command.flag);
                ++appliedCommandCount;
                break;
            case PhysicsCommandKind2D::SetEnabled:
                if (command.flag) {
                    b2Body_Enable(bodyRecord->backend);
                } else {
                    b2Body_Disable(bodyRecord->backend);
                }
                ++appliedCommandCount;
                break;
            case PhysicsCommandKind2D::SetAwake:
                b2Body_SetAwake(bodyRecord->backend, command.flag);
                ++appliedCommandCount;
                break;
            }
        }
    }

    PhysicsWorld2DConfig config{};
    std::thread::id ownerThread = std::this_thread::get_id();
    std::pmr::memory_resource* resource = nullptr;
    BodyPool bodies;
    ShapePool shapes;
    JointPool joints;
    PhysicsContactBeginEvent2D* beginContacts = nullptr;
    PhysicsContactEndEvent2D* endContacts = nullptr;
    PhysicsContactHitEvent2D* hitContacts = nullptr;
    ShapeTombstone* shapeTombstones = nullptr;
    PhysicsCommand2D* commands = nullptr;
    Core::usize beginCount = 0;
    Core::usize endCount = 0;
    Core::usize hitCount = 0;
    Core::usize commandCount = 0;
    Core::usize tombstoneWriteIndex = 0;
    bool beginOverflow = false;
    bool endOverflow = false;
    bool hitOverflow = false;
    Core::u64 droppedBeginContactCount = 0;
    Core::u64 droppedEndContactCount = 0;
    Core::u64 droppedHitContactCount = 0;
    Core::u64 appliedCommandCount = 0;
    Core::u64 skippedStaleCommandCount = 0;
    b2WorldId world = b2_nullWorldId;
    PhysicsJointId firstJoint{};
    Core::u64 completedStepCount = 0;
    bool open = true;
    bool inStep = false;
    Core::u64 nextQueryEpoch = 1;
};

namespace {

static_assert(MaximumConvexPolygonVertices2D == B2_MAX_POLYGON_VERTICES);

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

[[nodiscard]] bool isKnownShapeKind(PhysicsShapeKind2D kind) noexcept
{
    switch (kind) {
    case PhysicsShapeKind2D::Box:
    case PhysicsShapeKind2D::Circle:
    case PhysicsShapeKind2D::Capsule:
    case PhysicsShapeKind2D::ConvexPolygon:
    case PhysicsShapeKind2D::Chain:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownJointKind(PhysicsJointKind2D kind) noexcept
{
    switch (kind) {
    case PhysicsJointKind2D::Distance:
    case PhysicsJointKind2D::Revolute:
    case PhysicsJointKind2D::Prismatic:
        return true;
    }
    return false;
}

[[nodiscard]] bool tryNormalizeDirection(Math::Vec2 direction, b2Vec2& normalized) noexcept
{
    if (!isFinite(direction)) {
        return false;
    }
    const float scale = (std::max)(std::abs(direction.x), std::abs(direction.y));
    if (!(scale > 0.0F)) {
        return false;
    }

    const float scaledX = direction.x / scale;
    const float scaledY = direction.y / scale;
    const float scaledLength = std::sqrt(scaledX * scaledX + scaledY * scaledY);
    if (!std::isfinite(scaledLength) || !(scaledLength > 0.0F)) {
        return false;
    }
    normalized = {scaledX / scaledLength, scaledY / scaledLength};
    return std::isfinite(normalized.x) && std::isfinite(normalized.y);
}

[[nodiscard]] bool pointsEqual(b2Vec2 left, b2Vec2 right) noexcept
{
    return left.x == right.x && left.y == right.y;
}

[[nodiscard]] bool polygonBoundaryMatchesHull(
    std::span<const b2Vec2> boundary,
    const b2Hull& hull) noexcept
{
    if (boundary.size() != static_cast<Core::usize>(hull.count) || boundary.empty()) {
        return false;
    }

    Core::usize hullStart = boundary.size();
    for (Core::usize index = 0; index < boundary.size(); ++index) {
        if (pointsEqual(boundary.front(), hull.points[index])) {
            hullStart = index;
            break;
        }
    }
    if (hullStart == boundary.size()) {
        return false;
    }

    bool forwardMatches = true;
    bool reverseMatches = true;
    for (Core::usize index = 0; index < boundary.size(); ++index) {
        const Core::usize forwardIndex = (hullStart + index) % boundary.size();
        const Core::usize reverseIndex =
            (hullStart + boundary.size() - index) % boundary.size();
        forwardMatches = forwardMatches
            && pointsEqual(boundary[index], hull.points[forwardIndex]);
        reverseMatches = reverseMatches
            && pointsEqual(boundary[index], hull.points[reverseIndex]);
    }
    return forwardMatches || reverseMatches;
}

[[nodiscard]] Core::Status buildBackendConvexPolygon(
    const PhysicsShape2DDesc& desc,
    b2Polygon& polygon) noexcept
{
    if (desc.polygonVertexCount < 3U
        || desc.polygonVertexCount > MaximumConvexPolygonVertices2D) {
        return Core::failure(
            Physics2DErrorCode::InvalidShapeDescription,
            "Physics2D convex polygon requires between 3 and 8 vertices");
    }
    if (!isFinite(desc.localCenterMeters) || !std::isfinite(desc.localAngleRadians)) {
        return Core::failure(
            Physics2DErrorCode::InvalidShapeDescription,
            "Physics2D convex polygon local transform must be finite");
    }

    std::array<b2Vec2, MaximumConvexPolygonVertices2D> vertices{};
    const float cosine = std::cos(desc.localAngleRadians);
    const float sine = std::sin(desc.localAngleRadians);
    for (Core::u32 index = 0; index < desc.polygonVertexCount; ++index) {
        const Math::Vec2 vertex = desc.polygonVertices[index];
        if (!isFinite(vertex)) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D convex polygon vertices must be finite");
        }
        vertices[index] = {
            cosine * vertex.x - sine * vertex.y + desc.localCenterMeters.x,
            sine * vertex.x + cosine * vertex.y + desc.localCenterMeters.y,
        };
        if (!std::isfinite(vertices[index].x) || !std::isfinite(vertices[index].y)) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D convex polygon local transform must remain finite");
        }
    }

    const std::span<const b2Vec2> boundary(
        vertices.data(),
        static_cast<Core::usize>(desc.polygonVertexCount));
    const b2Hull hull = b2ComputeHull(
        boundary.data(),
        static_cast<int>(boundary.size()));
    if (hull.count != static_cast<int>(boundary.size())
        || !b2ValidateHull(&hull)
        || !polygonBoundaryMatchesHull(boundary, hull)) {
        return Core::failure(
            Physics2DErrorCode::InvalidShapeDescription,
            "Physics2D polygon vertices must form a backend-valid strictly convex boundary");
    }

    polygon = b2MakePolygon(&hull, 0.0F);
    return Core::success();
}

[[nodiscard]] bool tryMapBackendJointKind(
    b2JointType backendKind,
    PhysicsJointKind2D& kind) noexcept
{
    switch (backendKind) {
    case b2_distanceJoint:
        kind = PhysicsJointKind2D::Distance;
        return true;
    case b2_revoluteJoint:
        kind = PhysicsJointKind2D::Revolute;
        return true;
    case b2_prismaticJoint:
        kind = PhysicsJointKind2D::Prismatic;
        return true;
    default:
        return false;
    }
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
    if (config.bodyCapacity == 0 || config.shapeCapacity == 0 || config.jointCapacity == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D body, shape, and joint capacities must be greater than zero");
    }
    if (config.bodyCapacity > PhysicsWorld2DConfig::MaxBodyCapacity
        || config.bodyCapacity > PhysicsBodyId::InvalidIndex
        || config.shapeCapacity > PhysicsWorld2DConfig::MaxShapeCapacity
        || config.shapeCapacity > PhysicsShapeId::InvalidIndex
        || config.jointCapacity > PhysicsWorld2DConfig::MaxJointCapacity
        || config.jointCapacity > PhysicsJointId::InvalidIndex) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D body, shape, or joint capacity exceeds the supported generation range");
    }
    if (config.contactBeginCapacity > PhysicsWorld2DConfig::MaxContactEventCapacity
        || config.contactEndCapacity > PhysicsWorld2DConfig::MaxContactEventCapacity
        || config.contactHitCapacity > PhysicsWorld2DConfig::MaxContactEventCapacity
        || config.commandCapacity > PhysicsWorld2DConfig::MaxCommandCapacity) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D contact or command capacity exceeds the supported range");
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

Core::Status validatePhysicsShape2DDesc(const PhysicsShape2DDesc& desc) noexcept
{
    if (!isKnownShapeKind(desc.kind)
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
            "Physics2D shape contains invalid material or collision filter values");
    }
    switch (desc.kind) {
    case PhysicsShapeKind2D::Box:
        if (!isFinite(desc.halfExtentsMeters)
            || !isFinite(desc.localCenterMeters)
            || !std::isfinite(desc.localAngleRadians)
            || desc.halfExtentsMeters.x <= 0.0F
            || desc.halfExtentsMeters.y <= 0.0F) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D box shape requires finite local transform and positive half extents");
        }
        break;
    case PhysicsShapeKind2D::Circle:
        if (!std::isfinite(desc.radiusMeters)
            || !isFinite(desc.localCenterMeters)
            || !(desc.radiusMeters > 0.0F)) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D circle shape requires a finite local center and positive radius");
        }
        break;
    case PhysicsShapeKind2D::Capsule:
        if (!std::isfinite(desc.radiusMeters)
            || !isFinite(desc.localPointAMeters)
            || !isFinite(desc.localPointBMeters)
            || !(desc.radiusMeters > 0.0F)
            || (desc.localPointAMeters.x == desc.localPointBMeters.x
                && desc.localPointAMeters.y == desc.localPointBMeters.y)) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D capsule shape requires finite distinct endpoints and positive radius");
        }
        break;
    case PhysicsShapeKind2D::ConvexPolygon: {
        b2Polygon polygon{};
        if (const Core::Status status = buildBackendConvexPolygon(desc, polygon); !status) {
            return status;
        }
        break;
    }
    case PhysicsShapeKind2D::Chain:
        if (desc.chainVertexCount < MinimumChainVertices2D ||
            desc.chainVertexCount > MaximumChainVertices2D || desc.isSensor) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D chain requires 4..64 vertices and cannot be a sensor");
        }
        for (Core::u32 left = 0; left < desc.chainVertexCount; ++left) {
            if (!isFinite(desc.chainVertices[left])) {
                return Core::failure(
                    Physics2DErrorCode::InvalidShapeDescription,
                    "Physics2D chain vertices must be finite");
            }
            for (Core::u32 right = left + 1U; right < desc.chainVertexCount; ++right) {
                const double deltaX = static_cast<double>(desc.chainVertices[right].x) -
                                      desc.chainVertices[left].x;
                const double deltaY = static_cast<double>(desc.chainVertices[right].y) -
                                      desc.chainVertices[left].y;
                const double separationSquared = deltaX * deltaX + deltaY * deltaY;
                constexpr double MinimumSeparationSquared =
                    static_cast<double>(MinimumChainVertexSeparationMeters2D) *
                    MinimumChainVertexSeparationMeters2D;
                if (!std::isfinite(separationSquared) ||
                    separationSquared <= MinimumSeparationSquared) {
                    return Core::failure(
                        Physics2DErrorCode::InvalidShapeDescription,
                        "Physics2D chain vertices must be distinct and separated by more than 0.005 m");
                }
            }
        }
        break;
    }
    return Core::success();
}

Core::Status validatePhysicsJoint2DDesc(const PhysicsJoint2DDesc& desc) noexcept
{
    if (!isKnownJointKind(desc.kind) || !desc.bodyA.hasValue() || !desc.bodyB.hasValue()
        || desc.bodyA == desc.bodyB || !isFinite(desc.localAnchorAMeters) || !isFinite(desc.localAnchorBMeters)
        || !std::isfinite(desc.hertz) || !std::isfinite(desc.dampingRatio) || desc.hertz < 0.0F
        || desc.dampingRatio < 0.0F) {
        return Core::failure(
            Physics2DErrorCode::InvalidJointDescription,
            "Physics2D joint description has invalid common fields");
    }

    switch (desc.kind) {
    case PhysicsJointKind2D::Distance:
        if (!std::isfinite(desc.lengthMeters) || !(desc.lengthMeters > 0.0F)) {
            return Core::failure(
                Physics2DErrorCode::InvalidJointDescription,
                "Physics2D distance joint requires a positive finite length");
        }
        break;
    case PhysicsJointKind2D::Revolute: {
        constexpr float MaximumAngleRadians = std::numbers::pi_v<float>;
        constexpr float MaximumLimitRadians = 0.99F * std::numbers::pi_v<float>;
        if (!std::isfinite(desc.referenceAngleRadians) || !std::isfinite(desc.targetAngleRadians)
            || desc.referenceAngleRadians < -MaximumAngleRadians
            || desc.referenceAngleRadians > MaximumAngleRadians
            || desc.targetAngleRadians < -MaximumAngleRadians
            || desc.targetAngleRadians > MaximumAngleRadians
            || !std::isfinite(desc.lowerAngleRadians) || !std::isfinite(desc.upperAngleRadians)
            || desc.lowerAngleRadians < -MaximumLimitRadians
            || desc.upperAngleRadians > MaximumLimitRadians
            || desc.lowerAngleRadians > desc.upperAngleRadians
            || !std::isfinite(desc.motorSpeedRadiansPerSecond)
            || !std::isfinite(desc.maxMotorTorqueNewtonMeters)
            || desc.maxMotorTorqueNewtonMeters < 0.0F) {
            return Core::failure(
                Physics2DErrorCode::InvalidJointDescription,
                "Physics2D revolute joint angle, limit, or motor fields are invalid");
        }
        break;
    }
    case PhysicsJointKind2D::Prismatic: {
        b2Vec2 normalizedAxis{};
        if (!tryNormalizeDirection(desc.localAxisA, normalizedAxis)
            || !std::isfinite(desc.referenceAngleRadians) || !std::isfinite(desc.targetTranslationMeters)
            || !std::isfinite(desc.lowerTranslationMeters) || !std::isfinite(desc.upperTranslationMeters)
            || desc.lowerTranslationMeters > desc.upperTranslationMeters
            || !std::isfinite(desc.motorSpeedMetersPerSecond)
            || !std::isfinite(desc.maxMotorForceNewtons) || desc.maxMotorForceNewtons < 0.0F) {
            return Core::failure(
                Physics2DErrorCode::InvalidJointDescription,
                "Physics2D prismatic joint axis, limits, or motor fields are invalid");
        }
        break;
    }
    }
    return Core::success();
}

Core::Status validatePhysicsQueryFilter2D(const PhysicsQueryFilter2D& filter) noexcept
{
    if (filter.categoryBits == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D query filter categoryBits must be non-zero");
    }
    return Core::success();
}

Core::Status validatePhysicsAabb2D(const PhysicsAabb2D& aabb) noexcept
{
    if (!isFinite(aabb.lowerMeters)
        || !isFinite(aabb.upperMeters)
        || aabb.lowerMeters.x > aabb.upperMeters.x
        || aabb.lowerMeters.y > aabb.upperMeters.y) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D AABB must be finite with lower corner not above upper corner");
    }
    return Core::success();
}

Core::Status validatePhysicsRayCast2D(const PhysicsRayCast2D& ray) noexcept
{
    if (!isFinite(ray.originMeters) || !isFinite(ray.translationMeters)) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D ray cast origin and translation must be finite");
    }
    if (ray.translationMeters.x == 0.0F && ray.translationMeters.y == 0.0F) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D ray cast translation must be non-zero");
    }
    return Core::success();
}

namespace {

[[nodiscard]] b2QueryFilter toBackendQueryFilter(const PhysicsQueryFilter2D& filter) noexcept
{
    b2QueryFilter backend = b2DefaultQueryFilter();
    backend.categoryBits = filter.categoryBits;
    backend.maskBits = filter.maskBits;
    return backend;
}

[[nodiscard]] bool overlapHitLess(
    const PhysicsOverlapHit2D& left,
    const PhysicsOverlapHit2D& right) noexcept
{
    if (left.body.index() != right.body.index()) {
        return left.body.index() < right.body.index();
    }
    if (left.shape.index() != right.shape.index()) {
        return left.shape.index() < right.shape.index();
    }
    if (left.body.generation() != right.body.generation()) {
        return left.body.generation() < right.body.generation();
    }
    return left.shape.generation() < right.shape.generation();
}

[[nodiscard]] bool castHitLess(const PhysicsCastHit2D& left, const PhysicsCastHit2D& right) noexcept
{
    if (left.fraction != right.fraction) {
        return left.fraction < right.fraction;
    }
    if (left.body.index() != right.body.index()) {
        return left.body.index() < right.body.index();
    }
    if (left.shape.index() != right.shape.index()) {
        return left.shape.index() < right.shape.index();
    }
    if (left.body.generation() != right.body.generation()) {
        return left.body.generation() < right.body.generation();
    }
    return left.shape.generation() < right.shape.generation();
}

} // namespace

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
    auto jointPoolResult = Impl::JointPool::Create(config.jointCapacity, resource);
    if (!jointPoolResult) {
        return Core::failure(mapPoolError(
            std::move(jointPoolResult.error()),
            "PhysicsWorld2D::Create",
            "joint registry allocation"));
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
    auto commandStorageResult = allocateContactBuffer<PhysicsCommand2D>(
        resource,
        config.commandCapacity,
        "Physics2D deferred command buffer allocation failed");
    if (!commandStorageResult) {
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
        return Core::failure(commandStorageResult.error());
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
        if (*commandStorageResult != nullptr && config.commandCapacity > 0) {
            std::destroy_n(*commandStorageResult, config.commandCapacity);
            resource.deallocate(
                *commandStorageResult,
                sizeof(PhysicsCommand2D) * config.commandCapacity,
                alignof(PhysicsCommand2D));
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
            std::move(*jointPoolResult),
            *beginStorageResult,
            *endStorageResult,
            *hitStorageResult,
            *tombstoneStorageResult,
            *commandStorageResult,
            backendWorld,
            resource);
        b2World_SetUserData(backendWorld, impl);
        return PhysicsWorld2D(impl);
    } catch (const std::bad_alloc&) {
        if (storage != nullptr) {
            resource.deallocate(storage, sizeof(Impl), alignof(Impl));
        }
        if (*commandStorageResult != nullptr && config.commandCapacity > 0) {
            std::destroy_n(*commandStorageResult, config.commandCapacity);
            resource.deallocate(
                *commandStorageResult,
                sizeof(PhysicsCommand2D) * config.commandCapacity,
                alignof(PhysicsCommand2D));
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
        if (*commandStorageResult != nullptr && config.commandCapacity > 0) {
            std::destroy_n(*commandStorageResult, config.commandCapacity);
            resource.deallocate(
                *commandStorageResult,
                sizeof(PhysicsCommand2D) * config.commandCapacity,
                alignof(PhysicsCommand2D));
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
        if (*commandStorageResult != nullptr && config.commandCapacity > 0) {
            std::destroy_n(*commandStorageResult, config.commandCapacity);
            resource.deallocate(
                *commandStorageResult,
                sizeof(PhysicsCommand2D) * config.commandCapacity,
                alignof(PhysicsCommand2D));
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

Core::Status PhysicsWorld2D::validateJoint(PhysicsJointId joint) const noexcept
{
    if (!joint.hasValue()) {
        return Core::failure(
            Physics2DErrorCode::InvalidJoint,
            "Physics2D joint handle is invalid");
    }
    if (joint.owner() != m_impl->joints.owner()) {
        return Core::failure(
            Physics2DErrorCode::WrongWorld,
            "Physics2D joint belongs to another world");
    }
    if (!m_impl->joints.contains(joint)) {
        return Core::failure(
            Physics2DErrorCode::StaleJoint,
            "Physics2D joint handle is stale");
    }
    return Core::success();
}

Core::Result<PhysicsBodyId> PhysicsWorld2D::createBody(const PhysicsBody2DDesc& bodyDescription)
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsBody2DDesc(bodyDescription); !status) {
        return Core::failure(status.error());
    }
    auto bodyIdResult = m_impl->bodies.tryEmplace();
    if (!bodyIdResult) {
        return Core::failure(mapPoolError(
            std::move(bodyIdResult.error()),
            "PhysicsWorld2D::createBody",
            "body registry capacity"));
    }
    const PhysicsBodyId bodyId = *bodyIdResult;

    Impl::BodyRecord* bodyRecord = m_impl->bodies.tryGet(bodyId);
    if (bodyRecord == nullptr) {
        (void)m_impl->bodies.erase(bodyId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D could not resolve a newly reserved body slot");
    }
    bodyRecord->id = bodyId;
    bodyRecord->type = bodyDescription.type;

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
        (void)m_impl->bodies.erase(bodyId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Box2D failed to create a body");
    }

    return bodyId;
}

Core::Result<PhysicsShapeId> PhysicsWorld2D::createShape(
    PhysicsBodyId body,
    const PhysicsShape2DDesc& shapeDescription)
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateBody(body); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsShape2DDesc(shapeDescription); !status) {
        return Core::failure(status.error());
    }

    b2Polygon backendConvexPolygon{};
    if (shapeDescription.kind == PhysicsShapeKind2D::ConvexPolygon) {
        if (const Core::Status status =
                buildBackendConvexPolygon(shapeDescription, backendConvexPolygon);
            !status) {
            return Core::failure(status.error());
        }
    }

    Impl::BodyRecord* bodyRecord = m_impl->bodies.tryGet(body);
    if (bodyRecord == nullptr || !b2Body_IsValid(bodyRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D body registry lost its Box2D body");
    }
    if (shapeDescription.kind == PhysicsShapeKind2D::Chain &&
        bodyRecord->type != PhysicsBodyType2D::Static) {
        return Core::failure(
            Physics2DErrorCode::InvalidShapeDescription,
            "Physics2D chain shapes require a static body");
    }

    auto shapeIdResult = m_impl->shapes.tryEmplace();
    if (!shapeIdResult) {
        return Core::failure(mapPoolError(
            std::move(shapeIdResult.error()),
            "PhysicsWorld2D::createShape",
            "shape registry capacity"));
    }
    const PhysicsShapeId shapeId = *shapeIdResult;
    Impl::ShapeRecord* shapeRecord = m_impl->shapes.tryGet(shapeId);
    if (shapeRecord == nullptr) {
        (void)m_impl->shapes.erase(shapeId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D could not resolve a newly reserved shape slot");
    }
    shapeRecord->id = shapeId;
    shapeRecord->body = body;
    shapeRecord->kind = shapeDescription.kind;
    shapeRecord->isSensor = shapeDescription.isSensor;
    shapeRecord->filter = shapeDescription.filter;

    b2ShapeDef shapeDefinition = b2DefaultShapeDef();
    shapeDefinition.userData = shapeRecord;
    shapeDefinition.material.friction = shapeDescription.friction;
    shapeDefinition.material.restitution = shapeDescription.restitution;
    shapeDefinition.density = shapeDescription.density;
    shapeDefinition.filter.categoryBits = shapeDescription.filter.categoryBits;
    shapeDefinition.filter.maskBits = shapeDescription.filter.maskBits;
    shapeDefinition.filter.groupIndex = shapeDescription.filter.groupIndex;
    shapeDefinition.isSensor = shapeDescription.isSensor;
    shapeDefinition.enableSensorEvents = shapeDescription.enableSensorEvents;
    shapeDefinition.enableContactEvents = shapeDescription.enableContactEvents;
    shapeDefinition.enableHitEvents = shapeDescription.enableHitEvents;

    switch (shapeDescription.kind) {
    case PhysicsShapeKind2D::Box: {
        const b2Polygon polygon = b2MakeOffsetBox(
            shapeDescription.halfExtentsMeters.x,
            shapeDescription.halfExtentsMeters.y,
            {shapeDescription.localCenterMeters.x, shapeDescription.localCenterMeters.y},
            b2MakeRot(shapeDescription.localAngleRadians));
        shapeRecord->backend = b2CreatePolygonShape(bodyRecord->backend, &shapeDefinition, &polygon);
        break;
    }
    case PhysicsShapeKind2D::Circle: {
        const b2Circle circle{
            {shapeDescription.localCenterMeters.x, shapeDescription.localCenterMeters.y},
            shapeDescription.radiusMeters,
        };
        shapeRecord->backend = b2CreateCircleShape(bodyRecord->backend, &shapeDefinition, &circle);
        break;
    }
    case PhysicsShapeKind2D::Capsule: {
        const b2Capsule capsule{
            {shapeDescription.localPointAMeters.x, shapeDescription.localPointAMeters.y},
            {shapeDescription.localPointBMeters.x, shapeDescription.localPointBMeters.y},
            shapeDescription.radiusMeters,
        };
        shapeRecord->backend = b2CreateCapsuleShape(bodyRecord->backend, &shapeDefinition, &capsule);
        break;
    }
    case PhysicsShapeKind2D::ConvexPolygon:
        shapeRecord->backend =
            b2CreatePolygonShape(bodyRecord->backend, &shapeDefinition, &backendConvexPolygon);
        break;
    case PhysicsShapeKind2D::Chain: {
        std::array<b2Vec2, MaximumChainVertices2D> points{};
        for (Core::u32 index = 0; index < shapeDescription.chainVertexCount; ++index) {
            points[index] = {
                shapeDescription.chainVertices[index].x,
                shapeDescription.chainVertices[index].y};
        }
        b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
        material.friction = shapeDescription.friction;
        material.restitution = shapeDescription.restitution;
        b2ChainDef chainDefinition = b2DefaultChainDef();
        chainDefinition.userData = shapeRecord;
        chainDefinition.points = points.data();
        chainDefinition.count = static_cast<int>(shapeDescription.chainVertexCount);
        chainDefinition.materials = &material;
        chainDefinition.materialCount = 1;
        chainDefinition.filter.categoryBits = shapeDescription.filter.categoryBits;
        chainDefinition.filter.maskBits = shapeDescription.filter.maskBits;
        chainDefinition.filter.groupIndex = shapeDescription.filter.groupIndex;
        chainDefinition.isLoop = shapeDescription.chainLoop;
        chainDefinition.enableSensorEvents = shapeDescription.enableSensorEvents;
        shapeRecord->backendChain = b2CreateChain(bodyRecord->backend, &chainDefinition);
        if (B2_IS_NON_NULL(shapeRecord->backendChain) &&
            b2Chain_IsValid(shapeRecord->backendChain)) {
            std::array<b2ShapeId, MaximumChainVertices2D> segments{};
            const int segmentCount = b2Chain_GetSegments(
                shapeRecord->backendChain, segments.data(),
                static_cast<int>(segments.size()));
            if (segmentCount > 0) {
                shapeRecord->backend = segments[0];
                for (int index = 0; index < segmentCount; ++index) {
                    b2Shape_EnableSensorEvents(
                        segments[index], shapeDescription.enableSensorEvents);
                    b2Shape_EnableContactEvents(
                        segments[index], shapeDescription.enableContactEvents);
                    b2Shape_EnableHitEvents(
                        segments[index], shapeDescription.enableHitEvents);
                }
            }
        }
        break;
    }
    }

    const bool backendShapeValid = shapeDescription.kind == PhysicsShapeKind2D::Chain
        ? B2_IS_NON_NULL(shapeRecord->backendChain) && b2Chain_IsValid(shapeRecord->backendChain) &&
              B2_IS_NON_NULL(shapeRecord->backend) && b2Shape_IsValid(shapeRecord->backend)
        : B2_IS_NON_NULL(shapeRecord->backend) && b2Shape_IsValid(shapeRecord->backend);
    if (!backendShapeValid) {
        if (B2_IS_NON_NULL(shapeRecord->backendChain) &&
            b2Chain_IsValid(shapeRecord->backendChain)) {
            b2DestroyChain(shapeRecord->backendChain);
        }
        shapeRecord->backend = b2_nullShapeId;
        shapeRecord->backendChain = b2_nullChainId;
        (void)m_impl->shapes.erase(shapeId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Box2D failed to create a shape");
    }
    if (!m_impl->linkShapeToBody(*bodyRecord, *shapeRecord)) {
        if (B2_IS_NON_NULL(shapeRecord->backendChain)) {
            b2DestroyChain(shapeRecord->backendChain);
        } else {
            b2DestroyShape(shapeRecord->backend, true);
        }
        shapeRecord->backend = b2_nullShapeId;
        shapeRecord->backendChain = b2_nullChainId;
        (void)m_impl->shapes.erase(shapeId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D body shape list is inconsistent");
    }
    return shapeId;
}

Core::Status PhysicsWorld2D::destroyShape(PhysicsShapeId shape) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateShape(shape); !status) {
        return status;
    }

    Impl::ShapeRecord* shapeRecord = m_impl->shapes.tryGet(shape);
    const bool backendShapeValid = shapeRecord != nullptr &&
        ((B2_IS_NON_NULL(shapeRecord->backendChain) &&
          b2Chain_IsValid(shapeRecord->backendChain)) ||
         (B2_IS_NULL(shapeRecord->backendChain) &&
          B2_IS_NON_NULL(shapeRecord->backend) && b2Shape_IsValid(shapeRecord->backend)));
    if (!backendShapeValid) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D shape registry lost its Box2D shape");
    }
    if (!m_impl->unlinkShapeFromBody(*shapeRecord)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D body shape list is inconsistent");
    }

    m_impl->rememberDestroyedShape(*shapeRecord);
    if (B2_IS_NON_NULL(shapeRecord->backendChain)) {
        b2DestroyChain(shapeRecord->backendChain);
    } else {
        b2DestroyShape(shapeRecord->backend, true);
    }
    shapeRecord->backend = b2_nullShapeId;
    shapeRecord->backendChain = b2_nullChainId;
    const Core::GenerationEraseResult shapeEraseResult = m_impl->shapes.erase(shape);
    if (shapeEraseResult != Core::GenerationEraseResult::Erased) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D failed to retire a destroyed shape slot");
    }
    return Core::success();
}

Core::Result<PhysicsJointId> PhysicsWorld2D::createJoint(const PhysicsJoint2DDesc& jointDescription)
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsJoint2DDesc(jointDescription); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateBody(jointDescription.bodyA); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateBody(jointDescription.bodyB); !status) {
        return Core::failure(status.error());
    }

    b2Vec2 backendPrismaticAxis{};
    if (jointDescription.kind == PhysicsJointKind2D::Prismatic
        && !tryNormalizeDirection(jointDescription.localAxisA, backendPrismaticAxis)) {
        return Core::failure(
            Physics2DErrorCode::InvalidJointDescription,
            "Physics2D prismatic joint axis must be a finite non-zero direction");
    }

    Impl::BodyRecord* bodyA = m_impl->bodies.tryGet(jointDescription.bodyA);
    Impl::BodyRecord* bodyB = m_impl->bodies.tryGet(jointDescription.bodyB);
    if (bodyA == nullptr || bodyB == nullptr || !b2Body_IsValid(bodyA->backend) || !b2Body_IsValid(bodyB->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D joint body registry lost a Box2D body");
    }

    auto jointIdResult = m_impl->joints.tryEmplace();
    if (!jointIdResult) {
        return Core::failure(mapPoolError(
            std::move(jointIdResult.error()),
            "PhysicsWorld2D::createJoint",
            "joint registry capacity"));
    }
    const PhysicsJointId jointId = *jointIdResult;
    Impl::JointRecord* jointRecord = m_impl->joints.tryGet(jointId);
    if (jointRecord == nullptr) {
        (void)m_impl->joints.erase(jointId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D could not resolve a newly reserved joint slot");
    }
    jointRecord->id = jointId;
    jointRecord->bodyA = jointDescription.bodyA;
    jointRecord->bodyB = jointDescription.bodyB;

    switch (jointDescription.kind) {
    case PhysicsJointKind2D::Distance: {
        b2DistanceJointDef definition = b2DefaultDistanceJointDef();
        definition.bodyIdA = bodyA->backend;
        definition.bodyIdB = bodyB->backend;
        definition.localAnchorA = {jointDescription.localAnchorAMeters.x, jointDescription.localAnchorAMeters.y};
        definition.localAnchorB = {jointDescription.localAnchorBMeters.x, jointDescription.localAnchorBMeters.y};
        definition.length = jointDescription.lengthMeters;
        definition.enableSpring = jointDescription.enableSpring;
        definition.hertz = jointDescription.hertz;
        definition.dampingRatio = jointDescription.dampingRatio;
        definition.collideConnected = jointDescription.collideConnected;
        definition.userData = jointRecord;
        jointRecord->backend = b2CreateDistanceJoint(m_impl->world, &definition);
        break;
    }
    case PhysicsJointKind2D::Revolute: {
        b2RevoluteJointDef definition = b2DefaultRevoluteJointDef();
        definition.bodyIdA = bodyA->backend;
        definition.bodyIdB = bodyB->backend;
        definition.localAnchorA = {jointDescription.localAnchorAMeters.x, jointDescription.localAnchorAMeters.y};
        definition.localAnchorB = {jointDescription.localAnchorBMeters.x, jointDescription.localAnchorBMeters.y};
        definition.referenceAngle = jointDescription.referenceAngleRadians;
        definition.targetAngle = jointDescription.targetAngleRadians;
        definition.enableSpring = jointDescription.enableSpring;
        definition.hertz = jointDescription.hertz;
        definition.dampingRatio = jointDescription.dampingRatio;
        definition.enableLimit = jointDescription.enableLimit;
        definition.lowerAngle = jointDescription.lowerAngleRadians;
        definition.upperAngle = jointDescription.upperAngleRadians;
        definition.enableMotor = jointDescription.enableMotor;
        definition.motorSpeed = jointDescription.motorSpeedRadiansPerSecond;
        definition.maxMotorTorque = jointDescription.maxMotorTorqueNewtonMeters;
        definition.collideConnected = jointDescription.collideConnected;
        definition.userData = jointRecord;
        jointRecord->backend = b2CreateRevoluteJoint(m_impl->world, &definition);
        break;
    }
    case PhysicsJointKind2D::Prismatic: {
        b2PrismaticJointDef definition = b2DefaultPrismaticJointDef();
        definition.bodyIdA = bodyA->backend;
        definition.bodyIdB = bodyB->backend;
        definition.localAnchorA = {jointDescription.localAnchorAMeters.x, jointDescription.localAnchorAMeters.y};
        definition.localAnchorB = {jointDescription.localAnchorBMeters.x, jointDescription.localAnchorBMeters.y};
        definition.localAxisA = backendPrismaticAxis;
        definition.referenceAngle = jointDescription.referenceAngleRadians;
        definition.targetTranslation = jointDescription.targetTranslationMeters;
        definition.enableSpring = jointDescription.enableSpring;
        definition.hertz = jointDescription.hertz;
        definition.dampingRatio = jointDescription.dampingRatio;
        definition.enableLimit = jointDescription.enableLimit;
        definition.lowerTranslation = jointDescription.lowerTranslationMeters;
        definition.upperTranslation = jointDescription.upperTranslationMeters;
        definition.enableMotor = jointDescription.enableMotor;
        definition.motorSpeed = jointDescription.motorSpeedMetersPerSecond;
        definition.maxMotorForce = jointDescription.maxMotorForceNewtons;
        definition.collideConnected = jointDescription.collideConnected;
        definition.userData = jointRecord;
        jointRecord->backend = b2CreatePrismaticJoint(m_impl->world, &definition);
        break;
    }
    }
    if (B2_IS_NULL(jointRecord->backend) || !b2Joint_IsValid(jointRecord->backend)) {
        jointRecord->backend = b2_nullJointId;
        (void)m_impl->joints.erase(jointId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Box2D failed to create a Physics2D joint");
    }
    if (!m_impl->linkJoint(*jointRecord)) {
        b2DestroyJoint(jointRecord->backend);
        jointRecord->backend = b2_nullJointId;
        (void)m_impl->joints.erase(jointId);
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D joint list is inconsistent");
    }
    return jointId;
}

Core::Status PhysicsWorld2D::destroyJoint(PhysicsJointId joint) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateJoint(joint); !status) {
        return status;
    }
    Impl::JointRecord* jointRecord = m_impl->joints.tryGet(joint);
    if (jointRecord == nullptr || !b2Joint_IsValid(jointRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D joint registry lost its Box2D joint");
    }
    if (!m_impl->unlinkJoint(*jointRecord)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D joint list is inconsistent");
    }
    b2DestroyJoint(jointRecord->backend);
    jointRecord->backend = b2_nullJointId;
    if (m_impl->joints.erase(joint) != Core::GenerationEraseResult::Erased) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D failed to retire a destroyed joint slot");
    }
    return Core::success();
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

    PhysicsJointId jointId = m_impl->firstJoint;
    while (jointId.hasValue()) {
        Impl::JointRecord* jointRecord = m_impl->joints.tryGet(jointId);
        if (jointRecord == nullptr) {
            return Core::failure(
                Physics2DErrorCode::BackendFailure,
                "Physics2D joint list is inconsistent");
        }
        const PhysicsJointId next = jointRecord->next;
        if (jointRecord->bodyA == body || jointRecord->bodyB == body) {
            if (const Core::Status status = destroyJoint(jointId); !status) {
                return status;
            }
        }
        jointId = next;
    }

    while (bodyRecord->firstShape.hasValue()) {
        const PhysicsShapeId shape = bodyRecord->firstShape;
        if (const Core::Status status = destroyShape(shape); !status) {
            return status;
        }
    }

    b2DestroyBody(bodyRecord->backend);
    bodyRecord->backend = b2_nullBodyId;
    if (m_impl->bodies.erase(body) != Core::GenerationEraseResult::Erased) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D failed to retire a destroyed body slot");
    }
    return Core::success();
}

Core::Status PhysicsWorld2D::enqueueDestroyBody(PhysicsBodyId body) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::DestroyBody,
        body,
        {},
        0.0F,
        false});
}

Core::Status PhysicsWorld2D::enqueueSetTransform(
    PhysicsBodyId body,
    Math::Vec2 positionMeters,
    float angleRadians) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    if (!isFinite(positionMeters) || !std::isfinite(angleRadians)) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D set-transform command requires finite position and angle");
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::SetTransform,
        body,
        positionMeters,
        angleRadians,
        false});
}

Core::Status PhysicsWorld2D::enqueueSetLinearVelocity(
    PhysicsBodyId body,
    Math::Vec2 linearVelocityMetersPerSecond) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    if (!isFinite(linearVelocityMetersPerSecond)) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D set-linear-velocity command requires a finite velocity");
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::SetLinearVelocity,
        body,
        linearVelocityMetersPerSecond,
        0.0F,
        false});
}

Core::Status PhysicsWorld2D::enqueueSetAngularVelocity(
    PhysicsBodyId body,
    float angularVelocityRadiansPerSecond) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    if (!std::isfinite(angularVelocityRadiansPerSecond)) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D set-angular-velocity command requires a finite velocity");
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::SetAngularVelocity,
        body,
        {},
        angularVelocityRadiansPerSecond,
        false});
}

Core::Status PhysicsWorld2D::enqueueApplyForceToCenter(
    PhysicsBodyId body,
    Math::Vec2 forceNewtons,
    bool wake) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    if (!isFinite(forceNewtons)) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D apply-force command requires a finite force");
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::ApplyForceToCenter,
        body,
        forceNewtons,
        0.0F,
        wake});
}

Core::Status PhysicsWorld2D::enqueueApplyLinearImpulseToCenter(
    PhysicsBodyId body,
    Math::Vec2 impulseNewtonSeconds,
    bool wake) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    if (!isFinite(impulseNewtonSeconds)) {
        return Core::failure(
            Physics2DErrorCode::InvalidBodyDescription,
            "Physics2D apply-impulse command requires a finite impulse");
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::ApplyLinearImpulseToCenter,
        body,
        impulseNewtonSeconds,
        0.0F,
        wake});
}

Core::Status PhysicsWorld2D::enqueueSetEnabled(PhysicsBodyId body, bool enabled) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::SetEnabled,
        body,
        {},
        0.0F,
        enabled});
}

Core::Status PhysicsWorld2D::enqueueSetAwake(PhysicsBodyId body, bool awake) noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    if (const Core::Status status = validateBody(body); !status) {
        return status;
    }
    return m_impl->enqueueCommand(PhysicsCommand2D{
        PhysicsCommandKind2D::SetAwake,
        body,
        {},
        0.0F,
        awake});
}

Core::Status PhysicsWorld2D::clearCommands() noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return status;
    }
    m_impl->clearCommands();
    return Core::success();
}

Core::usize PhysicsWorld2D::pendingCommandCount() const noexcept
{
    if (m_impl == nullptr || !m_impl->open) {
        return 0;
    }
    return m_impl->commandCount;
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

    m_impl->flushCommands(*this);
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

Core::Result<PhysicsQueryWriteResult2D> PhysicsWorld2D::overlapAabb(
    const PhysicsAabb2D& aabb,
    const PhysicsQueryFilter2D& filter,
    std::span<PhysicsOverlapHit2D> out) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsAabb2D(aabb); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsQueryFilter2D(filter); !status) {
        return Core::failure(status.error());
    }
    if (!b2World_IsValid(m_impl->world)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D backend world is invalid");
    }
    if (m_impl->nextQueryEpoch == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D query epoch space is exhausted");
    }

    // Precise overlap uses a shape proxy matching the AABB extents, not broadphase-only.
    const float halfX = 0.5F * (aabb.upperMeters.x - aabb.lowerMeters.x);
    const float halfY = 0.5F * (aabb.upperMeters.y - aabb.lowerMeters.y);
    const float centerX = 0.5F * (aabb.lowerMeters.x + aabb.upperMeters.x);
    const float centerY = 0.5F * (aabb.lowerMeters.y + aabb.upperMeters.y);
    b2ShapeProxy proxy{};
    if (halfX <= 0.0F && halfY <= 0.0F) {
        const b2Vec2 point{centerX, centerY};
        proxy = b2MakeProxy(&point, 1, 0.0F);
    } else {
        const b2Polygon polygon = b2MakeOffsetBox(
            (std::max)(halfX, 0.0F),
            (std::max)(halfY, 0.0F),
            {centerX, centerY},
            b2Rot_identity);
        proxy = b2MakeProxy(polygon.vertices, polygon.count, polygon.radius);
    }

    Impl::OverlapCollectContext context{
        m_impl,
        out.data(),
        out.size(),
        0,
        0,
        m_impl->nextQueryEpoch++};
    (void)b2World_OverlapShape(
        m_impl->world,
        &proxy,
        toBackendQueryFilter(filter),
        &Impl::collectOverlapHit,
        &context);

    if (context.written > 1) {
        std::sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(context.written), overlapHitLess);
    }

    return PhysicsQueryWriteResult2D{
        context.written,
        context.totalFound,
        context.totalFound > context.written};
}

Core::Result<PhysicsQueryWriteResult2D> PhysicsWorld2D::castRay(
    const PhysicsRayCast2D& ray,
    const PhysicsQueryFilter2D& filter,
    std::span<PhysicsCastHit2D> out) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsRayCast2D(ray); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsQueryFilter2D(filter); !status) {
        return Core::failure(status.error());
    }
    if (!b2World_IsValid(m_impl->world)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D backend world is invalid");
    }
    if (m_impl->nextQueryEpoch == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D query epoch space is exhausted");
    }

    Impl::CastCollectContext context{
        m_impl,
        out.data(),
        out.size(),
        0,
        0,
        m_impl->nextQueryEpoch++};
    (void)b2World_CastRay(
        m_impl->world,
        {ray.originMeters.x, ray.originMeters.y},
        {ray.translationMeters.x, ray.translationMeters.y},
        toBackendQueryFilter(filter),
        &Impl::collectCastHit,
        &context);

    if (context.written > 1) {
        std::sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(context.written), castHitLess);
    }

    return PhysicsQueryWriteResult2D{
        context.written,
        context.totalFound,
        context.totalFound > context.written};
}

Core::Result<PhysicsCastHit2D> PhysicsWorld2D::castRayClosest(
    const PhysicsRayCast2D& ray,
    const PhysicsQueryFilter2D& filter) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsRayCast2D(ray); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validatePhysicsQueryFilter2D(filter); !status) {
        return Core::failure(status.error());
    }
    if (!b2World_IsValid(m_impl->world)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D backend world is invalid");
    }

    const b2RayResult closest = b2World_CastRayClosest(
        m_impl->world,
        {ray.originMeters.x, ray.originMeters.y},
        {ray.translationMeters.x, ray.translationMeters.y},
        toBackendQueryFilter(filter));
    if (!closest.hit || B2_IS_NULL(closest.shapeId) || !b2Shape_IsValid(closest.shapeId)) {
        return Core::failure(
            Physics2DErrorCode::InvalidQuery,
            "Physics2D ray cast closest found no hit");
    }
    void* userData = b2Shape_GetUserData(closest.shapeId);
    if (userData == nullptr) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D ray cast closest hit shape has no Tina user data");
    }
    auto* shapeRecord = static_cast<Impl::ShapeRecord*>(userData);
    if (shapeRecord == nullptr
        || !shapeRecord->id.hasValue()
        || !m_impl->shapes.contains(shapeRecord->id)
        || !m_impl->bodies.contains(shapeRecord->body)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D ray cast closest hit shape is not registered");
    }
    return PhysicsCastHit2D{
        shapeRecord->body,
        shapeRecord->id,
        {closest.point.x, closest.point.y},
        {closest.normal.x, closest.normal.y},
        closest.fraction};
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
    const bool backendShapeValid = shapeRecord != nullptr &&
        ((B2_IS_NON_NULL(shapeRecord->backendChain) &&
          b2Chain_IsValid(shapeRecord->backendChain)) ||
         (B2_IS_NULL(shapeRecord->backendChain) &&
          B2_IS_NON_NULL(shapeRecord->backend) && b2Shape_IsValid(shapeRecord->backend)));
    if (!backendShapeValid) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D shape registry lost its Box2D shape");
    }
    return shapeRecord->body;
}

Core::Result<PhysicsShapeState2D> PhysicsWorld2D::shapeState(
    PhysicsShapeId shape) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateShape(shape); !status) {
        return Core::failure(status.error());
    }
    const Impl::ShapeRecord* shapeRecord = m_impl->shapes.tryGet(shape);
    const bool backendShapeValid = shapeRecord != nullptr &&
        ((B2_IS_NON_NULL(shapeRecord->backendChain) &&
          b2Chain_IsValid(shapeRecord->backendChain)) ||
         (B2_IS_NULL(shapeRecord->backendChain) &&
          B2_IS_NON_NULL(shapeRecord->backend) && b2Shape_IsValid(shapeRecord->backend)));
    if (!backendShapeValid) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D shape registry lost its Box2D shape");
    }
    return PhysicsShapeState2D{
        .body = shapeRecord->body,
        .kind = shapeRecord->kind,
        .isSensor = shapeRecord->isSensor,
        .filter = shapeRecord->filter,
    };
}

Core::Result<PhysicsJointState2D> PhysicsWorld2D::jointState(
    PhysicsJointId joint) const noexcept
{
    if (const Core::Status status = ensureUsable(); !status) {
        return Core::failure(status.error());
    }
    if (const Core::Status status = validateJoint(joint); !status) {
        return Core::failure(status.error());
    }
    const Impl::JointRecord* jointRecord = m_impl->joints.tryGet(joint);
    if (jointRecord == nullptr || !b2Joint_IsValid(jointRecord->backend)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D joint registry lost its Box2D joint");
    }
    PhysicsJointKind2D kind{};
    if (!tryMapBackendJointKind(b2Joint_GetType(jointRecord->backend), kind)) {
        return Core::failure(
            Physics2DErrorCode::BackendFailure,
            "Physics2D joint registry contains an unsupported Box2D joint kind");
    }
    PhysicsJointState2D state{
        .kind = kind,
        .bodyA = jointRecord->bodyA,
        .bodyB = jointRecord->bodyB,
        .collideConnected = b2Joint_GetCollideConnected(jointRecord->backend),
    };
    switch (kind) {
    case PhysicsJointKind2D::Distance:
        state.lengthMeters = b2DistanceJoint_GetLength(jointRecord->backend);
        state.springEnabled = b2DistanceJoint_IsSpringEnabled(jointRecord->backend);
        state.springHertz = b2DistanceJoint_GetSpringHertz(jointRecord->backend);
        state.springDampingRatio =
            b2DistanceJoint_GetSpringDampingRatio(jointRecord->backend);
        break;
    case PhysicsJointKind2D::Revolute:
        state.currentAngleRadians = b2RevoluteJoint_GetAngle(jointRecord->backend);
        state.targetAngleRadians = b2RevoluteJoint_GetTargetAngle(jointRecord->backend);
        state.springEnabled = b2RevoluteJoint_IsSpringEnabled(jointRecord->backend);
        state.springHertz = b2RevoluteJoint_GetSpringHertz(jointRecord->backend);
        state.springDampingRatio =
            b2RevoluteJoint_GetSpringDampingRatio(jointRecord->backend);
        state.limitEnabled = b2RevoluteJoint_IsLimitEnabled(jointRecord->backend);
        state.lowerAngleRadians = b2RevoluteJoint_GetLowerLimit(jointRecord->backend);
        state.upperAngleRadians = b2RevoluteJoint_GetUpperLimit(jointRecord->backend);
        state.motorEnabled = b2RevoluteJoint_IsMotorEnabled(jointRecord->backend);
        state.motorSpeedRadiansPerSecond = b2RevoluteJoint_GetMotorSpeed(jointRecord->backend);
        state.maxMotorTorqueNewtonMeters = b2RevoluteJoint_GetMaxMotorTorque(jointRecord->backend);
        break;
    case PhysicsJointKind2D::Prismatic:
        state.currentTranslationMeters = b2PrismaticJoint_GetTranslation(jointRecord->backend);
        state.targetTranslationMeters = b2PrismaticJoint_GetTargetTranslation(jointRecord->backend);
        state.springEnabled = b2PrismaticJoint_IsSpringEnabled(jointRecord->backend);
        state.springHertz = b2PrismaticJoint_GetSpringHertz(jointRecord->backend);
        state.springDampingRatio =
            b2PrismaticJoint_GetSpringDampingRatio(jointRecord->backend);
        state.limitEnabled = b2PrismaticJoint_IsLimitEnabled(jointRecord->backend);
        state.lowerTranslationMeters = b2PrismaticJoint_GetLowerLimit(jointRecord->backend);
        state.upperTranslationMeters = b2PrismaticJoint_GetUpperLimit(jointRecord->backend);
        state.motorEnabled = b2PrismaticJoint_IsMotorEnabled(jointRecord->backend);
        state.motorSpeedMetersPerSecond = b2PrismaticJoint_GetMotorSpeed(jointRecord->backend);
        state.maxMotorForceNewtons = b2PrismaticJoint_GetMaxMotorForce(jointRecord->backend);
        break;
    }
    return state;
}

bool PhysicsWorld2D::contains(PhysicsBodyId body) const noexcept
{
    return m_impl != nullptr && m_impl->open && m_impl->bodies.contains(body);
}

bool PhysicsWorld2D::contains(PhysicsShapeId shape) const noexcept
{
    return m_impl != nullptr && m_impl->open && m_impl->shapes.contains(shape);
}

bool PhysicsWorld2D::contains(PhysicsJointId joint) const noexcept
{
    return m_impl != nullptr && m_impl->open && m_impl->joints.contains(joint);
}

PhysicsWorld2DStats PhysicsWorld2D::stats() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return PhysicsWorld2DStats{
        .bodyCount = m_impl->bodies.activeCount(),
        .shapeCount = m_impl->shapes.activeCount(),
        .jointCount = m_impl->joints.activeCount(),
        .bodyCapacity = m_impl->bodies.capacity(),
        .shapeCapacity = m_impl->shapes.capacity(),
        .jointCapacity = m_impl->joints.capacity(),
        .contactBeginCapacity = m_impl->config.contactBeginCapacity,
        .contactEndCapacity = m_impl->config.contactEndCapacity,
        .contactHitCapacity = m_impl->config.contactHitCapacity,
        .commandCapacity = m_impl->config.commandCapacity,
        .pendingCommandCount = m_impl->commandCount,
        .completedStepCount = m_impl->completedStepCount,
        .appliedCommandCount = m_impl->appliedCommandCount,
        .skippedStaleCommandCount = m_impl->skippedStaleCommandCount,
        .droppedBeginContactCount = m_impl->droppedBeginContactCount,
        .droppedEndContactCount = m_impl->droppedEndContactCount,
        .droppedHitContactCount = m_impl->droppedHitContactCount,
        .open = m_impl->open,
    };
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
    m_impl->joints.clear();
    m_impl->shapes.clear();
    m_impl->bodies.clear();
    m_impl->firstJoint = {};
    m_impl->clearPublishedContacts();
    m_impl->clearCommands();
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
