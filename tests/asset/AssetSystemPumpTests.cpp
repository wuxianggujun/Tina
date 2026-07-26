#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

TEST(AssetSystemPumpTests, RequestThenPumpMakesAssetsReady)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_pump_ok");

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
        .queueCapacity = 8,
        .defaultPumpBudget = 1,
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
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*catalog)).has_value());

    auto requested = system->request(std::array{package.materialId});
    ASSERT_TRUE(requested.has_value()) << requested.error().message;
    ASSERT_EQ(requested->size(), 1U);
    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::Queued);
    // Dependency texture also queued.
    EXPECT_EQ(system->pendingCount(), 2U);
    EXPECT_EQ(system->tryGet((*requested)[0]), nullptr);

    auto pump1 = system->pump(1);
    ASSERT_TRUE(pump1.has_value()) << pump1.error().message;
    EXPECT_EQ(pump1->processed, 1U);
    EXPECT_EQ(pump1->becameReady, 1U);
    EXPECT_EQ(pump1->remaining, 1U);

    auto pump2 = system->pump(1);
    ASSERT_TRUE(pump2.has_value());
    EXPECT_EQ(pump2->processed, 1U);
    EXPECT_EQ(pump2->becameReady, 1U);
    EXPECT_EQ(pump2->remaining, 0U);

    EXPECT_EQ(system->state((*requested)[0]), AssetLogicalState::ReadyCpu);
    auto lease = system->acquire((*requested)[0]);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ(lease->assetId(), package.materialId);

    removePackage(package);
}

TEST(AssetSystemPumpTests, PumpMarksMissingFileAsFailed)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_pump_fail", false);

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
        .validateOnOpen = false,
    };
    auto catalog = openCatalogPackage(toUtf8(package.root), openConfig);
    ASSERT_TRUE(catalog.has_value());
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*catalog)).has_value());

    auto requested = system->request(std::array{package.materialId});
    ASSERT_TRUE(requested.has_value());
    auto stats = system->pump(0);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_GE(stats->processed, 1U);
    EXPECT_GE(stats->becameFailed, 1U);

    // Material should be Failed (missing file). Texture may be Ready.
    const auto materialHandle = system->find(package.materialId);
    ASSERT_TRUE(materialHandle.has_value());
    EXPECT_EQ(system->state(*materialHandle), AssetLogicalState::Failed);
    auto lease = system->acquire(*materialHandle);
    ASSERT_FALSE(lease.has_value());
    EXPECT_EQ(lease.error().code, AssetErrorCode::AssetFailed);

    removePackage(package);
}

TEST(AssetSystemPumpTests, QueueCapacityIsBounded)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_queue_full");

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
        .queueCapacity = 1,
        .defaultPumpBudget = 1,
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

    // material expands to texture+material = 2 queue items → exceeds capacity 1
    auto failed = system->request(std::array{package.materialId});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, AssetErrorCode::AssetQueueFull);
    EXPECT_EQ(system->pendingCount(), 0U);
    EXPECT_EQ(system->store().activeCount(), 0U);

    removePackage(package);
}

TEST(AssetSystemPumpTests, UnloadImmediatelyHidesLookupWhileLeaseKeepsOldPayloadAlive)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_unload_lease_lookup");

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
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*catalog)).has_value());

    auto oldHandle = system->loadOne(package.textureId);
    ASSERT_TRUE(oldHandle.has_value()) << oldHandle.error().message;
    auto leaseResult = system->acquire(*oldHandle);
    ASSERT_TRUE(leaseResult.has_value()) << leaseResult.error().message;
    AssetLease lease = std::move(*leaseResult);
    ASSERT_NE(lease.get(), nullptr);

    ASSERT_TRUE(system->unload(*oldHandle).has_value());
    EXPECT_EQ(system->find(package.textureId), std::nullopt);
    EXPECT_EQ(system->state(*oldHandle), AssetLogicalState::UnloadPending);
    EXPECT_NE(lease.get(), nullptr);

    // Reentry allocates a new generation instead of returning the UnloadPending one.
    auto newHandle = system->requestOne(package.textureId);
    ASSERT_TRUE(newHandle.has_value()) << newHandle.error().message;
    EXPECT_NE(*newHandle, *oldHandle);
    EXPECT_EQ(system->find(package.textureId), newHandle);
    EXPECT_EQ(system->state(*newHandle), AssetLogicalState::Queued);

    lease = AssetLease{};
    EXPECT_EQ(system->state(*oldHandle), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->find(package.textureId), newHandle);

    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
