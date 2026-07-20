#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
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

TEST(AssetSystemAsyncPumpTests, RequestIoPumpMakesReady)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_async_pump_ok");

    auto taskSystem = Task::createBoundedTaskSystem(Task::TaskSystemCreateParams{
        .ioWorkerCount = 1,
        .ioQueueCapacity = 16,
        .mainQueueCapacity = 16,
    });
    ASSERT_TRUE(taskSystem.has_value()) << taskSystem.error().message;

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
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

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
    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::Queued);

    bool ready = false;
    for (int frame = 0; frame < 200 && !ready; ++frame)
    {
        auto stats = system->pump(4);
        ASSERT_TRUE(stats.has_value()) << stats.error().message;
        if (system->state((*requested)[0]) == AssetLogicalState::Ready)
        {
            ready = true;
            break;
        }
        if (stats->inFlight > 0 || stats->remaining > 0 || stats->dispatchedIo > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    EXPECT_TRUE(ready);
    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::Ready);
    auto lease = system->acquire((*requested)[0]);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ(lease->assetId(), package.materialId);

    (*taskSystem)->shutdownAndJoin();
    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
