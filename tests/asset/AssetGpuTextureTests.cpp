#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <utility>
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

struct CapturedTextureLevel final {
    Core::u16 width = 0;
    Core::u16 height = 0;
    std::vector<std::byte> bytes{};
};

class CapturingTextureDevice final : public Render::IRenderDevice {
  public:
    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(
        const Render::RenderFrame&) override
    {
        return Render::RenderFrameSubmission::SkippedSuspendedSurface();
    }

    [[nodiscard]] Core::Status present() override { return Core::success(); }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override { return {}; }
    void shutdown() noexcept override {}

    [[nodiscard]] Core::Result<Render::GpuTextureId> createTexture2D(
        const Render::Texture2DUploadDesc& desc) override
    {
        if (auto status = Render::validateTexture2DUploadDesc(desc); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        format = desc.format;
        colorSpace = desc.colorSpace;
        sampler = desc.sampler;
        levels.clear();
        for (const Render::Texture2DUploadLevel& level : desc.levels)
        {
            levels.push_back(CapturedTextureLevel{
                .width = level.width,
                .height = level.height,
                .bytes = std::vector<std::byte>(level.bytes.begin(), level.bytes.end()),
            });
        }
        return Render::GpuTextureId{1U, 1U};
    }

    [[nodiscard]] Core::Status setTexture2DBinding(
        Core::u32 bindingKey, Render::GpuTextureId texture) noexcept override
    {
        ++bindingAttempts;
        lastBindingKey = bindingKey;
        lastBoundTexture = texture;
        if (rejectBinding)
        {
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "capturing device rejected Texture2D binding");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status destroyTexture2D(Render::GpuTextureId texture) noexcept override
    {
        ++destroyAttempts;
        lastDestroyedTexture = texture;
        return Core::success();
    }

    Render::GpuTextureFormat format = Render::GpuTextureFormat::Invalid;
    Render::GpuTextureColorSpace colorSpace = Render::GpuTextureColorSpace::Invalid;
    Render::GpuTextureSamplerDesc sampler{};
    std::vector<CapturedTextureLevel> levels{};
    bool rejectBinding = false;
    Core::usize bindingAttempts = 0;
    Core::usize destroyAttempts = 0;
    Core::u32 lastBindingKey = 0;
    Render::GpuTextureId lastBoundTexture{};
    Render::GpuTextureId lastDestroyedTexture{};
};

TEST(AssetGpuTextureTests, UploadTypedTextureToNullDevice)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    std::vector<std::byte> pixels(4, std::byte{0xAB});
    auto cooked = AssetFormat::writeCookedTexture2DAssetRgba8(textureId, 1, 1, pixels);
    ASSERT_TRUE(cooked.has_value());

    auto file = makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
                                             CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value());

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto gpu = uploadTexture2DFromCooked(**device, *file);
    ASSERT_TRUE(gpu.has_value()) << gpu.error().message;
    ASSERT_TRUE(uploadAndBindTexture2DForSpriteKey(**device, *file, 1U).has_value());
    EXPECT_GE((*device)->statistics().liveResources, 1U);
}

TEST(AssetGpuTextureTests, CookedV2FieldsReachRenderWithoutLoss)
{
    std::array<std::byte, 32> level0{};
    std::array<std::byte, 16> level1{};
    std::array<std::byte, 16> level2{};
    std::array<std::byte, 16> level3{};
    level0.fill(std::byte{0x10});
    level1.fill(std::byte{0x21});
    level2.fill(std::byte{0x32});
    level3.fill(std::byte{0x43});
    const std::array levels{
        AssetFormat::Texture2DLevelDesc{.width = 8, .height = 4, .bytes = level0},
        AssetFormat::Texture2DLevelDesc{.width = 4, .height = 2, .bytes = level1},
        AssetFormat::Texture2DLevelDesc{.width = 2, .height = 1, .bytes = level2},
        AssetFormat::Texture2DLevelDesc{.width = 1, .height = 1, .bytes = level3},
    };
    const auto textureId = *Core::AssetId::fromBytes(idBytes(2U));
    auto cooked = AssetFormat::writeCookedTexture2DAsset(
        textureId,
        AssetFormat::Texture2DPayloadDesc{
            .pixelFormat = AssetFormat::Texture2DPixelFormat::Bc3Rgba,
            .colorSpace = AssetFormat::Texture2DColorSpace::Linear,
            .sampler =
                {
                    .wrapU = AssetFormat::Texture2DWrapMode::Mirror,
                    .wrapV = AssetFormat::Texture2DWrapMode::Clamp,
                    .minFilter = AssetFormat::Texture2DFilterMode::Anisotropic,
                    .magFilter = AssetFormat::Texture2DFilterMode::Anisotropic,
                    .mipFilter = AssetFormat::Texture2DMipFilterMode::Point,
                },
            .levels = levels,
        });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    CapturingTextureDevice device;
    auto gpu = uploadTexture2DFromCooked(device, *file);
    ASSERT_TRUE(gpu.has_value()) << gpu.error().message;
    EXPECT_EQ(device.format, Render::GpuTextureFormat::Bc3Rgba);
    EXPECT_EQ(device.colorSpace, Render::GpuTextureColorSpace::Linear);
    EXPECT_EQ(device.sampler.wrapU, Render::GpuTextureWrapMode::Mirror);
    EXPECT_EQ(device.sampler.wrapV, Render::GpuTextureWrapMode::Clamp);
    EXPECT_EQ(device.sampler.minFilter, Render::GpuTextureFilterMode::Anisotropic);
    EXPECT_EQ(device.sampler.magFilter, Render::GpuTextureFilterMode::Anisotropic);
    EXPECT_EQ(device.sampler.mipFilter, Render::GpuTextureMipFilterMode::Point);
    ASSERT_EQ(device.levels.size(), 4U);
    EXPECT_EQ(device.levels[0].width, 8U);
    EXPECT_EQ(device.levels[0].height, 4U);
    EXPECT_EQ(device.levels[0].bytes.size(), 32U);
    EXPECT_EQ(device.levels[0].bytes.front(), std::byte{0x10});
    EXPECT_EQ(device.levels[1].width, 4U);
    EXPECT_EQ(device.levels[1].height, 2U);
    EXPECT_EQ(device.levels[1].bytes.front(), std::byte{0x21});
    EXPECT_EQ(device.levels[2].bytes.front(), std::byte{0x32});
    EXPECT_EQ(device.levels[3].bytes.front(), std::byte{0x43});
}

