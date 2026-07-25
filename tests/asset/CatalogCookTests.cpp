#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <span>
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

TEST(CatalogCookTests, CookAndPublishFromRequest)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = {std::byte{'t'}, std::byte{'e'}, std::byte{'x'}},
    });
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .payload = {std::byte{'m'}, std::byte{'a'}, std::byte{'t'}},
        .dependencies =
            {
                AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = AssetFormat::AssetKind::Texture2D,
                    .flags = AssetFormat::DependencyFlags::Required,
                },
            },
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_catalog_cook_req";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), request).has_value());

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
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 2U);
    EXPECT_EQ(catalog->dependencyCount(), 1U);
    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, InlineTexture2dAndSpriteRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));
    const auto texHex = textureId.canonicalText();
    const auto spriteHex = spriteId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 1 1 FF0000FF\n";
    recipe += "sprite ";
    recipe.append(spriteHex.data(), spriteHex.size());
    recipe += " ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 0 0 1 1 0.5 0.5 16\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    EXPECT_EQ(request->assets.size(), 2U);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_recipe";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 2U);
    EXPECT_EQ(catalog->dependencyCount(), 1U);
    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, InlineSpriteAnimationRecipesCookAndLoadThreeClips)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(2U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(3U));
    const auto spriteC = *Core::AssetId::fromBytes(idBytes(4U));
    const auto onceClipId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto loopClipId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto pingPongClipId = *Core::AssetId::fromBytes(idBytes(7U));

    std::string recipe = "platform WindowsX64\n";
    const auto appendId = [&recipe](Core::AssetId id) {
        const auto text = id.canonicalText();
        recipe.append(text.data(), text.size());
    };
    recipe += "texture2d ";
    appendId(textureId);
    recipe += " 1 1 FFFFFFFF\n";
    for (const auto spriteId : std::array{spriteA, spriteB, spriteC})
    {
        recipe += "sprite ";
        appendId(spriteId);
        recipe += " ";
        appendId(textureId);
        recipe += "\n";
    }
    recipe += "spriteanim ";
    appendId(onceClipId);
    recipe += " Once ";
    appendId(spriteC);
    recipe += ":0.12\n";
    recipe += "spriteanim ";
    appendId(loopClipId);
    recipe += " Loop ";
    appendId(spriteA);
    recipe += ":0.20 ";
    appendId(spriteA);
    recipe += ":0.15\n";
    recipe += "spriteanim ";
    appendId(pingPongClipId);
    recipe += " PingPong ";
    appendId(spriteB);
    recipe += ":0.08 ";
    appendId(spriteA);
    recipe += ":0.09 ";
    appendId(spriteB);
    recipe += ":0.10\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 7U);
    const auto pingSpec = std::find_if(
        request->assets.begin(), request->assets.end(),
        [pingPongClipId](const CatalogCookAssetSpec& asset) { return asset.assetId == pingPongClipId; });
    ASSERT_NE(pingSpec, request->assets.end());
    EXPECT_EQ(pingSpec->assetKind, AssetFormat::AssetKind::SpriteAnimationClip);
    ASSERT_EQ(pingSpec->dependencies.size(), 2U);
    EXPECT_EQ(pingSpec->dependencies[0].assetId, spriteA);
    EXPECT_EQ(pingSpec->dependencies[1].assetId, spriteB);
    auto pingPayload = AssetFormat::parseSpriteAnimationClipPayload(pingSpec->payload);
    ASSERT_TRUE(pingPayload.has_value()) << pingPayload.error().message;
    EXPECT_EQ(pingPayload->playbackMode, AssetFormat::SpriteAnimationPlaybackMode::PingPong);
    ASSERT_TRUE(pingPayload->frame(0U).has_value());
    ASSERT_TRUE(pingPayload->frame(1U).has_value());
    EXPECT_EQ(pingPayload->frame(0U)->spriteDependencyIndex, 1U);
    EXPECT_EQ(pingPayload->frame(1U)->spriteDependencyIndex, 0U);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_sprite_animation";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 16,
                        .maxDependencies = 32,
                        .maxDependenciesPerAsset = 8,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 7U);
    EXPECT_EQ(catalog->dependencyCount(), 7U);

    auto loaded = loadCookedAssetsFromPackage(
        toUtf8(root), std::array{onceClipId, loopClipId, pingPongClipId}, openConfig,
        CookedAssetBatchLoadConfig{
            .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
            .memoryResource = &memory,
        });
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->assets.size(), 7U);
    const CookedAssetFile* pingFile = nullptr;
    for (const auto& asset : loaded->assets)
    {
        if (asset.header().assetId == pingPongClipId)
        {
            pingFile = &asset;
            break;
        }
    }
    ASSERT_NE(pingFile, nullptr);
    auto pingClip = parseSpriteAnimationClipFromCooked(*pingFile);
    ASSERT_TRUE(pingClip.has_value()) << pingClip.error().message;
    const auto firstFrame = pingClip->frame(0U);
    ASSERT_TRUE(firstFrame.has_value());
    const auto firstSprite = pingFile->dependency(firstFrame->spriteDependencyIndex);
    ASSERT_TRUE(firstSprite.has_value());
    EXPECT_EQ(firstSprite->assetId, spriteB);

    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, SpriteAnimationRecipeRejectsBadModeAndDuration)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto spriteText = spriteId.canonicalText();
    const auto clipText = clipId.canonicalText();

    std::string badMode = "spriteanim ";
    badMode.append(clipText.data(), clipText.size());
    badMode += " Bounce ";
    badMode.append(spriteText.data(), spriteText.size());
    badMode += ":0.1\n";
    EXPECT_FALSE(parseCatalogCookRecipe(badMode, ".").has_value());

    std::string badDuration = "spriteanim ";
    badDuration.append(clipText.data(), clipText.size());
    badDuration += " Once ";
    badDuration.append(spriteText.data(), spriteText.size());
    badDuration += ":0\n";
    EXPECT_FALSE(parseCatalogCookRecipe(badDuration, ".").has_value());
}

