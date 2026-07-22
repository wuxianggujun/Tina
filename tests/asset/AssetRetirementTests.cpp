#include <tina/asset/AssetRetirement.hpp>
#include <tina/asset/AssetGpuUpload.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/render/UploadTicket.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

// local helper - support may not export loadOneCooked; use package path.
[[nodiscard]] CookedAssetFile loadTextureFromPackage(TrackingMemoryResource& resource,
                                                     const TestSupport::TextureMaterialPackage& package)
{
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
        .validateOnOpen = false,
    };
    auto catalog = openCatalogPackage(toUtf8(package.root), openConfig);
    EXPECT_TRUE(catalog.has_value());
    if (!catalog)
    {
        return {};
    }
    auto file = loadCookedAssetFromCatalog(toUtf8(package.root), *catalog, package.textureId,
                                           CookedAssetFileLoadConfig{.memoryResource = &resource});
    EXPECT_TRUE(file.has_value());
    return file ? std::move(*file) : CookedAssetFile{};
}

TEST(AssetRetirementTests, UnloadAfterGpuReadyRecordsReleased)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_retirement_ok");
    auto ledger =
        Render::NullUploadLedger::Create(Render::UploadLedgerConfig{.capacity = 8, .memoryResource = &resource});
    ASSERT_TRUE(ledger.has_value());

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
        .uploadLedger = &(*ledger),
        .autoGpuUpload = true,
    });
    ASSERT_TRUE(system.has_value());

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
    auto catalog = openCatalogPackage(toUtf8(package.root), openConfig);
    ASSERT_TRUE(catalog.has_value());
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*catalog)).has_value());

    auto loaded = system->load(std::array{package.materialId});
    ASSERT_TRUE(loaded.has_value());
    ASSERT_TRUE(system->isGpuReady((*loaded)[0]));
    EXPECT_EQ(system->retirementStats().live, 0U);

    ASSERT_TRUE(system->unload((*loaded)[0]).has_value());
    const auto stats = system->retirementStats();
    EXPECT_GE(stats.released, 1U);
    EXPECT_EQ(stats.live, 0U);
    EXPECT_EQ(ledger->liveCount(), 0U);

    removePackage(package);
}

TEST(AssetRetirementTests, CancelOutstandingTicketFreesStaging)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_retirement_cancel");
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 4, .memoryResource = &resource});
    ASSERT_TRUE(store.has_value());
    auto ledger =
        Render::NullUploadLedger::Create(Render::UploadLedgerConfig{.capacity = 4, .memoryResource = &resource});
    ASSERT_TRUE(ledger.has_value());
    AssetRetirementLedger retirement;
    AssetGpuUploadCoordinator coordinator(*store, *ledger,
                                          AssetGpuUploadConfig{.submitBudget = 8, .pollBudget = 8, .retireOnGpuReady = false},
                                          &retirement);

    auto file = loadTextureFromPackage(resource, package);
    ASSERT_TRUE(file);
    auto handle = store->publish(std::move(file));
    ASSERT_TRUE(handle.has_value());
    ASSERT_TRUE(coordinator.track(*handle).has_value());
    auto stats = coordinator.pumpUploads();
    ASSERT_TRUE(stats.has_value());
    EXPECT_TRUE(store->isGpuReady(*handle));
    // Ticket still live because retireOnGpuReady=false.
    EXPECT_EQ(ledger->liveCount(), 1U);

    ASSERT_TRUE(coordinator.cancelUpload(*handle).has_value());
    EXPECT_EQ(ledger->liveCount(), 0U);
    EXPECT_GE(retirement.stats().released, 1U);
    EXPECT_EQ(retirement.stats().live, 0U);

    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
