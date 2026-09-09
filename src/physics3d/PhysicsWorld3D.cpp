#include <tina/physics3d/PhysicsWorld3D.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Physics3D {
namespace {

static_assert(JPH_VERSION_MAJOR == 5 && JPH_VERSION_MINOR == 5 && JPH_VERSION_PATCH == 0,
              "Physics3D requires the pinned Jolt 5.5.0 adapter ABI");
#ifdef JPH_DOUBLE_PRECISION
#error Physics3D uses a single-precision local solver with an explicit floating origin
#endif

constexpr JPH::ObjectLayer StaticLayer = 0;
constexpr JPH::ObjectLayer MovingLayer = 1;
constexpr float MaximumVelocity = 500.0F;
constexpr float MinimumShapeExtent = 0.001F;
constexpr float MinimumFixedDeltaSeconds = 0.000001F;
constexpr float MaximumFixedDeltaSeconds = 0.1F;
constexpr Core::u32 MaximumCollisionSteps = 16;
constexpr float MinimumMassKilograms = 0.001F;
constexpr float MaximumMassKilograms = 1000000.0F;
constexpr float MaximumGravityFactor = 100.0F;
constexpr float UnitQuaternionTolerance = 0.0001F;

[[nodiscard]] bool localPositionValid(Math::Vec3 value) noexcept
{
    return Math::isFinite(value) &&
           Math::largestComponent(Math::absolute(value)) <= Physics3DLimits::MaximumLocalCoordinateMeters;
}

[[nodiscard]] bool globalPositionValid(PhysicsGlobalPosition3D value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::abs(value.x) <= Physics3DLimits::MaximumGlobalCoordinateMeters &&
           std::abs(value.y) <= Physics3DLimits::MaximumGlobalCoordinateMeters &&
           std::abs(value.z) <= Physics3DLimits::MaximumGlobalCoordinateMeters;
}

[[nodiscard]] bool rotationValid(Math::Quaternion value) noexcept
{
    return Math::isFinite(value) && std::abs(Math::lengthSquared(value) - 1.0F) <= UnitQuaternionTolerance;
}

[[nodiscard]] bool velocityValid(Math::Vec3 value) noexcept
{
    return Math::isFinite(value) && Math::lengthSquared(value) <= MaximumVelocity * MaximumVelocity;
}

[[nodiscard]] JPH::Vec3 toBackend(Math::Vec3 value) noexcept
{
    return {value.x, value.y, value.z};
}
[[nodiscard]] JPH::RVec3 toBackendPosition(Math::Vec3 value) noexcept
{
    return {value.x, value.y, value.z};
}
[[nodiscard]] JPH::Quat toBackend(Math::Quaternion value) noexcept
{
    value = Math::normalized(value);
    return {value.x, value.y, value.z, value.w};
}
template <typename Vector> [[nodiscard]] Math::Vec3 fromBackend(const Vector& value) noexcept
{
    return {static_cast<float>(value.GetX()), static_cast<float>(value.GetY()), static_cast<float>(value.GetZ())};
}
[[nodiscard]] Math::Quaternion fromBackend(JPH::QuatArg value) noexcept
{
    return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
}

// Jolt requires process-global type registration. This lease only manages that
// backend prerequisite; it never stores or locates a Tina world. Release occurs
// after the last world's bodies, jobs and PhysicsSystem have been destroyed.
std::mutex registrationMutex;
Core::usize registrationUsers = 0;
std::once_flag allocatorRegistration;

class BackendRegistration final {
  public:
    [[nodiscard]] static Core::Result<BackendRegistration> acquire()
    {
        std::lock_guard lock(registrationMutex);
        if (!JPH::VerifyJoltVersionID())
        {
            return Core::failure(Physics3DErrorCode::BackendFailure,
                                 "Physics3D backend version or build features do not match the adapter ABI");
        }
        if (registrationUsers == 0)
        {
            if (JPH::Factory::sInstance != nullptr)
            {
                return Core::failure(Physics3DErrorCode::BackendFailure,
                                     "Physics3D cannot take over externally registered backend types");
            }
            std::call_once(allocatorRegistration, [] { JPH::RegisterDefaultAllocator(); });
            auto factory = std::make_unique<JPH::Factory>();
            JPH::Factory::sInstance = factory.get();
            auto rollback = Core::makeScopeExit([]() noexcept { JPH::Factory::sInstance = nullptr; });
            JPH::RegisterTypes();
            factory.release();
            rollback.release();
        }
        ++registrationUsers;
        return BackendRegistration{};
    }
    ~BackendRegistration() noexcept
    {
        if (!m_owned)
        {
            return;
        }
        std::lock_guard lock(registrationMutex);
        if (--registrationUsers == 0)
        {
            JPH::UnregisterTypes();
            delete std::exchange(JPH::Factory::sInstance, nullptr);
        }
    }
    BackendRegistration(BackendRegistration&& other) noexcept : m_owned(std::exchange(other.m_owned, false))
    {
    }
    BackendRegistration(const BackendRegistration&) = delete;
    BackendRegistration& operator=(const BackendRegistration&) = delete;

  private:
    BackendRegistration() = default;
    bool m_owned = true;
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return 2;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer.GetValue() == StaticLayer ? "Static" : "Moving";
    }
#endif
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
    {
        return first == MovingLayer || second == MovingLayer;
    }
};

class ObjectBroadPhasePairs final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const override
    {
        return object == MovingLayer || broadPhase.GetValue() == MovingLayer;
    }
};

[[nodiscard]] Core::Result<JPH::RefConst<JPH::Shape>> createBackendShape(const PhysicsShape3DDesc& desc)
{
    JPH::ShapeSettings::ShapeResult shape;
    switch (desc.kind)
    {
    case PhysicsShapeKind3D::Box:
        shape = JPH::BoxShapeSettings(toBackend(desc.halfExtentsMeters),
                                      (std::min)(0.05F, Math::smallestComponent(desc.halfExtentsMeters) * 0.1F))
                    .Create();
        break;
    case PhysicsShapeKind3D::Sphere:
        shape = JPH::SphereShapeSettings(desc.radiusMeters).Create();
        break;
    case PhysicsShapeKind3D::Capsule:
        shape = JPH::CapsuleShapeSettings(desc.halfHeightMeters, desc.radiusMeters).Create();
        break;
    }
    if (shape.HasError())
    {
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription, shape.GetError().c_str());
    }
    return JPH::RefConst<JPH::Shape>(shape.Get());
}

