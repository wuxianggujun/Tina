#include "EditorSourceImportLaunchOptions.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace Detail = Tina::EditorApp::Detail;

namespace {

#if defined(_WIN32)
inline constexpr std::string_view ProjectRoot = "C:/TinaProject";
inline constexpr std::string_view RecipePath = "C:/TinaProject/Source/game.recipe";
inline constexpr std::string_view GltfPath = "C:/TinaProject/Source/hero.glb";
#else
inline constexpr std::string_view ProjectRoot = "/TinaProject";
inline constexpr std::string_view RecipePath = "/TinaProject/Source/game.recipe";
inline constexpr std::string_view GltfPath = "/TinaProject/Source/hero.glb";
#endif

[[nodiscard]] std::string argument(std::string_view name, std::string_view path)
{
    std::string result{name};
    result += path;
    return result;
}

} // namespace

TEST(EditorSourceImportLaunchOptionsTests, RetainsMixedIntendedUnitSetInCallerOrder)
{
    Detail::EditorSourceImportLaunchOptions options{};
    const std::array arguments{
        argument("--project-root=", ProjectRoot),
        argument("--import-recipe=", RecipePath),
        argument("--import-gltf=", GltfPath),
        std::string{"--import-on-start"},
    };
    for (const auto& current : arguments) {
        auto parsed = Detail::parseEditorSourceImportLaunchOption(current, options);
        ASSERT_TRUE(parsed);
        EXPECT_TRUE(*parsed);
    }
    ASSERT_TRUE(Detail::validateEditorSourceImportLaunchOptions(options));

    ASSERT_EQ(options.intendedUnits.size(), 2U);
    EXPECT_EQ(options.intendedUnits[0].kind,
              Detail::EditorSourceImportLaunchUnitKind::CatalogRecipe);
    EXPECT_EQ(options.intendedUnits[1].kind,
              Detail::EditorSourceImportLaunchUnitKind::Gltf);
    EXPECT_TRUE(options.importOnStart);
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsDuplicateUnitWithoutChangingSet)
{
    Detail::EditorSourceImportLaunchOptions options{};
    auto initial = Detail::parseEditorSourceImportLaunchOption(
        argument("--import-gltf=", GltfPath), options);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(*initial);

    const auto duplicate = Detail::parseEditorSourceImportLaunchOption(
        argument("--import-gltf=", GltfPath), options);

    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(options.intendedUnits.size(), 1U);
}

TEST(EditorSourceImportLaunchOptionsTests, AutomaticImportRequiresProjectAndUnits)
{
    Detail::EditorSourceImportLaunchOptions options{};
    auto parsed = Detail::parseEditorSourceImportLaunchOption(
        "--import-on-start", options);
    ASSERT_TRUE(parsed);
    ASSERT_TRUE(*parsed);

    EXPECT_FALSE(Detail::validateEditorSourceImportLaunchOptions(options));
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsEquivalentDuplicatePathsWithoutMutation)
{
    Detail::EditorSourceImportLaunchOptions options{};
    auto initial = Detail::parseEditorSourceImportLaunchOption(
        argument("--import-gltf=", GltfPath), options);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(*initial);

#if defined(_WIN32)
    constexpr std::string_view equivalent =
        "c:\\TINAPROJECT\\Source\\nested\\..\\hero.glb";
#else
    constexpr std::string_view equivalent =
        "/TinaProject/Source/nested/../hero.glb";
#endif
    const auto duplicate = Detail::parseEditorSourceImportLaunchOption(
        argument("--import-gltf=", equivalent), options);

    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
    ASSERT_EQ(options.intendedUnits.size(), 1U);
    EXPECT_EQ(options.intendedUnits.front().pathUtf8, GltfPath);
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsUnitsOutsideProjectSource)
{
    Detail::EditorSourceImportLaunchOptions options{};
    ASSERT_TRUE(Detail::parseEditorSourceImportLaunchOption(
        argument("--project-root=", ProjectRoot), options));
#if defined(_WIN32)
    constexpr std::string_view outside = "C:/TinaProject/External/hero.glb";
#else
    constexpr std::string_view outside = "/TinaProject/External/hero.glb";
#endif
    ASSERT_TRUE(Detail::parseEditorSourceImportLaunchOption(
        argument("--import-gltf=", outside), options));

    const auto validated = Detail::validateEditorSourceImportLaunchOptions(options);

    ASSERT_FALSE(validated);
    EXPECT_EQ(validated.error().code, Tina::Core::CoreErrorCode::PermissionDenied);
}

TEST(EditorSourceImportLaunchOptionsTests, RejectsRelativeAndMismatchedUnitPaths)
{
    Detail::EditorSourceImportLaunchOptions options{};
    const auto relative = Detail::parseEditorSourceImportLaunchOption(
        "--import-gltf=Source/hero.glb", options);
    ASSERT_FALSE(relative);
    EXPECT_TRUE(options.intendedUnits.empty());

    const auto mismatched = Detail::parseEditorSourceImportLaunchOption(
        argument("--import-recipe=", GltfPath), options);
    ASSERT_FALSE(mismatched);
    EXPECT_TRUE(options.intendedUnits.empty());
}
