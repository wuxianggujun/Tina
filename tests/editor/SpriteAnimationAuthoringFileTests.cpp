#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>
#include <tina/editor/SpriteAnimationAuthoringFile.hpp>

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

class SpriteAnimationAuthoringFileTests : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() /
                 "tina_sprite_animation_authoring_file_tests";
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

TEST_F(SpriteAnimationAuthoringFileTests, SavesExactCurrentCookedArtifact)
{
    auto document = SpriteAnimationAuthoringDocument::Create({
        .clipId = assetId(0x50U),
        .playbackMode = AssetFormat::SpriteAnimationPlaybackMode::PingPong,
        .frames = {
            {.spriteId = assetId(0x11U), .durationSeconds = 0.1F},
            {.spriteId = assetId(0x12U), .durationSeconds = 0.2F},
        },
    });
    ASSERT_TRUE(document);
    const auto expected = document->cookPreview(AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(expected);
    const auto path = m_root / "nested" / "walk.tasset";

    ASSERT_TRUE(saveSpriteAnimationAuthoringDocument(
        toUtf8(path), *document, AssetFormat::TargetPlatform::LinuxX64));
    auto saved = Core::readFile(
        toUtf8(path), Core::ReadFileConfig{
                          .maxBytes = 4096,
                          .memoryResource = std::pmr::new_delete_resource(),
                      });
    ASSERT_TRUE(saved) << (saved ? "" : saved.error().message);
    EXPECT_EQ(saved->size(), expected->cookedBytes.size());
    EXPECT_TRUE(std::equal(saved->begin(), saved->end(),
                           expected->cookedBytes.begin(),
                           expected->cookedBytes.end()));

    auto cooked = AssetFormat::parseCookedAssetView(*saved);
    ASSERT_TRUE(cooked);
    EXPECT_EQ(cooked->header().assetKind,
              AssetFormat::AssetKind::SpriteAnimationClip);
    EXPECT_EQ(cooked->header().targetPlatform,
              AssetFormat::TargetPlatform::LinuxX64);
}

} // namespace
} // namespace Tina::Editor