[[nodiscard]] JPH::EMotionType motionType(PhysicsBodyType3D type) noexcept
{
    switch (type)
    {
    case PhysicsBodyType3D::Static:
        return JPH::EMotionType::Static;
    case PhysicsBodyType3D::Kinematic:
        return JPH::EMotionType::Kinematic;
    case PhysicsBodyType3D::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

} // namespace

struct PhysicsWorld3D::Impl final : JPH::ContactListener, JPH::CharacterContactListener {
    struct BodyRecord final {
        JPH::BodyID backend{};
        PhysicsBodyType3D type = PhysicsBodyType3D::Static;
        bool sensor = false;
        JPH::Ref<JPH::CharacterVirtual> character;
        CharacterController3DInput characterInput{};
        JPH::CharacterVirtual::ExtendedUpdateSettings characterUpdate{};
    };
    struct ContactRecord final {
        JPH::SubShapeIDPair key;
        PhysicsContactEvent3D event;
        bool characterContact = false;
    };
    using BodyPool = Core::GenerationPool<BodyRecord, Detail::PhysicsBodyRegistryTag>;

    Impl(BackendRegistration registration, PhysicsWorld3DConfig configuration, BodyPool pool)
        : backendRegistration(std::move(registration)), config(configuration), bodies(std::move(pool)),
          publicIds(config.bodyCapacity), shiftedPositions(config.bodyCapacity), queryIds(config.bodyCapacity),
          contactEvents(config.contactEventCapacity),
          jobs(JPH::cMaxPhysicsJobs), origin{config.initialOriginMeters, 0}
    {
        system.Init(config.bodyCapacity, 0, config.bodyPairCapacity, config.contactConstraintCapacity, broadPhaseLayers,
                    objectBroadPhasePairs, objectPairs);
        system.SetGravity(toBackend(config.gravityMetersPerSecondSquared));
        activeContacts.reserve(config.contactConstraintCapacity);
        system.SetContactListener(this);
    }

    ~Impl() noexcept
    {
        system.SetContactListener(nullptr);
        for (const auto id : publicIds)
        {
            if (auto* record = bodies.tryGet(id))
            {
                if (record->character != nullptr)
                {
                    record->character->SetListener(nullptr);
                    record->character = nullptr;
                    continue;
                }
                system.GetBodyInterface().RemoveBody(record->backend);
                system.GetBodyInterface().DestroyBody(record->backend);
            }
        }
    }

    [[nodiscard]] PhysicsBodyId publicId(JPH::BodyID native) const noexcept
    {
        if (native.IsInvalid() || native.GetIndex() >= publicIds.size()) return {};
        const auto id = publicIds[native.GetIndex()];
        const auto* record = bodies.tryGet(id);
        return record != nullptr && record->backend == native ? id : PhysicsBodyId{};
    }

    void enqueueContact(const PhysicsContactEvent3D& event) noexcept
    {
        if (eventCount == contactEvents.size())
        {
            if (droppedEvents != (std::numeric_limits<Core::u64>::max)()) ++droppedEvents;
            return;
        }
        contactEvents[(eventHead + eventCount++) % contactEvents.size()] = event;
    }

    void recordContact(const JPH::Body& first, const JPH::Body& second,
                       const JPH::ContactManifold& manifold)
    {
        const auto* firstRecord = bodies.tryGet(publicId(first.GetID()));
        const auto* secondRecord = bodies.tryGet(publicId(second.GetID()));
        // The virtual solver owns controller contacts, including static geometry
        // that the inner kinematic proxy does not collide with.
        if ((firstRecord && firstRecord->character != nullptr) ||
            (secondRecord && secondRecord->character != nullptr)) return;
        const JPH::SubShapeIDPair key(first.GetID(), manifold.mSubShapeID1,
                                     second.GetID(), manifold.mSubShapeID2);
        auto entry = std::lower_bound(activeContacts.begin(), activeContacts.end(), key,
            [](const ContactRecord& record, const JPH::SubShapeIDPair& candidate) { return record.key < candidate; });
        PhysicsContactEvent3D event{.first = publicId(first.GetID()), .second = publicId(second.GetID()),
            .positionMeters = fromBackend(manifold.GetWorldSpaceContactPointOn1(0)),
            .normal = fromBackend(manifold.mWorldSpaceNormal), .originRevision = origin.revision,
            .sensor = first.IsSensor() || second.IsSensor()};
        if (!event.first || !event.second)
        {
            contactCacheOverflow = true;
            return;
        }
        if (entry != activeContacts.end() && entry->key == key)
        {
            event.phase = PhysicsContactPhase3D::Stay;
            entry->event = event;
        } else
        {
            if (activeContacts.size() == config.contactConstraintCapacity)
            {
                contactCacheOverflow = true;
                return;
            }
            activeContacts.insert(entry, ContactRecord{key, event});
        }
        enqueueContact(event);
    }

    void OnContactAdded(const JPH::Body& first, const JPH::Body& second,
                        const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        recordContact(first, second, manifold);
    }
    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                            const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        recordContact(first, second, manifold);
    }
    void removeContact(const JPH::SubShapeIDPair& key, bool characterContact)
    {
        const auto entry = std::lower_bound(activeContacts.begin(), activeContacts.end(), key,
            [](const ContactRecord& record, const JPH::SubShapeIDPair& candidate) { return record.key < candidate; });
        if (entry == activeContacts.end() || !(entry->key == key) ||
            entry->characterContact != characterContact) return;
        auto event = entry->event;
        event.phase = PhysicsContactPhase3D::Exit;
        enqueueContact(event);
        activeContacts.erase(entry);
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& key) override
    {
        removeContact(key, false);
    }

    void recordCharacterContact(const JPH::CharacterVirtual* character, const JPH::BodyID& other,
        const JPH::SubShapeID& shape, JPH::RVec3Arg position, JPH::Vec3Arg normal)
    {
        const JPH::SubShapeIDPair key(character->GetInnerBodyID(), JPH::SubShapeID{}, other, shape);
        auto entry = std::lower_bound(activeContacts.begin(), activeContacts.end(), key,
            [](const ContactRecord& record, const JPH::SubShapeIDPair& candidate) { return record.key < candidate; });
        const auto first = publicId(character->GetInnerBodyID());
        const auto second = publicId(other);
        const auto* record = bodies.tryGet(second);
        if (!first || record == nullptr) { contactCacheOverflow = true; return; }
        PhysicsContactEvent3D event{.first = first, .second = second,
            .positionMeters = fromBackend(position), .normal = fromBackend(normal),
            .originRevision = origin.revision, .sensor = record->sensor};
        if (entry != activeContacts.end() && entry->key == key) {
            event.phase = PhysicsContactPhase3D::Stay;
            entry->event = event;
        } else {
            if (activeContacts.size() == config.contactConstraintCapacity) { contactCacheOverflow = true; return; }
            activeContacts.insert(entry, ContactRecord{key, event, true});
        }
        enqueueContact(event);
    }
    void OnContactAdded(const JPH::CharacterVirtual* character, const JPH::BodyID& other,
        const JPH::SubShapeID& shape, JPH::RVec3Arg position, JPH::Vec3Arg normal,
        JPH::CharacterContactSettings&) override
    {
        recordCharacterContact(character, other, shape, position, normal);
    }
    void OnContactPersisted(const JPH::CharacterVirtual* character, const JPH::BodyID& other,
        const JPH::SubShapeID& shape, JPH::RVec3Arg position, JPH::Vec3Arg normal,
        JPH::CharacterContactSettings&) override
    {
        recordCharacterContact(character, other, shape, position, normal);
    }
    void OnContactRemoved(const JPH::CharacterVirtual* character, const JPH::BodyID& other,
        const JPH::SubShapeID& shape) override
    {
        removeContact(JPH::SubShapeIDPair(character->GetInnerBodyID(), JPH::SubShapeID{}, other, shape), true);
    }

    void forgetContacts(PhysicsBodyId body) noexcept
    {
        for (auto entry = activeContacts.begin(); entry != activeContacts.end();)
        {
            if (entry->event.first == body || entry->event.second == body)
            {
                auto event = entry->event;
                event.phase = PhysicsContactPhase3D::Exit;
                enqueueContact(event);
                entry = activeContacts.erase(entry);
            } else ++entry;
        }
    }

    [[nodiscard]] bool matches(PhysicsBodyId id, const PhysicsQueryFilter3D& filter) const noexcept
    {
        const auto* record = bodies.tryGet(id);
        return record != nullptr && id != filter.ignoredBody &&
               (record->type == PhysicsBodyType3D::Static ? filter.includeStatic : filter.includeMoving) &&
               (!record->sensor || filter.includeSensors);
    }

    struct QueryBodyFilter final : JPH::BodyFilter {
        QueryBodyFilter(const Impl& owner, const PhysicsQueryFilter3D& query) : world(owner), filter(query)
        {
        }
        bool ShouldCollide(const JPH::BodyID& body) const override
        {
            return world.matches(world.publicId(body), filter);
        }
        bool ShouldCollideLocked(const JPH::Body& body) const override
        {
            return ShouldCollide(body.GetID());
        }
        const Impl& world;
        const PhysicsQueryFilter3D& filter;
    };

    struct AabbCollector final : JPH::CollideShapeBodyCollector {
        AabbCollector(const Impl& owner, const PhysicsQueryFilter3D& query) : world(owner), filter(query)
        {
        }
        void AddHit(const JPH::BodyID& body) override
        {
            const auto id = world.publicIds[body.GetIndex()];
            if (!world.matches(id, filter))
            {
                return;
            }
            if (count == world.queryIds.size())
            {
                overflow = true;
                ForceEarlyOut();
                return;
            }
            world.queryIds[count++] = id;
        }
        const Impl& world;
        const PhysicsQueryFilter3D& filter;
        Core::usize count = 0;
        bool overflow = false;
    };

    BackendRegistration backendRegistration;
    PhysicsWorld3DConfig config;
    BodyPool bodies;
    // Backend body index -> full public generation identity. Never synthesize IDs.
    std::vector<PhysicsBodyId> publicIds;
    std::vector<Math::Vec3> shiftedPositions;
    mutable std::vector<PhysicsBodyId> queryIds;
    std::vector<PhysicsContactEvent3D> contactEvents;
    std::vector<ContactRecord> activeContacts;
    Core::usize eventHead = 0;
    Core::usize eventCount = 0;
    Core::u64 droppedEvents = 0;
    bool contactCacheOverflow = false;
    BroadPhaseLayers broadPhaseLayers;
    ObjectPairs objectPairs;
    ObjectBroadPhasePairs objectBroadPhasePairs;
    JPH::TempAllocatorMalloc temporaryAllocator;
    JPH::JobSystemSingleThreaded jobs;
    JPH::PhysicsSystem system;
    PhysicsOrigin3D origin;
    std::thread::id ownerThread = std::this_thread::get_id();
    Core::u64 stepCount = 0;
    bool faulted = false;
};

