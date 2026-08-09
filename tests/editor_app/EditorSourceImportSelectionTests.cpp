#include "EditorSourceImportSelection.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Detail = Tina::EditorApp::Detail;

namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

class EditorSourceImportSelectionTests : public testing::Test {
protected:
    void SetUp() override
    {
        static std::atomic_uint64_t nextId{0};
        root_ = std::filesystem::temp_directory_path() /
                ("tina-editor-source-selection-" +
                 std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
        sourceRoot_ = root_ / "Source";
        ASSERT_TRUE(std::filesystem::create_directories(sourceRoot_));
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path writeSourceFile(
        const std::filesystem::path& relativePath)
    {
        const auto path = sourceRoot_ / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "fixture";
        EXPECT_TRUE(output.good());
        return path;
    }

    [[nodiscard]] Detail::EditorSourceImportUnit unit(
        Detail::EditorSourceImportUnitKind kind,
        const std::filesystem::path& path) const
    {
        return Detail::EditorSourceImportUnit{
            .kind = kind,
            .sourcePathUtf8 = pathToUtf8(path),
        };
    }

    std::filesystem::path root_{};
    std::filesystem::path sourceRoot_{};
};

TEST_F(EditorSourceImportSelectionTests, MergesMixedRecipeAndGltfSelectionInOrder)
{
    const auto recipe = writeSourceFile("catalog.recipe");
    const auto gltf = writeSourceFile("models/hero.gltf");
    const auto glb = writeSourceFile("models/prop.glb");
    const std::array selectedPaths{
        pathToUtf8(recipe),
        pathToUtf8(gltf),
        pathToUtf8(glb),
    };

    auto merged = Detail::mergeEditorSourceImportSelection(
        pathToUtf8(sourceRoot_), {}, selectedPaths);

    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->selectedPathCount, 3U);
    EXPECT_EQ(merged->addedUnitCount, 3U);
    ASSERT_EQ(merged->intendedUnits.size(), 3U);
    EXPECT_EQ(merged->intendedUnits[0].kind,
              Detail::EditorSourceImportUnitKind::CatalogRecipe);
    EXPECT_EQ(merged->intendedUnits[1].kind,
              Detail::EditorSourceImportUnitKind::Gltf);
    EXPECT_EQ(merged->intendedUnits[2].kind,
              Detail::EditorSourceImportUnitKind::Gltf);
}

TEST_F(EditorSourceImportSelectionTests, DeduplicatesExistingAndRepeatedSelections)
{
    const auto recipe = writeSourceFile("catalog.recipe");
    const auto gltf = writeSourceFile("models/hero.gltf");
    const std::array currentUnits{
        unit(Detail::EditorSourceImportUnitKind::CatalogRecipe, recipe),
    };
    const std::array selectedPaths{
        pathToUtf8(recipe),
        pathToUtf8(gltf),
        pathToUtf8(gltf),
    };

    auto merged = Detail::mergeEditorSourceImportSelection(
        pathToUtf8(sourceRoot_), currentUnits, selectedPaths);

    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->selectedPathCount, 3U);
    EXPECT_EQ(merged->addedUnitCount, 1U);
    ASSERT_EQ(merged->intendedUnits.size(), 2U);
    EXPECT_EQ(merged->intendedUnits[0].kind,
              Detail::EditorSourceImportUnitKind::CatalogRecipe);
    EXPECT_EQ(merged->intendedUnits[1].kind,
              Detail::EditorSourceImportUnitKind::Gltf);
}

TEST_F(EditorSourceImportSelectionTests, InvalidPathAfterValidSelectionLeavesCurrentSetUnchanged)
{
    const auto recipe = writeSourceFile("catalog.recipe");
    const auto gltf = writeSourceFile("models/hero.gltf");
    const auto unsupported = writeSourceFile("textures/albedo.png");
    std::vector currentUnits{
        unit(Detail::EditorSourceImportUnitKind::CatalogRecipe, recipe),
    };
    const auto originalPath = currentUnits.front().sourcePathUtf8;
    const std::array selectedPaths{
        pathToUtf8(gltf),
        pathToUtf8(unsupported),
    };

    const auto merged = Detail::mergeEditorSourceImportSelection(
        pathToUtf8(sourceRoot_), currentUnits, selectedPaths);

    ASSERT_FALSE(merged);
    EXPECT_EQ(merged.error().code, Tina::Core::CoreErrorCode::InvalidArgument);
    ASSERT_EQ(currentUnits.size(), 1U);
    EXPECT_EQ(currentUnits.front().sourcePathUtf8, originalPath);
    EXPECT_EQ(currentUnits.front().kind,
              Detail::EditorSourceImportUnitKind::CatalogRecipe);
}

TEST_F(EditorSourceImportSelectionTests, CapacityFailureLeavesCurrentSetUnchanged)
{
    const auto recipe = writeSourceFile("catalog.recipe");
    const auto firstGltf = writeSourceFile("models/first.gltf");
    const auto secondGltf = writeSourceFile("models/second.glb");
    std::vector currentUnits{
        unit(Detail::EditorSourceImportUnitKind::CatalogRecipe, recipe),
    };
    const auto originalPath = currentUnits.front().sourcePathUtf8;
    const std::array selectedPaths{
        pathToUtf8(firstGltf),
        pathToUtf8(secondGltf),
    };

    const auto merged = Detail::mergeEditorSourceImportSelection(
        pathToUtf8(sourceRoot_), currentUnits, selectedPaths, 2U);

    ASSERT_FALSE(merged);
    EXPECT_EQ(merged.error().code, Tina::Core::CoreErrorCode::CapacityExceeded);
    ASSERT_EQ(currentUnits.size(), 1U);
    EXPECT_EQ(currentUnits.front().sourcePathUtf8, originalPath);
}

} // namespace
