#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <new>

namespace Tina::AssetFormat {
namespace {

template <typename JointAt>
Core::Result<Core::ContentHash> signatureFor(Core::usize count, std::span<const float> inverseBind,
                                            JointAt&& jointAt)
try {
    if (count == 0 || count > SkinnedMeshWire::MaxJointCount || inverseBind.size() != count * 16U) {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "Invalid skeleton signature input shape");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(8U + count * 176U);
    const auto append = [&](Core::u32 value) {
        for (Core::u32 shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    };
    // Domain/version and count are part of the canonical identity.
    append(0x314c4b53U);
    append(static_cast<Core::u32>(count));
    for (Core::usize index = 0; index < count; ++index) {
        const auto joint = jointAt(static_cast<Core::u16>(index));
        if (!joint || joint->name.size() > SkinnedMeshWire::MaximumJointNameBytes ||
            (joint->parentJoint != SkinnedMeshWire::JointIndexNone && joint->parentJoint >= index)) {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "Invalid skeleton signature joint");
        }
        append(joint->parentJoint);
        append(static_cast<Core::u32>(joint->name.size()));
        for (unsigned char character : joint->name) { bytes.push_back(static_cast<std::byte>(character)); }
        const auto appendFloats = [&](std::span<const float> values) -> bool {
            for (float value : values) {
                if (!std::isfinite(value)) { return false; }
                append(std::bit_cast<Core::u32>(value == 0.0F ? 0.0F : value));
            }
            return true;
        };
        if (!appendFloats(joint->bindTranslation) || !appendFloats(joint->bindRotation) ||
            !appendFloats(joint->bindScale) || !appendFloats(inverseBind.subspan(index * 16U, 16U))) {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "Non-finite skeleton signature transform");
        }
    }
    return Core::digestContentHashV1(bytes);
} catch (const std::bad_alloc&) {
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Skeleton signature allocation failed");
}

} // namespace

Core::Result<Core::ContentHash> computeSkeletonSignature(
    std::span<const SkinnedMeshJointDesc> joints, std::span<const float> inverseBindMatrices)
{
    return signatureFor(joints.size(), inverseBindMatrices,
        [&](Core::u16 index) -> std::optional<SkinnedMeshJointView> {
            const auto& source = joints[index];
            SkinnedMeshJointView joint{.parentJoint = source.parentJoint};
            std::copy_n(source.bindTranslation, 3, joint.bindTranslation);
            std::copy_n(source.bindRotation, 4, joint.bindRotation);
            std::copy_n(source.bindScale, 3, joint.bindScale);
            joint.name = source.name;
            return joint;
        });
}

Core::Result<Core::ContentHash> computeSkeletonSignature(const SkinnedMeshPayloadView& mesh)
{
    return signatureFor(mesh.jointCount, mesh.inverseBindMatrices,
                        [&](Core::u16 index) { return mesh.joint(index); });
}

} // namespace Tina::AssetFormat