TEST(CatalogCookTests, InlineTilesetAndTileMapRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto texHex = textureId.canonicalText();
    const auto tilesetHex = tilesetId.canonicalText();
    const auto mapHex = mapId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 2 2 FFFFFFFF 000000FF FFFFFFFF 000000FF\n";
    recipe += "tileset ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " 16 16\n";
    recipe += "tile 1 1 0 0 0.5 0.5\n";
    recipe += "tile 2 0 0.5 0 1 0.5\n";
    recipe += "tilemap ";
    recipe.append(mapHex.data(), mapHex.size());
    recipe += " ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " 2 2 1.0\n";
    recipe += "tilelayer 10 1 visual\n";
    recipe += "property role render\n";
    recipe += "row 1 2\n";
    recipe += "row 2 0\n";
    recipe += "endlayer\n";
    recipe += "objectlayer 20 1 gameplay\n";
    recipe += "property domain gameplay\n";
    recipe += "point 101 1 spawn 1 1\n";
    recipe += "objectproperty 101 role player\n";
    recipe += "endlayer\n";
    recipe += "endtilemap\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    EXPECT_EQ(request->assets.size(), 3U);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_tilemap_recipe";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 16,
                        .maxDependencies = 16,
                        .maxDependenciesPerAsset = 8,
                        .memoryResource = &memory,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 3U);
    // Texture has 0 deps, tileset 1, tilemap 1
    EXPECT_EQ(catalog->dependencyCount(), 2U);

    // Load expands dependencies (Texture → Tileset → TileMap).
    auto loaded = loadCookedAssetsFromPackage(
        toUtf8(root), std::array{mapId}, openConfig,
        CookedAssetBatchLoadConfig{.file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                                   .memoryResource = &memory});
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->assets.size(), 3U);
    const CookedAssetFile* mapFile = nullptr;
    for (const auto& asset : loaded->assets)
    {
        if (asset.header().assetKind == AssetFormat::AssetKind::TileMap)
        {
            mapFile = &asset;
            break;
        }
    }
    ASSERT_NE(mapFile, nullptr);
    auto mapView = parseTileMapFromCooked(*mapFile);
    ASSERT_TRUE(mapView.has_value()) << mapView.error().message;
    EXPECT_EQ(mapView->widthCells, 2U);
    EXPECT_EQ(mapView->heightCells, 2U);
    ASSERT_TRUE(mapView->layerAt(0).has_value());
    ASSERT_TRUE(mapView->layerAt(1).has_value());
    EXPECT_EQ(mapView->layerAt(0)->stableLayerId, 10U);
    EXPECT_EQ(mapView->layerAt(1)->stableLayerId, 20U);
    const auto visual = mapView->findLayer(10);
    ASSERT_TRUE(visual.has_value());
    EXPECT_TRUE(visual->visible);
    ASSERT_TRUE(visual->findProperty("role").has_value());
    EXPECT_EQ(visual->findProperty("role")->value, "render");
    EXPECT_EQ(*visual->tileAt(0, 0), 1U);
    EXPECT_EQ(*visual->tileAt(1, 1), 0U);
    const auto gameplay = mapView->findLayer(20);
    ASSERT_TRUE(gameplay.has_value());
    EXPECT_EQ(gameplay->objectCount, 1U);
    const auto spawn = gameplay->findObject(101);
    ASSERT_TRUE(spawn.has_value());
    EXPECT_TRUE(spawn->visible);
    ASSERT_TRUE(spawn->findProperty("role").has_value());
    EXPECT_EQ(spawn->findProperty("role")->value, "player");

    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, TileMapRecipeRejectsLegacyRowsInvalidReferencesAndUnclosedBlocks)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto tilesetHex = tilesetId.canonicalText();
    const auto mapHex = mapId.canonicalText();

    std::string header = "tilemap ";
    header.append(mapHex.data(), mapHex.size());
    header += " ";
    header.append(tilesetHex.data(), tilesetHex.size());
    header += " 1 1 1.0\n";

    EXPECT_FALSE(parseCatalogCookRecipe(header + "row 1\nendtilemap\n", ".").has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(header + "tilelayer 10 1 visual\nrow 1\nendtilemap\n", ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(header + "tilelayer 10 1 visual\nrow 1\nendlayer\n", ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(
                     header + "objectlayer 20 1 gameplay\nobjectproperty 999 role spawn\nendlayer\nendtilemap\n",
                     ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(
                     header + "objectlayer 20 1 gameplay\npoint 101 2 spawn 0 0\nendlayer\nendtilemap\n", ".")
                     .has_value());
    EXPECT_FALSE(parseCatalogCookRecipe(
                     header +
                         "tilelayer 10 1 visual\nrow 1\nendlayer\ntilelayer 10 1 collision\nrow 1\nendlayer\n"
                         "endtilemap\n",
                     ".")
                     .has_value());
}

TEST(CatalogCookTests, InvalidTileReferenceDoesNotPublishPartialCatalog)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const auto textureHex = textureId.canonicalText();
    const auto tilesetHex = tilesetId.canonicalText();
    const auto mapHex = mapId.canonicalText();

    std::string recipe = "texture2d ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 1 1 FFFFFFFF\ntileset ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 16 16\ntile 1 1 0 0 1 1\ntilemap ";
    recipe.append(mapHex.data(), mapHex.size());
    recipe += " ";
    recipe.append(tilesetHex.data(), tilesetHex.size());
    recipe += " 1 1 1.0\ntilelayer 10 1 visual\nrow 99\nendlayer\nendtilemap\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    const auto root = std::filesystem::temp_directory_path() / "tina_invalid_tilemap_no_partial_publish";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    auto published = cookAndPublishCatalogPackage(toUtf8(root), *request);
    ASSERT_FALSE(published.has_value());
    EXPECT_FALSE(std::filesystem::exists(root / "manifest.tmnft"));
    std::filesystem::remove_all(root, ec);
}

TEST(CatalogCookTests, RecipeFileRoundTrip)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto dir = std::filesystem::temp_directory_path() / "tina_catalog_cook_recipe";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto texPayload = dir / "tex.bin";
    const auto matPayload = dir / "mat.bin";
    const auto recipePath = dir / "pack.recipe";
    ASSERT_TRUE(Core::writeFile(toUtf8(texPayload), std::as_bytes(std::span("TEX", 3))).has_value());
    ASSERT_TRUE(Core::writeFile(toUtf8(matPayload), std::as_bytes(std::span("MAT", 3))).has_value());

    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto texHex = textureId.canonicalText();
    const auto matHex = materialId.canonicalText();
    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "asset Texture2D ";
    recipe.append(texHex.data(), texHex.size());
    recipe += " tex.bin\n";
    recipe += "asset Material ";
    recipe.append(matHex.data(), matHex.size());
    recipe += " mat.bin ";
    recipe.append(texHex.data(), texHex.size());
    recipe += ":Texture2D\n";
    ASSERT_TRUE(Core::writeFile(toUtf8(recipePath),
                                std::as_bytes(std::span(recipe.data(), recipe.size())))
                    .has_value());

    auto request = loadCatalogCookRecipeFile(toUtf8(recipePath));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    EXPECT_EQ(request->assets.size(), 2U);

    const auto outRoot = dir / "out";
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(outRoot), *request).has_value());

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
    auto catalog = openCatalogPackage(toUtf8(outRoot), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 2U);
    EXPECT_EQ(catalog->dependencyCount(), 1U);

    std::filesystem::remove_all(dir, ec);
}

