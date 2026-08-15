#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

// SkinnedMesh cooked payload schema v1 (little-endian, after CookedAsset header/deps).
// Bytes 0..31 of the header are byte-identical in layout and meaning to the whole
// StaticMesh v1 header, and the vertex/submesh/index blocks reuse the StaticMesh
// P3N3T4UV2 record, the 16-byte submesh record and the u16 index array unchanged.
// Skin data lives in separate parallel arrays so the single product vertex layout
// (StaticMeshWire::VertexLayout) keeps exactly 12 floats per vertex.
//
// Layout:
//   u16 schemaVersion       (=1)
//   u16 vertexLayout        (=2, P3N3T4UV2)
//   u16 indexType           (1 = U16)
//   u16 submeshCount        (1..MaxSubmeshes)
//   u32 vertexCount         (1..MaxVertexCount)
//   u32 indexCount          (positive multiple of 3, <= MaxIndexCount)
//   f32 boundsCenterX/Y/Z
//   f32 boundsRadius
//   u16 jointCount          (1..MaxJointCount)
//   u16 influencesPerVertex (=4)
//   u32 reserved0/1/2       (=0)
//   f32 inverseBindMatrices[jointCount * 16]  // column-major, 64B per joint
//   SkinnedMeshJointWire[jointCount]          (44B each)
//   StaticMeshSubmeshWire[submeshCount]       (16B each)
//   f32 vertices[vertexCount * 12]            // interleaved P3N3T4UV2
//   u16 jointIndices[vertexCount * 4]
//   u16 jointWeights[vertexCount * 4]         // fixed-point, sums to WeightScale
//   u16 indices[indexCount]
//
// The inverse bind block is placed first so it starts at HeaderBytes (48), which is a
// multiple of 16 and pairs with the cooked payloadAlignment of 16 for SIMD loads.
//
// Scene Animator3D pose evaluation and Render GPU palette upload consume this
// immutable cooked payload without changing its wire contract.
namespace SkinnedMeshWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 48;
inline constexpr Core::u32 JointBytes = 44;
inline constexpr Core::u32 InverseBindMatrixBytes = 64;
inline constexpr Core::u32 SubmeshBytes = StaticMeshWire::SubmeshBytes;
inline constexpr Core::u16 VertexLayout = StaticMeshWire::VertexLayout;
inline constexpr Core::u16 FloatsPerVertex = StaticMeshWire::FloatsPerVertex;
inline constexpr Core::u16 InfluencesPerVertex = 4;
inline constexpr Core::u16 MaxSubmeshes = StaticMeshWire::MaxSubmeshes;
// Deliberately tighter than StaticMeshWire::MaxVertexCount: product indices are u16, so
// a vertex above 65535 is unreachable and unrepresentable. Do not "unify" these two.
inline constexpr Core::u32 MaxVertexCount = 65'535;
inline constexpr Core::u32 MaxIndexCount = StaticMeshWire::MaxIndexCount;
// Frozen GPU palette bound: 256 mat4 = 16 KiB of uniform data.
inline constexpr Core::u16 MaxJointCount = 256;
inline constexpr Core::u16 JointIndexNone = 0xFFFF;
inline constexpr Core::u16 WeightScale = 0xFFFF;
inline constexpr Core::u32 FloatsPerInverseBindMatrix = 16;

// Bytes 0..31 mirror the StaticMesh v1 header exactly; skin fields are appended.
static_assert(HeaderBytes == StaticMeshWire::HeaderBytes + 16U);
// Every block starts on a 4-byte boundary, so no padding is encoded between them.
static_assert(HeaderBytes % 4 == 0);
static_assert(JointBytes % 4 == 0);
static_assert(InverseBindMatrixBytes % 4 == 0);
static_assert(SubmeshBytes % 4 == 0);
// The inverse bind block starts at HeaderBytes and needs 16-byte alignment for SIMD.
static_assert(HeaderBytes % 16 == 0);
static_assert(InverseBindMatrixBytes % 16 == 0);
static_assert(InverseBindMatrixBytes == FloatsPerInverseBindMatrix * sizeof(float));
// parentJoint uses 0xFFFF as the root sentinel, so real joints must stay below it.
static_assert(MaxJointCount < JointIndexNone);
// Product indices are u16, so the vertex space cannot exceed the index space.
static_assert(MaxVertexCount <= 0xFFFFU);
} // namespace SkinnedMeshWire

