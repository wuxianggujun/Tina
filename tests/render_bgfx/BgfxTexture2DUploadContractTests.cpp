#include "fakes/bgfx/bgfx.h"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Render::Bgfx {

[[nodiscard]] tina_test_bgfx::TextureFormat::Enum toBgfxTextureFormatContractTest(
    GpuTextureFormat format) noexcept;

[[nodiscard]] u64 toBgfxTexture2DFlagsContractTest(const Texture2DUploadDesc& desc) noexcept;

[[nodiscard]] Core::Result<tina_test_bgfx::TextureHandle> createTexture2DUploadContractTest(
    const Texture2DUploadDesc& desc);

namespace {

namespace Fake = tina_test_bgfx;

class BgfxTexture2DUploadContractTest : public testing::Test {
protected:
    void SetUp() override
    {
        Fake::Contract::reset();
    }
};

// Level bytes carry the level index in every byte so a blob assembled in the wrong
// order is distinguishable from one assembled with the wrong sizes.
[[nodiscard]] std::vector<std::byte> levelFill(usize byteCount, u8 marker)
{
    return std::vector<std::byte>(byteCount, static_cast<std::byte>(marker));
}

TEST_F(BgfxTexture2DUploadContractTest, EveryPublishedFormatMapsToADistinctBgfxToken)
{
    EXPECT_EQ(toBgfxTextureFormatContractTest(GpuTextureFormat::Rgba8Unorm),
              Fake::TextureFormat::RGBA8);
    EXPECT_EQ(toBgfxTextureFormatContractTest(GpuTextureFormat::Bc1Rgba), Fake::TextureFormat::BC1);
    EXPECT_EQ(toBgfxTextureFormatContractTest(GpuTextureFormat::Bc3Rgba), Fake::TextureFormat::BC3);
    EXPECT_EQ(toBgfxTextureFormatContractTest(GpuTextureFormat::Bc7Rgba), Fake::TextureFormat::BC7);
    EXPECT_EQ(toBgfxTextureFormatContractTest(GpuTextureFormat::Astc4x4Rgba),
              Fake::TextureFormat::ASTC4x4);
    // Invalid must not alias a real format, or an unmapped format would upload as
    // whatever token happened to share its value.
    EXPECT_EQ(toBgfxTextureFormatContractTest(GpuTextureFormat::Invalid),
              Fake::TextureFormat::Count);
}

TEST_F(BgfxTexture2DUploadContractTest, SrgbIsTheOnlyColorSpaceThatSetsTheDecodeFlag)
{
    Texture2DUploadDesc srgbDesc{};
    srgbDesc.colorSpace = GpuTextureColorSpace::Srgb;
    Texture2DUploadDesc linearDesc{};
    linearDesc.colorSpace = GpuTextureColorSpace::Linear;

    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(srgbDesc) & BGFX_TEXTURE_SRGB, BGFX_TEXTURE_SRGB);
    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(linearDesc) & BGFX_TEXTURE_SRGB, 0U);
}

TEST_F(BgfxTexture2DUploadContractTest, BgfxDefaultsContributeNoSamplerBits)
{
    // Repeat wrap plus Linear min/mag and no mip filter is bgfx's own default state.
    // Emitting bits for it would make every default texture differ from a texture the
    // engine never configured, which is the state the shadow and atlas paths rely on.
    Texture2DUploadDesc desc{};
    desc.colorSpace = GpuTextureColorSpace::Linear;
    desc.sampler = GpuTextureSamplerDesc{
        .wrapU = GpuTextureWrapMode::Repeat,
        .wrapV = GpuTextureWrapMode::Repeat,
        .minFilter = GpuTextureFilterMode::Linear,
        .magFilter = GpuTextureFilterMode::Linear,
        .mipFilter = GpuTextureMipFilterMode::None,
    };

    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(desc), BGFX_TEXTURE_NONE);
}

TEST_F(BgfxTexture2DUploadContractTest, WrapModesStayOnTheirOwnAxisField)
{
    // bgfx packs each axis as a two-bit field, so Mirror on U and Clamp on V must not
    // combine into U_BORDER. Asserting the composed value catches a translation that
    // ORs the two axes into one field.
    Texture2DUploadDesc desc{};
    desc.colorSpace = GpuTextureColorSpace::Linear;
    desc.sampler.wrapU = GpuTextureWrapMode::Mirror;
    desc.sampler.wrapV = GpuTextureWrapMode::Clamp;
    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(desc), BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_CLAMP);

    desc.sampler.wrapU = GpuTextureWrapMode::Border;
    desc.sampler.wrapV = GpuTextureWrapMode::Border;
    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(desc), BGFX_SAMPLER_U_BORDER | BGFX_SAMPLER_V_BORDER);

    desc.sampler.wrapU = GpuTextureWrapMode::Clamp;
    desc.sampler.wrapV = GpuTextureWrapMode::Mirror;
    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(desc), BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_MIRROR);
}