Core::Status validatePhysicsWorld3DConfig(const PhysicsWorld3DConfig& config)
{
    if (config.bodyCapacity == 0 || config.bodyCapacity > Physics3DLimits::MaximumBodies ||
        config.bodyPairCapacity == 0 || config.bodyPairCapacity > Physics3DLimits::MaximumBodyPairs ||
        config.contactConstraintCapacity == 0 ||
        config.contactConstraintCapacity > Physics3DLimits::MaximumContactConstraints ||
        !std::isfinite(config.fixedDeltaSeconds) || config.fixedDeltaSeconds < MinimumFixedDeltaSeconds ||
        config.fixedDeltaSeconds > MaximumFixedDeltaSeconds || config.collisionSteps == 0 ||
        config.collisionSteps > MaximumCollisionSteps || !velocityValid(config.gravityMetersPerSecondSquared) ||
        !globalPositionValid(config.initialOriginMeters) || config.contactEventCapacity == 0 ||
        config.contactEventCapacity > Physics3DLimits::MaximumBodyPairs)
    {
        return Core::failure(
            Physics3DErrorCode::InvalidConfiguration,
            "Physics3D capacity, fixed step, gravity or initial origin is outside its supported range");
    }
    return Core::success();
}

Core::Status validatePhysicsBody3DDesc(const PhysicsBody3DDesc& desc)
{
    const auto validExtent = [](float value) {
        return std::isfinite(value) && value >= MinimumShapeExtent &&
               value <= Physics3DLimits::MaximumShapeExtentMeters;
    };
    bool shapeValid = false;
    switch (desc.shape.kind)
    {
    case PhysicsShapeKind3D::Box:
        shapeValid = validExtent(desc.shape.halfExtentsMeters.x) && validExtent(desc.shape.halfExtentsMeters.y) &&
                     validExtent(desc.shape.halfExtentsMeters.z);
        break;
    case PhysicsShapeKind3D::Sphere:
        shapeValid = validExtent(desc.shape.radiusMeters);
        break;
    case PhysicsShapeKind3D::Capsule:
        shapeValid = validExtent(desc.shape.radiusMeters) && validExtent(desc.shape.halfHeightMeters) &&
                     validExtent(desc.shape.radiusMeters + desc.shape.halfHeightMeters);
        break;
    }
    if (desc.type > PhysicsBodyType3D::Dynamic || !shapeValid || !localPositionValid(desc.positionMeters) ||
        !rotationValid(desc.rotation) || !velocityValid(desc.linearVelocityMetersPerSecond) ||
        !velocityValid(desc.angularVelocityRadiansPerSecond) ||
        (desc.type == PhysicsBodyType3D::Static && (desc.linearVelocityMetersPerSecond != Math::Vec3{} ||
                                                    desc.angularVelocityRadiansPerSecond != Math::Vec3{})) ||
        !std::isfinite(desc.massKilograms) || desc.massKilograms < MinimumMassKilograms ||
        desc.massKilograms > MaximumMassKilograms || !std::isfinite(desc.friction) || desc.friction < 0.0F ||
        desc.friction > 1.0F || !std::isfinite(desc.restitution) || desc.restitution < 0.0F ||
        desc.restitution > 1.0F || !std::isfinite(desc.gravityFactor) ||
        std::abs(desc.gravityFactor) > MaximumGravityFactor)
    {
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription,
                             "Physics3D body has invalid geometry, pose, motion or material parameters");
    }
    return Core::success();
}

