#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/physics2d/PhysicsIds.hpp>

#include <span>

namespace Tina::Physics2D {

struct PhysicsVec2 final {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(const PhysicsVec2&, const PhysicsVec2&) noexcept = default;
};

enum class PhysicsBodyType2D : Core::u8 {
    Static,
    Kinematic,
    Dynamic,
};

struct PhysicsCollisionFilter2D final {
    Core::u64 categoryBits = 1;
    Core::u64 maskBits = ~Core::u64{0};
    Core::i32 groupIndex = 0;

    friend constexpr bool operator==(
        const PhysicsCollisionFilter2D&,
        const PhysicsCollisionFilter2D&) noexcept = default;
};

struct PhysicsWorld2DConfig final {
    static constexpr Core::usize DefaultBodyCapacity = 1024;
    static constexpr Core::usize DefaultShapeCapacity = 2048;
    static constexpr Core::usize DefaultContactBeginCapacity = 256;
    static constexpr Core::usize DefaultContactEndCapacity = 256;
    static constexpr Core::usize DefaultContactHitCapacity = 64;
    static constexpr Core::usize MaxBodyCapacity = 1'048'576;
    static constexpr Core::usize MaxShapeCapacity = 2'097'152;
    static constexpr Core::usize MaxContactEventCapacity = 1'048'576;
    static constexpr Core::u32 DefaultSolverSubStepCount = 4;
    static constexpr Core::u32 MaxSolverSubStepCount = 64;

    Core::usize bodyCapacity = DefaultBodyCapacity;
    Core::usize shapeCapacity = DefaultShapeCapacity;
    Core::usize contactBeginCapacity = DefaultContactBeginCapacity;
    Core::usize contactEndCapacity = DefaultContactEndCapacity;
    Core::usize contactHitCapacity = DefaultContactHitCapacity;
    PhysicsVec2 gravityMetersPerSecondSquared{0.0F, -9.8F};
    float fixedDeltaSeconds = 1.0F / 60.0F;
    Core::u32 solverSubStepCount = DefaultSolverSubStepCount;
};

struct PhysicsBody2DDesc final {
    PhysicsBodyType2D type = PhysicsBodyType2D::Static;
    PhysicsVec2 positionMeters{};
    float angleRadians = 0.0F;
    PhysicsVec2 linearVelocityMetersPerSecond{};
    float angularVelocityRadiansPerSecond = 0.0F;
    float linearDamping = 0.0F;
    float angularDamping = 0.0F;
    float gravityScale = 1.0F;
    bool enableSleep = true;
    bool initiallyAwake = true;
    bool fixedRotation = false;
    bool continuousCollision = false;
    bool enabled = true;
};

struct PhysicsBoxShape2DDesc final {
    PhysicsVec2 halfExtentsMeters{0.5F, 0.5F};
    PhysicsVec2 centerMeters{};
    float angleRadians = 0.0F;
    float density = 1.0F;
    float friction = 0.6F;
    float restitution = 0.0F;
    bool enableContactEvents = true;
    bool enableHitEvents = false;
    PhysicsCollisionFilter2D filter{};
};

struct PhysicsBodyShape2D final {
    PhysicsBodyId body{};
    PhysicsShapeId shape{};

    friend constexpr bool operator==(const PhysicsBodyShape2D&, const PhysicsBodyShape2D&) noexcept = default;
};

struct PhysicsBodyState2D final {
    PhysicsVec2 positionMeters{};
    float angleRadians = 0.0F;
    PhysicsVec2 linearVelocityMetersPerSecond{};
    float angularVelocityRadiansPerSecond = 0.0F;
    bool awake = false;
    bool enabled = false;
};

// Owning snapshots copied from Box2D before step() returns. End events may
// reference shapes already destroyed between the previous step and this one;
// those carry last-known generation IDs with destroyed* flags set.
struct PhysicsContactBeginEvent2D final {
    PhysicsBodyId bodyA{};
    PhysicsBodyId bodyB{};
    PhysicsShapeId shapeA{};
    PhysicsShapeId shapeB{};
};

struct PhysicsContactEndEvent2D final {
    PhysicsBodyId bodyA{};
    PhysicsBodyId bodyB{};
    PhysicsShapeId shapeA{};
    PhysicsShapeId shapeB{};
    bool shapeADestroyed = false;
    bool shapeBDestroyed = false;
};

struct PhysicsContactHitEvent2D final {
    PhysicsBodyId bodyA{};
    PhysicsBodyId bodyB{};
    PhysicsShapeId shapeA{};
    PhysicsShapeId shapeB{};
    PhysicsVec2 pointMeters{};
    PhysicsVec2 normalFromAToB{};
    float approachSpeedMetersPerSecond = 0.0F;
};

// Borrowed view into World-owned fixed storage. Valid on the owner thread from
// a successful step() return until the next step(), shutdown(), move, or destroy.
struct PhysicsContactEvents2DView final {
    std::span<const PhysicsContactBeginEvent2D> beginEvents{};
    std::span<const PhysicsContactEndEvent2D> endEvents{};
    std::span<const PhysicsContactHitEvent2D> hitEvents{};
    bool beginOverflow = false;
    bool endOverflow = false;
    bool hitOverflow = false;
};

// Spatial query inputs/results. Hits are owning small values written into a
// caller-provided buffer; overflow reports totalFound without expanding storage.
struct PhysicsQueryFilter2D final {
    Core::u64 categoryBits = 1;
    Core::u64 maskBits = ~Core::u64{0};

    friend constexpr bool operator==(
        const PhysicsQueryFilter2D&,
        const PhysicsQueryFilter2D&) noexcept = default;
};

struct PhysicsAabb2D final {
    PhysicsVec2 lowerMeters{};
    PhysicsVec2 upperMeters{};

    friend constexpr bool operator==(const PhysicsAabb2D&, const PhysicsAabb2D&) noexcept = default;
};

struct PhysicsRayCast2D final {
    PhysicsVec2 originMeters{};
    PhysicsVec2 translationMeters{};

    friend constexpr bool operator==(const PhysicsRayCast2D&, const PhysicsRayCast2D&) noexcept = default;
};

struct PhysicsOverlapHit2D final {
    PhysicsBodyId body{};
    PhysicsShapeId shape{};

    friend constexpr bool operator==(const PhysicsOverlapHit2D&, const PhysicsOverlapHit2D&) noexcept = default;
};

struct PhysicsCastHit2D final {
    PhysicsBodyId body{};
    PhysicsShapeId shape{};
    PhysicsVec2 pointMeters{};
    PhysicsVec2 normalMeters{};
    float fraction = 0.0F;

    friend constexpr bool operator==(const PhysicsCastHit2D&, const PhysicsCastHit2D&) noexcept = default;
};

struct PhysicsQueryWriteResult2D final {
    Core::usize written = 0;
    Core::usize totalFound = 0;
    bool overflow = false;
};

struct PhysicsWorld2DStats final {
    Core::usize bodyCount = 0;
    Core::usize shapeCount = 0;
    Core::usize bodyCapacity = 0;
    Core::usize shapeCapacity = 0;
    Core::usize contactBeginCapacity = 0;
    Core::usize contactEndCapacity = 0;
    Core::usize contactHitCapacity = 0;
    Core::u64 completedStepCount = 0;
    Core::u64 droppedBeginContactCount = 0;
    Core::u64 droppedEndContactCount = 0;
    Core::u64 droppedHitContactCount = 0;
    bool open = false;
};

} // namespace Tina::Physics2D
