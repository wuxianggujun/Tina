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
        .dependencyCount = 0,
        .cookedFileBytes = static_cast<Core::u64>(marker) * 100U,
        .displayName = std::string(projectAssetKindLabel(kind)),
    };
}

[[nodiscard]] AssetFormat::AssetDependency dependency(
    Core::u8 marker, AssetFormat::AssetKind expectedKind,
    AssetFormat::DependencyFlags flags = AssetFormat::DependencyFlags::Required)
{
    return {
        .assetId = assetId(marker),
        .expectedKind = expectedKind,
        .flags = flags,
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
    EXPECT_EQ(browser->selectedItem(), nullptr);
    ASSERT_NE(browser->selectedInspectorSnapshot(), nullptr);
    EXPECT_EQ(browser->selectedInspectorSnapshot()->assetKind,
              AssetFormat::AssetKind::TileMap);
}

TEST(ProjectAssetBrowserTests, SearchesNameAndKindWithStableKeys)
{
    auto texture = asset(0x10U, AssetFormat::AssetKind::Texture2D);
    texture.displayName = "Hero Diffuse";
    auto sprite = asset(0x20U, AssetFormat::AssetKind::Sprite);
    sprite.displayName = "Player Sprite";
    auto audio = asset(0x30U, AssetFormat::AssetKind::AudioClip);
    audio.displayName = "Music Theme";
    auto prefab = asset(0x40U, AssetFormat::AssetKind::Prefab);
    prefab.displayName = "World Root";
    const std::array assets{audio, prefab, sprite, texture};

    auto browser = ProjectAssetBrowserModel::Create(assets);
    ASSERT_TRUE(browser);
    ASSERT_EQ(browser->visibleItemCount(), 4U);
    EXPECT_EQ(browser->visibleItemStableKey(0U), 1U);
    EXPECT_EQ(browser->visibleItemStableKey(1U), 2U);
    EXPECT_EQ(browser->visibleItemStableKey(2U), 3U);
    EXPECT_EQ(browser->visibleItemStableKey(3U), 4U);
    EXPECT_EQ(browser->visibleItemStableKey(4U), 0U);

    ASSERT_TRUE(browser->selectAsset(assetId(0x40U)));
    ASSERT_TRUE(browser->setSearchQuery("hero"));
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->visibleItem(0U)->assetId, assetId(0x10U));
    EXPECT_EQ(browser->visibleItemStableKey(0U), 1U);
    EXPECT_EQ(browser->selectedItem(), nullptr);
    ASSERT_NE(browser->selectedInspectorSnapshot(), nullptr);
    EXPECT_EQ(browser->selectedInspectorSnapshot()->assetId, assetId(0x40U));

    ASSERT_TRUE(browser->setSearchQuery("AUDIO"));
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->visibleItem(0U)->assetId, assetId(0x30U));
    EXPECT_EQ(browser->visibleItemStableKey(0U), 3U);

    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::TwoD));
    ASSERT_EQ(browser->visibleItemCount(), 0U);
    EXPECT_EQ(browser->selectedItem(), nullptr);

    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::Media));
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->visibleItem(0U)->assetId, assetId(0x30U));
    EXPECT_EQ(browser->selectedItem(), nullptr);
    ASSERT_TRUE(browser->setSearchQuery("diff"));
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->visibleItem(0U)->assetId, assetId(0x10U));
    EXPECT_EQ(browser->visibleItemStableKey(0U), 1U);
    EXPECT_EQ(browser->selectedItem(), nullptr);

    ASSERT_TRUE(browser->setSearchQuery(""));
    ASSERT_EQ(browser->visibleItemCount(), 2U);
    EXPECT_EQ(browser->visibleItemStableKey(0U), 1U);
    EXPECT_EQ(browser->visibleItemStableKey(1U), 3U);

    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::All));
    ASSERT_TRUE(browser->setSearchQuery(""));
    ASSERT_NE(browser->selectedItem(), nullptr);
    EXPECT_EQ(browser->selectedItem()->assetId, assetId(0x40U));
}

