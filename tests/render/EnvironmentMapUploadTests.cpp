#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <limits>
#include <vector>

namespace Tina::Tests {
namespace {

struct EnvironmentMapFixture final {
    std::vector<std::byte> diffuse = std::vector<std::byte>(48U, std::byte{0x11});
    std::vector<std::byte> specular = std::vector<std::byte>(240U, std::byte{0x22});
    std::vector<std::byte> brdf = std::vector<std::byte>(8U, std::byte{0x33});

    [[nodiscard]] Render::EnvironmentMapUploadDesc desc() const noexcept
    {
        return Render::EnvironmentMapUploadDesc{
            .diffuseFaceSize = 1,
            .specularFaceSize = 2,
            .specularMipCount = 2,
            .brdfWidth = 2,
            .brdfHeight = 1,
            .diffuseRgba16FloatPixels = diffuse,
            .specularRgba16FloatPixels = specular,
            .brdfRg16FloatPixels = brdf,
        };
    }
};

void countPinRelease(void* userData) noexcept
{
    ++*static_cast<Core::u32*>(userData);
}

TEST(EnvironmentMapUploadTests, ValidatesCompleteMipChainAndExactSectionSizes)
{
    EnvironmentMapFixture fixture;
    EXPECT_TRUE(Render::validateEnvironmentMapUploadDesc(fixture.desc()).has_value());

    auto missingDimension = fixture.desc();
    missingDimension.brdfHeight = 0;
    auto missingDimensionResult = Render::validateEnvironmentMapUploadDesc(missingDimension);
    ASSERT_FALSE(missingDimensionResult.has_value());
    EXPECT_EQ(missingDimensionResult.error().code,
              Render::RenderErrorCode::InvalidEnvironmentMapUpload);

    auto incompleteMipChain = fixture.desc();
    incompleteMipChain.specularMipCount = 1;
    auto incompleteMipResult = Render::validateEnvironmentMapUploadDesc(incompleteMipChain);
    ASSERT_FALSE(incompleteMipResult.has_value());
    EXPECT_EQ(incompleteMipResult.error().code,
              Render::RenderErrorCode::InvalidEnvironmentMapUpload);

    auto wrongSectionSize = fixture.desc();
    wrongSectionSize.specularRgba16FloatPixels =
        std::span(fixture.specular).first(fixture.specular.size() - 1U);
    auto wrongSectionResult = Render::validateEnvironmentMapUploadDesc(wrongSectionSize);
    ASSERT_FALSE(wrongSectionResult.has_value());
    EXPECT_EQ(wrongSectionResult.error().code,
              Render::RenderErrorCode::InvalidEnvironmentMapUpload);

    auto backendLimit = fixture.desc();
    backendLimit.diffuseFaceSize = (std::numeric_limits<Core::u16>::max)();
    auto backendLimitResult = Render::validateEnvironmentMapUploadDesc(backendLimit);
    ASSERT_FALSE(backendLimitResult.has_value());
    EXPECT_EQ(backendLimitResult.error().code,
              Render::RenderErrorCode::InvalidEnvironmentMapUpload);
}

TEST(EnvironmentMapUploadTests, NullDeviceOwnsBindsAndRetiresAggregateAtomically)
{
    EnvironmentMapFixture fixture;
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    auto foreignDevice = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(foreignDevice.has_value());

    auto environment = (*device)->createEnvironmentMap(fixture.desc());
    auto foreignEnvironment = (*foreignDevice)->createEnvironmentMap(fixture.desc());
    ASSERT_TRUE(environment.has_value()) << environment.error().message;
    ASSERT_TRUE(foreignEnvironment.has_value()) << foreignEnvironment.error().message;
    EXPECT_EQ(environment->index, foreignEnvironment->index);
    EXPECT_EQ(environment->generation, foreignEnvironment->generation);
    EXPECT_NE(environment->owner, foreignEnvironment->owner);
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    auto crossDevice = (*device)->validateEnvironmentMap(*foreignEnvironment);
    ASSERT_FALSE(crossDevice.has_value());
    EXPECT_EQ(crossDevice.error().code, Render::RenderErrorCode::EnvironmentMapNotFound);
    EXPECT_FALSE((*device)
                     ->setMesh3DImageBasedLighting(Render::Mesh3DImageBasedLightingDesc{
                         .environmentMap = *foreignEnvironment,
                     })
                     .has_value());

    ASSERT_TRUE((*device)
                    ->setMesh3DImageBasedLighting(Render::Mesh3DImageBasedLightingDesc{
                        .environmentMap = *environment,
                        .intensity = 1.25F,
                        .rotationRadians = 0.5F,
                    })
                    .has_value());
    EXPECT_FALSE((*device)
                     ->setMesh3DImageBasedLighting(Render::Mesh3DImageBasedLightingDesc{
                         .environmentMap = *environment,
                         .intensity = -0.01F,
                     })
                     .has_value());
    EXPECT_FALSE((*device)
                     ->setMesh3DImageBasedLighting(Render::Mesh3DImageBasedLightingDesc{
                         .environmentMap = *environment,
                         .rotationRadians = std::numeric_limits<float>::infinity(),
                     })
                     .has_value());

    Core::u32 releases = 0;
    Render::FramePin completionPin{
        Render::FramePinKind::AssetLease, 17U, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireEnvironmentMap(*environment, completionPin).has_value());
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
    EXPECT_EQ((*device)->statistics().pendingGpuRetirements, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);
    EXPECT_TRUE((*device)->clearMesh3DImageBasedLighting().has_value());

    Render::FramePin stalePin{
        Render::FramePinKind::AssetLease, 18U, &releases, &countPinRelease};
    auto stale = (*device)->retireEnvironmentMap(*environment, stalePin);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::EnvironmentMapNotFound);
    EXPECT_TRUE(stalePin.hasValue());
    EXPECT_EQ(releases, 1U);
    stalePin.release();
    EXPECT_EQ(releases, 2U);

    ASSERT_TRUE((*foreignDevice)->destroyEnvironmentMap(*foreignEnvironment).has_value());
}

} // namespace
} // namespace Tina::Tests
