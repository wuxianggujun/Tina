#include <tina/asset/AssetSpriteRender.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderScene.hpp>
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

TEST(AssetToRenderScenePipelineTests, CookLoadToRenderSceneCommitWithUv)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));

    // 4x2 texture so half UV width is easy to reason about.
    std::vector<std::byte> pixels(4U * 2U * 4U, std::byte{0xCC});
    auto texPayload = AssetFormat::writeTexture2DPayloadBytes(
        AssetFormat::Texture2DPayloadDesc{.width = 4, .height = 2, .pixels = pixels});
    ASSERT_TRUE(texPayload.has_value()) << texPayload.error().message;
    auto spritePayload = AssetFormat::writeSpritePayloadBytes(AssetFormat::SpritePayloadDesc{
        .u0 = 0.25f,
        .v0 = 0.0f,
        .u1 = 0.75f,
        .v1 = 1.0f,
        .pivotX = 0.5f,
        .pivotY = 0.5f,
        .pixelsPerUnit = 4.0f,
        .textureId = textureId,
    });
    ASSERT_TRUE(spritePayload.has_value()) << spritePayload.error().message;

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

    const auto root = std::filesystem::temp_directory_path() / "tina_asset_to_render_scene";
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
        .requireTyped2dPayloads = true,
    });
    ASSERT_TRUE(system.has_value());
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(root)).has_value());

    auto loaded = system->load(std::array{spriteId});
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_TRUE(system->isGpuReady((*loaded)[0]));

    const auto* spriteFile = system->tryGet((*loaded)[0]);
    ASSERT_NE(spriteFile, nullptr);
    auto texHandle = system->find(textureId);
    ASSERT_TRUE(texHandle.has_value());
    const auto* textureFile = system->tryGet(*texHandle);
    ASSERT_NE(textureFile, nullptr);

    auto renderInput = makeSpriteRenderInput(*spriteFile, textureFile,
                                             SpriteRenderParams{
                                                 .stableEntityKey = 42,
                                                 .centerX = 0.0f,
                                                 .centerY = 0.0f,
                                                 .spriteKey = 1, // fixture-compatible key for bgfx path
                                             });
    ASSERT_TRUE(renderInput.has_value()) << renderInput.error().message;
    EXPECT_FLOAT_EQ(renderInput->u0, 0.25f);
    EXPECT_FLOAT_EQ(renderInput->u1, 0.75f);
    // width = 4px * 0.5 UV / 4 ppu = 0.5 m; height = 2px * 1.0 / 4 = 0.5 m
    EXPECT_FLOAT_EQ(renderInput->widthMeters, 0.5f);
    EXPECT_FLOAT_EQ(renderInput->heightMeters, 0.5f);

    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 8}, memory);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    ASSERT_TRUE(builder->beginFrame({}).has_value());
    auto writer = builder->writer();
    ASSERT_TRUE(writer.setCamera2D(Render::RenderCamera2DInput{
                                       .stableCameraKey = 1,
                                       .centerX = 0.0f,
                                       .centerY = 0.0f,
                                       .worldWidth = 10.0f,
                                       .worldHeight = 10.0f,
                                       .actualPixelsPerMeter = 100.0f,
                                   })
                    .has_value());
    ASSERT_TRUE(writer.addSprite2D(*renderInput).has_value());
    auto view = builder->commit();
    ASSERT_TRUE(view.has_value()) << view.error().message;
    ASSERT_EQ(view->sprites2D().size(), 1U);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u0, 0.25f);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u1, 0.75f);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].widthMeters, 0.5f);
    EXPECT_EQ(view->sprites2D()[0].stableEntityKey, 42U);
    EXPECT_EQ(view->statistics().visibleSpriteCount, 1U);

    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