TEST_F(BgfxTexture2DUploadContractTest, EachFilterStageSetsItsOwnBit)
{
    Texture2DUploadDesc desc{};
    desc.colorSpace = GpuTextureColorSpace::Linear;
    desc.sampler.minFilter = GpuTextureFilterMode::Point;
    desc.sampler.magFilter = GpuTextureFilterMode::Anisotropic;
    desc.sampler.mipFilter = GpuTextureMipFilterMode::Point;

    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(desc),
              BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_ANISOTROPIC | BGFX_SAMPLER_MIP_POINT);

    desc.sampler.minFilter = GpuTextureFilterMode::Anisotropic;
    desc.sampler.magFilter = GpuTextureFilterMode::Point;
    // Linear mip filtering is bgfx's default, so it contributes nothing even though it
    // is not the descriptor's default.
    desc.sampler.mipFilter = GpuTextureMipFilterMode::Linear;
    EXPECT_EQ(toBgfxTexture2DFlagsContractTest(desc),
              BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_POINT);
}

TEST_F(BgfxTexture2DUploadContractTest, MipChainUploadsAsOneBlobInLevelOrder)
{
    // 4x4 RGBA8 halving to 2x2 then 1x1: 64, 16 and 4 bytes.
    const auto level0 = levelFill(64U, 0xA0U);
    const auto level1 = levelFill(16U, 0xB1U);
    const auto level2 = levelFill(4U, 0xC2U);
    const std::array<Texture2DUploadLevel, 3> levels{
        Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
        Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level1},
        Texture2DUploadLevel{.width = 1, .height = 1, .bytes = level2},
    };
    Texture2DUploadDesc desc{};
    desc.format = GpuTextureFormat::Rgba8Unorm;
    desc.colorSpace = GpuTextureColorSpace::Srgb;
    desc.sampler.mipFilter = GpuTextureMipFilterMode::Linear;
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_TRUE(created.has_value()) << created.error().message;
    ASSERT_EQ(Fake::Contract::state.textureCreates.size(), 1U);
    const auto& create = Fake::Contract::state.textureCreates.front();
    EXPECT_EQ(create.width, 4U);
    EXPECT_EQ(create.height, 4U);
    EXPECT_TRUE(create.hasMips);
    EXPECT_EQ(create.layers, 1U);
    EXPECT_EQ(create.format, Fake::TextureFormat::RGBA8);
    EXPECT_TRUE(create.initialMemoryProvided);

    std::vector<u8> expected;
    expected.insert(expected.end(), 64U, 0xA0U);
    expected.insert(expected.end(), 16U, 0xB1U);
    expected.insert(expected.end(), 4U, 0xC2U);
    // Byte-for-byte rather than by size: a size-only check passes when the levels are
    // concatenated in the wrong order, which is exactly how a chain ends up sampling
    // the smallest level at full size.
    EXPECT_EQ(create.initialPixels, expected);
}

TEST_F(BgfxTexture2DUploadContractTest, SingleLevelUploadDeclaresNoMips)
{
    const auto level0 = levelFill(4U, 0x7FU);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 1, .height = 1, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_TRUE(created.has_value()) << created.error().message;
    ASSERT_EQ(Fake::Contract::state.textureCreates.size(), 1U);
    // hasMips true on a one-level upload makes bgfx read past the supplied bytes for
    // the levels it now expects.
    EXPECT_FALSE(Fake::Contract::state.textureCreates.front().hasMips);
}

TEST_F(BgfxTexture2DUploadContractTest, CompressedLevelsUploadAtBlockSize)
{
    // BC1 is 8 bytes per 4x4 block, so 8x8 is 4 blocks. The tail levels are each below
    // one block yet still cost a whole padded block, so 2x2 and 1x1 are 8 bytes too --
    // a byte-per-pixel assumption would size this chain 64/16/4/1.
    const auto level0 = levelFill(32U, 0x11U);
    const auto level1 = levelFill(8U, 0x22U);
    const auto level2 = levelFill(8U, 0x33U);
    const auto level3 = levelFill(8U, 0x44U);
    const std::array<Texture2DUploadLevel, 4> levels{
        Texture2DUploadLevel{.width = 8, .height = 8, .bytes = level0},
        Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level1},
        Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level2},
        Texture2DUploadLevel{.width = 1, .height = 1, .bytes = level3},
    };
    Texture2DUploadDesc desc{};
    desc.format = GpuTextureFormat::Bc1Rgba;
    desc.colorSpace = GpuTextureColorSpace::Srgb;
    desc.sampler.mipFilter = GpuTextureMipFilterMode::Linear;
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_TRUE(created.has_value()) << created.error().message;
    ASSERT_EQ(Fake::Contract::state.textureCreates.size(), 1U);
    const auto& create = Fake::Contract::state.textureCreates.front();
    EXPECT_EQ(create.format, Fake::TextureFormat::BC1);
    EXPECT_EQ(create.initialPixels.size(), 56U);
}

