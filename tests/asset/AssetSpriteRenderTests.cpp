#include <tina/asset/AssetSpriteRender.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>

#include <gtest/gtest.h>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

void countRelease(void* userData) noexcept
{
    ++*static_cast<Core::u32*>(userData);
}

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] CookedAssetFile loadFromBytes(std::pmr::memory_resource& memory, std::vector<std::byte> bytes)
{
    auto file = makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>(bytes.begin(), bytes.end(), &memory),
                                             CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    return file ? std::move(*file) : CookedAssetFile{};
}

TEST(AssetSpriteRenderTests, BuildsUvAndSizeFromTypedPayloads)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));

    std::vector<std::byte> pixels(2U * 2U * 4U, std::byte{255});
    auto texBytes = AssetFormat::writeCookedTexture2DAssetRgba8(textureId, 2, 2, pixels);
    ASSERT_TRUE(texBytes.has_value());
    auto spriteBytes = AssetFormat::writeCookedSpriteAsset(spriteId, AssetFormat::SpritePayloadDesc{
                                                                         .u0 = 0.0f,
                                                                         .v0 = 0.0f,
                                                                         .u1 = 0.5f,
                                                                         .v1 = 1.0f,
                                                                         .pivotX = 0.5f,
                                                                         .pivotY = 0.5f,
                                                                         .pixelsPerUnit = 2.0f,
                                                                         .textureId = textureId,
                                                                     });
    ASSERT_TRUE(spriteBytes.has_value());

    auto texture = loadFromBytes(memory, *texBytes);
    auto sprite = loadFromBytes(memory, *spriteBytes);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(sprite);

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    Core::u32 releaseCount = 0;
    auto textureResource = packet.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Texture2D,
            .deviceBindingKey = 7,
        },
        Render::FramePin{Render::FramePinKind::Custom, 7, &releaseCount, &countRelease});
    ASSERT_TRUE(textureResource.has_value()) << textureResource.error().message;

    auto input = makeSpriteRenderInput(sprite, &texture, *textureResource,
                                       SpriteRenderParams{.stableEntityKey = 7, .centerX = 1.0f, .centerY = 2.0f});
    ASSERT_TRUE(input.has_value()) << input.error().message;
    EXPECT_EQ(input->texture, *textureResource);
    EXPECT_EQ(input->stableEntityKey, 7U);
    EXPECT_FLOAT_EQ(input->u0, 0.0f);
    EXPECT_FLOAT_EQ(input->u1, 0.5f);
    EXPECT_FLOAT_EQ(input->v0, 0.0f);
    EXPECT_FLOAT_EQ(input->v1, 1.0f);
    // width = 2px * 0.5 UV / 2 ppu = 0.5 meters
    EXPECT_FLOAT_EQ(input->widthMeters, 0.5f);
    EXPECT_FLOAT_EQ(input->heightMeters, 1.0f);
    EXPECT_FLOAT_EQ(input->centerX, 1.0f);
    EXPECT_FLOAT_EQ(input->centerY, 2.0f);

    const auto invalidResource = makeSpriteRenderInput(sprite, &texture, {});
    ASSERT_FALSE(invalidResource.has_value());
    EXPECT_EQ(invalidResource.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(releaseCount, 0U);
    ASSERT_TRUE(packet.abandon().has_value());
    EXPECT_EQ(releaseCount, 1U);
}

} // namespace
} // namespace Tina::Asset