TEST(CatalogCookTests, InlineAudioClipSineRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto clipId = *Core::AssetId::fromBytes(idBytes(4U));
    const auto clipHex = clipId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "audioclip ";
    recipe.append(clipHex.data(), clipHex.size());
    recipe += " 48000 1 64 sine 880\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::AudioClip);
    EXPECT_EQ(request->assets[0].assetId, clipId);

    const auto root = std::filesystem::temp_directory_path() / "tina_inline_audioclip";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, clipId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto clip = parseAudioClipFromCooked(*asset);
    ASSERT_TRUE(clip.has_value()) << clip.error().message;
    EXPECT_EQ(clip->sampleRate, 48000U);
    EXPECT_EQ(clip->frameCount, 64U);
    EXPECT_EQ(clip->channels, 1U);

    std::filesystem::remove_all(root, ec);
}

// M11-A20: cook PCM16 WAV file into AudioClip payload via recipe `file` form.
TEST(CatalogCookTests, AudioClipFileWavRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto clipId = *Core::AssetId::fromBytes(idBytes(7U));
    const auto clipHex = clipId.canonicalText();

    const auto dir = std::filesystem::temp_directory_path() / "tina_audioclip_wav_src";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto wavPath = dir / "click.wav";

    // Minimal mono 16-bit PCM WAV: 8 samples @ 8000 Hz.
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint32_t dataBytes = 16;
    constexpr std::uint32_t fmtChunkSize = 16;
    constexpr std::uint32_t riffSize = 4 + (8 + fmtChunkSize) + (8 + dataBytes);
    std::vector<std::byte> wav;
    wav.reserve(44 + dataBytes);
    const auto append = [&](const void* data, std::size_t size) {
        const auto* begin = static_cast<const std::byte*>(data);
        wav.insert(wav.end(), begin, begin + size);
    };
    const auto appendU16 = [&](std::uint16_t value) {
        const std::uint8_t le[2] = {static_cast<std::uint8_t>(value & 0xFF),
                                    static_cast<std::uint8_t>((value >> 8) & 0xFF)};
        append(le, 2);
    };
    const auto appendU32 = [&](std::uint32_t value) {
        const std::uint8_t le[4] = {static_cast<std::uint8_t>(value & 0xFF),
                                    static_cast<std::uint8_t>((value >> 8) & 0xFF),
                                    static_cast<std::uint8_t>((value >> 16) & 0xFF),
                                    static_cast<std::uint8_t>((value >> 24) & 0xFF)};
        append(le, 4);
    };
    append("RIFF", 4);
    appendU32(riffSize);
    append("WAVE", 4);
    append("fmt ", 4);
    appendU32(fmtChunkSize);
    appendU16(1);
    appendU16(channels);
    appendU32(sampleRate);
    appendU32(sampleRate * channels * (bitsPerSample / 8));
    appendU16(static_cast<std::uint16_t>(channels * (bitsPerSample / 8)));
    appendU16(bitsPerSample);
    append("data", 4);
    appendU32(dataBytes);
    const std::int16_t samples[8] = {0, 8000, 16000, 8000, 0, -8000, -16000, -8000};
    for (const std::int16_t sample : samples)
    {
        appendU16(static_cast<std::uint16_t>(sample));
    }
    ASSERT_TRUE(Core::writeFile(toUtf8(wavPath), wav).has_value());

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "audioclip ";
    recipe.append(clipHex.data(), clipHex.size());
    recipe += " file click.wav\n";

    auto request = parseCatalogCookRecipe(recipe, toUtf8(dir));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::AudioClip);

    const auto outRoot = dir / "pkg";
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(outRoot), *request).has_value());

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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(outRoot), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    auto asset = loadCookedAssetFromCatalog(toUtf8(outRoot), *catalog, clipId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto clip = parseAudioClipFromCooked(*asset);
    ASSERT_TRUE(clip.has_value()) << clip.error().message;
    EXPECT_EQ(clip->sampleRate, 8000U);
    EXPECT_EQ(clip->frameCount, 8U);
    EXPECT_EQ(clip->channels, 1U);
    EXPECT_NEAR(clip->interleavedPcm[1], 8000.0F / 32768.0F, 1.0e-4F);

    std::filesystem::remove_all(dir, ec);
}

