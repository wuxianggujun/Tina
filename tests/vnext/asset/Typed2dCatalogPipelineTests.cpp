#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
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

TEST(Typed2dCatalogPipelineTests, CookLoadParseTextureAndSprite)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));

    std::vector<std::byte> pixels(1U * 1U * 4U, std::byte{0xAB}); // 1x1 rgba
    auto texPayload = AssetFormat::writeTexture2DPayloadBytes(AssetFormat::Texture2DPayloadDesc{
        .width = 1,
        .height = 1,
        .pixels = pixels,
    });
    ASSERT_TRUE(texPayload.has_value());
    auto spritePayload = AssetFormat::writeSpritePayloadBytes(AssetFormat::SpritePayloadDesc{
        .u0 = 0.0f,
        .v0 = 0.0f,
        .u1 = 1.0f,
        .v1 = 1.0f,
        .pixelsPerUnit = 16.0f,
        .textureId = textureId,
    });
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
        .assetTypeVersion = AssetFormat::SpriteWire::SchemaVersion,
        .payload = std::move(*spritePayload),
        .dependencies =
            {
                AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = AssetFormat::AssetKind::Texture2D,
                    .flags = AssetFormat::DependencyFlags::Required,
                },
            },
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed2d_pipeline";
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
        .autoGpuUpload = true,
    });
    ASSERT_TRUE(system.has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value());
    ASSERT_TRUE(system->bindCatalog(toUtf8(root), std::move(*catalog)).has_value());

    auto loaded = system->load(std::array{spriteId});
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_TRUE(system->isGpuReady((*loaded)[0]));

    const auto* spriteFile = system->tryGet((*loaded)[0]);
    ASSERT_NE(spriteFile, nullptr);
    auto sprite = AssetFormat::parseSpritePayload(spriteFile->payload());
    ASSERT_TRUE(sprite.has_value()) << sprite.error().message;
    EXPECT_FLOAT_EQ(sprite->pixelsPerUnit, 16.0f);

    auto textureHandle = system->find(textureId);
    ASSERT_TRUE(textureHandle.has_value());
    const auto* textureFile = system->tryGet(*textureHandle);
    ASSERT_NE(textureFile, nullptr);
    auto texture = AssetFormat::parseTexture2DPayload(textureFile->payload());
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ(texture->width, 1);
    EXPECT_EQ(texture->height, 1);
    EXPECT_EQ(texture->pixels[0], std::byte{0xAB});

    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
