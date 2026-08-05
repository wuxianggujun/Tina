#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <string>
#include <thread>

namespace Tina::Asset {
namespace {

using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::TrackingMemoryResource;
using TestSupport::writeTextureMaterialPackage;

void rewriteTexturePayload(TestSupport::TextureMaterialPackage& package,
                           const std::array<std::byte, 4>& payload)
{
    const auto digest = Core::digestContentHashV1(payload);
    ASSERT_TRUE(digest.has_value());

    constexpr Core::u32 PayloadAlignment = 16U;
    const auto payloadOffset = TestSupport::alignUp(AssetFormat::Wire::CookedAssetHeaderBytes,
                                                    PayloadAlignment);
    TestSupport::putFixed(package.textureBytes, 48U, digest->bytes());
    TestSupport::putFixed(package.textureBytes, static_cast<Core::usize>(payloadOffset), payload);

    const auto artifact = AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Texture2D,
                                                               package.textureId);
    ASSERT_TRUE(artifact.has_value());
    TestSupport::writeBytes(
        package.root / Tina::TestSupport::pathFromUtf8Bytes(artifact->view()),
        package.textureBytes);
    TestSupport::writeBytes(
        package.root / "manifest.tmnft",
        TestSupport::makeTextureMaterialManifest(package.textureBytes.size(), *digest,
                                                 package.materialBytes.size(),
                                                 TestSupport::defaultPayloadHash()));
}

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
    const auto replacementPackage = writeTextureMaterialPackage("tina_asset_system_idle_reload_success_new");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());

    ASSERT_TRUE(system->reloadCatalogWhenIdle(toUtf8(replacementPackage.root)).has_value());
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalogRoot(), toUtf8(replacementPackage.root));
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, BusyReloadPreservesCatalogAndRoot)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_busy");
    const auto replacementPackage = writeTextureMaterialPackage("tina_asset_system_idle_reload_busy_new");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    auto loaded = system->loadOne(package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    auto replacementCatalog = openCatalog(resource, replacementPackage);
    ASSERT_TRUE(replacementCatalog.has_value()) << replacementCatalog.error().message;
    auto directBindStatus = system->bindCatalog(toUtf8(replacementPackage.root), std::move(*replacementCatalog));
    ASSERT_FALSE(directBindStatus.has_value());
    EXPECT_EQ(directBindStatus.error().code, AssetErrorCode::CatalogReloadBusy);

    auto status = system->reloadCatalogWhenIdle(toUtf8(replacementPackage.root));
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::CatalogReloadBusy);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    ASSERT_TRUE(system->unload(*loaded).has_value());
    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, ValidationFailureLeavesExistingBindingUntouched)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_invalid");
    const auto invalidPackage = writeTextureMaterialPackage("tina_asset_system_idle_reload_invalid_new", false);
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    const auto originalRoot = system->catalogRoot();

    CatalogReloadConfig reloadConfig{};
    reloadConfig.package.validateOnOpen = false;
    reloadConfig.package.validation.verifyContent = false;
    auto status = system->reloadCatalogWhenIdle(toUtf8(invalidPackage.root), reloadConfig);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::NotFound);
    EXPECT_EQ(system->catalogRoot(), originalRoot);
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
    removePackage(invalidPackage);
}

TEST(AssetSystemCatalogReloadTests, ChangePlanCapacityFailurePreservesExistingBinding)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_plan_capacity");
    auto replacementPackage =
        writeTextureMaterialPackage("tina_asset_system_idle_reload_plan_capacity_new");
    rewriteTexturePayload(replacementPackage,
                          {std::byte{0x51}, std::byte{0x62}, std::byte{0x73}, std::byte{0x84}});
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());

    CatalogReloadConfig reloadConfig{};
    reloadConfig.changePlan.maxChanges = 1U;
    auto status = system->reloadCatalogWhenIdle(toUtf8(replacementPackage.root), reloadConfig);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::CatalogCapacityExceeded);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, RejectsReloadFromNonOwnerThread)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_owner");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());

    Core::Status status = Core::success();
    std::thread worker([&]() {
        status = system->reloadCatalogWhenIdle(toUtf8(package.root));
    });
    worker.join();

    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::WrongOwnerThread);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
