#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] Render::StaticMeshUploadDesc makeUnitTriangleDesc(std::array<float, 36>& vertices,
                                                                std::array<std::uint32_t, 3>& indices) noexcept
{
    // One triangle: 3 verts * 12 floats (P3_N3_T4_UV2).
    vertices = {
        0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
    };
    indices = {0, 1, 2};
    return Render::StaticMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .vertices = vertices,
        .indices = indices,
    };
}

[[nodiscard]] Render::SkinnedMeshUploadDesc makeUnitSkinnedTriangleDesc(
    std::array<float, 36>& vertices,
    std::array<std::uint16_t, 12>& jointIndices,
    std::array<std::uint16_t, 12>& jointWeights,
    std::array<std::uint32_t, 3>& indices) noexcept
{
    (void)makeUnitTriangleDesc(vertices, indices);
    jointIndices.fill(0);
    jointWeights = {
        65535, 0, 0, 0,
        65535, 0, 0, 0,
        65535, 0, 0, 0,
    };
    return Render::SkinnedMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .jointCount = 1,
        .vertices = vertices,
        .jointIndices = jointIndices,
        .jointWeights = jointWeights,
        .indices = indices,
    };
}

void countPinRelease(void* userData) noexcept
{
    ++*static_cast<Core::u32*>(userData);
}

} // namespace

TEST(NullRenderDeviceMeshTest, CreateBindDestroyLifecycle)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    auto foreignDevice = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(foreignDevice.has_value());

    std::array<float, 36> vertices{};
    std::array<std::uint32_t, 3> indices{};
    const auto desc = makeUnitTriangleDesc(vertices, indices);

    auto mesh = (*device)->createStaticMesh(desc);
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    auto foreignMesh = (*foreignDevice)->createStaticMesh(desc);
    ASSERT_TRUE(foreignMesh.has_value()) << foreignMesh.error().message;
    EXPECT_EQ(mesh->index, foreignMesh->index);
    EXPECT_EQ(mesh->generation, foreignMesh->generation);
    EXPECT_NE(mesh->owner, foreignMesh->owner);
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    auto foreignBinding = (*device)->setMesh3DBinding(2U, *foreignMesh);
    ASSERT_FALSE(foreignBinding.has_value());
    EXPECT_EQ(foreignBinding.error().code, Render::RenderErrorCode::MeshNotFound);
    auto foreignDestroy = (*device)->destroyGpuMesh(*foreignMesh);
    ASSERT_FALSE(foreignDestroy.has_value());
    EXPECT_EQ(foreignDestroy.error().code, Render::RenderErrorCode::MeshNotFound);

    ASSERT_TRUE((*device)->setMesh3DBinding(2U, *mesh).has_value());
    ASSERT_TRUE((*device)->setMesh3DBinding(2U, {}).has_value());
    ASSERT_TRUE((*device)->destroyGpuMesh(*mesh).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);

    auto stale = (*device)->destroyGpuMesh(*mesh);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::MeshNotFound);
    ASSERT_TRUE((*foreignDevice)->destroyGpuMesh(*foreignMesh).has_value());
}

