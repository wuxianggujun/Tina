#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/ProjectAssetBrowser.hpp>

#include <gtest/gtest.h>

#include <array>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 marker)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] ProjectAssetDescriptor asset(Core::u8 marker,
                                           AssetFormat::AssetKind kind)
{
    return {
        .assetId = assetId(marker),
        .assetKind = kind,
        .assetTypeVersion = 1,
        .dependencyCount = marker,
        .cookedFileBytes = static_cast<Core::u64>(marker) * 100U,
        .displayName = std::string(projectAssetKindLabel(kind)),
    };
}

TEST(ProjectAssetBrowserTests, OwnsSortedCatalogIndexAndFiltersByWorkspace)
{
    const std::array assets{
        asset(0x40U, AssetFormat::AssetKind::AudioClip),
        asset(0x10U, AssetFormat::AssetKind::Sprite),
        asset(0x30U, AssetFormat::AssetKind::Prefab),
        asset(0x20U, AssetFormat::AssetKind::TileMap),
    };
    auto browser = ProjectAssetBrowserModel::Create(assets);
    ASSERT_TRUE(browser);
    ASSERT_EQ(browser->visibleItemCount(), 4U);
    ASSERT_NE(browser->visibleItem(0), nullptr);
    EXPECT_EQ(browser->visibleItem(0)->assetId, assetId(0x10U));
    EXPECT_EQ(browser->selectedItem()->assetId, assetId(0x10U));

    ASSERT_TRUE(browser->selectAsset(assetId(0x20U)));
    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::TwoD));
    ASSERT_EQ(browser->visibleItemCount(), 2U);
    ASSERT_TRUE(browser->selectedVisibleIndex());
    EXPECT_EQ(browser->selectedItem()->assetId, assetId(0x20U));
    EXPECT_EQ(projectAssetOpenKind(browser->selectedItem()->assetKind),
              ProjectAssetOpenKind::TileMap2D);

    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::ThreeD));
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->selectedItem()->assetKind, AssetFormat::AssetKind::Prefab);
    EXPECT_EQ(projectAssetKindLabel(browser->selectedItem()->assetKind), "Prefab");
}

TEST(ProjectAssetBrowserTests, RejectsCapacityDuplicatesAndInvisibleSelectionAtomically)
{
    const std::array assets{
        asset(0x10U, AssetFormat::AssetKind::Sprite),
        asset(0x20U, AssetFormat::AssetKind::StaticMesh),
    };
    auto exhausted = ProjectAssetBrowserModel::Create(
        assets, ProjectAssetBrowserConfig{.itemCapacity = 1});
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, EditorErrorCode::ProjectAssetCapacityExceeded);

    const std::array duplicates{
        asset(0x10U, AssetFormat::AssetKind::Sprite),
        asset(0x10U, AssetFormat::AssetKind::Texture2D),
    };
    auto duplicate = ProjectAssetBrowserModel::Create(duplicates);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, EditorErrorCode::InvalidConfiguration);

    auto browser = ProjectAssetBrowserModel::Create(assets);
    ASSERT_TRUE(browser);
    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::TwoD));
    const auto before = browser->selectedItem()->assetId;
    const auto missing = browser->selectAsset(assetId(0x20U));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, EditorErrorCode::ProjectAssetNotFound);
    EXPECT_EQ(browser->selectedItem()->assetId, before);
}

} // namespace
} // namespace Tina::Editor
