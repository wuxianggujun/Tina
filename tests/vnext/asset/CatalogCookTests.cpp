#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

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
    recipe += "row 1 2\n";
    recipe += "row 2 0\n";

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
    EXPECT_EQ(*mapView->tileAt(0, 0), 1U);
    EXPECT_EQ(*mapView->tileAt(1, 1), 0U);

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

} // namespace
} // namespace Tina::Asset
