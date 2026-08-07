#include <tina/editor/EditorProjectCreation.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace Tina::Editor {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto encoded = path.lexically_normal().generic_u8string();
    return std::string(encoded.begin(), encoded.end());
}

class EditorProjectCreationTests : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_testRoot = std::filesystem::temp_directory_path() /
                     "tina_editor_project_creation_tests";
        std::error_code errorCode;
        std::filesystem::remove_all(m_testRoot, errorCode);
        ASSERT_TRUE(std::filesystem::create_directory(m_testRoot, errorCode));
        ASSERT_FALSE(errorCode);
    }

    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove_all(m_testRoot, errorCode);
    }

    std::filesystem::path m_testRoot{};
};

TEST_F(EditorProjectCreationTests, CreatesDefaultEmptyProjectDirectoriesAndWorkspace)
{
    const auto projectRoot = m_testRoot / "DefaultProject";
    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
    });

    ASSERT_TRUE(workspace) << workspace.error().message;
    EXPECT_EQ(workspace->projectRootUtf8(), toUtf8(projectRoot));
    EXPECT_EQ(workspace->sourceRootUtf8(), toUtf8(projectRoot / "Source"));
    EXPECT_EQ(workspace->cookedCatalogRootUtf8(), toUtf8(projectRoot / "Catalog"));
    EXPECT_EQ(workspace->targetPlatform(), AssetFormat::TargetPlatform::WindowsX64);
    EXPECT_TRUE(std::filesystem::is_empty(projectRoot / "Source"));
    EXPECT_TRUE(std::filesystem::is_empty(projectRoot / "Catalog"));
}

TEST_F(EditorProjectCreationTests, AdoptsExistingEmptyRootWithCustomDirectoryNames)
{
    const auto projectRoot = m_testRoot / "CustomProject";
    ASSERT_TRUE(std::filesystem::create_directory(projectRoot));

    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
        .sourceDirectoryUtf8 = "Authoring",
        .cookedCatalogDirectoryUtf8 = "Cooked",
        .targetPlatform = AssetFormat::TargetPlatform::LinuxX64,
    });

    ASSERT_TRUE(workspace) << workspace.error().message;
    EXPECT_EQ(workspace->sourceRootUtf8(), toUtf8(projectRoot / "Authoring"));
    EXPECT_EQ(workspace->cookedCatalogRootUtf8(), toUtf8(projectRoot / "Cooked"));
    EXPECT_EQ(workspace->targetPlatform(), AssetFormat::TargetPlatform::LinuxX64);
    EXPECT_TRUE(std::filesystem::is_directory(projectRoot / "Authoring"));
    EXPECT_TRUE(std::filesystem::is_directory(projectRoot / "Cooked"));
}

TEST_F(EditorProjectCreationTests, RejectsNonEmptyRootWithoutChangingExistingContents)
{
    const auto projectRoot = m_testRoot / "ExistingProject";
    ASSERT_TRUE(std::filesystem::create_directory(projectRoot));
    const auto sentinel = projectRoot / "keep.txt";
    {
        std::ofstream output(sentinel, std::ios::binary);
        ASSERT_TRUE(output);
        output << "keep";
    }

    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
    });

    ASSERT_FALSE(workspace);
    EXPECT_EQ(workspace.error().code, Core::CoreErrorCode::AlreadyExists);
    EXPECT_TRUE(std::filesystem::is_regular_file(sentinel));
    EXPECT_FALSE(std::filesystem::exists(projectRoot / "Source"));
    EXPECT_FALSE(std::filesystem::exists(projectRoot / "Catalog"));
}

TEST_F(EditorProjectCreationTests, RejectsFileAtProjectRootWithoutReplacingIt)
{
    const auto projectRoot = m_testRoot / "ExistingFile";
    {
        std::ofstream output(projectRoot, std::ios::binary);
        ASSERT_TRUE(output);
        output << "keep";
    }

    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
    });

    ASSERT_FALSE(workspace);
    EXPECT_EQ(workspace.error().code, Core::CoreErrorCode::AlreadyExists);
    EXPECT_TRUE(std::filesystem::is_regular_file(projectRoot));
}

TEST_F(EditorProjectCreationTests, RejectsDirectorySymlinkAtProjectRootWithoutTouchingTarget)
{
    const auto targetRoot = m_testRoot / "SymlinkTarget";
    const auto projectRoot = m_testRoot / "SymlinkProject";
    ASSERT_TRUE(std::filesystem::create_directory(targetRoot));
    const auto sentinel = targetRoot / "keep.txt";
    {
        std::ofstream output(sentinel, std::ios::binary);
        ASSERT_TRUE(output);
        output << "keep";
    }

    std::error_code linkError;
    std::filesystem::create_directory_symlink(targetRoot, projectRoot, linkError);
    if (linkError)
    {
        GTEST_SKIP() << "directory symlinks are unavailable: " << linkError.value();
    }

    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
    });

    ASSERT_FALSE(workspace);
    EXPECT_EQ(workspace.error().code, Core::CoreErrorCode::AlreadyExists);
    EXPECT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(projectRoot)));
    EXPECT_TRUE(std::filesystem::is_regular_file(sentinel));
    EXPECT_FALSE(std::filesystem::exists(targetRoot / "Source"));
    EXPECT_FALSE(std::filesystem::exists(targetRoot / "Catalog"));
}