Core::Result<PhysicsWorld3D> PhysicsWorld3D::Create(PhysicsWorld3DConfig config)
try
{
    if (auto status = validatePhysicsWorld3DConfig(config); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto registration = BackendRegistration::acquire();
    if (!registration)
    {
        return Core::failure(std::move(registration.error()));
    }
    auto pool = Impl::BodyPool::Create(config.bodyCapacity);
    if (!pool)
    {
        return Core::failure(std::move(pool.error()));
    }
    return PhysicsWorld3D(std::make_unique<Impl>(std::move(*registration), config, std::move(*pool)));
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Physics3D world allocation failed");
} catch (const std::exception& error)
{
    return Core::failure(Physics3DErrorCode::BackendFailure, error.what());
}

PhysicsWorld3D::PhysicsWorld3D(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl))
{
}
PhysicsWorld3D::~PhysicsWorld3D() noexcept
{
    if (m_impl && m_impl->ownerThread != std::this_thread::get_id())
    {
        std::terminate();
    }
}
PhysicsWorld3D::PhysicsWorld3D(PhysicsWorld3D&& other) noexcept : m_impl(std::move(other.m_impl))
{
    if (m_impl)
    {
        m_impl->ownerThread = std::this_thread::get_id();
    }
}

Core::Status PhysicsWorld3D::ensureUsable() const
{
    if (!m_impl)
    {
        return Core::failure(Physics3DErrorCode::WorldClosed, "Physics3D world is closed");
    }
    if (m_impl->ownerThread != std::this_thread::get_id())
    {
        return Core::failure(Physics3DErrorCode::WrongOwnerThread, "Physics3D access requires its owner thread");
    }
    if (m_impl->faulted)
    {
        return Core::failure(Physics3DErrorCode::WorldFaulted, "Physics3D solver failed; shut down this world");
    }
    return Core::success();
}

Core::Status PhysicsWorld3D::validateBody(PhysicsBodyId body, bool allowFaulted) const
{
    if (auto status = ensureUsable(); !status &&
        !(allowFaulted && status.error().code == Physics3DErrorCode::WorldFaulted))
    {
        return status;
    }
    if (!body)
    {
        return Core::failure(Physics3DErrorCode::InvalidBody, "Physics3D body ID is empty");
    }
    if (body.owner() != m_impl->bodies.owner())
    {
        return Core::failure(Physics3DErrorCode::WrongWorld, "Physics3D body belongs to a different world");
    }
    if (!m_impl->bodies.contains(body))
    {
        return Core::failure(Physics3DErrorCode::StaleBody, "Physics3D body generation is stale");
    }
    return Core::success();
}

Core::Status PhysicsWorld3D::validateFilter(const PhysicsQueryFilter3D& filter) const
{
    return filter.ignoredBody ? validateBody(filter.ignoredBody) : ensureUsable();
}

Core::Result<PhysicsBodyId> PhysicsWorld3D::createBody(const PhysicsBody3DDesc& desc)
try
{
    if (auto status = ensureUsable(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = validatePhysicsBody3DDesc(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto global = toGlobalPosition(desc.positionMeters); !global)
    {
        return Core::failure(std::move(global.error()));
    }
    if (m_impl->bodies.availableCount() == 0)
    {
        return Core::failure(Physics3DErrorCode::CapacityExceeded, "Physics3D body capacity is exhausted");
    }
    auto shape = createBackendShape(desc.shape);
    if (!shape)
    {
        return Core::failure(std::move(shape.error()));
    }
    auto id = m_impl->bodies.tryEmplace(Impl::BodyRecord{.type = desc.type, .sensor = desc.sensor});
    if (!id)
    {
        return Core::failure(std::move(id.error()));
    }
    auto rollbackId = Core::makeScopeExit([&]() noexcept { (void)m_impl->bodies.erase(*id); });
    JPH::BodyCreationSettings settings(shape->GetPtr(), toBackendPosition(desc.positionMeters),
                                       toBackend(desc.rotation), motionType(desc.type),
                                       desc.type == PhysicsBodyType3D::Static ? StaticLayer : MovingLayer);
    settings.mLinearVelocity = toBackend(desc.linearVelocityMetersPerSecond);
    settings.mAngularVelocity = toBackend(desc.angularVelocityRadiansPerSecond);
    settings.mMaxLinearVelocity = MaximumVelocity;
    settings.mMaxAngularVelocity = MaximumVelocity;
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mGravityFactor = desc.gravityFactor;
    settings.mIsSensor = desc.sensor;
    settings.mCollideKinematicVsNonDynamic = desc.sensor;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = desc.massKilograms;
    auto& bodyInterface = m_impl->system.GetBodyInterface();
    auto* backend = bodyInterface.CreateBody(settings);
    if (backend == nullptr)
    {
        return Core::failure(Physics3DErrorCode::CapacityExceeded, "Physics3D backend body capacity is exhausted");
    }
    const auto backendId = backend->GetID();
    auto rollbackBackend = Core::makeScopeExit([&]() noexcept { bodyInterface.DestroyBody(backendId); });
    bodyInterface.AddBody(backendId, desc.startAwake && desc.type != PhysicsBodyType3D::Static
                                         ? JPH::EActivation::Activate
                                         : JPH::EActivation::DontActivate);
    m_impl->bodies.tryGet(*id)->backend = backendId;
    m_impl->publicIds[backendId.GetIndex()] = *id;
    rollbackBackend.release();
    rollbackId.release();
    return *id;
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Physics3D body allocation failed");
}

Core::Result<PhysicsBodyId> PhysicsWorld3D::createCharacter(const CharacterController3DDesc& desc)
try
{
    if (auto status = ensureUsable(); !status) return Core::failure(std::move(status.error()));
    PhysicsBody3DDesc bodyDesc{.type = PhysicsBodyType3D::Kinematic,
        .shape = {.kind = PhysicsShapeKind3D::Capsule, .radiusMeters = desc.radiusMeters,
                  .halfHeightMeters = desc.halfHeightMeters},
        .positionMeters = desc.positionMeters, .rotation = desc.rotation, .massKilograms = desc.massKilograms};
    if (auto status = validatePhysicsBody3DDesc(bodyDesc); !status) return Core::failure(std::move(status.error()));
    if (Math::lengthSquared(Math::rotate(desc.rotation, Math::Vec3{0, 1, 0}) - Math::Vec3{0, 1, 0}) > 0.000001F ||
        !std::isfinite(desc.maximumSlopeRadians) || desc.maximumSlopeRadians < 0.0F ||
        desc.maximumSlopeRadians >= 1.570796327F || !std::isfinite(desc.stepHeightMeters) ||
        desc.stepHeightMeters < 0.0F || desc.stepHeightMeters > desc.halfHeightMeters * 2.0F ||
        !std::isfinite(desc.floorSnapMeters) || desc.floorSnapMeters < 0.0F || desc.floorSnapMeters > 10.0F)
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription, "Physics3D invalid character slope/step/snap");
    if (auto global = toGlobalPosition(desc.positionMeters); !global) return Core::failure(std::move(global.error()));
    auto shape = createBackendShape(bodyDesc.shape);
    if (!shape) return Core::failure(std::move(shape.error()));
    auto id = m_impl->bodies.tryEmplace(Impl::BodyRecord{.type = PhysicsBodyType3D::Kinematic});
    if (!id) return Core::failure(std::move(id.error()));
    auto rollback = Core::makeScopeExit([&]() noexcept { (void)m_impl->bodies.erase(*id); });
    JPH::CharacterVirtualSettings settings;
    settings.mShape = *shape;
    settings.mInnerBodyShape = *shape;
    settings.mInnerBodyLayer = MovingLayer;
    settings.mMass = desc.massKilograms;
    settings.mMaxSlopeAngle = desc.maximumSlopeRadians;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), desc.halfHeightMeters);
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        &settings, toBackendPosition(desc.positionMeters), toBackend(desc.rotation), &m_impl->system);
    const auto native = character->GetInnerBodyID();
    if (native.IsInvalid()) return Core::failure(Physics3DErrorCode::CapacityExceeded,
                                               "Physics3D character proxy capacity exhausted");
    auto& record = *m_impl->bodies.tryGet(*id);
    record.backend = native;
    record.character = character;
    record.characterUpdate.mWalkStairsStepUp = JPH::Vec3(0, desc.stepHeightMeters, 0);
    record.characterUpdate.mStickToFloorStepDown = JPH::Vec3(0, -desc.floorSnapMeters, 0);
    m_impl->publicIds[native.GetIndex()] = *id;
    auto rollbackMapping = Core::makeScopeExit([&]() noexcept {
        if (m_impl->bodies.contains(*id)) m_impl->publicIds[native.GetIndex()] = {};
    });
    const PhysicsQueryFilter3D filter{.includeSensors = true, .ignoredBody = *id};
    const Impl::QueryBodyFilter bodyFilter(*m_impl, filter);
    character->RefreshContacts({}, {}, bodyFilter, {}, m_impl->temporaryAllocator);
    if (character->GetMaxHitsExceeded())
        return Core::failure(Physics3DErrorCode::CapacityExceeded, "Physics3D initial character contact budget exceeded");
    character->SetListener(m_impl.get());
    rollbackMapping.release();
    rollback.release();
    return *id;
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Physics3D character allocation failed");
} catch (const std::exception& error)
{
    return Core::failure(Physics3DErrorCode::BackendFailure, error.what());
}