TEST(AssetGpuTextureTests, AllCookedCompressedFormatsReachMatchingRenderFormat)
{
    struct FormatCase final {
        AssetFormat::Texture2DPixelFormat cooked;
        Render::GpuTextureFormat gpu;
        std::size_t bytes;
    };
    constexpr std::array Cases{
        FormatCase{AssetFormat::Texture2DPixelFormat::Bc1Rgba,
                   Render::GpuTextureFormat::Bc1Rgba, 8U},
        FormatCase{AssetFormat::Texture2DPixelFormat::Bc3Rgba,
                   Render::GpuTextureFormat::Bc3Rgba, 16U},
        FormatCase{AssetFormat::Texture2DPixelFormat::Bc7Rgba,
                   Render::GpuTextureFormat::Bc7Rgba, 16U},
        FormatCase{AssetFormat::Texture2DPixelFormat::Astc4x4Rgba,
                   Render::GpuTextureFormat::Astc4x4Rgba, 16U},
    };
    std::pmr::unsynchronized_pool_resource memory;

    for (std::size_t index = 0; index < Cases.size(); ++index)
    {
        const FormatCase& testCase = Cases[index];
        SCOPED_TRACE(index);
        std::vector<std::byte> pixels(testCase.bytes, std::byte{0x5A});
        const std::array levels{
            AssetFormat::Texture2DLevelDesc{.width = 4, .height = 4, .bytes = pixels},
        };
        auto cooked = AssetFormat::writeCookedTexture2DAsset(
            *Core::AssetId::fromBytes(idBytes(static_cast<Core::u8>(10U + index))),
            AssetFormat::Texture2DPayloadDesc{
                .pixelFormat = testCase.cooked,
                .colorSpace = AssetFormat::Texture2DColorSpace::Srgb,
                .sampler = {.mipFilter = AssetFormat::Texture2DMipFilterMode::None},
                .levels = levels,
            });
        ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
        auto file = makeCookedAssetFileFromBytes(
            std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
            CookedAssetFileLoadConfig{.memoryResource = &memory});
        ASSERT_TRUE(file.has_value()) << file.error().message;

        CapturingTextureDevice device;
        auto gpu = uploadTexture2DFromCooked(device, *file);
        ASSERT_TRUE(gpu.has_value()) << gpu.error().message;
        EXPECT_EQ(device.format, testCase.gpu);
        EXPECT_EQ(device.colorSpace, Render::GpuTextureColorSpace::Srgb);
        ASSERT_EQ(device.levels.size(), 1U);
        EXPECT_EQ(device.levels.front().bytes.size(), testCase.bytes);
    }
}

TEST(AssetGpuTextureTests, RejectsLegacyCookedAssetTypeVersionEvenWithV2Payload)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(3U));
    const std::array<std::byte, 4> pixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto payload = AssetFormat::writeTexture2DPayloadBytesRgba8(1, 1, pixels);
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        // Deliberately the superseded version, not the current constant: this asserts the
        // header gate rejects a stale cooked type version even when the payload parses.
        .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion - 1,
        .assetId = textureId,
        .payload = *payload,
    });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    CapturingTextureDevice device;
    auto gpu = uploadTexture2DFromCooked(device, *file);
    ASSERT_FALSE(gpu.has_value());
    EXPECT_EQ(gpu.error().code, AssetErrorCode::CatalogEntryMismatch);
    EXPECT_TRUE(device.levels.empty());
}

TEST(AssetGpuTextureTests, BindingFailureDestroysFreshlyUploadedTexture)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(4U));
    const std::array<std::byte, 4> pixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto cooked = AssetFormat::writeCookedTexture2DAssetRgba8(textureId, 1, 1, pixels);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    CapturingTextureDevice device;
    device.rejectBinding = true;
    auto binding = uploadAndBindTexture2DForSpriteKey(device, *file, 17U);
    ASSERT_FALSE(binding.has_value());
    EXPECT_EQ(binding.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(device.bindingAttempts, 1U);
    EXPECT_EQ(device.lastBindingKey, 17U);
    EXPECT_TRUE(device.lastBoundTexture);
    EXPECT_EQ(device.destroyAttempts, 1U);
    EXPECT_EQ(device.lastDestroyedTexture, device.lastBoundTexture);
}

TEST(AssetGpuTextureTests, ZeroBindingKeyFailsBeforeUploadingTexture)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(5U));
    const std::array<std::byte, 4> pixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto cooked = AssetFormat::writeCookedTexture2DAssetRgba8(textureId, 1, 1, pixels);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    CapturingTextureDevice device;
    auto binding = uploadAndBindTexture2DForSpriteKey(device, *file, 0U);
    ASSERT_FALSE(binding.has_value());
    EXPECT_EQ(binding.error().code, AssetErrorCode::InvalidCatalogConfig);
    EXPECT_TRUE(device.levels.empty());
    EXPECT_EQ(device.bindingAttempts, 0U);
    EXPECT_EQ(device.destroyAttempts, 0U);
}

} // namespace
} // namespace Tina::Asset
