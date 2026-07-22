#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/asset/CatalogPackageSummary.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CookedAssetBatch.hpp>
#include <tina/asset_format/AssetFormat.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

TEST(CatalogPackagePipelineTests, OpenPlanLoadValidateAndSummarize)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_pipeline_ok");

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &resource,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
            },
    };
    CookedAssetBatchLoadConfig batchConfig{
        .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
        .memoryResource = &resource,
    };

    {
        auto opened = openCatalogPackage(toUtf8(package.root), openConfig);
        ASSERT_TRUE(opened.has_value()) << opened.error().message;
        EXPECT_EQ(opened->entryCount(), 2U);

        auto plan = planCatalogLoads(*opened, std::array{package.materialId},
                                     CatalogLoadPlanConfig{.memoryResource = &resource});
        ASSERT_TRUE(plan.has_value()) << plan.error().message;
        ASSERT_EQ(plan->size(), 2U);
        EXPECT_EQ((*plan)[0].assetId, package.textureId);
        EXPECT_EQ((*plan)[1].assetId, package.materialId);

        auto batch = loadCookedAssetsFromPlan(toUtf8(package.root), *opened, *plan, batchConfig);
        ASSERT_TRUE(batch.has_value()) << batch.error().message;
        ASSERT_EQ(batch->size(), 2U);

        auto status = validateCatalogPackageOnDisk(toUtf8(package.root), *opened,
                                                   CatalogPackageValidationConfig{
                                                       .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                       .verifyContent = true,
                                                   });
        ASSERT_TRUE(status.has_value()) << status.error().message;

        auto summary = buildCatalogPackageSummary(
            *opened, CatalogPackageSummaryConfig{.memoryResource = &resource, .includeEntries = true});
        ASSERT_TRUE(summary.has_value()) << summary.error().message;
        EXPECT_EQ(summary->entryCount, 2U);
        EXPECT_EQ(summary->dependencyCount, 1U);
        ASSERT_EQ(summary->entries.size(), 2U);
    }

    // One-shot path agrees with stepwise path.
    {
        auto loaded = loadCookedAssetsFromPackage(toUtf8(package.root), std::array{package.materialId}, openConfig,
                                                  batchConfig);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        EXPECT_EQ(loaded->catalog.entryCount(), 2U);
        ASSERT_EQ(loaded->assets.size(), 2U);
        EXPECT_EQ(loaded->assets[0].header().assetId, package.textureId);
        EXPECT_EQ(loaded->assets[1].header().assetId, package.materialId);
    }

    removePackage(package);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackagePipelineTests, ValidateOnOpenRejectsIncompletePackage)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_pipeline_incomplete", false);

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &resource,
                    },
            },
        .validateOnOpen = true,
        .validation = CatalogPackageValidationConfig{.verifyContent = false},
    };

    const auto opened = openCatalogPackage(toUtf8(package.root), openConfig);
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().code, Core::CoreErrorCode::NotFound);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);

    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
