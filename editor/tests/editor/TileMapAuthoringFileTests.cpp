#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/editor/TileMapAuthoringDocument.hpp>
#include <tina/editor/TileMapAuthoringFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory_resource>
#include <string>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 marker)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

class TileMapAuthoringFileTests : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() /
                 "tina_tile_map_authoring_file_tests";
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    std::filesystem::path m_root{};
};

TEST_F(TileMapAuthoringFileTests, SavesExactCurrentRootAndChunkArtifacts)
{
    auto document = TileMapAuthoringDocument::Create({
        .tileMapId = assetId(0x51U),
        .tilesetId = assetId(0x52U),
        .widthCells = 4,
        .heightCells = 4,
        .cellSizeMeters = 0.5F,
        .chunkSizeCells = 2,
        .layers = {
            TileMapAuthoringLayer{
                .stableLayerId = 7,
                .kind = AssetFormat::TileMapLayerKind::Tile,
                .name = "Ground",
                .chunks = {
                    {.chunkX = 0, .chunkY = 0, .cells = {1, 0, 0, 2}},
                    {.chunkX = 1, .chunkY = 0, .cells = {0, 3, 0, 0}},
                },
            },
        },
    });
    ASSERT_TRUE(document);
    const auto expected = document->cookPreview(
        AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(expected);

    const auto result = saveTileMapAuthoringDocument(
        toUtf8(m_root), *document, AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(result) << (result ? "" : result.error().message);
    EXPECT_EQ(result->artifactCount, expected->artifacts.size());

    Core::u64 expectedBytes = 0;
    for (const auto& artifact : expected->artifacts) {
        expectedBytes += artifact.cookedBytes.size();
        const auto path = m_root / std::filesystem::u8path(artifact.path.view());
        auto saved = Core::readFile(
            toUtf8(path), Core::ReadFileConfig{
                              .maxBytes = 4096,
                              .memoryResource = std::pmr::new_delete_resource(),
                          });
        ASSERT_TRUE(saved) << (saved ? "" : saved.error().message);
        EXPECT_EQ(saved->size(), artifact.cookedBytes.size());
        EXPECT_TRUE(std::equal(saved->begin(), saved->end(),
                               artifact.cookedBytes.begin(),
                               artifact.cookedBytes.end()));
    }
    EXPECT_EQ(result->byteCount, expectedBytes);
}

} // namespace
} // namespace Tina::Editor