TEST_F(BgfxTexture2DUploadContractTest, ProbesTheAdapterWithTheTranslatedFormatAndFlags)
{
    // BC7 costs 16 bytes per 4x4 block, so a single-block 4x4 level is 16 bytes.
    const auto level0 = levelFill(16U, 0x33U);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.format = GpuTextureFormat::Bc7Rgba;
    desc.colorSpace = GpuTextureColorSpace::Srgb;
    desc.sampler.wrapU = GpuTextureWrapMode::Clamp;
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_TRUE(created.has_value()) << created.error().message;
    ASSERT_EQ(Fake::Contract::state.textureValidations.size(), 1U);
    const auto& probe = Fake::Contract::state.textureValidations.front();
    // Probing with different flags than the create uses would clear a format/flag pair
    // the adapter actually refuses.
    EXPECT_EQ(probe.format, Fake::TextureFormat::BC7);
    EXPECT_EQ(probe.flags, toBgfxTexture2DFlagsContractTest(desc));
    EXPECT_EQ(probe.layers, 1U);
    // A 2D upload must not be probed as a cube map: the two are separate capabilities.
    EXPECT_FALSE(probe.cubeMap);
}

TEST_F(BgfxTexture2DUploadContractTest, UnsupportedAdapterFormatFailsBeforeCreating)
{
    Fake::Contract::state.rejectTextureValidationFormat = Fake::TextureFormat::ASTC4x4;
    const auto level0 = levelFill(16U, 0x44U);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.format = GpuTextureFormat::Astc4x4Rgba;
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, RenderErrorCode::InvalidTextureUpload);
    // Creating anyway would surface as an invalid handle, hiding that the format rather
    // than the pixels was refused.
    EXPECT_TRUE(Fake::Contract::state.textureCreates.empty());
}

TEST_F(BgfxTexture2DUploadContractTest, ASupportedFormatStillUploadsWhenAnotherIsRefused)
{
    // Guards the per-format probe against degrading into a blanket refusal.
    Fake::Contract::state.rejectTextureValidationFormat = Fake::TextureFormat::ASTC4x4;
    const auto level0 = levelFill(16U, 0x55U);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.format = GpuTextureFormat::Rgba8Unorm;
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_TRUE(created.has_value()) << created.error().message;
    EXPECT_EQ(Fake::Contract::state.textureCreates.size(), 1U);
}

TEST_F(BgfxTexture2DUploadContractTest, FailedUploadAllocationReportsInsteadOfCreating)
{
    Fake::Contract::state.failAllocCall = 1;
    const auto level0 = levelFill(16U, 0x66U);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, RenderErrorCode::InvalidTextureUpload);
    // Passing a null Memory to bgfx::createTexture2D would create an uninitialised
    // texture that samples as garbage rather than reporting the failure.
    EXPECT_TRUE(Fake::Contract::state.textureCreates.empty());
}

TEST_F(BgfxTexture2DUploadContractTest, RejectedCreateReportsInsteadOfReturningAnInvalidHandle)
{
    Fake::Contract::state.rejectTextureCreate = true;
    const auto level0 = levelFill(16U, 0x77U);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, RenderErrorCode::InvalidTextureUpload);
}

TEST_F(BgfxTexture2DUploadContractTest, InvalidDescriptorIsRejectedBeforeTouchingBgfx)
{
    // A level whose byte count contradicts its extent, which the shared validator owns.
    const auto level0 = levelFill(3U, 0x88U);
    const std::array<Texture2DUploadLevel, 1> levels{
        Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
    };
    Texture2DUploadDesc desc{};
    desc.levels = levels;

    auto created = createTexture2DUploadContractTest(desc);

    ASSERT_FALSE(created.has_value());
    EXPECT_TRUE(Fake::Contract::state.textureValidations.empty());
    EXPECT_TRUE(Fake::Contract::state.textureCreates.empty());
    EXPECT_EQ(Fake::Contract::state.allocCalls, 0U);
}

} // namespace
} // namespace Tina::Render::Bgfx
