#include "fakes/bgfx/bgfx.h"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

namespace Tina::Render::Bgfx {

inline constexpr u16 BgfxCascadedDirectionalShadowAtlasExtentContractTest = 2048;
inline constexpr u16 BgfxCascadedDirectionalShadowTileExtentContractTest = 1024;

struct BgfxCascadedDirectionalShadowResourcesContractTest final {
    tina_test_bgfx::TextureHandle depthAtlas = BGFX_INVALID_HANDLE;
    tina_test_bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return tina_test_bgfx::isValid(depthAtlas) &&
               tina_test_bgfx::isValid(frameBuffer);
    }
};

[[nodiscard]] Core::Result<BgfxCascadedDirectionalShadowResourcesContractTest>
createCascadedDirectionalShadowResourcesContractTest();

void destroyCascadedDirectionalShadowResourcesContractTest(
    BgfxCascadedDirectionalShadowResourcesContractTest& resources) noexcept;

namespace {

class BgfxCascadedDirectionalShadowResourcesTest : public testing::Test {
  protected:
    void SetUp() override
    {
        tina_test_bgfx::Contract::reset();
    }
};

TEST_F(BgfxCascadedDirectionalShadowResourcesTest,
       CreatesSampledD16TwoByTwoAtlasAndNonOwningFramebuffer)
{
    constexpr u64 ExpectedFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
                                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    auto resources = createCascadedDirectionalShadowResourcesContractTest();

    ASSERT_TRUE(resources.has_value()) << resources.error().message;
    EXPECT_EQ(BgfxCascadedDirectionalShadowTileExtentContractTest * 2U,
              BgfxCascadedDirectionalShadowAtlasExtentContractTest);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureCreates.size(), 1U);
    const auto& texture = tina_test_bgfx::Contract::state.textureCreates.front();
    EXPECT_EQ(texture.width, BgfxCascadedDirectionalShadowAtlasExtentContractTest);
    EXPECT_EQ(texture.height, BgfxCascadedDirectionalShadowAtlasExtentContractTest);
    EXPECT_FALSE(texture.hasMips);
    EXPECT_EQ(texture.layers, 1U);
    EXPECT_EQ(texture.format, tina_test_bgfx::TextureFormat::D16);
    EXPECT_EQ(texture.flags, ExpectedFlags);
    EXPECT_FALSE(texture.initialMemoryProvided);

    ASSERT_EQ(tina_test_bgfx::Contract::state.frameBufferCreates.size(), 1U);
    const auto& frameBuffer = tina_test_bgfx::Contract::state.frameBufferCreates.front();
    ASSERT_EQ(frameBuffer.attachments.size(), 1U);
    EXPECT_EQ(frameBuffer.attachments.front().idx, resources->depthAtlas.idx);
    EXPECT_FALSE(frameBuffer.destroyTextures);
}

TEST_F(BgfxCascadedDirectionalShadowResourcesTest, FramebufferFailureRollsBackDepthAtlas)
{
    tina_test_bgfx::Contract::state.rejectFrameBufferCreate = true;

    auto resources = createCascadedDirectionalShadowResourcesContractTest();

    ASSERT_FALSE(resources.has_value());
    EXPECT_EQ(resources.error().code, RenderErrorCode::DeviceInitializationFailed);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.frameBufferDestroys.empty());
    ASSERT_EQ(tina_test_bgfx::Contract::state.textures.size(), 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textures.front().destroyed);
}

TEST_F(BgfxCascadedDirectionalShadowResourcesTest, DestroyReleasesFramebufferBeforeDepthAtlas)
{
    auto resources = createCascadedDirectionalShadowResourcesContractTest();
    ASSERT_TRUE(resources.has_value()) << resources.error().message;

    destroyCascadedDirectionalShadowResourcesContractTest(*resources);

    ASSERT_EQ(tina_test_bgfx::Contract::state.destroyedResources.size(), 2U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.destroyedResources[0].kind,
              tina_test_bgfx::Contract::DestroyedResourceKind::FrameBuffer);
    EXPECT_EQ(tina_test_bgfx::Contract::state.destroyedResources[1].kind,
              tina_test_bgfx::Contract::DestroyedResourceKind::Texture);
    EXPECT_FALSE(tina_test_bgfx::isValid(resources->frameBuffer));
    EXPECT_FALSE(tina_test_bgfx::isValid(resources->depthAtlas));
}

} // namespace
} // namespace Tina::Render::Bgfx
