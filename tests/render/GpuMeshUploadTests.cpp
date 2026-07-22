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

} // namespace Tina::Tests
