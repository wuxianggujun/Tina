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
    auto file = loadCookedAssetFromCatalog(*catalog, package.textureId,
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
    EXPECT_GE(stats.releasedTotal, 1U);
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
    EXPECT_GE(retirement.stats().releasedTotal, 1U);
    EXPECT_EQ(retirement.stats().live, 0U);

    removePackage(package);
}

TEST(AssetRetirementTests, CompletionAndCancellationUseExactResourceIdentity)
{
    TrackingMemoryResource memory;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(store.has_value());
    const auto id = TestSupport::assetId(1U);
    auto handle = store->beginQueued(id, AssetFormat::AssetKind::Texture2D);
    ASSERT_TRUE(handle.has_value());
    AssetRetirementLedger retirement;
    const AssetRetirementRecord first{
        .assetId = id, .handle = *handle, .texture = Render::GpuTextureId{1U, 1U},
        .kind = AssetRetirementKind::GpuTexture2D,
    };
    const AssetRetirementRecord second{
        .assetId = id, .handle = *handle, .texture = Render::GpuTextureId{2U, 1U},
        .kind = AssetRetirementKind::GpuTexture2D,
    };
    ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, first.texture));
    ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, second.texture));
    retirement.markRetiring(first);
    retirement.markRetiring(second);
    retirement.markReleased(first);
    EXPECT_EQ(retirement.stats().releasedTotal, 1U);
    EXPECT_EQ(retirement.stats().retiring, 1U);
    retirement.cancel(second);
    EXPECT_FALSE(retirement.contains(first));
    EXPECT_FALSE(retirement.contains(second));

    ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, second.texture));
    retirement.markRetiring(second);
    retirement.markReleased(first);
    retirement.cancel(first);
    ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, second.texture));
    EXPECT_EQ(retirement.records().size(), 1U);
    EXPECT_EQ(retirement.stats().releasedTotal, 1U);
    EXPECT_EQ(retirement.stats().retiring, 1U);
    EXPECT_EQ(retirement.records()[0].texture, second.texture);
    retirement.markReleased(second);
    EXPECT_EQ(retirement.stats().releasedTotal, 2U);
    EXPECT_EQ(retirement.stats().live, 0U);
    EXPECT_TRUE(retirement.records().empty());
}

TEST(AssetRetirementTests, ReleasedStorageTracksPeakLiveWorkNotLifetimeChurn)
{
    TrackingMemoryResource memory;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(store);
    const auto id = TestSupport::assetId(1U);
    auto handle = store->beginQueued(id, AssetFormat::AssetKind::Texture2D);
    ASSERT_TRUE(handle);
    AssetRetirementLedger retirement;
    const AssetRetirementRecord retained{
        .assetId = id, .handle = *handle, .texture = Render::GpuTextureId{1U, 1U},
        .kind = AssetRetirementKind::GpuTexture2D,
    };
    ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, retained.texture));
    retirement.markRetiring(retained);
    constexpr Core::u32 Iterations = 100000;
    Core::usize steadyCapacity = 0;
    for (Core::u32 index = 0; index < Iterations; ++index)
    {
        const AssetRetirementRecord current{
            .assetId = id, .handle = *handle, .texture = Render::GpuTextureId{2U, index + 1U},
            .kind = AssetRetirementKind::GpuTexture2D,
        };
        ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, current.texture));
        retirement.markRetiring(current);
        retirement.markReleased(current);
        retirement.markReleased(current);
        retirement.cancel(current);
        if (index == 0) { steadyCapacity = retirement.stats().recordCapacity; }
        ASSERT_EQ(retirement.stats().recordCapacity, steadyCapacity);
        ASSERT_EQ(retirement.liveCount(), 1U);
        ASSERT_TRUE(retirement.contains(retained));
    }
    EXPECT_EQ(retirement.stats().releasedTotal, Iterations);
    EXPECT_EQ(retirement.releasedCount(AssetRetirementKind::GpuTexture2D), Iterations);
    EXPECT_EQ(retirement.releasedCount(AssetRetirementKind::GpuMesh), 0U);
    retirement.markReleased(retained);
    EXPECT_TRUE(retirement.records().empty());
    EXPECT_EQ(retirement.stats().releasedTotal, Iterations + 1U);
}

TEST(AssetRetirementTests, GrowthIsAmortizedAndCompletionCompactionPreservesExactIdentity)
{
    TrackingMemoryResource memory;
    auto store = AssetStore::Create(AssetStoreConfig{.capacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(store);
    const auto id = TestSupport::assetId(1U);
    auto handle = store->beginQueued(id, AssetFormat::AssetKind::Texture2D);
    ASSERT_TRUE(handle);
    AssetRetirementLedger retirement;
    Core::usize growths = 0;
    Core::usize capacity = 0;
    constexpr Core::u32 Count = 1024;
    for (Core::u32 index = 0; index < Count; ++index)
    {
        ASSERT_TRUE(retirement.enqueueTexture2D(*handle, id, Render::GpuTextureId{index + 1U, 1U}));
        if (retirement.stats().recordCapacity != capacity)
        {
            capacity = retirement.stats().recordCapacity;
            ++growths;
        }
    }
    EXPECT_LT(growths, 32U);
    for (Core::u32 index = 0; index < Count; ++index)
    {
        const AssetRetirementRecord resource{
            .assetId = id, .handle = *handle, .texture = Render::GpuTextureId{index + 1U, 1U},
            .kind = AssetRetirementKind::GpuTexture2D,
        };
        ASSERT_TRUE(retirement.contains(resource));
        if ((index % 2U) == 0U) { retirement.markReleased(resource); }
        else { retirement.cancel(resource); }
        ASSERT_FALSE(retirement.contains(resource));
    }
    EXPECT_TRUE(retirement.records().empty());
    EXPECT_EQ(retirement.stats().releasedTotal, Count / 2U);
}

} // namespace
} // namespace Tina::Asset
