#include "fakes/bgfx/bgfx.h"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace Tina::Render::Bgfx {

[[nodiscard]] Core::Result<tina_test_bgfx::TextureHandle> createUIGlyphAtlasTextureContractTest(
    u32 width,
    u32 height,
    std::span<const u8> pixels);

[[nodiscard]] Core::Status updateUIGlyphAtlasTextureContractTest(
    tina_test_bgfx::TextureHandle texture,
    u32 width,
    u32 height,
    std::span<const u8> pixels);

namespace {

class BgfxUIAtlasTextureContractTest : public testing::Test {
protected:
    void SetUp() override
    {
        tina_test_bgfx::Contract::reset();
    }
};

TEST_F(BgfxUIAtlasTextureContractTest, InitialAndLaterUploadsMutateTheSameTexture)
{
    constexpr std::array<u8, 4> InitialPixels{0U, 32U, 96U, 255U};
    constexpr std::array<u8, 4> UpdatedPixels{255U, 160U, 64U, 0U};
    constexpr u64 ExpectedSamplerFlags =
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;

    auto atlas = createUIGlyphAtlasTextureContractTest(2, 2, InitialPixels);

    ASSERT_TRUE(atlas.has_value()) << atlas.error().message;
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureCreates.size(), 1U);
    const auto& create = tina_test_bgfx::Contract::state.textureCreates.front();
    EXPECT_EQ(create.width, 2U);
    EXPECT_EQ(create.height, 2U);
    EXPECT_FALSE(create.hasMips);
    EXPECT_EQ(create.layers, 1U);
    EXPECT_EQ(create.format, tina_test_bgfx::TextureFormat::R8);
    EXPECT_EQ(create.flags, ExpectedSamplerFlags);
    EXPECT_FALSE(create.initialMemoryProvided);
    EXPECT_TRUE(create.initialPixels.empty());

    ASSERT_EQ(tina_test_bgfx::Contract::state.textureUpdates.size(), 1U);
    const auto* texture = tina_test_bgfx::Contract::findTexture(*atlas);
    ASSERT_NE(texture, nullptr);
    EXPECT_TRUE(texture->mutableStorage);
    EXPECT_EQ(texture->pixels, std::vector<u8>(InitialPixels.begin(), InitialPixels.end()));

    const Core::Status update =
        updateUIGlyphAtlasTextureContractTest(*atlas, 2, 2, UpdatedPixels);

    ASSERT_TRUE(update) << update.error().message;
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureUpdates.size(), 2U);
    texture = tina_test_bgfx::Contract::findTexture(*atlas);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->pixels, std::vector<u8>(UpdatedPixels.begin(), UpdatedPixels.end()));
    EXPECT_EQ(tina_test_bgfx::Contract::state.immutableUpdateRejects, 0U);
}

TEST_F(BgfxUIAtlasTextureContractTest, InitialUploadFailureDestroysTheCreatedTexture)
{
    constexpr std::array<u8, 4> Pixels{0U, 32U, 96U, 255U};
    tina_test_bgfx::Contract::state.failCopyCall = 1;

    auto atlas = createUIGlyphAtlasTextureContractTest(2, 2, Pixels);

    ASSERT_FALSE(atlas.has_value());
    EXPECT_EQ(atlas.error().code, RenderErrorCode::DeviceInitializationFailed);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureCreates.size(), 1U);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textures.size(), 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textures.front().mutableStorage);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textures.front().destroyed);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 1U);
    EXPECT_EQ(
        tina_test_bgfx::Contract::state.textureDestroys.front().idx,
        tina_test_bgfx::Contract::state.textures.front().handle.idx);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textureUpdates.empty());
}

} // namespace
} // namespace Tina::Render::Bgfx
