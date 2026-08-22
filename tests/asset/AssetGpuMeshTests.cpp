#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

TEST(AssetGpuMeshTests, UploadTypedStaticMeshToNullDevice)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto meshId = *Core::AssetId::fromBytes(idBytes(1U));

    std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{};
    std::array<float, 24 * AssetFormat::StaticMeshWire::FloatsPerVertex> vertices{};
    std::array<Core::u16, 36> indices{};
    const auto desc = AssetFormat::makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);
    ASSERT_FALSE(desc.vertices.empty());

    auto cooked = AssetFormat::writeCookedStaticMeshAsset(meshId, desc);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    auto file = makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
                                             CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    auto gpu = uploadStaticMeshFromCooked(**device, *file);
    ASSERT_TRUE(gpu.has_value()) << gpu.error().message;
    ASSERT_TRUE(uploadAndBindStaticMeshForMeshKey(**device, *file, 2U).has_value());
    EXPECT_GE((*device)->statistics().liveResources, 1U);

    ASSERT_TRUE((*device)->setMesh3DBinding(2U, {}).has_value());
    ASSERT_TRUE((*device)->destroyGpuMesh(*gpu).has_value());
}

TEST(AssetGpuMeshTests, RejectsZeroMeshKey)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto meshId = *Core::AssetId::fromBytes(idBytes(2U));
    std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{};
    std::array<float, 24 * AssetFormat::StaticMeshWire::FloatsPerVertex> vertices{};
    std::array<Core::u16, 36> indices{};
    const auto desc = AssetFormat::makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);
    auto cooked = AssetFormat::writeCookedStaticMeshAsset(meshId, desc);
    ASSERT_TRUE(cooked.has_value());
    auto file = makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
                                             CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value());
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    auto status = uploadAndBindStaticMeshForMeshKey(**device, *file, 0U);
    ASSERT_FALSE(status.has_value());
}

TEST(AssetGpuMeshTests, UploadsStaticMeshToNullDevice)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto meshId = *Core::AssetId::fromBytes(idBytes(3U));
    const std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{
        AssetFormat::StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3}};
    const std::array<float, 3 * AssetFormat::StaticMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1,
    };
    const std::array<Core::u16, 3> indices{0, 1, 2};
    auto cooked = AssetFormat::writeCookedStaticMeshAsset(meshId, AssetFormat::StaticMeshPayloadDesc{
        .boundsCenterX = 0.5F,
        .boundsCenterY = 0.5F,
        .boundsRadius = 0.7072F,
        .submeshes = submeshes,
        .vertices = vertices,
        .indices = indices,
    });
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    auto gpu = uploadStaticMeshFromCooked(**device, *file);
    ASSERT_TRUE(gpu.has_value()) << (gpu ? "" : gpu.error().message);
    EXPECT_TRUE((*device)->destroyGpuMesh(*gpu).has_value());
}

} // namespace
} // namespace Tina::Asset
