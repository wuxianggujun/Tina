#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

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

TEST(TypedPayloadValidationTests, AcceptsTypedTextureWhenEnabled)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    std::vector<std::byte> pixels(4, std::byte{9});
    auto payload = AssetFormat::writeTexture2DPayloadBytes(
        AssetFormat::Texture2DPayloadDesc{.width = 1, .height = 1, .pixels = pixels});
    ASSERT_TRUE(payload.has_value());

    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .assetTypeVersion = 1,
        .payload = std::move(*payload),
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_ok";
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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    std::filesystem::remove_all(root, ec);
}

TEST(TypedPayloadValidationTests, RejectsRawTextureWhenTypedRequired)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = {std::byte{'r'}, std::byte{'a'}, std::byte{'w'}},
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_bad";
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
                .verifyTypedPayload = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_FALSE(catalog.has_value());

    // Without typed check, raw payload still opens.
    openConfig.validation.verifyTypedPayload = false;
    auto catalogRaw = openCatalogPackage(toUtf8(root), openConfig);
    ASSERT_TRUE(catalogRaw.has_value()) << catalogRaw.error().message;

    std::filesystem::remove_all(root, ec);
}

TEST(TypedPayloadValidationTests, RejectsPrefabDependencySetMissingPayloadReferences)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto prefabId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto meshId = *Core::AssetId::fromBytes(idBytes(2U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(3U));
    const std::array nodes{AssetFormat::PrefabNodeDesc{
        .stableNodeId = 1,
        .meshId = meshId,
        .materialId = materialId,
    }};
    auto payload = AssetFormat::writePrefabPayloadBytes(AssetFormat::PrefabPayloadDesc{.nodes = nodes});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    CatalogCookRequest request{.targetPlatform = AssetFormat::TargetPlatform::WindowsX64};
    request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Prefab,
        .assetId = prefabId,
        .assetTypeVersion = AssetFormat::PrefabWire::SchemaVersion,
        .payload = std::move(*payload),
    });

    const auto root = std::filesystem::temp_directory_path() / "tina_typed_validate_prefab_deps";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(cookAndPublishCatalogPackage(toUtf8(root), request).has_value());

    auto catalog = openCatalogPackage(
        toUtf8(root),
        CatalogPackageOpenConfig{
            .manifest = CatalogFileLoadConfig{.catalog = CatalogConfig{.maxEntries = 8,
                                                                        .maxDependencies = 8,
                                                                        .maxDependenciesPerAsset = 4,
                                                                        .memoryResource = &memory}},
            .validateOnOpen = true,
            .validation = CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
        });
    ASSERT_FALSE(catalog.has_value());
    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace Tina::Asset
