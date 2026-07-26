#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] Render::StaticMeshUploadDesc makeUnitTriangleDesc(std::array<float, 24>& vertices,
                                                                std::array<std::uint16_t, 3>& indices) noexcept
{
    // One triangle: 3 verts * 8 floats (P3_N3_UV2).
    vertices = {
        0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
    };
    indices = {0, 1, 2};
    return Render::StaticMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .vertices = vertices,
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
    ASSERT_TRUE(device.has_value());

    std::array<float, 24> vertices{};
    std::array<std::uint16_t, 3> indices{};
    const auto desc = makeUnitTriangleDesc(vertices, indices);

    auto mesh = (*device)->createStaticMeshP3N3UV2(desc);
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    ASSERT_TRUE((*device)->setMesh3DBinding(2U, *mesh).has_value());
    ASSERT_TRUE((*device)->setMesh3DBinding(2U, {}).has_value());
    ASSERT_TRUE((*device)->destroyStaticMesh(*mesh).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);

    auto stale = (*device)->destroyStaticMesh(*mesh);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::MeshNotFound);
}

TEST(NullRenderDeviceMeshTest, RejectsBadUpload)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 8> vertices{0, 0, 0, 0, 1, 0, 0, 0};
    std::array<std::uint16_t, 3> badIndices{0, 1, 2}; // index 1/2 out of range for 1 vertex
    auto mesh = (*device)->createStaticMeshP3N3UV2(Render::StaticMeshUploadDesc{
        .vertexCount = 1,
        .indexCount = 3,
        .vertices = vertices,
        .indices = badIndices,
    });
    ASSERT_FALSE(mesh.has_value());
    EXPECT_EQ(mesh.error().code, Render::RenderErrorCode::InvalidMeshUpload);
}

TEST(NullRenderDeviceMeshTest, RejectsZeroMeshKeyBinding)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 24> vertices{};
    std::array<std::uint16_t, 3> indices{};
    const auto desc = makeUnitTriangleDesc(vertices, indices);
    auto mesh = (*device)->createStaticMeshP3N3UV2(desc);
    ASSERT_TRUE(mesh.has_value());

    auto bad = (*device)->setMesh3DBinding(0U, *mesh);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, Render::RenderErrorCode::InvalidMeshUpload);

    ASSERT_TRUE((*device)->destroyStaticMesh(*mesh).has_value());
}

TEST(NullRenderDeviceMeshTest, AllocatedBindingKeysStartAtTwoAndAreNeverConsumedOrReusedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    auto invalid = (*device)->createMesh3DBinding({});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidMeshUpload);

    std::array<float, 24> vertices{};
    std::array<std::uint16_t, 3> indices{};
    auto staleMesh = (*device)->createStaticMeshP3N3UV2(makeUnitTriangleDesc(vertices, indices));
    ASSERT_TRUE(staleMesh.has_value()) << staleMesh.error().message;
    ASSERT_TRUE((*device)->destroyStaticMesh(*staleMesh).has_value());
    auto staleBinding = (*device)->createMesh3DBinding(*staleMesh);
    ASSERT_FALSE(staleBinding.has_value());
    EXPECT_EQ(staleBinding.error().code, Render::RenderErrorCode::MeshNotFound);

    auto mesh = (*device)->createStaticMeshP3N3UV2(makeUnitTriangleDesc(vertices, indices));
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
    ASSERT_TRUE((*device)->destroyStaticMesh(*mesh).has_value());
}

TEST(NullRenderDeviceMeshTest, RetirementPinCompletesImmediatelyAndIsNotConsumedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<float, 24> vertices{};
    std::array<std::uint16_t, 3> indices{};
    auto mesh = (*device)->createStaticMeshP3N3UV2(makeUnitTriangleDesc(vertices, indices));
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;

    Core::u32 releases = 0;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, 9, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireStaticMesh(*mesh, completionPin).has_value());
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().pendingGpuRetirements, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);

    Render::FramePin failurePin{Render::FramePinKind::AssetLease, 10, &releases, &countPinRelease};
    auto stale = (*device)->retireStaticMesh(*mesh, failurePin);
    ASSERT_FALSE(stale.has_value());
    EXPECT_TRUE(failurePin.hasValue());
    EXPECT_EQ(releases, 1U);
    failurePin.release();
    EXPECT_EQ(releases, 2U);
}

} // namespace Tina::Tests
