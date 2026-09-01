#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/UploadTicket.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory_resource>
#include <string>
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

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

TEST(AssetSystemCatalogApiTests, OpenAndBindAndFindByKind)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));
    std::vector<std::byte> pixels(4, std::byte{1});
    auto texPayload = AssetFormat::writeTexture2DPayloadBytesRgba8(1, 1, pixels);
    auto spritePayload = AssetFormat::writeSpritePayloadBytes(AssetFormat::SpritePayloadDesc{
        .pixelsPerUnit = 8.0f,
        .textureId = textureId,
    });
    ASSERT_TRUE(texPayload.has_value());
    ASSERT_TRUE(spritePayload.has_value());

    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion,
        .payload = std::move(*texPayload),
    });
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Sprite,
        .assetId = spriteId,
        .assetTypeVersion = 1,
        .payload = std::move(*spritePayload),
        .dependencies = {AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        }},
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_open_bind_api";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), request).has_value());

    auto ledger =
        Render::NullUploadLedger::Create(Render::UploadLedgerConfig{.capacity = 4, .memoryResource = &memory});
    ASSERT_TRUE(ledger.has_value());
    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &memory,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .memoryResource = &memory,
            },
        .uploadLedger = &(*ledger),
    });
    ASSERT_TRUE(system.has_value());
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(root)).has_value());

    auto firstSprite = system->catalogFirstIdOfKind(AssetFormat::AssetKind::Sprite);
    ASSERT_TRUE(firstSprite.has_value());
    EXPECT_EQ(*firstSprite, spriteId);

    auto loaded = system->load(std::array{*firstSprite});
    ASSERT_TRUE(loaded.has_value());
    auto loadedSprite = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Sprite);
    ASSERT_TRUE(loadedSprite.has_value());
    const auto* file = system->tryGet(*loadedSprite);
    ASSERT_NE(file, nullptr);
    auto sprite = parseSpriteFromCooked(*file);
    ASSERT_TRUE(sprite.has_value());
    EXPECT_FLOAT_EQ(sprite->pixelsPerUnit, 8.0f);

    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