Core::Status PhysicsWorld3D::setCharacterInput(PhysicsBodyId body, const CharacterController3DInput& input)
{
    if (auto status = validateBody(body); !status) return status;
    auto& record = *m_impl->bodies.tryGet(body);
    if (record.character == nullptr || input.horizontalVelocityMetersPerSecond.y != 0.0F ||
        !velocityValid(input.horizontalVelocityMetersPerSecond) || !std::isfinite(input.jumpSpeedMetersPerSecond) ||
        input.jumpSpeedMetersPerSecond < 0.0F || input.jumpSpeedMetersPerSecond > MaximumVelocity)
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription, "Physics3D invalid character input or identity");
    record.characterInput = input;
    return Core::success();
}

Core::Result<CharacterController3DState> PhysicsWorld3D::characterState(PhysicsBodyId body) const
{
    if (auto status = validateBody(body); !status) return Core::failure(std::move(status.error()));
    const auto& record = *m_impl->bodies.tryGet(body);
    if (record.character == nullptr)
        return Core::failure(Physics3DErrorCode::InvalidBody, "Physics3D body is not a character controller");
    const auto& character = *record.character;
    CharacterGroundState3D ground = CharacterGroundState3D::Airborne;
    switch (character.GetGroundState())
    {
    case JPH::CharacterBase::EGroundState::OnGround: ground = CharacterGroundState3D::Grounded; break;
    case JPH::CharacterBase::EGroundState::OnSteepGround: ground = CharacterGroundState3D::Steep; break;
    case JPH::CharacterBase::EGroundState::NotSupported: ground = CharacterGroundState3D::Unsupported; break;
    case JPH::CharacterBase::EGroundState::InAir: break;
    }
    return CharacterController3DState{.body = body, .positionMeters = fromBackend(character.GetPosition()),
        .velocityMetersPerSecond = fromBackend(character.GetLinearVelocity()),
        .groundNormal = fromBackend(character.GetGroundNormal()), .groundBody = m_impl->publicId(character.GetGroundBodyID()),
        .ground = ground, .originRevision = m_impl->origin.revision};
}

Core::Status PhysicsWorld3D::destroyBody(PhysicsBodyId body)
{
    if (auto status = validateBody(body, true); !status)
    {
        return status;
    }
    const auto backend = m_impl->bodies.tryGet(body)->backend;
    m_impl->forgetContacts(body);
    auto& record = *m_impl->bodies.tryGet(body);
    if (record.character != nullptr) {
        record.character->SetListener(nullptr);
        record.character = nullptr;
    }
    else
    {
        m_impl->system.GetBodyInterface().RemoveBody(backend);
        m_impl->system.GetBodyInterface().DestroyBody(backend);
    }
    m_impl->publicIds[backend.GetIndex()] = {};
    (void)m_impl->bodies.erase(body);
    return Core::success();
}

