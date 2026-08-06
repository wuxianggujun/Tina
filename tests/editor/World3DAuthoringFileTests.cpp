#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>
#include <tina/editor/World3DAuthoringFile.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

class World3DAuthoringFileTests : public ::testing::Test {
  protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() / "tina_world3d_authoring_file_tests";
        std::error_code errorCode;
        std::filesystem::remove_all(m_root, errorCode);
    }

    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove_all(m_root, errorCode);
    }

    std::filesystem::path m_root{};
};

TEST_F(World3DAuthoringFileTests, SavesAndAtomicallyOverwritesCanonicalPrefab)
{
    auto document = World3DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    const auto path = m_root / "nested" / "world.tprefab";

    ASSERT_TRUE(document->upsertNode(AssetFormat::PrefabNodeDesc{
        .stableNodeId = 2,
        .parentIndex = 0,
        .positionZ = -2.0F,
    }));
    ASSERT_TRUE(saveWorld3DAuthoringDocument(toUtf8(path), *document));

    ASSERT_TRUE(document->upsertNode(AssetFormat::PrefabNodeDesc{
        .stableNodeId = 2,
        .parentIndex = 0,
        .positionX = 3.0F,
        .positionY = 4.0F,
        .positionZ = 5.0F,
    }));
    const auto expected = std::vector(document->payloadBytes().begin(), document->payloadBytes().end());
    ASSERT_TRUE(saveWorld3DAuthoringDocument(toUtf8(path), *document));

    auto saved = Core::readFile(toUtf8(path), Core::ReadFileConfig{
                                                  .maxBytes = 4096,
                                                  .memoryResource = std::pmr::new_delete_resource(),
                                              });
    ASSERT_TRUE(saved) << (saved ? "" : saved.error().message);
    EXPECT_EQ(std::vector(saved->begin(), saved->end()), expected);
}

TEST_F(World3DAuthoringFileTests, LoadsCanonicalPrefabAsCleanBaseline)
{
    auto source = World3DAuthoringDocument::Create();
    ASSERT_TRUE(source);
    ASSERT_TRUE(source->upsertNode(AssetFormat::PrefabNodeDesc{
        .stableNodeId = 2,
        .parentIndex = 0,
        .positionZ = 7.0F,
        .scaleZ = 1.5F,
    }));
    const auto path = m_root / "load" / "world.tprefab";
    ASSERT_TRUE(saveWorld3DAuthoringDocument(toUtf8(path), *source));

    auto target = World3DAuthoringDocument::Create();
    ASSERT_TRUE(target);
    ASSERT_TRUE(target->upsertNode(AssetFormat::PrefabNodeDesc{.stableNodeId = 3, .parentIndex = 0}));
    ASSERT_TRUE(target->undo());
    ASSERT_TRUE(target->canRedo());

    ASSERT_TRUE(loadWorld3DAuthoringDocument(toUtf8(path), *target));
    EXPECT_EQ(std::vector(target->payloadBytes().begin(), target->payloadBytes().end()),
              std::vector(source->payloadBytes().begin(), source->payloadBytes().end()));
    EXPECT_EQ(target->nodeCount(), 2U);
    EXPECT_EQ(target->historyEntryCount(), 1U);
    EXPECT_FALSE(target->canUndo());
    EXPECT_FALSE(target->canRedo());
}

TEST_F(World3DAuthoringFileTests, LoadFailurePreservesDocumentAndHistory)
{
    const auto path = m_root / "invalid" / "world.tprefab";
    const std::array invalidBytes{std::byte{0x54}, std::byte{0x49}, std::byte{0x4E}};
    ASSERT_TRUE(Core::writeFile(toUtf8(path), invalidBytes,
                                Core::WriteFileConfig{.createParents = true}));

    auto document = World3DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->upsertNode(AssetFormat::PrefabNodeDesc{.stableNodeId = 2, .parentIndex = 0}));
    ASSERT_TRUE(document->upsertNode(AssetFormat::PrefabNodeDesc{.stableNodeId = 3, .parentIndex = 0}));
    ASSERT_TRUE(document->undo());
    const auto before = std::vector(document->payloadBytes().begin(), document->payloadBytes().end());
    const Core::u64 revision = document->revision();
    const Core::usize undoDepth = document->undoDepth();
    const Core::usize redoDepth = document->redoDepth();

    const auto status = loadWorld3DAuthoringDocument(toUtf8(path), *document);
    ASSERT_FALSE(status);
    EXPECT_EQ(std::vector(document->payloadBytes().begin(), document->payloadBytes().end()), before);
    EXPECT_EQ(document->revision(), revision);
    EXPECT_EQ(document->undoDepth(), undoDepth);
    EXPECT_EQ(document->redoDepth(), redoDepth);
}

} // namespace
} // namespace Tina::Editor
