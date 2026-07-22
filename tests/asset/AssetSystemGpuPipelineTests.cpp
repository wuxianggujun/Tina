#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/render/UploadTicket.hpp>
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <thread>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

TEST(AssetSystemGpuPipelineTests, SyncLoadAutoAdvancesToReadyGpu)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_gpu_pipeline_sync");

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
        .queueCapacity = 8,
        .defaultPumpBudget = 8,
        .taskSystem = nullptr,
        .uploadLedger = &(*ledger),
        .autoGpuUpload = true,
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;
    EXPECT_TRUE(system->hasGpuUpload());

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
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(loaded->size(), 1U);
    EXPECT_EQ(system->state((*loaded)[0]), AssetLogicalState::ReadyGpu);
    EXPECT_TRUE(system->isGpuReady((*loaded)[0]));
    // Dependency texture also uploaded.
    auto texture = system->find(package.textureId);
    ASSERT_TRUE(texture.has_value());
    EXPECT_TRUE(system->isGpuReady(*texture));

    removePackage(package);
}

TEST(AssetSystemGpuPipelineTests, RequestPumpWithIoAndGpu)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_gpu_pipeline_async");

    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 16,
        .mainQueueCapacity = 16,
    });
    ASSERT_TRUE(taskSystem.has_value());

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
        .queueCapacity = 8,
        .defaultPumpBudget = 4,
        .taskSystem = taskSystem->get(),
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

    auto requested = system->request(std::array{package.materialId});
    ASSERT_TRUE(requested.has_value()) << requested.error().message;

    bool gpuReady = false;
    for (int frame = 0; frame < 300 && !gpuReady; ++frame)
    {
        auto stats = system->pump(4);
        ASSERT_TRUE(stats.has_value()) << stats.error().message;
        if (system->isGpuReady((*requested)[0]))
        {
            gpuReady = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(gpuReady);
    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::ReadyGpu);

    (*taskSystem)->shutdownAndJoin();
    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