// Bind-pose local transform of one joint. parentJoint is JointIndexNone for a root and
// otherwise strictly less than the joint's own index: parents always precede children,
// so a palette can be composed in a single forward pass and cycles cannot be encoded.
struct SkinnedMeshJointDesc final {
    Core::u16 parentJoint = SkinnedMeshWire::JointIndexNone;
    Core::u16 reserved = 0;
    float bindTranslation[3] = {0.0F, 0.0F, 0.0F};
    // xyzw quaternion, matching glTF and PrefabNodeDesc.
    float bindRotation[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    float bindScale[3] = {1.0F, 1.0F, 1.0F};
};

struct SkinnedMeshJointView final {
    Core::u16 parentJoint = SkinnedMeshWire::JointIndexNone;
    Core::u16 reserved = 0;
    float bindTranslation[3] = {0.0F, 0.0F, 0.0F};
    float bindRotation[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    float bindScale[3] = {1.0F, 1.0F, 1.0F};
};

struct SkinnedMeshPayloadDesc final {
    StaticMeshIndexType indexType = StaticMeshIndexType::U16;
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 1.0F;
    std::span<const SkinnedMeshJointDesc> joints{};
    // Column-major mat4 per joint: joints.size() * 16 floats.
    std::span<const float> inverseBindMatrices{};
    std::span<const StaticMeshSubmeshDesc> submeshes{};
    // P3N3T4UV2: [px,py,pz,nx,ny,nz,tx,ty,tz,tw,u,v]
    std::span<const float> vertices{};
    // vertexCount * 4 entries each; weights are fixed point summing to WeightScale.
    std::span<const Core::u16> jointIndices{};
    std::span<const Core::u16> jointWeights{};
    std::span<const Core::u16> indices{};
};

struct SkinnedMeshPayloadView final {
    Core::u16 schemaVersion = 0;
    StaticMeshIndexType indexType = StaticMeshIndexType::Invalid;
    Core::u16 submeshCount = 0;
    Core::u32 vertexCount = 0;
    Core::u32 indexCount = 0;
    float boundsCenterX = 0.0F;
    float boundsCenterY = 0.0F;
    float boundsCenterZ = 0.0F;
    float boundsRadius = 0.0F;
    Core::u16 jointCount = 0;
    Core::u16 influencesPerVertex = 0;
    std::span<const float> inverseBindMatrices{};
    std::span<const std::byte> jointsBytes{};
    std::span<const StaticMeshSubmeshView> submeshes{};
    std::span<const float> vertices{};
    std::span<const Core::u16> jointIndices{};
    std::span<const Core::u16> jointWeights{};
    std::span<const Core::u16> indices{};

    // The wire joint record is not layout-compatible with the decoded view, so joints
    // are decoded on demand rather than exposed as a zero-copy span.
    [[nodiscard]] std::optional<SkinnedMeshJointView> joint(Core::u16 index) const noexcept;

    // Column-major mat4 of one joint, or empty when index is out of range.
    [[nodiscard]] std::span<const float> inverseBindMatrix(Core::u16 index) const noexcept;

    [[nodiscard]] bool empty() const noexcept
    {
        return vertexCount == 0 || indexCount == 0 || jointCount == 0 || vertices.empty() ||
               indices.empty();
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeSkinnedMeshPayloadBytes(const SkinnedMeshPayloadDesc& desc);

// Borrows payload bytes from a CookedAssetView / raw payload span. All returned views
// alias into `payload` storage, which must outlive the view unchanged.
[[nodiscard]] Core::Result<SkinnedMeshPayloadView> parseSkinnedMeshPayload(std::span<const std::byte> payload);

// Convenience: full cooked SkinnedMesh asset file (no dependencies).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedSkinnedMeshAsset(Core::AssetId assetId, const SkinnedMeshPayloadDesc& desc,
                            TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