TEST(ProjectAssetBrowserTests, RejectsInvalidSearchUtf8AtomicallyAndRestoresSelection)
{
    auto sprite = asset(0x10U, AssetFormat::AssetKind::Sprite);
    sprite.displayName = "Player";
    auto texture = asset(0x20U, AssetFormat::AssetKind::Texture2D);
    texture.displayName = "Texture";
    const std::array assets{sprite, texture};

    auto browser = ProjectAssetBrowserModel::Create(assets);
    ASSERT_TRUE(browser);
    ASSERT_TRUE(browser->selectAsset(assetId(0x20U)));
    ASSERT_TRUE(browser->setSearchQuery("player"));
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->selectedItem(), nullptr);
    ASSERT_NE(browser->selectedInspectorSnapshot(), nullptr);
    EXPECT_EQ(browser->selectedInspectorSnapshot()->assetId, assetId(0x20U));

    const std::array<char, 2> invalidUtf8{static_cast<char>(0xc3), '('};
    const auto beforeQuery = std::string(browser->searchQuery());
    const auto invalid = browser->setSearchQuery(
        std::string_view(invalidUtf8.data(), invalidUtf8.size()));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::InvalidConfiguration);
    EXPECT_EQ(browser->searchQuery(), beforeQuery);
    ASSERT_EQ(browser->visibleItemCount(), 1U);
    EXPECT_EQ(browser->visibleItem(0U)->assetId, assetId(0x10U));
    EXPECT_EQ(browser->selectedItem(), nullptr);

    ASSERT_TRUE(browser->setSearchQuery(""));
    ASSERT_EQ(browser->visibleItemCount(), 2U);
    EXPECT_EQ(browser->selectedItem()->assetId, assetId(0x20U));
}

TEST(ProjectAssetBrowserTests, RestoresHiddenSelectionAcrossCatalogRebuild)
{
    const std::array assets{
        asset(0x10U, AssetFormat::AssetKind::Sprite),
        asset(0x20U, AssetFormat::AssetKind::StaticMesh),
    };

    auto browser = ProjectAssetBrowserModel::Create(assets);
    ASSERT_TRUE(browser);
    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::TwoD));
    ASSERT_TRUE(browser->restoreAssetSelection(assetId(0x20U)));
    EXPECT_EQ(browser->selectedItem(), nullptr);
    ASSERT_TRUE(browser->selectedAssetId());
    EXPECT_EQ(*browser->selectedAssetId(), assetId(0x20U));
    ASSERT_NE(browser->selectedInspectorSnapshot(), nullptr);
    EXPECT_EQ(browser->selectedInspectorSnapshot()->assetKind,
              AssetFormat::AssetKind::StaticMesh);

    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::ThreeD));
    ASSERT_NE(browser->selectedItem(), nullptr);
    EXPECT_EQ(browser->selectedItem()->assetId, assetId(0x20U));

    const auto missing = browser->restoreAssetSelection(assetId(0x30U));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, EditorErrorCode::ProjectAssetNotFound);
    ASSERT_TRUE(browser->selectedAssetId());
    EXPECT_EQ(*browser->selectedAssetId(), assetId(0x20U));
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

    const auto invalidVisible = browser->selectVisibleIndex(4U);
    ASSERT_FALSE(invalidVisible);
    EXPECT_EQ(browser->selectedItem()->assetId, before);

    const auto invalidFilter =
        browser->setFilter(static_cast<ProjectAssetFilter>(0xffU));
    ASSERT_FALSE(invalidFilter);
    EXPECT_EQ(browser->filter(), ProjectAssetFilter::TwoD);
    EXPECT_EQ(browser->selectedItem()->assetId, before);
}

