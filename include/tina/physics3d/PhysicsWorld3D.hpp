#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/physics3d/PhysicsErrors.hpp>
#include <tina/physics3d/PhysicsTypes.hpp>

#include <memory>
#include <optional>
#include <span>

namespace Tina::Physics3D {

[[nodiscard]] Core::Status validatePhysicsWorld3DConfig(const PhysicsWorld3DConfig& config);
[[nodiscard]] Core::Status validatePhysicsBody3DDesc(const PhysicsBody3DDesc& desc);

// Owner-thread, single-threaded solver, fixed-step world. No Scene/Runtime owner
// or worker is created. Move transfers ownership to the receiving thread;
// destruction must occur on that owner thread, like shutdown().
// Registry/query storage is reserved at Create; backend shapes/bodies and solver
// temporary memory use the backend allocator, not a supplied PMR resource.
class PhysicsWorld3D final {
  public:
    [[nodiscard]] static Core::Result<PhysicsWorld3D> Create(PhysicsWorld3DConfig config = {});
    ~PhysicsWorld3D() noexcept;
    PhysicsWorld3D(const PhysicsWorld3D&) = delete;
    PhysicsWorld3D& operator=(const PhysicsWorld3D&) = delete;
    PhysicsWorld3D(PhysicsWorld3D&& other) noexcept;
    PhysicsWorld3D& operator=(PhysicsWorld3D&&) = delete;

    [[nodiscard]] Core::Result<PhysicsBodyId> createBody(const PhysicsBody3DDesc& desc);
    [[nodiscard]] Core::Result<PhysicsBodyId> createCharacter(const CharacterController3DDesc& desc);
    [[nodiscard]] Core::Status setCharacterInput(PhysicsBodyId character, const CharacterController3DInput& input);
    [[nodiscard]] Core::Result<CharacterController3DState> characterState(PhysicsBodyId character) const;
    [[nodiscard]] Core::Status destroyBody(PhysicsBodyId body);
    [[nodiscard]] Core::Result<PhysicsBodyState3D> bodyState(PhysicsBodyId body) const;
    [[nodiscard]] Core::Status setTransform(PhysicsBodyId body, Math::Vec3 positionMeters, Math::Quaternion rotation,
                                            bool wake = true);
    [[nodiscard]] Core::Status setLinearVelocity(PhysicsBodyId body, Math::Vec3 velocityMetersPerSecond);
    [[nodiscard]] Core::Status addLinearImpulse(PhysicsBodyId body, Math::Vec3 impulseNewtonSeconds);
    [[nodiscard]] Core::Status setAwake(PhysicsBodyId body, bool awake);
    // Error after solver entry is not rollback: the world becomes faulted and
    // accepts only stats()/destroyBody()/shutdown(), preventing consumption of a partial step.
    [[nodiscard]] Core::Status step();
    [[nodiscard]] Core::Result<float> fixedDeltaSeconds() const;

    [[nodiscard]] Core::Result<std::optional<PhysicsRayHit3D>>
    castRayClosest(const PhysicsRayCast3D& ray, const PhysicsQueryFilter3D& filter = {}) const;
    [[nodiscard]] Core::Result<std::optional<PhysicsRayHit3D>>
    castShapeClosest(const PhysicsShapeCast3D& cast, const PhysicsQueryFilter3D& filter = {}) const;
    // Consumes only written events; retained events survive steps and body removal.
    // droppedCount reports queue overflow since the previous successful read.
    [[nodiscard]] Core::Result<PhysicsContactRead3D> readContactEvents(std::span<PhysicsContactEvent3D> output);
    // Conservative broadphase AABB query, not exact shape overlap. Sorted by
    // public body index; caller owns output. Empty output still counts all hits.
    [[nodiscard]] Core::Result<PhysicsQueryWriteResult3D>
    queryAabb(const Math::Aabb3& bounds, const PhysicsQueryFilter3D& filter, std::span<PhysicsBodyId> output) const;

    [[nodiscard]] Core::Result<PhysicsOrigin3D> origin() const;
    [[nodiscard]] Core::Result<PhysicsGlobalPosition3D> toGlobalPosition(Math::Vec3 localMeters) const;
    [[nodiscard]] Core::Result<Math::Vec3> toLocalPosition(PhysicsGlobalPosition3D globalMeters) const;
    // local' = local - offset; globalOrigin' = globalOrigin + offset.
    // Preflights every body and the accumulated origin before any mutation.
    // Preserves IDs, rotations, velocities and active/sleeping states. A zero
    // offset is a no-op. Cached contact pairs are invalidated. Active bodies'
    // sleep-test timers restart; bodies that were asleep remain asleep.
    // Call between steps, before publishing Scene/camera/query data. Old local
    // snapshots are invalid after a nonzero shift; use their originRevision.
    [[nodiscard]] Core::Result<PhysicsOriginShift3D> shiftOrigin(Math::Vec3 offsetMeters);

    [[nodiscard]] Core::Result<PhysicsWorld3DStats> stats() const;
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] Core::Status shutdown();

  private:
    struct Impl;
    explicit PhysicsWorld3D(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] Core::Status ensureUsable() const;
    [[nodiscard]] Core::Status validateBody(PhysicsBodyId body, bool allowFaulted = false) const;
    [[nodiscard]] Core::Status validateFilter(const PhysicsQueryFilter3D& filter) const;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Tina::Physics3D