TEST(NullRenderDeviceMeshTest, StaticAndSkinnedUploadsAcceptVertexIndicesAbove65535)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    constexpr Core::u32 vertexCount = 65'537U;
    constexpr Core::usize floatsPerVertex = 12U;
    std::vector<float> vertices(static_cast<Core::usize>(vertexCount) * floatsPerVertex, 0.0F);
    for (Core::usize vertex = 0; vertex < vertexCount; ++vertex)
    {
        const Core::usize offset = vertex * floatsPerVertex;
        vertices[offset] = vertex % 3U == 1U ? 1.0F : 0.0F;
        vertices[offset + 1U] = vertex % 3U == 2U ? 1.0F : 0.0F;
        vertices[offset + 5U] = 1.0F;
        vertices[offset + 6U] = 1.0F;
        vertices[offset + 9U] = 1.0F;
    }
    std::array<Core::u32, 3> indices{0U, 1U, vertexCount - 1U};
    const Render::StaticMeshUploadDesc staticDesc{
        .vertexCount = vertexCount,
        .indexCount = 3,
        .vertices = vertices,
        .indices = indices,
    };
    auto staticMesh = (*device)->createStaticMesh(staticDesc);
    ASSERT_TRUE(staticMesh.has_value()) << staticMesh.error().message;

    constexpr Core::usize influencesPerVertex = 4U;
    std::vector<Core::u16> joints(static_cast<Core::usize>(vertexCount) * influencesPerVertex, 0U);
    std::vector<Core::u16> weights(joints.size(), 0U);
    for (Core::usize vertex = 0; vertex < vertexCount; ++vertex)
    {
        weights[vertex * influencesPerVertex] = (std::numeric_limits<Core::u16>::max)();
    }
    const Render::SkinnedMeshUploadDesc skinnedDesc{
        .vertexCount = vertexCount,
        .indexCount = 3,
        .jointCount = 1,
        .vertices = vertices,
        .jointIndices = joints,
        .jointWeights = weights,
        .indices = indices,
    };
    auto skinnedMesh = (*device)->createSkinnedMesh(skinnedDesc);
    ASSERT_TRUE(skinnedMesh.has_value()) << skinnedMesh.error().message;

    indices.back() = vertexCount;
    auto badStatic = (*device)->createStaticMesh(staticDesc);
    ASSERT_FALSE(badStatic.has_value());
    EXPECT_EQ(badStatic.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    auto badSkinned = (*device)->createSkinnedMesh(skinnedDesc);
    ASSERT_FALSE(badSkinned.has_value());
    EXPECT_EQ(badSkinned.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    ASSERT_TRUE((*device)->destroyGpuMesh(*staticMesh).has_value());
    ASSERT_TRUE((*device)->destroyGpuMesh(*skinnedMesh).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
}

TEST(NullRenderDeviceMeshTest, RejectsBadUpload)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 12> vertices{0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0};
    std::array<std::uint32_t, 3> badIndices{0, 1, 2}; // index 1/2 out of range for 1 vertex
    auto mesh = (*device)->createStaticMesh(Render::StaticMeshUploadDesc{
        .vertexCount = 1,
        .indexCount = 3,
        .vertices = vertices,
        .indices = badIndices,
    });
    ASSERT_FALSE(mesh.has_value());
    EXPECT_EQ(mesh.error().code, Render::RenderErrorCode::InvalidMeshUpload);
}

TEST(NullRenderDeviceMeshTest, TangentLayoutUsesValidatedUploadPath)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 36> vertices{};
    std::array<std::uint32_t, 3> indices{};
    const auto desc = makeUnitTriangleDesc(vertices, indices);
    auto mesh = (*device)->createStaticMesh(desc);
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ((*device)->statistics().liveResources, 1U);
    ASSERT_TRUE((*device)->setMesh3DBinding(2U, *mesh).has_value());

    std::array<float, 24> wrongStrideVertices{};
    auto wrongStride = (*device)->createStaticMesh(Render::StaticMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .vertices = wrongStrideVertices,
        .indices = indices,
    });
    ASSERT_FALSE(wrongStride.has_value());
    EXPECT_EQ(wrongStride.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    vertices[6] = (std::numeric_limits<float>::infinity)();
    auto nonFinite = (*device)->createStaticMesh(desc);
    ASSERT_FALSE(nonFinite.has_value());
    EXPECT_EQ(nonFinite.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    vertices[6] = 1.0F;

    vertices[6] = 1.0e-7F;
    auto nearZeroTangent = (*device)->createStaticMesh(desc);
    ASSERT_FALSE(nearZeroTangent.has_value());
    EXPECT_EQ(nearZeroTangent.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    vertices[6] = 1.0F;

    vertices[9] = 0.5F;
    auto invalidHandedness = (*device)->createStaticMesh(desc);
    ASSERT_FALSE(invalidHandedness.has_value());
    EXPECT_EQ(invalidHandedness.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    vertices[9] = 1.0F;

    const std::array<std::uint32_t, 3> outOfRangeIndices{0, 1, 3};
    auto outOfRange = (*device)->createStaticMesh(Render::StaticMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .vertices = vertices,
        .indices = outOfRangeIndices,
    });
    ASSERT_FALSE(outOfRange.has_value());
    EXPECT_EQ(outOfRange.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    Core::u32 releases = 0;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, 11, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireGpuMesh(*mesh, completionPin).has_value());
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);

    auto staleBinding = (*device)->setMesh3DBinding(3U, *mesh);
    ASSERT_FALSE(staleBinding.has_value());
    EXPECT_EQ(staleBinding.error().code, Render::RenderErrorCode::MeshNotFound);
    Render::FramePin stalePin{Render::FramePinKind::AssetLease, 12, &releases, &countPinRelease};
    auto staleRetirement = (*device)->retireGpuMesh(*mesh, stalePin);
    ASSERT_FALSE(staleRetirement.has_value());
    EXPECT_TRUE(stalePin.hasValue());
    EXPECT_EQ(releases, 1U);
    stalePin.release();

    vertices[9] = -1.0F;
    auto replacement = (*device)->createStaticMesh(desc);
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    ASSERT_TRUE((*device)->destroyGpuMesh(*replacement).has_value());
    auto staleDestroy = (*device)->destroyGpuMesh(*replacement);
    ASSERT_FALSE(staleDestroy.has_value());
    EXPECT_EQ(staleDestroy.error().code, Render::RenderErrorCode::MeshNotFound);
    EXPECT_EQ(releases, 2U);
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 2U);
}

