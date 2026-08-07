#include "EditorSourceImportLaunchOptions.hpp"

#include <gtest/gtest.h>

namespace Detail = Tina::EditorApp::Detail;

TEST(EditorSourceImportLaunchOptionsTests, RetainsMixedIntendedUnitSetInCallerOrder)
{
    Detail::EditorSourceImportLaunchOptions options{};
    for (const std::string_view argument : {
             "--project-root=C:/TinaProject",
             "--import-recipe=C:/TinaProject/Source/game.recipe",
             "--import-gltf=C:/TinaProject/Source/hero.glb",
             "--import-on-start",
         }) {
        auto parsed = Detail::parseEditorSourceImportLaunchOption(argument, options);
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
        "--import-gltf=C:/Project/Source/hero.gltf", options);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(*initial);

    const auto duplicate = Detail::parseEditorSourceImportLaunchOption(
        "--import-gltf=C:/Project/Source/hero.gltf", options);

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
