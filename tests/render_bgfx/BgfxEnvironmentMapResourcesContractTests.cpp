#include "fakes/bgfx/bgfx.h"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace Tina::Render::Bgfx {

struct BgfxEnvironmentMapResourcesContractTest final {
    tina_test_bgfx::TextureHandle diffuseIrradiance = BGFX_INVALID_HANDLE;
    tina_test_bgfx::TextureHandle prefilteredSpecular = BGFX_INVALID_HANDLE;
    tina_test_bgfx::TextureHandle brdfLut = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return tina_test_bgfx::isValid(diffuseIrradiance) &&
               tina_test_bgfx::isValid(prefilteredSpecular) &&
               tina_test_bgfx::isValid(brdfLut);
    }
};

[[nodiscard]] Core::Result<BgfxEnvironmentMapResourcesContractTest>
createEnvironmentMapResourcesContractTest(const EnvironmentMapUploadDesc& desc);

void destroyEnvironmentMapResourcesContractTest(
    BgfxEnvironmentMapResourcesContractTest& resources) noexcept;

namespace {

[[nodiscard]] std::vector<u8> bytesOf(std::span<const std::byte> bytes)
{
    std::vector<u8> result(bytes.size());
    if (!bytes.empty())
    {
        std::memcpy(result.data(), bytes.data(), bytes.size());
    }
    return result;
}

void fillSequence(std::vector<std::byte>& bytes, u8 seed)
{
    for (usize index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<std::byte>(
            static_cast<u8>(seed + static_cast<u8>(index)));
    }
}

class BgfxEnvironmentMapResourcesTest : public testing::Test {
  protected:
    void SetUp() override
    {
        tina_test_bgfx::Contract::reset();
        diffusePixels_.resize(2U * 2U * 6U * 8U);
        specularPixels_.resize((4U * 4U + 2U * 2U + 1U) * 6U * 8U);
        brdfPixels_.resize(2U * 3U * 4U);
        fillSequence(diffusePixels_, 0x10U);
        fillSequence(specularPixels_, 0x40U);
        fillSequence(brdfPixels_, 0x90U);
        desc_ = EnvironmentMapUploadDesc{
            .diffuseFaceSize = 2,
            .specularFaceSize = 4,
            .specularMipCount = 3,
            .brdfWidth = 2,
            .brdfHeight = 3,
            .diffuseRgba16FloatPixels = diffusePixels_,
            .specularRgba16FloatPixels = specularPixels_,
            .brdfRg16FloatPixels = brdfPixels_,
        };
    }

    std::vector<std::byte> diffusePixels_{};
    std::vector<std::byte> specularPixels_{};
    std::vector<std::byte> brdfPixels_{};
    EnvironmentMapUploadDesc desc_{};
};

TEST_F(BgfxEnvironmentMapResourcesTest, CreatesLatestFloatTextureContractAndUploadsMipMajorFaces)
{
    constexpr u64 CubeFlags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP |
                              BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    constexpr u64 BrdfFlags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP |
                              BGFX_SAMPLER_V_CLAMP;

    auto resources = createEnvironmentMapResourcesContractTest(desc_);

    ASSERT_TRUE(resources.has_value()) << resources.error().message;
    ASSERT_TRUE(resources->valid());
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureCreates.size(), 3U);

    const auto& diffuseCreate = tina_test_bgfx::Contract::state.textureCreates[0];
    EXPECT_TRUE(diffuseCreate.cubeMap);
    EXPECT_EQ(diffuseCreate.width, 2U);
    EXPECT_EQ(diffuseCreate.height, 2U);
    EXPECT_FALSE(diffuseCreate.hasMips);
    EXPECT_EQ(diffuseCreate.layers, 1U);
    EXPECT_EQ(diffuseCreate.format, tina_test_bgfx::TextureFormat::RGBA16F);
    EXPECT_EQ(diffuseCreate.flags, CubeFlags);
    EXPECT_FALSE(diffuseCreate.initialMemoryProvided);

    const auto& specularCreate = tina_test_bgfx::Contract::state.textureCreates[1];
    EXPECT_TRUE(specularCreate.cubeMap);
    EXPECT_EQ(specularCreate.width, 4U);
    EXPECT_EQ(specularCreate.height, 4U);
    EXPECT_TRUE(specularCreate.hasMips);
    EXPECT_EQ(specularCreate.layers, 1U);
    EXPECT_EQ(specularCreate.format, tina_test_bgfx::TextureFormat::RGBA16F);
    EXPECT_EQ(specularCreate.flags, CubeFlags);
    EXPECT_FALSE(specularCreate.initialMemoryProvided);

    const auto& brdfCreate = tina_test_bgfx::Contract::state.textureCreates[2];
    EXPECT_FALSE(brdfCreate.cubeMap);
    EXPECT_EQ(brdfCreate.width, 2U);
    EXPECT_EQ(brdfCreate.height, 3U);
    EXPECT_FALSE(brdfCreate.hasMips);
    EXPECT_EQ(brdfCreate.layers, 1U);
    EXPECT_EQ(brdfCreate.format, tina_test_bgfx::TextureFormat::RG16F);
    EXPECT_EQ(brdfCreate.flags, BrdfFlags);
    EXPECT_TRUE(brdfCreate.initialMemoryProvided);
    EXPECT_EQ(brdfCreate.initialPixels, bytesOf(brdfPixels_));

