#include <tina/animation3d/Skeleton3D.hpp>

#include <tina/math/Mat4.hpp>

#include <algorithm>
#include <bit>
#include <new>
#include <utility>

namespace Tina::Animation3D {

namespace {

// Math::Mat4 already stores its 16 floats column-major, which is the layout Render's
// skinned palette expects, so these are straight copies rather than a transpose.
void writeMatrix(std::span<float> destination, Core::usize offset, const Math::Mat4& value) noexcept
{
    for (Core::usize element = 0; element < 16U; ++element) {
        destination[offset + element] = value.columns[element];
    }
}

[[nodiscard]] Math::Mat4 readMatrix(std::span<const float> source, Core::usize offset) noexcept
{
    Math::Mat4 value{};
    for (Core::usize element = 0; element < 16U; ++element) {
        value.columns[element] = source[offset + element];
    }
    return value;
}

} // namespace

Pose3D::Pose3D(std::pmr::vector<Scene::LocalTransform> transforms) noexcept
    : m_transforms(std::move(transforms))
{
}

Core::Result<Pose3D> Pose3D::Create(Core::u16 jointCount, std::pmr::memory_resource& resource)
{
    if (jointCount == 0U || jointCount > MaximumJointCount) {
        return Core::failure(Animation3DErrorCode::InvalidArgument,
                             "pose joint count must be within the skeleton bound");
    }
    try {
        std::pmr::vector<Scene::LocalTransform> transforms{
            std::pmr::polymorphic_allocator<Scene::LocalTransform>{&resource}};
        transforms.resize(jointCount);
        return Pose3D(std::move(transforms));
    } catch (const std::bad_alloc&) {
        return Core::failure(Animation3DErrorCode::AllocationFailed, "pose allocation failed");
    }
}

void Pose3D::copyFrom(const Pose3D& other) noexcept
{
    const Core::usize count = (std::min)(m_transforms.size(), other.m_transforms.size());
    for (Core::usize index = 0; index < count; ++index) {
        m_transforms[index] = other.m_transforms[index];
    }
}

void JointMask::includeAll() noexcept
{
    m_empty = true;
    for (Core::u64& word : m_words) {
        word = ~Core::u64{0};
    }
}

void JointMask::excludeAll() noexcept
{
    m_empty = false;
    for (Core::u64& word : m_words) {
        word = 0U;
    }
}

void JointMask::include(Core::u16 joint) noexcept
{
    if (joint >= MaximumJointCount) {
        return;
    }
    // The first explicit include turns an "everything" mask into a selective one. Without
    // this, include() on a default mask would be a no-op that reads as success.
    if (m_empty) {
        m_empty = false;
        for (Core::u64& word : m_words) {
            word = 0U;
        }
    }
    m_words[joint / 64U] |= (Core::u64{1} << (joint % 64U));
}

void JointMask::exclude(Core::u16 joint) noexcept
{
    if (joint >= MaximumJointCount) {
        return;
    }
    // Excluding from an "everything" mask has to materialise the everything first,
    // otherwise the exclusion is lost.
    if (m_empty) {
        m_empty = false;
        for (Core::u64& word : m_words) {
            word = ~Core::u64{0};
        }
    }
    m_words[joint / 64U] &= ~(Core::u64{1} << (joint % 64U));
}

Core::u16 JointMask::includedCount(Core::u16 jointCount) const noexcept
{
    const Core::u16 bounded = (std::min)(jointCount, MaximumJointCount);
    if (m_empty) {
        return bounded;
    }
    Core::u16 count = 0;
    for (Core::u16 joint = 0; joint < bounded; ++joint) {
        if (includes(joint)) {
            ++count;
        }
    }
    return count;
}

Skeleton3D::Skeleton3D(std::pmr::vector<Core::u16> parents,
                       std::pmr::vector<Scene::LocalTransform> bindPose,
                       std::pmr::vector<float> inverseBindMatrices,
                       std::pmr::vector<std::string> jointNames) noexcept
    : m_parents(std::move(parents)), m_bindPose(std::move(bindPose)),
      m_inverseBindMatrices(std::move(inverseBindMatrices)), m_jointNames(std::move(jointNames))
{
}

Core::Result<Skeleton3D> Skeleton3D::Create(const AssetFormat::SkinnedMeshPayloadView& mesh,
                                           std::pmr::memory_resource& resource)
{
    if (mesh.jointCount == 0U || mesh.jointCount > MaximumJointCount) {
        return Core::failure(Animation3DErrorCode::InvalidArgument,
                             "skinned mesh joint count is outside the supported range");
    }
    const Core::usize expectedFloats =
        static_cast<Core::usize>(mesh.jointCount) * AssetFormat::SkinnedMeshWire::FloatsPerInverseBindMatrix;
    if (mesh.inverseBindMatrices.size() != expectedFloats) {
        return Core::failure(Animation3DErrorCode::InvalidArgument,
                             "skinned mesh inverse bind matrix count does not match jointCount");
    }

    try {
        std::pmr::vector<Core::u16> parents{std::pmr::polymorphic_allocator<Core::u16>{&resource}};
        std::pmr::vector<Scene::LocalTransform> bindPose{
            std::pmr::polymorphic_allocator<Scene::LocalTransform>{&resource}};
        std::pmr::vector<float> inverseBind{std::pmr::polymorphic_allocator<float>{&resource}};
        std::pmr::vector<std::string> names{std::pmr::polymorphic_allocator<std::string>{&resource}};
        parents.reserve(mesh.jointCount);
        bindPose.reserve(mesh.jointCount);
        names.reserve(mesh.jointCount);
        inverseBind.assign(mesh.inverseBindMatrices.begin(), mesh.inverseBindMatrices.end());

        for (Core::u16 index = 0; index < mesh.jointCount; ++index) {
            const auto joint = mesh.joint(index);
            if (!joint) {
                return Core::failure(Animation3DErrorCode::InvalidArgument,
                                     "skinned mesh joint table is truncated");
            }
            // Re-checked rather than trusted: the payload parser enforces this, but a
            // hand-built view could not be, and composeSkinningMatrices' single forward
            // pass reads the parent's already-written matrix.
            if (joint->parentJoint != JointIndexNone && joint->parentJoint >= index) {
                return Core::failure(Animation3DErrorCode::InvalidArgument,
                                     "skeleton joint parents must precede their children");
            }
            parents.push_back(joint->parentJoint);
            const Scene::LocalTransform bind{
                .position = Math::Vec3{joint->bindTranslation[0], joint->bindTranslation[1],
                                       joint->bindTranslation[2]},
                .rotation = Math::Quaternion{joint->bindRotation[0], joint->bindRotation[1],
                                             joint->bindRotation[2], joint->bindRotation[3]},
                .scale = Math::Vec3{joint->bindScale[0], joint->bindScale[1], joint->bindScale[2]},
            };
            if (!Scene::isValid(bind)) {
                return Core::failure(Animation3DErrorCode::InvalidArgument,
                                     "skeleton bind pose transform is not finite");
            }
            bindPose.push_back(bind);
            names.emplace_back(joint->name);
        }

        return Skeleton3D(std::move(parents), std::move(bindPose), std::move(inverseBind),
                          std::move(names));
    } catch (const std::bad_alloc&) {
        return Core::failure(Animation3DErrorCode::AllocationFailed, "skeleton allocation failed");
    }
}

Core::u16 Skeleton3D::parent(Core::u16 joint) const noexcept
{
    return joint < m_parents.size() ? m_parents[joint] : JointIndexNone;
}

const Scene::LocalTransform& Skeleton3D::bindPose(Core::u16 joint) const noexcept
{
    return m_bindPose[joint];
}

std::string_view Skeleton3D::jointName(Core::u16 joint) const noexcept
{
    return joint < m_jointNames.size() ? std::string_view{m_jointNames[joint]} : std::string_view{};
}

std::optional<Core::u16> Skeleton3D::findJoint(std::string_view name) const noexcept
{
    // An unnamed joint is not addressable, so an empty query matches nothing rather than
    // the first unnamed joint.
    if (name.empty()) {
        return std::nullopt;
    }
    for (Core::u16 index = 0; index < m_jointNames.size(); ++index) {
        if (m_jointNames[index] == name) {
            return index;
        }
    }
    return std::nullopt;
}

Core::Result<JointMask> Skeleton3D::resolveMask(std::span<const std::string_view> jointNames,
                                                bool includeDescendants) const
{
    JointMask mask{};
    mask.excludeAll();
    for (const std::string_view name : jointNames) {
        const auto index = findJoint(name);
        if (!index) {
            return Core::failure(Animation3DErrorCode::UnknownJointName,
                                 "skeleton has no joint with the requested name");
        }
        mask.include(*index);
    }
    if (!includeDescendants) {
        return mask;
    }

    // One forward pass suffices: a parent index is always lower than its child's, so by the
    // time a joint is visited its parent's inclusion is already final.
    for (Core::u16 index = 0; index < m_parents.size(); ++index) {
        const Core::u16 parentIndex = m_parents[index];
        if (parentIndex != JointIndexNone && mask.includes(parentIndex)) {
            mask.include(index);
        }
    }
    return mask;
}

Core::Status Skeleton3D::writeBindPose(Pose3D& pose) const noexcept
{
    if (pose.jointCount() != jointCount()) {
        return Core::failure(Animation3DErrorCode::SkeletonMismatch,
                             "pose joint count does not match the skeleton");
    }
    for (Core::u16 index = 0; index < jointCount(); ++index) {
        pose.at(index) = m_bindPose[index];
    }
    return Core::success();
}

Core::Status Skeleton3D::composeGlobalMatrices(const Pose3D& pose,
                                               std::span<float> outGlobalMatrices) const noexcept
{
    if (pose.jointCount() != jointCount()) {
        return Core::failure(Animation3DErrorCode::SkeletonMismatch,
                             "pose joint count does not match the skeleton");
    }
    const Core::usize required =
        static_cast<Core::usize>(jointCount()) * AssetFormat::SkinnedMeshWire::FloatsPerInverseBindMatrix;
    if (outGlobalMatrices.size() != required) {
        return Core::failure(Animation3DErrorCode::InvalidArgument,
                             "global matrix span size does not match jointCount * 16");
    }

    for (Core::u16 index = 0; index < jointCount(); ++index) {
        const Scene::LocalTransform& local = pose.at(index);
        const Math::Mat4 localMatrix = Math::fromTrs(local.position, local.rotation, local.scale);
        const Core::usize offset = static_cast<Core::usize>(index) * 16U;
        const Core::u16 parentIndex = m_parents[index];
        // Parents precede children, so the parent's global matrix is already written.
        const Math::Mat4 global = parentIndex == JointIndexNone
            ? localMatrix
            : Math::multiply(readMatrix(outGlobalMatrices, static_cast<Core::usize>(parentIndex) * 16U),
                             localMatrix);
        if (!Math::isFinite(global)) {
            return Core::failure(Animation3DErrorCode::EvaluationFailed,
                                 "composed global joint matrix is not finite");
        }
        writeMatrix(outGlobalMatrices, offset, global);
    }
    return Core::success();
}

Core::Status Skeleton3D::composeSkinningMatrices(const Pose3D& pose,
                                                 std::span<float> outSkinningMatrices) const noexcept
{
    // Globals are composed into the output span first, then each is replaced by
    // `global * inverseBind` in place. No scratch buffer is needed: the second pass is
    // per-joint independent -- it never reads a parent -- so it cannot observe a slot the
    // first pass has already rewritten. The hierarchy dependency lives entirely inside
    // composeGlobalMatrices, which is why it has to finish before this loop starts.
    if (Core::Status status = composeGlobalMatrices(pose, outSkinningMatrices); !status) {
        return status;
    }
    for (Core::u16 index = 0; index < jointCount(); ++index) {
        const Core::usize offset = static_cast<Core::usize>(index) * 16U;
        const Math::Mat4 global = readMatrix(outSkinningMatrices, offset);
        const Math::Mat4 inverseBind = readMatrix(m_inverseBindMatrices, offset);
        const Math::Mat4 skinning = Math::multiply(global, inverseBind);
        if (!Math::isFinite(skinning)) {
            return Core::failure(Animation3DErrorCode::EvaluationFailed,
                                 "composed skinning matrix is not finite");
        }
        writeMatrix(outSkinningMatrices, offset, skinning);
    }
    return Core::success();
}

} // namespace Tina::Animation3D
