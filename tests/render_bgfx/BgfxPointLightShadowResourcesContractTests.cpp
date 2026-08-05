#include "fakes/bgfx/bgfx.h"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <array>

namespace Tina::Render::Bgfx {

inline constexpr usize BgfxPointLightShadowFaceCount = 6U;
inline constexpr u16 BgfxPointLightShadowMapExtentContractTest = 512;

struct BgfxPointLightShadowResourcesContractTest final {
    BgfxPointLightShadowResourcesContractTest() noexcept
    {
        for (tina_test_bgfx::TextureHandle& depthMap : depthMaps)
        {
            depthMap = BGFX_INVALID_HANDLE;
        }
        for (tina_test_bgfx::FrameBufferHandle& frameBuffer : frameBuffers)
        {
            frameBuffer = BGFX_INVALID_HANDLE;
        }
    }

    std::array<tina_test_bgfx::TextureHandle, BgfxPointLightShadowFaceCount> depthMaps{};
    std::array<tina_test_bgfx::FrameBufferHandle, BgfxPointLightShadowFaceCount> frameBuffers{};

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] Core::Result<BgfxPointLightShadowResourcesContractTest>
createPointLightShadowResourcesContractTest();

void destroyPointLightShadowResourcesContractTest(
    BgfxPointLightShadowResourcesContractTest& resources) noexcept;

namespace {

class BgfxPointLightShadowResourcesTest : public testing::Test {
  protected:
    void SetUp() override { tina_test_bgfx::Contract::reset(); }
};

TEST_F(BgfxPointLightShadowResourcesTest, CreatesSixSampledD16MapsAndFramebuffers)
{
    constexpr u64 ExpectedFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
                                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    auto resources = createPointLightShadowResourcesContractTest();

    ASSERT_TRUE(resources.has_value()) << resources.error().message;
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureCreates.size(), 6U);
    ASSERT_EQ(tina_test_bgfx::Contract::state.frameBufferCreates.size(), 6U);
    for (usize faceIndex = 0; faceIndex < BgfxPointLightShadowFaceCount; ++faceIndex)
    {
        const auto& texture = tina_test_bgfx::Contract::state.textureCreates[faceIndex];
        EXPECT_EQ(texture.width, BgfxPointLightShadowMapExtentContractTest);
        EXPECT_EQ(texture.height, BgfxPointLightShadowMapExtentContractTest);
        EXPECT_EQ(texture.format, tina_test_bgfx::TextureFormat::D16);
        EXPECT_EQ(texture.flags, ExpectedFlags);
        const auto& frameBuffer =
            tina_test_bgfx::Contract::state.frameBufferCreates[faceIndex];
        ASSERT_EQ(frameBuffer.attachments.size(), 1U);
        EXPECT_EQ(frameBuffer.attachments.front().idx,
                  resources->depthMaps[faceIndex].idx);
        EXPECT_FALSE(frameBuffer.destroyTextures);
    }
}

TEST_F(BgfxPointLightShadowResourcesTest, FirstFramebufferFailureRollsBackDepthMap)
{
    tina_test_bgfx::Contract::state.rejectFrameBufferCreate = true;

    auto resources = createPointLightShadowResourcesContractTest();

    ASSERT_FALSE(resources.has_value());
    EXPECT_EQ(resources.error().code, RenderErrorCode::DeviceInitializationFailed);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.frameBufferDestroys.empty());
}

TEST_F(BgfxPointLightShadowResourcesTest, DestroyReleasesAllFramebuffersBeforeMaps)
{
    auto resources = createPointLightShadowResourcesContractTest();
    ASSERT_TRUE(resources.has_value()) << resources.error().message;

    destroyPointLightShadowResourcesContractTest(*resources);

    ASSERT_EQ(tina_test_bgfx::Contract::state.destroyedResources.size(), 12U);
    for (usize resourceIndex = 0; resourceIndex < 6U; ++resourceIndex)
    {
        EXPECT_EQ(tina_test_bgfx::Contract::state.destroyedResources[resourceIndex].kind,
                  tina_test_bgfx::Contract::DestroyedResourceKind::FrameBuffer);
        EXPECT_EQ(tina_test_bgfx::Contract::state.destroyedResources[6U + resourceIndex].kind,
                  tina_test_bgfx::Contract::DestroyedResourceKind::Texture);
    }
    EXPECT_FALSE(resources->valid());
}

} // namespace
} // namespace Tina::Render::Bgfx
