#include <tina/asset/SourceImportPipeline.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

TEST(SourceImportPipelineTests, LinuxRecipeRecooksThenReusesCommittedBaseline)
{
    const auto workRoot = std::filesystem::temp_directory_path() /
                          "tina_source_import_pipeline_linux";
    std::error_code error;
    std::filesystem::remove_all(workRoot, error);
    ASSERT_TRUE(std::filesystem::create_directories(workRoot / "Source", error));
    ASSERT_FALSE(error);

    const auto recipePath = workRoot / "Source" / "game.recipe";
    {
        std::ofstream recipe(recipePath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(recipe.good());
        recipe << "platform LinuxX64\n"
                  "texture2d 010000000000000000000000000000a4 1 1 FF0000FF\n";
    }

    std::pmr::unsynchronized_pool_resource memory;
    CatalogPackageStageConfig stageConfig{};
    stageConfig.validation.manifest.catalog.maxEntries = 8;
    stageConfig.validation.manifest.catalog.maxDependencies = 8;
    stageConfig.validation.manifest.catalog.maxDependenciesPerAsset = 4;
    stageConfig.validation.manifest.catalog.memoryResource = &memory;
    stageConfig.validation.validation.file.memoryResource = &memory;
    stageConfig.validation.validation.verifyTypedPayload = true;

    const std::string sourceRoot = toUtf8(workRoot / "Source");
    const std::string catalogRoot = toUtf8(workRoot / "Catalog");
    const std::string statePath = toUtf8(workRoot / "import-state.tmeta");
    const std::string recipePathUtf8 = toUtf8(recipePath);
    const std::array units{
        SourceImportPipelineUnit{
            .kind = SourceImportPipelineUnitKind::CatalogRecipe,
            .sourceUtf8Path = recipePathUtf8,
        },
    };
    const SourceImportPipelineRequest request{
        .sourceRootUtf8 = sourceRoot,
        .targetPlatform = AssetFormat::TargetPlatform::LinuxX64,
        .units = units,
        .baselineCatalogRootUtf8 = catalogRoot,
        .baselineStateUtf8Path = statePath,
        .stageConfig = stageConfig,
    };

    auto first = executeSourceImportPipeline(request);
    ASSERT_TRUE(first.has_value()) << (first ? "" : first.error().message);
    EXPECT_EQ(first->mode, SourceImportPipelineMode::FullRecook);
    EXPECT_EQ(first->unitsRecooked, 1U);
    EXPECT_EQ(first->objectsCooked, 1U);
    EXPECT_TRUE(first->stageCreated);
    EXPECT_TRUE(first->importStateCommitted);

    auto second = executeSourceImportPipeline(request);
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    EXPECT_EQ(second->mode, SourceImportPipelineMode::CleanReuse);
    EXPECT_EQ(second->unitsRecooked, 0U);
    EXPECT_EQ(second->objectsReused, 1U);
    EXPECT_FALSE(second->stageCreated);
    EXPECT_FALSE(second->importStateCommitted);

    std::filesystem::remove_all(workRoot, error);
}

} // namespace
} // namespace Tina::Asset
