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

[[nodiscard]] CatalogSnapshot openPackageCatalog(TrackingMemoryResource& resource,
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
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
            },
    };
    auto catalog = openCatalogPackage(toUtf8(package.root), openConfig);
    EXPECT_TRUE(catalog.has_value()) << (catalog ? "" : catalog.error().message);
    return catalog ? std::move(*catalog) : CatalogSnapshot{};
}

TEST(AssetSystemTests, BindLoadDedupeAcquireAndUnload)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_ok");

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
    });
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto catalog = openPackageCatalog(resource, package);
    ASSERT_TRUE(catalog);
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(catalog)).has_value());
    EXPECT_TRUE(system->hasCatalog());

    auto first = system->load(std::array{package.materialId});
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_EQ(first->size(), 1U);
    EXPECT_EQ(system->store().activeCount(), 2U); // texture + material

    auto second = system->load(std::array{package.materialId});
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->size(), 1U);
    EXPECT_EQ((*first)[0], (*second)[0]);
    EXPECT_EQ(system->store().activeCount(), 2U);

    auto lease = system->acquire((*first)[0]);
    ASSERT_TRUE(lease.has_value());
    ASSERT_NE(lease->get(), nullptr);
    EXPECT_EQ(lease->assetId(), package.materialId);

    lease = AssetLease{};
    ASSERT_TRUE(system->unload((*first)[0]).has_value());
    EXPECT_EQ(system->find(package.materialId), std::nullopt);
    EXPECT_EQ(system->tryGet((*first)[0]), nullptr);
    // Texture remains published.
    EXPECT_TRUE(system->find(package.textureId).has_value());

    removePackage(package);
}

TEST(AssetSystemTests, LoadAllAndBudgetGate)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_budget");

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
                .maxTotalCookedFileBytes = 1,
            },
    });
    ASSERT_TRUE(system.has_value());
    auto catalog = openPackageCatalog(resource, package);
    ASSERT_TRUE(catalog);
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(catalog)).has_value());

    auto failed = system->load({});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_EQ(system->store().activeCount(), 0U);

    // Raise budget and succeed.
    auto okSystem = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
                .maxTotalCookedFileBytes = 1024ULL * 1024ULL,
            },
    });
    ASSERT_TRUE(okSystem.has_value());
    auto catalog2 = openPackageCatalog(resource, package);
    ASSERT_TRUE(catalog2);
    ASSERT_TRUE(okSystem->bindCatalog(toUtf8(package.root), std::move(catalog2)).has_value());
    auto all = okSystem->load({});
    ASSERT_TRUE(all.has_value()) << all.error().message;
    EXPECT_EQ(all->size(), 2U);
    EXPECT_EQ(okSystem->store().activeCount(), 2U);

    removePackage(package);
}

TEST(AssetSystemTests, FailureRollsBackOnlyThisCall)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_partial", false);

    auto system = AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .memoryResource = &resource,
            },
    });
    ASSERT_TRUE(system.has_value());
    // Incomplete package: open without validate so bind can succeed; load material then fails.
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
    auto opened = openCatalogPackage(toUtf8(package.root), openConfig);
    ASSERT_TRUE(opened.has_value()) << opened.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*opened)).has_value());

    // Preload texture alone.
    auto textureOnly = system->loadOne(package.textureId);
    ASSERT_TRUE(textureOnly.has_value()) << textureOnly.error().message;
    EXPECT_EQ(system->store().activeCount(), 1U);

    // Loading material pulls texture (cached) then missing material file → fail and must not
    // unload pre-existing texture.
    auto failed = system->load(std::array{package.materialId});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(system->store().activeCount(), 1U);
    EXPECT_TRUE(system->find(package.textureId).has_value());

    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
