#include <tina/editor/EditorDocumentTabs.hpp>
#include <tina/editor/EditorErrors.hpp>

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

TEST(EditorDocumentTabsTests, DeduplicatesActivatesAndClosesCatalogDocuments)
{
    const std::array initial{
        EditorDocumentTabDesc{
            .key = {.kind = EditorDocumentKind::World2D},
            .title = "World2D",
            .pinned = true,
        },
        EditorDocumentTabDesc{
            .key = {.kind = EditorDocumentKind::World3D},
            .title = "World3D",
            .pinned = true,
        },
    };
    auto tabs = EditorDocumentTabs::Create(
        initial, EditorDocumentTabsConfig{.tabCapacity = 4});
    ASSERT_TRUE(tabs);
    EXPECT_EQ(tabs->activeIndex(), 0U);

    const EditorDocumentTabDesc prefab{
        .key = {.kind = EditorDocumentKind::World3D, .assetId = assetId(0x33U)},
        .title = "Prefab 33000000",
    };
    auto opened = tabs->open(prefab);
    ASSERT_TRUE(opened);
    EXPECT_EQ(*opened, 2U);
    EXPECT_EQ(tabs->activeIndex(), 2U);
    EXPECT_EQ(editorDocumentWorkspace(tabs->activeTab()->key.kind),
              EditorDocumentWorkspace::ThreeD);

    auto reopened = tabs->open(prefab);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(*reopened, 2U);
    EXPECT_EQ(tabs->tabCount(), 3U);

    ASSERT_TRUE(tabs->setDirty(2U, true));
    const auto guarded = tabs->close(2U);
    ASSERT_FALSE(guarded);
    EXPECT_EQ(guarded.error().code,
              EditorErrorCode::DirtyDocumentRequiresConfirmation);
    EXPECT_EQ(tabs->tabCount(), 3U);
    ASSERT_TRUE(tabs->close(2U, true));
    EXPECT_EQ(tabs->activeIndex(), 1U);
}

TEST(EditorDocumentTabsTests, EnforcesPinnedAndFixedCapacityContracts)
{
    const std::array initial{
        EditorDocumentTabDesc{
            .key = {.kind = EditorDocumentKind::World2D},
            .title = "World2D",
            .pinned = true,
        },
    };
    auto tabs = EditorDocumentTabs::Create(
        initial, EditorDocumentTabsConfig{.tabCapacity = 2});
    ASSERT_TRUE(tabs);
    const auto pinned = tabs->close(0U, true);
    ASSERT_FALSE(pinned);
    EXPECT_EQ(pinned.error().code, EditorErrorCode::PinnedDocumentCannotClose);

    ASSERT_TRUE(tabs->open({
        .key = {.kind = EditorDocumentKind::TileMap2D, .assetId = assetId(0x44U)},
        .title = "TileMap 44000000",
    }));
    auto exhausted = tabs->open({
        .key = {.kind = EditorDocumentKind::SpriteAnimation2D,
                .assetId = assetId(0x55U)},
        .title = "Animation 55000000",
    });
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, EditorErrorCode::DocumentTabCapacityExceeded);
    EXPECT_EQ(tabs->tabCount(), 2U);
}

} // namespace
} // namespace Tina::Editor