TEST_F(EditorProjectCreationTests, DoesNotFollowExistingSourceDirectorySymlink)
{
    const auto projectRoot = m_testRoot / "SourceSymlinkProject";
    const auto targetRoot = m_testRoot / "ExternalSource";
    ASSERT_TRUE(std::filesystem::create_directory(projectRoot));
    ASSERT_TRUE(std::filesystem::create_directory(targetRoot));

    std::error_code linkError;
    std::filesystem::create_directory_symlink(targetRoot, projectRoot / "Source", linkError);
    if (linkError)
    {
        GTEST_SKIP() << "directory symlinks are unavailable: " << linkError.value();
    }

    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
    });

    ASSERT_FALSE(workspace);
    EXPECT_TRUE(std::filesystem::is_symlink(
        std::filesystem::symlink_status(projectRoot / "Source")));
    EXPECT_TRUE(std::filesystem::is_empty(targetRoot));
    EXPECT_FALSE(std::filesystem::exists(projectRoot / "Catalog"));
}

TEST_F(EditorProjectCreationTests, RejectsInvalidNamesBeforeCreatingProjectRoot)
{
    const auto projectRoot = m_testRoot / "InvalidNames";
    const auto projectRootUtf8 = toUtf8(projectRoot);

    auto nested = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = projectRootUtf8,
        .sourceDirectoryUtf8 = "nested/Source",
    });
    ASSERT_FALSE(nested);
    EXPECT_EQ(nested.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(projectRoot));

    auto traversal = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = projectRootUtf8,
        .cookedCatalogDirectoryUtf8 = "..",
    });
    ASSERT_FALSE(traversal);
    EXPECT_EQ(traversal.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(projectRoot));

    auto overlap = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = projectRootUtf8,
        .sourceDirectoryUtf8 = "Content",
        .cookedCatalogDirectoryUtf8 = "Content",
    });
    ASSERT_FALSE(overlap);
    EXPECT_EQ(overlap.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(projectRoot));

    const std::string invalidUtf8{"\xC0\xAF", 2U};
    auto invalidEncoding = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = projectRootUtf8,
        .sourceDirectoryUtf8 = invalidUtf8,
    });
    ASSERT_FALSE(invalidEncoding);
    EXPECT_EQ(invalidEncoding.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(projectRoot));
}

TEST_F(EditorProjectCreationTests, RejectsInvalidRootAndPlatformBeforeFilesystemMutation)
{
    auto relative = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = "relative/project",
    });
    ASSERT_FALSE(relative);

    const std::string invalidUtf8{"\xC0\xAF", 2U};
    auto invalidEncoding = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = invalidUtf8,
    });
    ASSERT_FALSE(invalidEncoding);
    EXPECT_EQ(invalidEncoding.error().code, Core::CoreErrorCode::InvalidArgument);

    const auto projectRoot = m_testRoot / "InvalidPlatform";
    auto platform = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
        .targetPlatform = AssetFormat::TargetPlatform::Invalid,
    });
    ASSERT_FALSE(platform);
    EXPECT_FALSE(std::filesystem::exists(projectRoot));
}

#if defined(_WIN32)
TEST_F(EditorProjectCreationTests, RejectsCaseInsensitiveWindowsDirectoryOverlap)
{
    const auto projectRoot = m_testRoot / "CaseOverlap";
    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
        .sourceDirectoryUtf8 = "Source",
        .cookedCatalogDirectoryUtf8 = "source",
    });

    ASSERT_FALSE(workspace);
    EXPECT_EQ(workspace.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(projectRoot));
}

TEST_F(EditorProjectCreationTests, RejectsWindowsNamesThatAliasDevicesOrDirectories)
{
    const auto projectRoot = m_testRoot / "WindowsNames";
    for (const std::string_view invalidName : {"Source.", "CON", "com1.txt", "bad:name"})
    {
        auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
            .projectRootUtf8 = toUtf8(projectRoot),
            .sourceDirectoryUtf8 = invalidName,
        });
        ASSERT_FALSE(workspace) << invalidName;
        EXPECT_EQ(workspace.error().code, Core::CoreErrorCode::InvalidArgument);
        EXPECT_FALSE(std::filesystem::exists(projectRoot));
    }
}
#endif

TEST_F(EditorProjectCreationTests, MissingParentFailsWithoutLeavingPartialDirectories)
{
    const auto projectRoot = m_testRoot / "missing" / "Project";
    auto workspace = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(projectRoot),
    });

    ASSERT_FALSE(workspace);
    EXPECT_EQ(workspace.error().code, Core::CoreErrorCode::NotFound);
    EXPECT_FALSE(std::filesystem::exists(m_testRoot / "missing"));
}

TEST_F(EditorProjectCreationTests, CreationFailureRollsBackOnlyDirectoriesOwnedByTransaction)
{
    const std::string overlongDirectoryName(300U, 'c');

    const auto newProjectRoot = m_testRoot / "NewRollbackProject";
    auto newProject = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(newProjectRoot),
        .cookedCatalogDirectoryUtf8 = overlongDirectoryName,
    });
    ASSERT_FALSE(newProject);
    EXPECT_FALSE(std::filesystem::exists(newProjectRoot));

    const auto existingProjectRoot = m_testRoot / "ExistingRollbackProject";
    ASSERT_TRUE(std::filesystem::create_directory(existingProjectRoot));
    auto existingProject = CreateNewEditorProject(EditorProjectCreationRequest{
        .projectRootUtf8 = toUtf8(existingProjectRoot),
        .cookedCatalogDirectoryUtf8 = overlongDirectoryName,
    });
    ASSERT_FALSE(existingProject);
    EXPECT_TRUE(std::filesystem::is_directory(existingProjectRoot));
    EXPECT_TRUE(std::filesystem::is_empty(existingProjectRoot));
}

} // namespace
} // namespace Tina::Editor