TEST(NullRenderDeviceMeshTest, RejectsZeroMeshKeyBinding)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 36> vertices{};
    std::array<std::uint32_t, 3> indices{};
    const auto desc = makeUnitTriangleDesc(vertices, indices);
    auto mesh = (*device)->createStaticMesh(desc);
    ASSERT_TRUE(mesh.has_value());

    auto bad = (*device)->setMesh3DBinding(0U, *mesh);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, Render::RenderErrorCode::InvalidMeshUpload);

    ASSERT_TRUE((*device)->destroyGpuMesh(*mesh).has_value());
}

TEST(NullRenderDeviceMeshTest, SkinnedUploadValidatesInfluencesAndSharesRetirementLifecycle)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<float, 36> vertices{};
    std::array<std::uint16_t, 12> jointIndices{};
    std::array<std::uint16_t, 12> jointWeights{};
    std::array<std::uint32_t, 3> indices{};
    auto desc = makeUnitSkinnedTriangleDesc(vertices, jointIndices, jointWeights, indices);

    jointIndices[0] = 1;
    auto badJoint = (*device)->createSkinnedMesh(desc);
    ASSERT_FALSE(badJoint.has_value());
    EXPECT_EQ(badJoint.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    jointIndices[0] = 0;

    jointWeights[0] = 65534;
    auto badWeights = (*device)->createSkinnedMesh(desc);
    ASSERT_FALSE(badWeights.has_value());
    EXPECT_EQ(badWeights.error().code, Render::RenderErrorCode::InvalidMeshUpload);
    jointWeights[0] = 65535;

    auto mesh = (*device)->createSkinnedMesh(desc);
    ASSERT_TRUE(mesh.has_value()) << (mesh ? "" : mesh.error().message);
    ASSERT_TRUE((*device)->setMesh3DBinding(2U, *mesh));
    ASSERT_TRUE((*device)->setMesh3DBinding(2U, {}));
    Core::u32 releases = 0;
    Render::FramePin completionPin{
        Render::FramePinKind::AssetLease, 20, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireGpuMesh(*mesh, completionPin));
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
}

TEST(NullRenderDeviceMeshTest, AllocatedBindingKeysStartAtTwoAndAreNeverConsumedOrReusedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    auto invalid = (*device)->createMesh3DBinding({});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidMeshUpload);

    std::array<float, 36> vertices{};
    std::array<std::uint32_t, 3> indices{};
    auto staleMesh = (*device)->createStaticMesh(makeUnitTriangleDesc(vertices, indices));
    ASSERT_TRUE(staleMesh.has_value()) << staleMesh.error().message;
    ASSERT_TRUE((*device)->destroyGpuMesh(*staleMesh).has_value());
    auto staleBinding = (*device)->createMesh3DBinding(*staleMesh);
    ASSERT_FALSE(staleBinding.has_value());
    EXPECT_EQ(staleBinding.error().code, Render::RenderErrorCode::MeshNotFound);

    auto mesh = (*device)->createStaticMesh(makeUnitTriangleDesc(vertices, indices));
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;

    auto first = (*device)->createMesh3DBinding(*mesh);
    auto second = (*device)->createMesh3DBinding(*mesh);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(*first, 2U);
    EXPECT_EQ(*second, 3U);

    ASSERT_TRUE((*device)->setMesh3DBinding(*first, {}).has_value());
    auto third = (*device)->createMesh3DBinding(*mesh);
    ASSERT_TRUE(third.has_value()) << third.error().message;
    EXPECT_EQ(*third, 4U);

    ASSERT_TRUE((*device)->setMesh3DBinding(*second, {}).has_value());
    ASSERT_TRUE((*device)->setMesh3DBinding(*third, {}).has_value());
    ASSERT_TRUE((*device)->destroyGpuMesh(*mesh).has_value());
}

TEST(NullRenderDeviceMeshTest, RetirementPinCompletesImmediatelyAndIsNotConsumedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 36> vertices{};
    std::array<std::uint32_t, 3> indices{};
    auto mesh = (*device)->createStaticMesh(makeUnitTriangleDesc(vertices, indices));
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;

    Core::u32 releases = 0;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, 9, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireGpuMesh(*mesh, completionPin).has_value());
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().pendingGpuRetirements, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);

    Render::FramePin failurePin{Render::FramePinKind::AssetLease, 10, &releases, &countPinRelease};
    auto stale = (*device)->retireGpuMesh(*mesh, failurePin);
    ASSERT_FALSE(stale.has_value());
    EXPECT_TRUE(failurePin.hasValue());
    EXPECT_EQ(releases, 1U);
    failurePin.release();
    EXPECT_EQ(releases, 2U);
}

} // namespace Tina::Tests