Core::Result<PhysicsBodyState3D> PhysicsWorld3D::bodyState(PhysicsBodyId body) const
{
    if (auto status = validateBody(body); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    const auto& record = *m_impl->bodies.tryGet(body);
    const auto& bodies = m_impl->system.GetBodyInterface();
    return PhysicsBodyState3D{.body = body,
                              .type = record.type,
                              .positionMeters = record.character != nullptr
                                  ? fromBackend(record.character->GetPosition())
                                  : fromBackend(bodies.GetPosition(record.backend)),
                              .rotation = fromBackend(bodies.GetRotation(record.backend)),
                              .linearVelocityMetersPerSecond = record.character != nullptr
                                  ? fromBackend(record.character->GetLinearVelocity())
                                  : fromBackend(bodies.GetLinearVelocity(record.backend)),
                              .angularVelocityRadiansPerSecond = fromBackend(bodies.GetAngularVelocity(record.backend)),
                              .originRevision = m_impl->origin.revision,
                              .sensor = record.sensor,
                              .awake = bodies.IsActive(record.backend)};
}

Core::Status PhysicsWorld3D::setTransform(PhysicsBodyId body, Math::Vec3 positionMeters, Math::Quaternion rotation,
                                          bool wake)
try
{
    if (auto status = validateBody(body); !status)
    {
        return status;
    }
    if (!localPositionValid(positionMeters) || !rotationValid(rotation))
    {
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription,
                             "Physics3D transform is outside its local range");
    }
    if (auto global = toGlobalPosition(positionMeters); !global)
    {
        return Core::failure(std::move(global.error()));
    }
    const auto& record = *m_impl->bodies.tryGet(body);
    if (record.character != nullptr)
    {
        if (Math::lengthSquared(Math::rotate(rotation, Math::Vec3{0, 1, 0}) - Math::Vec3{0, 1, 0}) > 0.000001F)
            return Core::failure(Physics3DErrorCode::InvalidBodyDescription, "Physics3D character rotation must preserve Y-up");
        auto quarantine = Core::makeScopeExit([&]() noexcept { m_impl->faulted = true; });
        record.character->SetPosition(toBackendPosition(positionMeters));
        record.character->SetRotation(toBackend(rotation));
        const PhysicsQueryFilter3D filter{.includeSensors = true, .ignoredBody = body};
        const Impl::QueryBodyFilter bodyFilter(*m_impl, filter);
        record.character->RefreshContacts({}, {}, bodyFilter, {}, m_impl->temporaryAllocator);
        if (record.character->GetMaxHitsExceeded() || m_impl->contactCacheOverflow)
            return Core::failure(Physics3DErrorCode::CapacityExceeded, "Physics3D character teleport contact budget exceeded");
        quarantine.release();
        return Core::success();
    }
    m_impl->system.GetBodyInterface().SetPositionAndRotation(
        record.backend, toBackendPosition(positionMeters), toBackend(rotation),
        wake && record.type != PhysicsBodyType3D::Static ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    return Core::success();
}

catch (const std::exception& error)
{
    return Core::failure(Physics3DErrorCode::BackendFailure, error.what());
}

Core::Status PhysicsWorld3D::setLinearVelocity(PhysicsBodyId body, Math::Vec3 velocityMetersPerSecond)
{
    if (auto status = validateBody(body); !status)
    {
        return status;
    }
    const auto& record = *m_impl->bodies.tryGet(body);
    if (record.character != nullptr || record.type == PhysicsBodyType3D::Static || !velocityValid(velocityMetersPerSecond))
    {
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription,
                             "Physics3D velocity requires a moving body and bounded finite speed");
    }
    m_impl->system.GetBodyInterface().SetLinearVelocity(record.backend, toBackend(velocityMetersPerSecond));
    return Core::success();
}

Core::Status PhysicsWorld3D::addLinearImpulse(PhysicsBodyId body, Math::Vec3 impulseNewtonSeconds)
{
    if (auto status = validateBody(body); !status)
    {
        return status;
    }
    const auto& record = *m_impl->bodies.tryGet(body);
    if (record.type != PhysicsBodyType3D::Dynamic || !velocityValid(impulseNewtonSeconds))
    {
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription,
                             "Physics3D impulse requires a dynamic body and bounded finite magnitude");
    }
    m_impl->system.GetBodyInterface().AddImpulse(record.backend, toBackend(impulseNewtonSeconds));
    return Core::success();
}

Core::Status PhysicsWorld3D::setAwake(PhysicsBodyId body, bool awake)
{
    if (auto status = validateBody(body); !status)
    {
        return status;
    }
    const auto& record = *m_impl->bodies.tryGet(body);
    if (record.character != nullptr || record.type == PhysicsBodyType3D::Static)
    {
        return Core::failure(Physics3DErrorCode::InvalidBodyDescription, "Physics3D static bodies cannot be activated");
    }
    if (awake)
    {
        m_impl->system.GetBodyInterface().ActivateBody(record.backend);
    } else
    {
        m_impl->system.GetBodyInterface().DeactivateBody(record.backend);
    }
    return Core::success();
}

Core::Status PhysicsWorld3D::step()
{
    if (auto status = ensureUsable(); !status)
    {
        return status;
    }
    // A solver exception cannot be unwound into a trustworthy old world state.
    auto quarantine = Core::makeScopeExit([&]() noexcept { m_impl->faulted = true; });
    try
    {
        for (const auto id : m_impl->publicIds)
        {
            auto* record = m_impl->bodies.tryGet(id);
            if (record == nullptr || record->character == nullptr) continue;
            auto& character = *record->character;
            character.UpdateGroundVelocity();
            auto velocity = record->characterInput.horizontalVelocityMetersPerSecond;
            const auto ground = fromBackend(character.GetGroundVelocity());
            if (character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround &&
                character.GetLinearVelocity().GetY() <= ground.y + 0.1F)
            {
                velocity = velocity + ground;
                velocity.y += record->characterInput.jumpSpeedMetersPerSecond;
            } else velocity.y = character.GetLinearVelocity().GetY();
            record->characterInput.jumpSpeedMetersPerSecond = 0.0F;
            velocity = velocity + m_impl->config.gravityMetersPerSecondSquared * m_impl->config.fixedDeltaSeconds;
            if (!Math::isFinite(velocity))
                return Core::failure(Physics3DErrorCode::BackendFailure, "Physics3D character velocity became nonfinite");
            const float speedSquared = Math::lengthSquared(velocity);
            if (speedSquared > MaximumVelocity * MaximumVelocity)
                velocity = velocity * (MaximumVelocity / std::sqrt(speedSquared));
            character.SetLinearVelocity(character.CancelVelocityTowardsSteepSlopes(toBackend(velocity)));
            const PhysicsQueryFilter3D filter{.includeSensors = true, .ignoredBody = id};
            const Impl::QueryBodyFilter bodyFilter(*m_impl, filter);
            character.ExtendedUpdate(m_impl->config.fixedDeltaSeconds,
                toBackend(m_impl->config.gravityMetersPerSecondSquared), record->characterUpdate,
                {}, {}, bodyFilter, {}, m_impl->temporaryAllocator);
            if (!localPositionValid(fromBackend(character.GetPosition())) ||
                !velocityValid(fromBackend(character.GetLinearVelocity())) || character.GetMaxHitsExceeded())
                return Core::failure(Physics3DErrorCode::BackendFailure,
                                     "Physics3D character exceeded its coordinate or contact budget");
        }
        const auto result =
            m_impl->system.Update(m_impl->config.fixedDeltaSeconds, static_cast<int>(m_impl->config.collisionSteps),
                                  &m_impl->temporaryAllocator, &m_impl->jobs);
        if (result != JPH::EPhysicsUpdateError::None || m_impl->contactCacheOverflow)
        {
            Core::Error error(Physics3DErrorCode::BackendFailure,
                              "Physics3D solver capacity exceeded; partial step quarantined");
            error.setNativeCode(static_cast<Core::i64>(result));
            return Core::failure(std::move(error));
        }
        if (m_impl->stepCount != (std::numeric_limits<Core::u64>::max)())
        {
            ++m_impl->stepCount;
        }
        quarantine.release();
        return Core::success();
    } catch (const std::exception& error)
    {
        return Core::failure(Physics3DErrorCode::BackendFailure, error.what());
    } catch (...)
    {
        return Core::failure(Physics3DErrorCode::BackendFailure, "Physics3D solver threw an unknown exception");
    }
}