    ASSERT_EQ(tina_test_bgfx::Contract::state.textureUpdates.size(), 24U);
    usize updateIndex = 0;
    constexpr usize DiffuseFaceBytes = 2U * 2U * 8U;
    for (u8 face = 0; face < 6U; ++face)
    {
        const auto& update =
            tina_test_bgfx::Contract::state.textureUpdates[updateIndex++];
        EXPECT_TRUE(update.cubeMap);
        EXPECT_EQ(update.texture.idx, resources->diffuseIrradiance.idx);
        EXPECT_EQ(update.layer, 0U);
        EXPECT_EQ(update.side, face);
        EXPECT_EQ(update.mip, 0U);
        EXPECT_EQ(update.x, 0U);
        EXPECT_EQ(update.y, 0U);
        EXPECT_EQ(update.width, 2U);
        EXPECT_EQ(update.height, 2U);
        EXPECT_EQ(update.pixels,
                  bytesOf(std::span<const std::byte>{diffusePixels_}.subspan(
                      static_cast<usize>(face) * DiffuseFaceBytes,
                      DiffuseFaceBytes)));
    }

    usize specularOffset = 0;
    u16 mipExtent = 4;
    for (u8 mip = 0; mip < 3U; ++mip)
    {
        const usize faceBytes = static_cast<usize>(mipExtent) * mipExtent * 8U;
        for (u8 face = 0; face < 6U; ++face)
        {
            const auto& update =
                tina_test_bgfx::Contract::state.textureUpdates[updateIndex++];
            EXPECT_TRUE(update.cubeMap);
            EXPECT_EQ(update.texture.idx, resources->prefilteredSpecular.idx);
            EXPECT_EQ(update.layer, 0U);
            EXPECT_EQ(update.side, face);
            EXPECT_EQ(update.mip, mip);
            EXPECT_EQ(update.x, 0U);
            EXPECT_EQ(update.y, 0U);
            EXPECT_EQ(update.width, mipExtent);
            EXPECT_EQ(update.height, mipExtent);
            EXPECT_EQ(update.pixels,
                      bytesOf(std::span<const std::byte>{specularPixels_}.subspan(
                          specularOffset, faceBytes)));
            specularOffset += faceBytes;
        }
        mipExtent = static_cast<u16>(mipExtent / 2U);
    }
    EXPECT_EQ(updateIndex, tina_test_bgfx::Contract::state.textureUpdates.size());
    EXPECT_EQ(specularOffset, specularPixels_.size());
    EXPECT_EQ(tina_test_bgfx::Contract::state.immutableUpdateRejects, 0U);
}

TEST_F(BgfxEnvironmentMapResourcesTest, DiffuseCreateFailureDoesNotDestroyInvalidHandle)
{
    tina_test_bgfx::Contract::state.rejectTextureCreateCall = 1;

    auto resources = createEnvironmentMapResourcesContractTest(desc_);

    ASSERT_FALSE(resources.has_value());
    EXPECT_EQ(resources.error().code, RenderErrorCode::InvalidEnvironmentMapUpload);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureCreates.size(), 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textureDestroys.empty());
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textureUpdates.empty());
}

TEST_F(BgfxEnvironmentMapResourcesTest, SpecularCreateFailureRollsBackDiffuse)
{
    tina_test_bgfx::Contract::state.rejectTextureCreateCall = 2;

    auto resources = createEnvironmentMapResourcesContractTest(desc_);

    ASSERT_FALSE(resources.has_value());
    EXPECT_EQ(resources.error().code, RenderErrorCode::InvalidEnvironmentMapUpload);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 1U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys[0].idx, 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textureUpdates.empty());
}

TEST_F(BgfxEnvironmentMapResourcesTest, BrdfCreateFailureRollsBackSpecularThenDiffuse)
{
    tina_test_bgfx::Contract::state.rejectTextureCreateCall = 3;

    auto resources = createEnvironmentMapResourcesContractTest(desc_);

    ASSERT_FALSE(resources.has_value());
    EXPECT_EQ(resources.error().code, RenderErrorCode::InvalidEnvironmentMapUpload);
    ASSERT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 2U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys[0].idx, 2U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys[1].idx, 1U);
    EXPECT_TRUE(tina_test_bgfx::Contract::state.textureUpdates.empty());
}

TEST_F(BgfxEnvironmentMapResourcesTest, DestroyReleasesBrdfSpecularDiffuseAndIsIdempotent)
{
    auto resources = createEnvironmentMapResourcesContractTest(desc_);
    ASSERT_TRUE(resources.has_value()) << resources.error().message;

    destroyEnvironmentMapResourcesContractTest(*resources);

    ASSERT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 3U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys[0].idx, 3U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys[1].idx, 2U);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys[2].idx, 1U);
    EXPECT_FALSE(resources->valid());

    destroyEnvironmentMapResourcesContractTest(*resources);
    EXPECT_EQ(tina_test_bgfx::Contract::state.textureDestroys.size(), 3U);
}

} // namespace
} // namespace Tina::Render::Bgfx
