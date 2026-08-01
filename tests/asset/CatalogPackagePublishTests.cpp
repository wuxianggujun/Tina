#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

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

TEST(CatalogPackagePublishTests, PublishThenOpenValidates)
{
    std::pmr::unsynchronized_pool_resource memory;
    constexpr std::array<std::byte, 4> Payload{std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}};
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));
    const std::array deps{AssetFormat::CookedAssetWriteDependency{
        .assetId = textureId,
        .expectedKind = AssetFormat::AssetKind::Texture2D,
        .flags = AssetFormat::DependencyFlags::Required,
    }};

    auto texture = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = Payload,
        .computeContentHash = true,
    });
    auto material = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::Material,
        .assetId = materialId,
        .dependencies = deps,
        .payload = Payload,
        .computeContentHash = true,
    });
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());
    const auto hash = *Core::digestContentHashV1(Payload);
    const std::array entries{
        AssetFormat::CookedManifestWriteEntry{
            .assetId = textureId,
            .contentHash = hash,
            .assetKind = AssetFormat::AssetKind::Texture2D,
            .cookedFileBytes = texture->size(),
        },
        AssetFormat::CookedManifestWriteEntry{
            .assetId = materialId,
            .contentHash = hash,
            .assetKind = AssetFormat::AssetKind::Material,
            .cookedFileBytes = material->size(),
            .dependencies = deps,
        },
    };
    auto manifest = AssetFormat::writeCookedManifestBytes(AssetFormat::CookedManifestWriteDesc{
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .entries = entries,
    });
    ASSERT_TRUE(manifest.has_value());

    const auto root = std::filesystem::temp_directory_path()
        / std::filesystem::path{u8"tina_publish_catalog_\u76ee\u5f55"};
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const std::array objects{
        CatalogPackageObjectBlob{.assetKind = AssetFormat::AssetKind::Texture2D, .assetId = textureId, .bytes = *texture},
        CatalogPackageObjectBlob{.assetKind = AssetFormat::AssetKind::Material, .assetId = materialId, .bytes = *material},
    };
    ASSERT_TRUE(publishCatalogPackage(toUtf8(root), "manifest.tmnft", *manifest, objects).has_value());

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

} // namespace
} // namespace Tina::Asset
