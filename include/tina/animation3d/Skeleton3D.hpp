#pragma once

#include <tina/animation3d/AnimationErrors.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/scene/Transform.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Animation3D {

inline constexpr Core::u16 MaximumJointCount = AssetFormat::SkinnedMeshWire::MaxJointCount;
inline constexpr Core::u16 JointIndexNone = AssetFormat::SkinnedMeshWire::JointIndexNone;

// A pose in joint-local space, one transform per joint, indexed by joint index.
//
// This is the currency every graph node deals in: samplers produce one, blends consume two
// and produce one, IK edits one in place. It is deliberately local-space rather than
// global: blending two global poses gives limbs that stretch, because interpolating a
// child's world position independently of its parent's does not preserve bone length.
// Global matrices are derived once at the end, in Skeleton3D::composeSkinningMatrices.
class Pose3D final {
  public:
    Pose3D() = default;

    [[nodiscard]] static Core::Result<Pose3D> Create(
        Core::u16 jointCount,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    Pose3D(const Pose3D&) = delete;
    Pose3D& operator=(const Pose3D&) = delete;
    Pose3D(Pose3D&&) noexcept = default;
    Pose3D& operator=(Pose3D&&) noexcept = default;

    [[nodiscard]] Core::u16 jointCount() const noexcept
    {
        return static_cast<Core::u16>(m_transforms.size());
    }
    [[nodiscard]] bool empty() const noexcept { return m_transforms.empty(); }

    [[nodiscard]] std::span<Scene::LocalTransform> transforms() noexcept { return m_transforms; }
    [[nodiscard]] std::span<const Scene::LocalTransform> transforms() const noexcept
    {
        return m_transforms;
    }

    // Unchecked; callers iterate over jointCount(). Bounds-checked access would cost a
    // branch per joint per blend per frame in the hottest loop this module has.
    [[nodiscard]] Scene::LocalTransform& at(Core::u16 joint) noexcept { return m_transforms[joint]; }
    [[nodiscard]] const Scene::LocalTransform& at(Core::u16 joint) const noexcept
    {
        return m_transforms[joint];
    }

    void copyFrom(const Pose3D& other) noexcept;

  private:
    explicit Pose3D(std::pmr::vector<Scene::LocalTransform> transforms) noexcept;

    std::pmr::vector<Scene::LocalTransform> m_transforms;
};

// Which joints a layer or blend is allowed to write. One bit per joint index.
//
// Index-based rather than name-based storage, resolved from names once at build time
// through Skeleton3D::findJoint. A mask that held strings would either re-resolve every
// frame or cache indices anyway, and per-frame string comparison in a per-joint loop is
// the kind of cost that does not show up until a rig has 200 bones.
class JointMask final {
  public:
    // Empty means "every joint", not "no joint". A default-constructed mask on a layer has
    // to mean the layer animates the whole skeleton, because that is what a caller who did
    // not ask for masking wants; "no joints" is expressible by weight 0 instead.
    constexpr JointMask() = default;

    [[nodiscard]] constexpr bool includesEveryJoint() const noexcept { return m_empty; }

    void includeAll() noexcept;
    void excludeAll() noexcept;
    void include(Core::u16 joint) noexcept;
    void exclude(Core::u16 joint) noexcept;

    [[nodiscard]] constexpr bool includes(Core::u16 joint) const noexcept
    {
        if (m_empty) {
            return true;
        }
        if (joint >= MaximumJointCount) {
            return false;
        }
        return (m_words[joint / 64U] & (Core::u64{1} << (joint % 64U))) != 0U;
    }

    [[nodiscard]] Core::u16 includedCount(Core::u16 jointCount) const noexcept;

  private:
    static constexpr Core::usize WordCount = MaximumJointCount / 64U;
    static_assert(MaximumJointCount % 64U == 0U);

