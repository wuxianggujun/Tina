#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/EditorProjectWorkspace.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Tina::Editor {
namespace {

[[nodiscard]] std::string toGenericUtf8(const std::filesystem::path& path)
{
    const auto value = path.lexically_normal().generic_u8string();
    return std::string(value.begin(), value.end());
}

[[nodiscard]] std::string toRawGenericUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return std::string(value.begin(), value.end());
}

[[nodiscard]] EditorProjectWorkspaceDesc workspaceDesc(
    const std::filesystem::path& projectRoot,
    std::string& projectRootUtf8,
    std::string& sourceRootUtf8,
    std::string& catalogRootUtf8)
{
    projectRootUtf8 = toRawGenericUtf8(projectRoot / ".");
    sourceRootUtf8 = toRawGenericUtf8(projectRoot / "authoring" / "draft" / ".." /
                                      "source");
    catalogRootUtf8 = toRawGenericUtf8(projectRoot / "cooked" / "." / "catalog");
    return {
        .projectRootUtf8 = projectRootUtf8,
        .sourceRootUtf8 = sourceRootUtf8,
        .cookedCatalogRootUtf8 = catalogRootUtf8,
        .targetPlatform = AssetFormat::TargetPlatform::LinuxX64,
    };
}

TEST(EditorProjectWorkspaceTests, OwnsCanonicalRootsAndKeepsViewsStable)
{
    const auto projectRoot = std::filesystem::temp_directory_path() /
                             "tina_editor_project_workspace";
    std::string projectRootUtf8;
    std::string sourceRootUtf8;
    std::string catalogRootUtf8;
    const auto desc = workspaceDesc(projectRoot, projectRootUtf8,
                                    sourceRootUtf8, catalogRootUtf8);

    auto workspace = EditorProjectWorkspace::Create(desc);
    ASSERT_TRUE(workspace) << workspace.error().message;
    const auto expectedProjectRoot = toGenericUtf8(projectRoot);
    const auto expectedSourceRoot = toGenericUtf8(projectRoot / "authoring" / "source");
    const auto expectedCatalogRoot = toGenericUtf8(projectRoot / "cooked" / "catalog");
    const auto* projectViewData = workspace->projectRootUtf8().data();
    const auto* sourceViewData = workspace->sourceRootUtf8().data();
    const auto* catalogViewData = workspace->cookedCatalogRootUtf8().data();

    projectRootUtf8.assign("input storage changed");
    sourceRootUtf8.clear();
    catalogRootUtf8.clear();

    EXPECT_EQ(workspace->projectRootUtf8(), expectedProjectRoot);
    EXPECT_EQ(workspace->sourceRootUtf8(), expectedSourceRoot);
    EXPECT_EQ(workspace->cookedCatalogRootUtf8(), expectedCatalogRoot);
    EXPECT_EQ(workspace->targetPlatform(), AssetFormat::TargetPlatform::LinuxX64);
    EXPECT_EQ(workspace->projectRootUtf8().data(), projectViewData);
    EXPECT_EQ(workspace->sourceRootUtf8().data(), sourceViewData);
    EXPECT_EQ(workspace->cookedCatalogRootUtf8().data(), catalogViewData);
}

TEST(EditorProjectWorkspaceTests, RejectsInvalidEncodingCapacityPlatformAndRelativeRoots)
{
    const auto projectRoot = std::filesystem::temp_directory_path() /
                             "tina_editor_project_workspace_invalid";
    std::string projectRootUtf8;
    std::string sourceRootUtf8;
    std::string catalogRootUtf8;
    auto desc = workspaceDesc(projectRoot, projectRootUtf8,
                              sourceRootUtf8, catalogRootUtf8);

    std::string invalidUtf8{"\xC0\xAF", 2U};
    desc.sourceRootUtf8 = invalidUtf8;
    auto invalidEncoding = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(invalidEncoding);
    EXPECT_EQ(invalidEncoding.error().code, EditorErrorCode::InvalidConfiguration);

    desc.sourceRootUtf8 = sourceRootUtf8;
    auto exhausted = EditorProjectWorkspace::Create(
        desc, EditorProjectWorkspaceConfig{.rootPathByteCapacity =
                                               projectRootUtf8.size() - 1U});
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code, EditorErrorCode::InvalidConfiguration);

    desc.projectRootUtf8 = "relative/project";
    auto relative = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(relative);
    EXPECT_EQ(relative.error().code, EditorErrorCode::InvalidConfiguration);

    desc.projectRootUtf8 = projectRootUtf8;
    desc.targetPlatform = static_cast<AssetFormat::TargetPlatform>(99U);
    auto invalidPlatform = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(invalidPlatform);
    EXPECT_EQ(invalidPlatform.error().code, EditorErrorCode::InvalidConfiguration);
}

TEST(EditorProjectWorkspaceTests, RejectsRootsOutsideProjectOrOverlappingEachOther)
{
    const auto projectRoot = std::filesystem::temp_directory_path() /
                             "tina_editor_project_workspace_relations";
    std::string projectRootUtf8;
    std::string sourceRootUtf8;
    std::string catalogRootUtf8;
    auto desc = workspaceDesc(projectRoot, projectRootUtf8,
                              sourceRootUtf8, catalogRootUtf8);

    const auto outsideSource = toGenericUtf8(projectRoot.parent_path() / "outside-source");
    desc.sourceRootUtf8 = outsideSource;
    auto outside = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(outside);
    EXPECT_EQ(outside.error().code, EditorErrorCode::InvalidConfiguration);

    desc.sourceRootUtf8 = sourceRootUtf8;
    desc.cookedCatalogRootUtf8 = sourceRootUtf8;
    auto sameRoot = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(sameRoot);
    EXPECT_EQ(sameRoot.error().code, EditorErrorCode::InvalidConfiguration);

    const auto* sourceFirst = reinterpret_cast<const char8_t*>(sourceRootUtf8.data());
    const auto nestedCatalog = toGenericUtf8(
        std::filesystem::path{std::u8string(sourceFirst,
                                           sourceFirst + sourceRootUtf8.size())} /
        "cooked");
    desc.cookedCatalogRootUtf8 = nestedCatalog;
    auto nested = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(nested);
    EXPECT_EQ(nested.error().code, EditorErrorCode::InvalidConfiguration);

    desc.sourceRootUtf8 = projectRootUtf8;
    desc.cookedCatalogRootUtf8 = catalogRootUtf8;
    auto projectAsSource = EditorProjectWorkspace::Create(desc);
    ASSERT_FALSE(projectAsSource);
    EXPECT_EQ(projectAsSource.error().code, EditorErrorCode::InvalidConfiguration);
}

#if defined(_WIN32)
TEST(EditorProjectWorkspaceTests, RejectsCaseInsensitiveWindowsRootOverlap)
{
    const auto projectRoot = std::filesystem::temp_directory_path() /
                             "tina_editor_project_workspace_case";
    const auto projectRootUtf8 = toGenericUtf8(projectRoot);
    const auto sourceRootUtf8 = toGenericUtf8(projectRoot / "Source");
    const auto catalogRootUtf8 = toGenericUtf8(projectRoot / "source");

    auto workspace = EditorProjectWorkspace::Create({
        .projectRootUtf8 = projectRootUtf8,
        .sourceRootUtf8 = sourceRootUtf8,
        .cookedCatalogRootUtf8 = catalogRootUtf8,
    });

    ASSERT_FALSE(workspace);
    EXPECT_EQ(workspace.error().code, EditorErrorCode::InvalidConfiguration);
}
#endif

} // namespace
} // namespace Tina::Editor
