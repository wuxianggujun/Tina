#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World2DAuthoringFile.hpp>

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

[[nodiscard]] Core::ReadFileConfig readConfig() noexcept
{
    return {
        .maxBytes = 4096,
        .memoryResource = std::pmr::new_delete_resource(),
    };
}

class World2DAuthoringFileTests : public ::testing::Test {
  protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() / "tina_world2d_authoring_file_tests";
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

TEST_F(World2DAuthoringFileTests, SavesCanonicalSnapshotAndCreatesParentDirectories)
{
    auto document = World2DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{
        .stableEntityId = 7,
        .positionX = 3.0F,
    }));

    const auto path = m_root / "nested" / "world.tworld";
    ASSERT_TRUE(saveWorld2DAuthoringDocument(toUtf8(path), *document));

    auto saved = Core::readFile(toUtf8(path), readConfig());
    ASSERT_TRUE(saved) << (saved ? "" : saved.error().message);
    EXPECT_EQ(std::vector(saved->begin(), saved->end()),
              std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end()));
}

TEST_F(World2DAuthoringFileTests, AtomicallyOverwritesWithLatestPublishedRevision)
{
    auto document = World2DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    const auto path = m_root / "world.tworld";

    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(saveWorld2DAuthoringDocument(toUtf8(path), *document));

    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{
        .stableEntityId = 2,
        .parentStableEntityId = 1,
    }));
    const auto expected = std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end());
    ASSERT_TRUE(saveWorld2DAuthoringDocument(toUtf8(path), *document));

    auto saved = Core::readFile(toUtf8(path), readConfig());
    ASSERT_TRUE(saved) << (saved ? "" : saved.error().message);
    EXPECT_EQ(std::vector(saved->begin(), saved->end()), expected);
}

TEST_F(World2DAuthoringFileTests, RejectsInvalidPathWithoutMutatingDocument)
{
    auto document = World2DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 1}));
    const auto before = std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end());
    const Core::u64 revision = document->revision();

    const auto status = saveWorld2DAuthoringDocument({}, *document);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(document->revision(), revision);
    EXPECT_EQ(std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end()), before);
}

TEST_F(World2DAuthoringFileTests, LoadsCanonicalSnapshotAsNewBaseline)
{
    auto source = World2DAuthoringDocument::Create();
    ASSERT_TRUE(source);
    ASSERT_TRUE(source->upsertEntity(AssetFormat::World2DEntityDesc{
        .stableEntityId = 9,
        .positionX = 2.5F,
        .positionY = -1.25F,
    }));
    const auto path = m_root / "load" / "world.tworld";
    ASSERT_TRUE(saveWorld2DAuthoringDocument(toUtf8(path), *source));

    auto target = World2DAuthoringDocument::Create();
    ASSERT_TRUE(target);
    ASSERT_TRUE(target->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(target->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 2}));
    ASSERT_TRUE(target->undo());
    ASSERT_TRUE(target->canUndo());
    ASSERT_TRUE(target->canRedo());

    ASSERT_TRUE(loadWorld2DAuthoringDocument(toUtf8(path), *target));
    EXPECT_EQ(std::vector(target->snapshotBytes().begin(), target->snapshotBytes().end()),
              std::vector(source->snapshotBytes().begin(), source->snapshotBytes().end()));
    EXPECT_EQ(target->entityCount(), 1U);
    EXPECT_EQ(target->historyEntryCount(), 1U);
    EXPECT_FALSE(target->canUndo());
    EXPECT_FALSE(target->canRedo());
}

TEST_F(World2DAuthoringFileTests, LoadFailurePreservesDocumentAndHistory)
{
    const auto path = m_root / "invalid" / "world.tworld";
    const std::array invalidBytes{std::byte{0x54}, std::byte{0x49}, std::byte{0x4E}};
    ASSERT_TRUE(Core::writeFile(toUtf8(path), invalidBytes,
                                Core::WriteFileConfig{.createParents = true}));

    auto document = World2DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 2}));
    ASSERT_TRUE(document->undo());
    const auto before = std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end());
    const Core::u64 revision = document->revision();
    const Core::usize undoDepth = document->undoDepth();
    const Core::usize redoDepth = document->redoDepth();

    const auto status = loadWorld2DAuthoringDocument(toUtf8(path), *document);
    ASSERT_FALSE(status);
    EXPECT_EQ(std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end()), before);
    EXPECT_EQ(document->revision(), revision);
    EXPECT_EQ(document->undoDepth(), undoDepth);
    EXPECT_EQ(document->redoDepth(), redoDepth);
}

TEST_F(World2DAuthoringFileTests, MissingLoadPathPreservesDocument)
{
    auto document = World2DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->upsertEntity(AssetFormat::World2DEntityDesc{.stableEntityId = 1}));
    const auto before = std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end());
    const Core::u64 revision = document->revision();

    const auto status = loadWorld2DAuthoringDocument(toUtf8(m_root / "missing.tworld"), *document);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::NotFound);
    EXPECT_EQ(std::vector(document->snapshotBytes().begin(), document->snapshotBytes().end()), before);
    EXPECT_EQ(document->revision(), revision);
}

} // namespace
} // namespace Tina::Editor