    // Tracked separately from an all-ones bitset so "unset" and "explicitly everything"
    // stay distinguishable in diagnostics.
    bool m_empty = true;
    Core::u64 m_words[WordCount]{};
};

// An immutable skeleton: bind pose, hierarchy, inverse bind matrices and joint names,
// copied out of a cooked SkinnedMesh payload.
//
// It owns a copy rather than borrowing the payload because a graph outlives any single
// AssetLease borrow window, and the alternative -- every node holding a lease -- would put
// asset lifetime into the animation layer, which is exactly the coupling ADR 0031 keeps
// out of Scene.
class Skeleton3D final {
  public:
    [[nodiscard]] static Core::Result<Skeleton3D> Create(
        const AssetFormat::SkinnedMeshPayloadView& mesh,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    Skeleton3D(const Skeleton3D&) = delete;
    Skeleton3D& operator=(const Skeleton3D&) = delete;
    Skeleton3D(Skeleton3D&&) noexcept = default;
    Skeleton3D& operator=(Skeleton3D&&) = delete;

    [[nodiscard]] Core::u16 jointCount() const noexcept
    {
        return static_cast<Core::u16>(m_parents.size());
    }

    // JointIndexNone for a root. Always strictly less than the joint's own index, which is
    // what lets composeSkinningMatrices run as a single forward pass.
    [[nodiscard]] Core::u16 parent(Core::u16 joint) const noexcept;
    [[nodiscard]] const Scene::LocalTransform& bindPose(Core::u16 joint) const noexcept;
    [[nodiscard]] std::span<const Scene::LocalTransform> bindPose() const noexcept
    {
        return m_bindPose;
    }
    // Empty for an unnamed joint or an out-of-range index.
    [[nodiscard]] std::string_view jointName(Core::u16 joint) const noexcept;

    // Index of the joint carrying this name. Linear over at most 256 joints; a caller
    // resolving a whole mask should call resolveMask instead of looping on this.
    [[nodiscard]] std::optional<Core::u16> findJoint(std::string_view name) const noexcept;

    // Resolves joint names into a mask. Fails with UnknownJointName on the first name this
    // skeleton does not carry, rather than skipping it: a mask that silently drops the
    // bones it could not find produces an animation that is subtly wrong everywhere, and
    // the usual cause is a mask authored against a different rig.
    //
    // includeDescendants covers the common authoring intent -- "the upper body" means a
    // spine joint and everything under it -- without making the caller enumerate a rig.
    [[nodiscard]] Core::Result<JointMask> resolveMask(std::span<const std::string_view> jointNames,
                                                     bool includeDescendants) const;

    // Writes the bind pose into an existing pose. Fails on a jointCount mismatch.
    [[nodiscard]] Core::Status writeBindPose(Pose3D& pose) const noexcept;

    // Composes local transforms into `globalPose * inverseBind` per joint, column-major,
    // 16 floats each -- the exact layout Render's skinned palette expects.
    //
    // Fails EvaluationFailed on a non-finite result and leaves `outSkinningMatrices`
    // partially written; callers keep their previous good palette rather than uploading
    // this one. A collapsed skeleton on screen is worse than a stale one.
    [[nodiscard]] Core::Status composeSkinningMatrices(
        const Pose3D& pose, std::span<float> outSkinningMatrices) const noexcept;

    // Same composition, but stopping at global joint matrices. IK solvers need these:
    // reaching a world-space goal requires knowing where the chain currently is.
    [[nodiscard]] Core::Status composeGlobalMatrices(const Pose3D& pose,
                                                     std::span<float> outGlobalMatrices) const noexcept;

  private:
    Skeleton3D(std::pmr::vector<Core::u16> parents,
               std::pmr::vector<Scene::LocalTransform> bindPose,
               std::pmr::vector<float> inverseBindMatrices,
               std::pmr::vector<std::string> jointNames) noexcept;

    std::pmr::vector<Core::u16> m_parents;
    std::pmr::vector<Scene::LocalTransform> m_bindPose;
    std::pmr::vector<float> m_inverseBindMatrices;
    // Owned copies: the payload they came from is not kept alive by this type.
    std::pmr::vector<std::string> m_jointNames;
};

} // namespace Tina::Animation3D
