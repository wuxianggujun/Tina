#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Geometry3D.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/physics3d/PhysicsIds.hpp>

namespace Tina::Physics3D {

namespace Physics3DLimits {
inline constexpr Core::u32 MaximumBodies = 65536;
inline constexpr Core::u32 MaximumBodyPairs = 1024U * 1024U;
inline constexpr Core::u32 MaximumContactConstraints = 65536;
inline constexpr float MaximumLocalCoordinateMeters = 1000000.0F;
inline constexpr float MaximumShapeExtentMeters = 10000.0F;
// Keeps global double precision below a millimeter. This is not an infinite world.
inline constexpr double MaximumGlobalCoordinateMeters = 1.0e12;
} // namespace Physics3DLimits

// Global position is deliberately distinct from a local simulation Vec3.
// Global values must be subtracted in double before narrowing to local float.
struct PhysicsGlobalPosition3D final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    friend bool operator==(const PhysicsGlobalPosition3D&, const PhysicsGlobalPosition3D&) noexcept = default;
};

struct PhysicsOrigin3D final {
    PhysicsGlobalPosition3D positionMeters{};
    Core::u64 revision = 0;
};

enum class PhysicsBodyType3D : Core::u8 { Static, Kinematic, Dynamic };
enum class PhysicsShapeKind3D : Core::u8 { Box, Sphere, Capsule };

// One immutable, centered shape per body. Capsule's cylindrical axis is local Y;
// halfHeight excludes the two hemispheres. No mutable shape registry is exposed.
struct PhysicsShape3DDesc final {
    PhysicsShapeKind3D kind = PhysicsShapeKind3D::Box;
    Math::Vec3 halfExtentsMeters{0.5F, 0.5F, 0.5F};
    float radiusMeters = 0.5F;
    float halfHeightMeters = 0.5F;
};

struct PhysicsBody3DDesc final {
    PhysicsBodyType3D type = PhysicsBodyType3D::Static;
    PhysicsShape3DDesc shape{};
    Math::Vec3 positionMeters{};
    Math::Quaternion rotation{};
    Math::Vec3 linearVelocityMetersPerSecond{};
    Math::Vec3 angularVelocityRadiansPerSecond{};
    float massKilograms = 1.0F;
    float friction = 0.5F;
    float restitution = 0.0F;
    float gravityFactor = 1.0F;
    bool sensor = false;
    bool startAwake = true;
};

struct PhysicsBodyState3D final {
    PhysicsBodyId body{};
    PhysicsBodyType3D type = PhysicsBodyType3D::Static;
    Math::Vec3 positionMeters{};
    Math::Quaternion rotation{};
    Math::Vec3 linearVelocityMetersPerSecond{};
    Math::Vec3 angularVelocityRadiansPerSecond{};
    Core::u64 originRevision = 0;
    bool sensor = false;
    bool awake = false;
};

struct PhysicsWorld3DConfig final {
    Core::u32 bodyCapacity = 1024;
    Core::u32 bodyPairCapacity = 4096;
    Core::u32 contactConstraintCapacity = 2048;
    float fixedDeltaSeconds = 1.0F / 60.0F;
    Core::u32 collisionSteps = 1;
    Math::Vec3 gravityMetersPerSecondSquared{0.0F, -9.81F, 0.0F};
    PhysicsGlobalPosition3D initialOriginMeters{};
};

struct PhysicsQueryFilter3D final {
    bool includeStatic = true;
    bool includeMoving = true;
    bool includeSensors = false;
    PhysicsBodyId ignoredBody{};
};

// displacement is the complete finite segment, not a normalized direction.
struct PhysicsRayCast3D final {
    Math::Vec3 originMeters{};
    Math::Vec3 displacementMeters{0.0F, -1.0F, 0.0F};
};

struct PhysicsRayHit3D final {
    PhysicsBodyId body{};
    float fraction = 0.0F;
    Math::Vec3 positionMeters{};
    Math::Vec3 normal{};
    Core::u64 originRevision = 0;
};

struct PhysicsQueryWriteResult3D final {
    Core::usize writtenCount = 0;
    Core::usize totalCount = 0;
    Core::u64 originRevision = 0;
    [[nodiscard]] bool overflowed() const noexcept
    {
        return writtenCount < totalCount;
    }
};

struct PhysicsOriginShift3D final {
    PhysicsOrigin3D before{};
    PhysicsOrigin3D after{};
    Math::Vec3 offsetMeters{};
    Core::usize shiftedBodyCount = 0;
};

struct PhysicsWorld3DStats final {
    Core::usize bodyCount = 0;
    Core::u64 stepCount = 0;
    Core::u64 originShiftCount = 0;
    bool faulted = false;
};

} // namespace Tina::Physics3D