Core::Result<float> PhysicsWorld3D::fixedDeltaSeconds() const
{
    if (auto status = ensureUsable(); !status) return Core::failure(std::move(status.error()));
    return m_impl->config.fixedDeltaSeconds;
}

Core::Result<std::optional<PhysicsRayHit3D>> PhysicsWorld3D::castRayClosest(const PhysicsRayCast3D& ray,
                                                                            const PhysicsQueryFilter3D& filter) const
{
    if (auto status = validateFilter(filter); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!localPositionValid(ray.originMeters) || !Math::isFinite(ray.displacementMeters) ||
        !localPositionValid(ray.originMeters + ray.displacementMeters) ||
        !(Math::lengthSquared(ray.displacementMeters) > 0.0F))
    {
        return Core::failure(Physics3DErrorCode::InvalidQuery, "Physics3D ray must be a nonzero finite local segment");
    }
    const JPH::RRayCast backendRay(toBackendPosition(ray.originMeters), toBackend(ray.displacementMeters));
    JPH::RayCastResult hit;
    const Impl::QueryBodyFilter bodyFilter(*m_impl, filter);
    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(backendRay, hit, {}, {}, bodyFilter))
    {
        return std::optional<PhysicsRayHit3D>{};
    }
    const auto position = backendRay.GetPointOnRay(hit.mFraction);
    const JPH::BodyLockRead bodyLock(m_impl->system.GetBodyLockInterface(), hit.mBodyID);
    if (!bodyLock.Succeeded())
    {
        return Core::failure(Physics3DErrorCode::BackendFailure, "Physics3D ray hit no longer resolves to a body");
    }
    return std::optional{PhysicsRayHit3D{
        .body = m_impl->publicIds[hit.mBodyID.GetIndex()],
        .fraction = hit.mFraction,
        .positionMeters = fromBackend(position),
        .normal = fromBackend(bodyLock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, position)),
        .originRevision = m_impl->origin.revision}};
}

Core::Result<std::optional<PhysicsRayHit3D>> PhysicsWorld3D::castShapeClosest(
    const PhysicsShapeCast3D& cast, const PhysicsQueryFilter3D& filter) const
try
{
    if (auto status = validateFilter(filter); !status) return Core::failure(std::move(status.error()));
    const PhysicsBody3DDesc desc{.shape = cast.shape, .positionMeters = cast.positionMeters, .rotation = cast.rotation};
    if (auto status = validatePhysicsBody3DDesc(desc); !status) return Core::failure(std::move(status.error()));
    if (!Math::isFinite(cast.displacementMeters) || !localPositionValid(cast.positionMeters + cast.displacementMeters) ||
        !(Math::lengthSquared(cast.displacementMeters) > 0.0F))
        return Core::failure(Physics3DErrorCode::InvalidQuery, "Physics3D sweep requires a nonzero finite local segment");
    auto shape = createBackendShape(cast.shape);
    if (!shape) return Core::failure(std::move(shape.error()));
    const auto sweep = JPH::RShapeCast::sFromWorldTransform(shape->GetPtr(), JPH::Vec3::sReplicate(1.0F),
        JPH::RMat44::sRotationTranslation(toBackend(cast.rotation), toBackendPosition(cast.positionMeters)),
        toBackend(cast.displacementMeters));
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const Impl::QueryBodyFilter bodyFilter(*m_impl, filter);
    m_impl->system.GetNarrowPhaseQuery().CastShape(sweep, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);
    if (!collector.HadHit()) return std::optional<PhysicsRayHit3D>{};
    const auto& hit = collector.mHit;
    return std::optional{PhysicsRayHit3D{.body = m_impl->publicId(hit.mBodyID2), .fraction = hit.mFraction,
        .positionMeters = fromBackend(hit.mContactPointOn2),
        .normal = fromBackend(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),
        .originRevision = m_impl->origin.revision}};
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Physics3D sweep shape allocation failed");
}

Core::Result<PhysicsContactRead3D> PhysicsWorld3D::readContactEvents(std::span<PhysicsContactEvent3D> output)
{
    if (auto status = ensureUsable(); !status) return Core::failure(std::move(status.error()));
    const auto count = (std::min)(output.size(), m_impl->eventCount);
    for (Core::usize index = 0; index < count; ++index)
        output[index] = m_impl->contactEvents[(m_impl->eventHead + index) % m_impl->contactEvents.size()];
    m_impl->eventHead = (m_impl->eventHead + count) % m_impl->contactEvents.size();
    m_impl->eventCount -= count;
    return PhysicsContactRead3D{count, m_impl->eventCount, std::exchange(m_impl->droppedEvents, 0)};
}

Core::Result<PhysicsQueryWriteResult3D> PhysicsWorld3D::queryAabb(const Math::Aabb3& bounds,
                                                                  const PhysicsQueryFilter3D& filter,
                                                                  std::span<PhysicsBodyId> output) const
{
    if (auto status = validateFilter(filter); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!localPositionValid(bounds.lower) || !localPositionValid(bounds.upper) || bounds.lower.x > bounds.upper.x ||
        bounds.lower.y > bounds.upper.y || bounds.lower.z > bounds.upper.z)
    {
        return Core::failure(Physics3DErrorCode::InvalidQuery,
                             "Physics3D query requires ordered finite local AABB bounds");
    }
    Impl::AabbCollector collector(*m_impl, filter);
    m_impl->system.GetBroadPhaseQuery().CollideAABox(JPH::AABox(toBackend(bounds.lower), toBackend(bounds.upper)),
                                                     collector);
    if (collector.overflow)
    {
        return Core::failure(Physics3DErrorCode::BackendFailure,
                             "Physics3D broadphase exceeded the body query capacity");
    }
    auto begin = m_impl->queryIds.begin();
    std::sort(begin, begin + collector.count,
              [](PhysicsBodyId first, PhysicsBodyId second) { return first.index() < second.index(); });
    const auto written = (std::min)(output.size(), collector.count);
    std::copy_n(begin, written, output.begin());
    return PhysicsQueryWriteResult3D{written, collector.count, m_impl->origin.revision};
}

