#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <memory_resource>
#include <string>

namespace Tina::Asset {
namespace {

using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::TrackingMemoryResource;
using TestSupport::writeTextureMaterialPackage;

[[nodiscard]] Core::Result<CatalogSnapshot> openCatalog(TrackingMemoryResource& resource,
                                                        const TestSupport::TextureMaterialPackage& package)
{
    return openCatalogPackage(
        toUtf8(package.root),
        CatalogPackageOpenConfig{
            .manifest = CatalogFileLoadConfig{
                .catalog = CatalogConfig{
                    .maxEntries = 8,
                    .maxDependencies = 8,
                    .maxDependenciesPerAsset = 4,
                    .memoryResource = &resource,
                },
            },
            .validateOnOpen = true,
            .validation = CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
            },
        });
}

[[nodiscard]] Core::Result<AssetSystem> makeSystem(TrackingMemoryResource& resource)
{
    return AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = 8,
        .memoryResource = &resource,
        .batch = CookedAssetBatchLoadConfig{
            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
            .memoryResource = &resource,
        },
    });
}

TEST(AssetSystemCatalogReloadTests, ReloadsAtomicallyWhenStoreIsIdle)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_success");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());

    auto replacementCatalog = openCatalog(resource, package);
    ASSERT_TRUE(replacementCatalog.has_value()) << replacementCatalog.error().message;
    ASSERT_TRUE(system->reloadCatalogWhenIdle("replacement-root", std::move(*replacementCatalog)).has_value());
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalogRoot(), "replacement-root");
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
}

TEST(AssetSystemCatalogReloadTests, BusyReloadPreservesCatalogAndRoot)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_busy");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    auto loaded = system->loadOne(package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    auto replacementCatalog = openCatalog(resource, package);
    ASSERT_TRUE(replacementCatalog.has_value()) << replacementCatalog.error().message;
    auto status = system->reloadCatalogWhenIdle("replacement-root", std::move(*replacementCatalog));
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::CatalogReloadBusy);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    ASSERT_TRUE(system->unload(*loaded).has_value());
    removePackage(package);
}

TEST(AssetSystemCatalogReloadTests, InvalidReplacementLeavesExistingBindingUntouched)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_invalid");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    const auto originalRoot = system->catalogRoot();

    auto status = system->reloadCatalogWhenIdle("replacement-root", CatalogSnapshot{});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::InvalidCatalogConfig);
    EXPECT_EQ(system->catalogRoot(), originalRoot);
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