// M11-E1: cook canonical unit cube StaticMesh via recipe `staticmesh <id> cube`.
TEST(CatalogCookTests, StaticMeshCubeRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto meshId = *Core::AssetId::fromBytes(idBytes(9U));
    const auto meshHex = meshId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "staticmesh ";
    recipe.append(meshHex.data(), meshHex.size());
    recipe += " cube\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::StaticMesh);
    EXPECT_EQ(request->assets[0].assetId, meshId);

    const auto root = std::filesystem::temp_directory_path() / "tina_staticmesh_cube";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, meshId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto mesh = parseStaticMeshFromCooked(*asset);
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ(mesh->vertexCount, 24U);
    EXPECT_EQ(mesh->indexCount, 36U);
    EXPECT_EQ(mesh->submeshCount, 1U);

    std::filesystem::remove_all(root, ec);
}

// M11-E4: cook UnlitBaseColor Material via recipe `material <id> unlit r g b [a]`.
TEST(CatalogCookTests, MaterialUnlitRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto materialId = *Core::AssetId::fromBytes(idBytes(10U));
    const auto materialHex = materialId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "material ";
    recipe.append(materialHex.data(), materialHex.size());
    recipe += " unlit 0.95 0.24 0.30 1.0\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::Material);
    EXPECT_EQ(request->assets[0].assetId, materialId);

    const auto root = std::filesystem::temp_directory_path() / "tina_material_unlit";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->entryCount(), 1U);

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, materialId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    auto material = parseMaterialFromCooked(*asset);
    ASSERT_TRUE(material.has_value()) << material.error().message;
    EXPECT_EQ(material->model, AssetFormat::MaterialModel::UnlitBaseColor);
    EXPECT_EQ(material->schemaVersion, AssetFormat::MaterialWire::SchemaVersion);
    EXPECT_FLOAT_EQ(material->baseColorR, 0.95F);
    EXPECT_FLOAT_EQ(material->baseColorG, 0.24F);
    EXPECT_FLOAT_EQ(material->baseColorB, 0.30F);
    EXPECT_FLOAT_EQ(material->baseColorA, 1.0F);
    EXPECT_FLOAT_EQ(material->metallicFactor, 1.0F);
    EXPECT_FLOAT_EQ(material->roughnessFactor, 1.0F);
    EXPECT_FALSE(material->hasBaseColorTexture);
    EXPECT_FALSE(material->hasMetallicRoughnessTexture);
    EXPECT_FALSE(material->hasNormalTexture);

    std::filesystem::remove_all(root, ec);
}

// M11-E5: material with optional Texture2D dependency.
TEST(CatalogCookTests, MaterialUnlitWithTextureRecipe)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(11U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(12U));
    const auto textureHex = textureId.canonicalText();
    const auto materialHex = materialId.canonicalText();

    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += " 2 2 FFFFFFFF 000000FF FFFFFFFF 000000FF\n";
    recipe += "material ";
    recipe.append(materialHex.data(), materialHex.size());
    recipe += " unlit 1 1 1 1 ";
    recipe.append(textureHex.data(), textureHex.size());
    recipe += "\n";

    auto request = parseCatalogCookRecipe(recipe, ".");
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 2U);

    const auto root = std::filesystem::temp_directory_path() / "tina_material_tex";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), *request).has_value());

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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    auto asset = loadCookedAssetFromCatalog(toUtf8(root), *catalog, materialId,
                                            CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().dependencyCount, 1U);
    auto dep = asset->dependency(0);
    ASSERT_TRUE(dep.has_value());
    EXPECT_EQ(dep->assetId, textureId);

    auto material = parseMaterialFromCooked(*asset);
    ASSERT_TRUE(material.has_value()) << material.error().message;
    EXPECT_TRUE(material->hasBaseColorTexture);

    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