Core::Result<PhysicsOrigin3D> PhysicsWorld3D::origin() const
{
    if (auto status = ensureUsable(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return m_impl->origin;
}

Core::Result<PhysicsGlobalPosition3D> PhysicsWorld3D::toGlobalPosition(Math::Vec3 localMeters) const
{
    if (auto status = ensureUsable(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    const auto& origin = m_impl->origin.positionMeters;
    const PhysicsGlobalPosition3D result{origin.x + localMeters.x, origin.y + localMeters.y, origin.z + localMeters.z};
    if (!localPositionValid(localMeters) || !globalPositionValid(result))
    {
        return Core::failure(Physics3DErrorCode::InvalidOriginShift,
                             "Physics3D global conversion exceeds its coordinate range");
    }
    return result;
}

Core::Result<Math::Vec3> PhysicsWorld3D::toLocalPosition(PhysicsGlobalPosition3D globalMeters) const
{
    if (auto status = ensureUsable(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!globalPositionValid(globalMeters))
    {
        return Core::failure(Physics3DErrorCode::InvalidOriginShift,
                             "Physics3D global position is outside its supported range");
    }
    const auto& origin = m_impl->origin.positionMeters;
    const double x = globalMeters.x - origin.x;
    const double y = globalMeters.y - origin.y;
    const double z = globalMeters.z - origin.z;
    if (std::abs(x) > Physics3DLimits::MaximumLocalCoordinateMeters ||
        std::abs(y) > Physics3DLimits::MaximumLocalCoordinateMeters ||
        std::abs(z) > Physics3DLimits::MaximumLocalCoordinateMeters)
    {
        return Core::failure(Physics3DErrorCode::InvalidOriginShift,
                             "Physics3D global position is outside the current local region");
    }
    return Math::Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

Core::Result<PhysicsOriginShift3D> PhysicsWorld3D::shiftOrigin(Math::Vec3 offsetMeters)
try
{
    if (auto status = ensureUsable(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    PhysicsOriginShift3D result{.before = m_impl->origin, .after = m_impl->origin, .offsetMeters = offsetMeters};
    if (offsetMeters == Math::Vec3{})
    {
        return result;
    }
    const auto& previous = result.before.positionMeters;
    const PhysicsGlobalPosition3D next{previous.x + offsetMeters.x, previous.y + offsetMeters.y,
                                       previous.z + offsetMeters.z};
    if (!localPositionValid(offsetMeters) || !globalPositionValid(next) ||
        m_impl->origin.revision == (std::numeric_limits<Core::u64>::max)() ||
        (offsetMeters.x != 0.0F && next.x == previous.x) || (offsetMeters.y != 0.0F && next.y == previous.y) ||
        (offsetMeters.z != 0.0F && next.z == previous.z))
    {
        return Core::failure(Physics3DErrorCode::InvalidOriginShift,
                             "Physics3D origin shift exceeds its coordinate or revision precision budget");
    }
    auto& bodies = m_impl->system.GetBodyInterface();
    // All validation and scratch writes precede the first backend mutation.
    // Include inactive and static bodies: leaving them behind breaks the world.
    for (const auto id : m_impl->publicIds)
    {
        const auto* record = m_impl->bodies.tryGet(id);
        if (record == nullptr)
        {
            continue;
        }
        const auto shifted = (record->character != nullptr ? fromBackend(record->character->GetPosition())
                                                           : fromBackend(bodies.GetPosition(record->backend))) - offsetMeters;
        if (!localPositionValid(shifted))
        {
            return Core::failure(Physics3DErrorCode::InvalidOriginShift,
                                 "Physics3D origin shift would move a body outside the local coordinate range");
        }
        m_impl->shiftedPositions[record->backend.GetIndex()] = shifted;
    }
    auto quarantine = Core::makeScopeExit([&]() noexcept { m_impl->faulted = true; });
    for (const auto id : m_impl->publicIds)
    {
        const auto* record = m_impl->bodies.tryGet(id);
        if (record == nullptr)
        {
            continue;
        }
        const auto shifted = toBackendPosition(m_impl->shiftedPositions[record->backend.GetIndex()]);
        if (record->character != nullptr) record->character->SetPosition(shifted);
        else bodies.SetPosition(record->backend, shifted, JPH::EActivation::DontActivate);
        bodies.InvalidateContactCache(record->backend);
        ++result.shiftedBodyCount;
    }
    result.after = {next, result.before.revision + 1};
    m_impl->origin = result.after;
    for (auto& contact : m_impl->activeContacts) {
        contact.event.positionMeters = contact.event.positionMeters - offsetMeters;
        contact.event.originRevision = result.after.revision;
    }
    for (const auto id : m_impl->publicIds)
    {
        auto* record = m_impl->bodies.tryGet(id);
        if (record == nullptr || record->character == nullptr) continue;
        const PhysicsQueryFilter3D filter{.includeSensors = true, .ignoredBody = id};
        const Impl::QueryBodyFilter bodyFilter(*m_impl, filter);
        record->character->RefreshContacts({}, {}, bodyFilter, {}, m_impl->temporaryAllocator);
        if (record->character->GetMaxHitsExceeded() || m_impl->contactCacheOverflow)
            return Core::failure(Physics3DErrorCode::CapacityExceeded, "Physics3D origin shift contact budget exceeded");
    }
    quarantine.release();
    return result;
} catch (const std::exception& error) {
    return Core::failure(Physics3DErrorCode::BackendFailure, error.what());
} catch (...) {
    return Core::failure(Physics3DErrorCode::BackendFailure, "Physics3D origin shift failed with an unknown exception");
}

Core::Result<PhysicsWorld3DStats> PhysicsWorld3D::stats() const
{
    if (!m_impl)
    {
        return Core::failure(Physics3DErrorCode::WorldClosed, "Physics3D world is closed");
    }
    if (m_impl->ownerThread != std::this_thread::get_id())
    {
        return Core::failure(Physics3DErrorCode::WrongOwnerThread, "Physics3D stats requires its owner thread");
    }
    return PhysicsWorld3DStats{m_impl->bodies.activeCount(), m_impl->stepCount, m_impl->origin.revision,
                               m_impl->faulted};
}

bool PhysicsWorld3D::isOpen() const noexcept
{
    return m_impl != nullptr;
}

Core::Status PhysicsWorld3D::shutdown()
{
    if (m_impl && m_impl->ownerThread != std::this_thread::get_id())
    {
        return Core::failure(Physics3DErrorCode::WrongOwnerThread, "Physics3D shutdown requires its owner thread");
    }
    m_impl.reset();
    return Core::success();
}

} // namespace Tina::Physics3D
