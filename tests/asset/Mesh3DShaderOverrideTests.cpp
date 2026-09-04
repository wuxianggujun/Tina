#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/Mesh3DShaderOverride.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId testId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] CookedAssetFile loadCooked(std::pmr::memory_resource& memory,
                                         Core::Result<std::vector<std::byte>> bytes)
{
    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error().message);
    if (!bytes)
    {
        return {};
    }
    std::pmr::vector<std::byte> owned{&memory};
    owned.assign(bytes->begin(), bytes->end());
    auto file = makeCookedAssetFileFromBytes(std::move(owned),
                                             CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    return file ? std::move(*file) : CookedAssetFile{};
}

// Cooks a canonical unit cube whose payload flag and cooked dependency stream can be set
// independently, which is the only way to build the cook defects this reader must reject.
[[nodiscard]] CookedAssetFile makeStaticMesh(
    std::pmr::memory_resource& memory,
    Core::AssetId payloadOverrideId,
    std::span<const AssetFormat::CookedAssetWriteDependency> dependencies)
{
    std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{};
    std::array<float, 24 * AssetFormat::StaticMeshWire::FloatsPerVertex> vertices{};
    std::array<Core::u16, 36> indices{};
    AssetFormat::StaticMeshPayloadDesc desc =
        AssetFormat::makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);
    desc.shaderOverrideId = payloadOverrideId;

    auto payload = AssetFormat::writeStaticMeshPayloadBytes(desc);
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    if (!payload)
    {
        return {};
    }
    return loadCooked(memory, AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
                                  .assetKind = AssetFormat::AssetKind::StaticMesh,
                                  .assetTypeVersion = AssetFormat::StaticMeshWire::SchemaVersion,
                                  .assetId = testId(0x11),
                                  .dependencies = dependencies,
                                  .payload = *payload,
                              }));
}

[[nodiscard]] AssetFormat::CookedAssetWriteDependency shaderDependency(
    Core::AssetId assetId,
    AssetFormat::DependencyFlags flags = AssetFormat::DependencyFlags::Required)
{
    return AssetFormat::CookedAssetWriteDependency{
        .assetId = assetId,
        .expectedKind = AssetFormat::AssetKind::Shader,
        .flags = flags,
    };
}

TEST(Mesh3DShaderOverrideTests, StaticMeshOverrideResolvesToTheShaderDependency)
{
    std::pmr::monotonic_buffer_resource memory{};
    const auto shaderId = testId(0x21);
    const std::array dependencies{shaderDependency(shaderId)};
    const auto file = makeStaticMesh(memory, shaderId, dependencies);

    auto result = readMesh3DShaderOverride(file);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(**result, shaderId);
}

TEST(Mesh3DShaderOverrideTests, StaticMeshWithoutOverrideResolvesToNullopt)
{
    std::pmr::monotonic_buffer_resource memory{};
    const auto file = makeStaticMesh(memory, Core::AssetId{}, {});

    auto result = readMesh3DShaderOverride(file);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    EXPECT_FALSE(result->has_value());
}

TEST(Mesh3DShaderOverrideTests, SkinnedMeshOverrideResolvesToTheShaderDependency)
{
    std::pmr::monotonic_buffer_resource memory{};
    const auto shaderId = testId(0x22);

    const std::array<AssetFormat::SkinnedMeshJointDesc, 1> joints{};
    const std::array<float, 16> inverseBind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{
        AssetFormat::StaticMeshSubmeshDesc{.indexCount = 3}};
    const std::array<float, 3 * AssetFormat::SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};

    const auto file = loadCooked(
        memory, AssetFormat::writeCookedSkinnedMeshAsset(
                    testId(0x12), AssetFormat::SkinnedMeshPayloadDesc{
                                      .boundsRadius = 1.0F,
                                      .joints = joints,
                                      .inverseBindMatrices = inverseBind,
                                      .submeshes = submeshes,
                                      .vertices = vertices,
                                      .jointIndices = jointIndices,
                                      .jointWeights = jointWeights,
                                      .indices = indices,
                                      .shaderOverrideId = shaderId,
                                  }));

    auto result = readMesh3DShaderOverride(file);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(**result, shaderId);
}

TEST(Mesh3DShaderOverrideTests, RejectsFlagWithoutAnyShaderDependency)
{
    std::pmr::monotonic_buffer_resource memory{};
    // The payload declares an override, but the cooked file pins nothing: the mesh asks for a
    // fragment stage no consumer can resolve.
    const auto file = makeStaticMesh(memory, testId(0x23), {});

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(Mesh3DShaderOverrideTests, RejectsFlagWhenOnlyANonShaderDependencyIsPresent)
{
    std::pmr::monotonic_buffer_resource memory{};
    const std::array dependencies{AssetFormat::CookedAssetWriteDependency{
        .assetId = testId(0x24),
        .expectedKind = AssetFormat::AssetKind::Texture2D,
        .flags = AssetFormat::DependencyFlags::Required,
    }};
    const auto file = makeStaticMesh(memory, testId(0x24), dependencies);

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(Mesh3DShaderOverrideTests, RejectsShaderDependencyWithoutTheFlag)
{
    std::pmr::monotonic_buffer_resource memory{};
    // Invisible to every payload reader while still pinning the Shader.
    const std::array dependencies{shaderDependency(testId(0x25))};
    const auto file = makeStaticMesh(memory, Core::AssetId{}, dependencies);

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(Mesh3DShaderOverrideTests, RejectsDeferredShaderDependency)
{
    std::pmr::monotonic_buffer_resource memory{};
    // Deferred means requesting the mesh does not enqueue the Shader, so the draw could reach
    // submit with nothing to link. Required is set too because the wire rejects Deferred alone.
    const auto shaderId = testId(0x26);
    const std::array dependencies{
        shaderDependency(shaderId, AssetFormat::DependencyFlags::Required |
                                       AssetFormat::DependencyFlags::Deferred)};
    const auto file = makeStaticMesh(memory, shaderId, dependencies);

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(Mesh3DShaderOverrideTests, RejectsTwoShaderDependencies)
{
    std::pmr::monotonic_buffer_resource memory{};
    // The payload flag is a single bit, so a second Shader would be silently dropped.
    const auto first = testId(0x27);
    const auto second = testId(0x28);
    ASSERT_LT(first, second); // writeCookedAssetBytes demands a strictly AssetId-sorted stream.
    const std::array dependencies{shaderDependency(first), shaderDependency(second)};
    const auto file = makeStaticMesh(memory, first, dependencies);

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(Mesh3DShaderOverrideTests, RejectsAnAssetKindThatIsNotAMesh)
{
    std::pmr::monotonic_buffer_resource memory{};
    const std::array<std::byte, 4> payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto file = loadCooked(memory, AssetFormat::writeCookedAssetBytes(
                                             AssetFormat::CookedAssetWriteDesc{
                                                 .assetKind = AssetFormat::AssetKind::Material,
                                                 .assetTypeVersion = 1,
                                                 .assetId = testId(0x13),
                                                 .payload = payload,
                                             }));

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::CatalogEntryMismatch);
}

TEST(Mesh3DShaderOverrideTests, RejectsAnEmptyCookedAssetFile)
{
    const CookedAssetFile file{};

    auto result = readMesh3DShaderOverride(file);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::InvalidCatalogConfig);
}

} // namespace
} // namespace Tina::Asset