TEST(ProjectAssetBrowserTests, OwnsCanonicalInspectorMetadataAndSortedDependencies)
{
    auto sprite = asset(0x30U, AssetFormat::AssetKind::Sprite);
    sprite.dependencyCount = 2U;
    sprite.canonicalRelativeCookedPath = "caller/value/is/not/trusted";
    sprite.dependencies = {
        dependency(0x20U, AssetFormat::AssetKind::Material,
                   AssetFormat::DependencyFlags::Required |
                       AssetFormat::DependencyFlags::Deferred),
        dependency(0x10U, AssetFormat::AssetKind::Texture2D),
    };
    std::array assets{
        sprite,
        asset(0x10U, AssetFormat::AssetKind::Texture2D),
        asset(0x20U, AssetFormat::AssetKind::Material),
    };

    auto browser = ProjectAssetBrowserModel::Create(assets);
    ASSERT_TRUE(browser);
    const auto* snapshot = browser->inspectorSnapshot(assetId(0x30U));
    ASSERT_NE(snapshot, nullptr);
    const auto expectedPath = AssetFormat::makeCookedArtifactPath(
        AssetFormat::AssetKind::Sprite, assetId(0x30U));
    ASSERT_TRUE(expectedPath);
    EXPECT_EQ(snapshot->canonicalRelativeCookedPath, expectedPath->view());
    ASSERT_EQ(snapshot->dependencies.size(), 2U);
    EXPECT_EQ(snapshot->dependencies[0].assetId, assetId(0x10U));
    EXPECT_EQ(snapshot->dependencies[1].assetId, assetId(0x20U));

    assets[0].displayName = "mutated";
    assets[0].dependencies.clear();
    EXPECT_EQ(snapshot->displayName, "Sprite");
    EXPECT_EQ(snapshot->dependencies.size(), 2U);

    ASSERT_TRUE(browser->selectAsset(assetId(0x30U)));
    EXPECT_EQ(browser->selectedInspectorSnapshot(), snapshot);
    ASSERT_TRUE(browser->setFilter(ProjectAssetFilter::ThreeD));
    EXPECT_EQ(browser->inspectorSnapshot(assetId(0x30U)), snapshot);
    EXPECT_EQ(browser->selectedInspectorSnapshot()->assetKind,
              AssetFormat::AssetKind::Sprite);
}

TEST(ProjectAssetBrowserTests, RejectsInvalidInspectorMetadataAndDependencyGraph)
{
    const auto texture = asset(0x10U, AssetFormat::AssetKind::Texture2D);
    auto sprite = asset(0x20U, AssetFormat::AssetKind::Sprite);
    sprite.dependencyCount = 1U;
    sprite.dependencies = {dependency(0x10U, AssetFormat::AssetKind::Texture2D)};

    const std::array validAssets{texture, sprite};
    auto exhausted = ProjectAssetBrowserModel::Create(
        validAssets, ProjectAssetBrowserConfig{
                         .itemCapacity = 2U,
                         .dependencyCapacity = 0U,
                         .dependencyCapacityPerAsset = 0U,
                     });
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, EditorErrorCode::ProjectAssetCapacityExceeded);

    auto mismatchedCount = sprite;
    mismatchedCount.dependencyCount = 0U;
    const std::array countAssets{texture, mismatchedCount};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(countAssets));

    auto invalidVersion = texture;
    invalidVersion.assetTypeVersion = 0U;
    const std::array versionAssets{invalidVersion, sprite};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(versionAssets));

    auto invalidBytes = texture;
    invalidBytes.cookedFileBytes = 0U;
    const std::array byteAssets{invalidBytes, sprite};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(byteAssets));

    auto selfReferencing = sprite;
    selfReferencing.dependencies = {
        dependency(0x20U, AssetFormat::AssetKind::Sprite),
    };
    const std::array selfAssets{texture, selfReferencing};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(selfAssets));

    auto duplicateDependency = sprite;
    duplicateDependency.dependencyCount = 2U;
    duplicateDependency.dependencies = {
        dependency(0x10U, AssetFormat::AssetKind::Texture2D),
        dependency(0x10U, AssetFormat::AssetKind::Texture2D),
    };
    const std::array duplicateDependencyAssets{texture, duplicateDependency};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(duplicateDependencyAssets));

    auto missingTarget = sprite;
    missingTarget.dependencies = {
        dependency(0x30U, AssetFormat::AssetKind::Texture2D),
    };
    const std::array missingAssets{texture, missingTarget};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(missingAssets));

    auto wrongKind = sprite;
    wrongKind.dependencies = {
        dependency(0x10U, AssetFormat::AssetKind::Material),
    };
    const std::array wrongKindAssets{texture, wrongKind};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(wrongKindAssets));

    auto invalidFlags = sprite;
    invalidFlags.dependencies = {
        dependency(0x10U, AssetFormat::AssetKind::Texture2D,
                   AssetFormat::DependencyFlags::None),
    };
    const std::array invalidFlagAssets{texture, invalidFlags};
    EXPECT_FALSE(ProjectAssetBrowserModel::Create(invalidFlagAssets));
}

} // namespace
} // namespace Tina::Editor
