#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportPipeline.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <set>
#include <string>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

void writePngFixture(const std::filesystem::path& path)
{
    constexpr std::array<std::uint8_t, 120> bytes{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00,
        0x00, 0x04, 0x67, 0x41, 0x4d, 0x41, 0x00, 0x00, 0xb1, 0x8f, 0x0b, 0xfc,
        0x61, 0x05, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00,
        0x0e, 0xc3, 0x00, 0x00, 0x0e, 0xc3, 0x01, 0xc7, 0x6f, 0xa8, 0x64, 0x00,
        0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0xf8, 0xff,
        0xff, 0xff, 0x7f, 0x00, 0x09, 0xfb, 0x03, 0xfd, 0x05, 0x43, 0x45, 0xca,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

[[nodiscard]] bool rewriteFirstImporterVersion(const std::filesystem::path& statePath,
                                               Core::u32 importerVersion)
{
    auto bytes = Core::readFile(
        toUtf8(statePath),
        Core::ReadFileConfig{.memoryResource = std::pmr::new_delete_resource()});
    if (!bytes) {
        return false;
    }
    auto metadata = AssetFormat::parseSourceImportMetadataView(*bytes);
    if (!metadata || metadata->header().unitCount == 0U) {
        return false;
    }

    const auto unitOffset = static_cast<std::size_t>(metadata->header().unitsOffset);
    if (unitOffset + AssetFormat::SourceImportWire::UnitEntryBytes > bytes->size()) {
        return false;
    }
    for (Core::u32 shift = 0U; shift < 32U; shift += 8U) {
        (*bytes)[unitOffset + 20U + shift / 8U] =
            static_cast<std::byte>((importerVersion >> shift) & 0xFFU);
    }
    return Core::writeFile(toUtf8(statePath), *bytes).has_value();
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
    ASSERT_EQ(first->unitOutputs.size(), 1U);
    EXPECT_EQ(first->unitOutputs.front().sourceUtf8Path, recipePathUtf8);
    ASSERT_EQ(first->unitOutputs.front().outputs.size(), 1U);
    EXPECT_EQ(first->unitOutputs.front().outputs.front().assetKind,
              AssetFormat::AssetKind::Texture2D);

    auto second = executeSourceImportPipeline(request);
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    EXPECT_EQ(second->mode, SourceImportPipelineMode::CleanReuse);
    EXPECT_EQ(second->unitsRecooked, 0U);
    EXPECT_EQ(second->objectsReused, 1U);
    EXPECT_FALSE(second->stageCreated);
    EXPECT_FALSE(second->importStateCommitted);
    ASSERT_EQ(second->unitOutputs.size(), 1U);
    ASSERT_EQ(second->unitOutputs.front().outputs.size(), 1U);
    EXPECT_EQ(second->unitOutputs.front().outputs.front().assetId,
              first->unitOutputs.front().outputs.front().assetId);
    EXPECT_EQ(second->unitOutputs.front().outputs.front().assetKind,
              first->unitOutputs.front().outputs.front().assetKind);

    std::filesystem::remove_all(workRoot, error);
}

TEST(SourceImportPipelineTests, IncrementalTextureImportRetainsPriorPngAndMapsOutputs)
{
    const auto workRoot = std::filesystem::temp_directory_path() /
                          "tina_source_import_pipeline_texture_incremental";
    std::error_code error;
    std::filesystem::remove_all(workRoot, error);
    ASSERT_TRUE(std::filesystem::create_directories(workRoot / "Source", error));
    ASSERT_FALSE(error);
    const auto firstPath = workRoot / "Source" / "first.png";
    const auto secondPath = workRoot / "Source" / "second.png";
    writePngFixture(firstPath);
    writePngFixture(secondPath);

    std::pmr::unsynchronized_pool_resource memory;
    CatalogPackageStageConfig stageConfig{};
    stageConfig.validation.manifest.catalog.maxEntries = 16;
    stageConfig.validation.manifest.catalog.maxDependencies = 16;
    stageConfig.validation.manifest.catalog.maxDependenciesPerAsset = 4;
    stageConfig.validation.manifest.catalog.memoryResource = &memory;
    stageConfig.validation.validation.file.memoryResource = &memory;
    stageConfig.validation.validation.verifyTypedPayload = true;

    const std::string sourceRoot = toUtf8(workRoot / "Source");
    const std::string catalogRoot = toUtf8(workRoot / "Catalog");
    const std::string statePath = toUtf8(workRoot / "import-state.tmeta");
    const std::string firstPathUtf8 = toUtf8(firstPath);
    const std::string secondPathUtf8 = toUtf8(secondPath);
    const std::array firstUnits{SourceImportPipelineUnit{
        .kind = SourceImportPipelineUnitKind::Texture,
        .sourceUtf8Path = firstPathUtf8,
    }};
    auto first = executeSourceImportPipeline(SourceImportPipelineRequest{
        .sourceRootUtf8 = sourceRoot,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .units = firstUnits,
        .baselineCatalogRootUtf8 = catalogRoot,
        .baselineStateUtf8Path = statePath,
        .stageConfig = stageConfig,
    });
    ASSERT_TRUE(first) << (first ? "" : first.error().message);
    ASSERT_EQ(first->unitOutputs.size(), 1U);
    ASSERT_EQ(first->unitOutputs.front().outputs.size(), 1U);

    ASSERT_TRUE(std::filesystem::create_directories(workRoot / "stage", error));
    ASSERT_FALSE(error);
    const std::array secondUnits{
        firstUnits.front(),
        SourceImportPipelineUnit{
            .kind = SourceImportPipelineUnitKind::Texture,
            .sourceUtf8Path = secondPathUtf8,
        },
    };
    auto second = executeSourceImportPipeline(SourceImportPipelineRequest{
        .sourceRootUtf8 = sourceRoot,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .units = secondUnits,
        .baselineCatalogRootUtf8 = catalogRoot,
        .baselineStateUtf8Path = statePath,
        .stageCatalogRootUtf8 = toUtf8(workRoot / "stage" / "Catalog"),
        .stageStateUtf8Path = toUtf8(workRoot / "stage" / "import-state.tmeta"),
        .stageConfig = stageConfig,
    });
    ASSERT_TRUE(second) << (second ? "" : second.error().message);
    EXPECT_EQ(second->mode, SourceImportPipelineMode::IncrementalRecook);
    EXPECT_EQ(second->objectsReused, 1U);
    EXPECT_EQ(second->objectsCooked, 1U);
    ASSERT_EQ(second->unitOutputs.size(), 2U);

    std::set<Core::AssetId> assetIds;
    for (const auto& unit : second->unitOutputs)
    {
        ASSERT_EQ(unit.outputs.size(), 1U);
        std::set<AssetFormat::AssetKind> kinds;
        for (const auto& output : unit.outputs)
        {
            EXPECT_TRUE(assetIds.insert(output.assetId).second);
            kinds.insert(output.assetKind);
        }
        EXPECT_TRUE(kinds.contains(AssetFormat::AssetKind::Texture2D));
    }

    ASSERT_TRUE(std::filesystem::create_directories(workRoot / "empty-stage", error));
    ASSERT_FALSE(error);
    const std::array<SourceImportPipelineUnit, 0> noUnits{};
    auto empty = executeSourceImportPipeline(SourceImportPipelineRequest{
        .sourceRootUtf8 = sourceRoot,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .units = noUnits,
        .baselineCatalogRootUtf8 = second->catalogRootUtf8,
        .baselineStateUtf8Path = second->stateUtf8Path,
        .stageCatalogRootUtf8 = toUtf8(workRoot / "empty-stage" / "Catalog"),
        .stageStateUtf8Path = toUtf8(workRoot / "empty-stage" / "import-state.tmeta"),
        .stageConfig = stageConfig,
    });
    ASSERT_TRUE(empty) << (empty ? "" : empty.error().message);
    EXPECT_TRUE(empty->unitOutputs.empty());
    EXPECT_EQ(empty->unitsRemoved, 2U);
    EXPECT_EQ(empty->catalogEntries, 0U);

    std::filesystem::remove_all(workRoot, error);
}

TEST(SourceImportPipelineTests, ImporterVersionMigrationForcesFullRecookWithoutReuse)
{
    const auto workRoot = std::filesystem::temp_directory_path() /
                          "tina_source_import_pipeline_importer_migration";
    std::error_code error;
    std::filesystem::remove_all(workRoot, error);
    ASSERT_TRUE(std::filesystem::create_directories(workRoot / "Source", error));
    ASSERT_FALSE(error);

    const auto imagePath = workRoot / "Source" / "icon.png";
    writePngFixture(imagePath);

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
    const std::string imagePathUtf8 = toUtf8(imagePath);
    const std::array units{SourceImportPipelineUnit{
        .kind = SourceImportPipelineUnitKind::Texture,
        .sourceUtf8Path = imagePathUtf8,
    }};
    const SourceImportPipelineRequest initialRequest{
        .sourceRootUtf8 = sourceRoot,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .units = units,
        .baselineCatalogRootUtf8 = catalogRoot,
        .baselineStateUtf8Path = statePath,
        .stageConfig = stageConfig,
    };

    auto initial = executeSourceImportPipeline(initialRequest);
    ASSERT_TRUE(initial) << (initial ? "" : initial.error().message);
    ASSERT_EQ(initial->mode, SourceImportPipelineMode::FullRecook);
    ASSERT_EQ(initial->unitsRecooked, 1U);
    ASSERT_EQ(initial->objectsCooked, 1U);
    ASSERT_TRUE(initial->importStateCommitted);
    ASSERT_TRUE(rewriteFirstImporterVersion(std::filesystem::path(statePath), 1U));

    auto missingStage = executeSourceImportPipeline(initialRequest);
    ASSERT_FALSE(missingStage);
    EXPECT_EQ(missingStage.error().code, AssetErrorCode::InvalidCatalogConfig);

    ASSERT_TRUE(std::filesystem::create_directories(workRoot / "migration-stage", error));
    ASSERT_FALSE(error);
    const std::string migrationStageCatalogRoot =
        toUtf8(workRoot / "migration-stage" / "Catalog");
    const std::string migrationStageStatePath =
        toUtf8(workRoot / "migration-stage" / "import-state.tmeta");
    const SourceImportPipelineRequest migrationRequest{
        .sourceRootUtf8 = sourceRoot,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .units = units,
        .baselineCatalogRootUtf8 = catalogRoot,
        .baselineStateUtf8Path = statePath,
        .stageCatalogRootUtf8 = migrationStageCatalogRoot,
        .stageStateUtf8Path = migrationStageStatePath,
        .stageConfig = stageConfig,
    };
    auto migrated = executeSourceImportPipeline(migrationRequest);
    ASSERT_TRUE(migrated) << (migrated ? "" : migrated.error().message);
    EXPECT_EQ(migrated->mode, SourceImportPipelineMode::FullRecook);
    EXPECT_EQ(migrated->probeState, SourceImportProbeState::Dirty);
    EXPECT_EQ(migrated->probeReason, SourceImportProbeReason::ImporterVersionChanged);
    EXPECT_EQ(migrated->unitsRecooked, 1U);
    EXPECT_EQ(migrated->objectsReused, 0U);
    EXPECT_EQ(migrated->objectsCooked, 1U);
    EXPECT_TRUE(migrated->stageCreated);
    EXPECT_TRUE(migrated->importStateCommitted);
    ASSERT_EQ(migrated->unitOutputs.size(), 1U);
    ASSERT_EQ(migrated->unitOutputs.front().outputs.size(), 1U);
    EXPECT_EQ(migrated->unitOutputs.front().outputs.front().assetId,
              initial->unitOutputs.front().outputs.front().assetId);

    std::filesystem::remove_all(workRoot, error);
}

} // namespace
} // namespace Tina::Asset
