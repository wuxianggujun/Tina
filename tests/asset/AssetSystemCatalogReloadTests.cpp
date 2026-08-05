#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory_resource>
#include <optional>
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

[[nodiscard]] Core::Result<AssetSystem> makeSystem(TrackingMemoryResource& resource,
                                                   Core::usize storeCapacity = 8U)
{
    return AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = storeCapacity,
        .memoryResource = &resource,
        .batch = CookedAssetBatchLoadConfig{
            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
            .memoryResource = &resource,
        },
    });
}

[[nodiscard]] const CatalogResidentMigration*
findMigration(const CatalogReloadResult& result, Core::AssetId assetId)
{
    const auto it = std::find_if(
        result.residentMigrations.begin(), result.residentMigrations.end(),
        [assetId](const CatalogResidentMigration& migration) {
            return migration.assetId == assetId;
        });
    return it == result.residentMigrations.end() ? nullptr : std::addressof(*it);
}

TEST(AssetSystemCatalogReloadTests, ReloadsAtomicallyWithoutResidentAssets)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_idle_reload_success");
    const auto replacementPackage = writeTextureMaterialPackage("tina_asset_system_idle_reload_success_new");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());

    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root));
    ASSERT_TRUE(reload.has_value()) << reload.error().message;
    EXPECT_TRUE(reload->changes.changes.empty());
    EXPECT_TRUE(reload->residentMigrations.empty());
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalogRoot(), toUtf8(replacementPackage.root));
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, MigratesResidentHandlesAndKeepsPreviousLeasePayloadAlive)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_resident_reload");
    auto replacementPackage = writeTextureMaterialPackage("tina_asset_system_resident_reload_new");
    constexpr std::array ReplacementPayload{
        std::byte{0x51}, std::byte{0x62}, std::byte{0x73}, std::byte{0x84},
    };
    rewriteTexturePayload(replacementPackage, ReplacementPayload);
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    auto oldMaterial = system->loadOne(package.materialId);
    ASSERT_TRUE(oldMaterial.has_value()) << oldMaterial.error().message;
    const auto oldTexture = system->find(package.textureId);
    ASSERT_TRUE(oldTexture.has_value());
    auto oldTextureLease = system->acquire(*oldTexture);
    ASSERT_TRUE(oldTextureLease.has_value()) << oldTextureLease.error().message;
    ASSERT_NE(oldTextureLease->get(), nullptr);
    const std::pmr::vector<std::byte> oldPayload(
        oldTextureLease->get()->payload().begin(), oldTextureLease->get()->payload().end(),
        &resource);

    auto replacementCatalog = openCatalog(resource, replacementPackage);
    ASSERT_TRUE(replacementCatalog.has_value()) << replacementCatalog.error().message;
    auto directBindStatus = system->bindCatalog(toUtf8(replacementPackage.root), std::move(*replacementCatalog));
    ASSERT_FALSE(directBindStatus.has_value());
    EXPECT_EQ(directBindStatus.error().code, AssetErrorCode::CatalogReloadBusy);

    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root));
    ASSERT_TRUE(reload.has_value()) << reload.error().message;
    EXPECT_EQ(reload->changes.modifiedCount, 1U);
    EXPECT_EQ(reload->changes.affectedCount, 1U);
    ASSERT_EQ(reload->residentMigrations.size(), 2U);

    const auto* textureMigration = findMigration(*reload, package.textureId);
    ASSERT_NE(textureMigration, nullptr);
    EXPECT_EQ(textureMigration->kind, CatalogResidentMigrationKind::Replaced);
    EXPECT_EQ(textureMigration->previous, *oldTexture);
    EXPECT_TRUE(textureMigration->current);
    EXPECT_NE(textureMigration->current, *oldTexture);

    const auto* materialMigration = findMigration(*reload, package.materialId);
    ASSERT_NE(materialMigration, nullptr);
    EXPECT_EQ(materialMigration->kind, CatalogResidentMigrationKind::Replaced);
    EXPECT_EQ(materialMigration->previous, *oldMaterial);
    EXPECT_TRUE(materialMigration->current);
    EXPECT_NE(materialMigration->current, *oldMaterial);

    EXPECT_EQ(system->catalogRoot(), toUtf8(replacementPackage.root));
    EXPECT_EQ(system->find(package.textureId), textureMigration->current);
    EXPECT_EQ(system->find(package.materialId), materialMigration->current);
    EXPECT_EQ(system->state(*oldTexture), AssetLogicalState::UnloadPending);
    EXPECT_EQ(system->state(*oldMaterial), AssetLogicalState::Unloaded);
    ASSERT_NE(oldTextureLease->get(), nullptr);
    EXPECT_TRUE(std::ranges::equal(oldTextureLease->get()->payload(), oldPayload));

    const CookedAssetFile* replacementTexture = system->tryGet(textureMigration->current);
    ASSERT_NE(replacementTexture, nullptr);
    EXPECT_TRUE(std::ranges::equal(replacementTexture->payload(), ReplacementPayload));

    *oldTextureLease = AssetLease{};
    EXPECT_EQ(system->state(*oldTexture), AssetLogicalState::Unloaded);

    ASSERT_TRUE(system->unload(textureMigration->current).has_value());
    ASSERT_TRUE(system->unload(materialMigration->current).has_value());
    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, QueuedWorkRejectsReloadAndPreservesBinding)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_reload_queued");
    const auto replacementPackage = writeTextureMaterialPackage("tina_asset_system_reload_queued_new");
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    auto queued = system->requestOne(package.textureId);
    ASSERT_TRUE(queued.has_value()) << queued.error().message;

    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root));
    ASSERT_FALSE(reload.has_value());
    EXPECT_EQ(reload.error().code, AssetErrorCode::CatalogReloadBusy);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(package.textureId), *queued);

    ASSERT_TRUE(system->unload(*queued).has_value());
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
    auto status = system->reloadCatalog(toUtf8(invalidPackage.root), reloadConfig);
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
    auto status = system->reloadCatalog(toUtf8(replacementPackage.root), reloadConfig);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::CatalogCapacityExceeded);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    ASSERT_NE(system->catalog(), nullptr);
    EXPECT_EQ(system->catalog()->entryCount(), 2U);

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, ResidentMigrationCapacityPreflightPreservesHandles)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_reload_resident_capacity");
    auto replacementPackage =
        writeTextureMaterialPackage("tina_asset_system_reload_resident_capacity_new");
    rewriteTexturePayload(replacementPackage,
                          {std::byte{0x21}, std::byte{0x32}, std::byte{0x43}, std::byte{0x54}});
    auto system = makeSystem(resource);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    auto oldMaterial = system->loadOne(package.materialId);
    ASSERT_TRUE(oldMaterial.has_value()) << oldMaterial.error().message;
    const auto oldTexture = system->find(package.textureId);
    ASSERT_TRUE(oldTexture.has_value());
    ASSERT_EQ(system->store().activeCount(), 2U);

    CatalogReloadConfig reloadConfig{};
    reloadConfig.maxResidentMigrations = 1U;
    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root), reloadConfig);
    ASSERT_FALSE(reload.has_value());
    EXPECT_EQ(reload.error().code, AssetErrorCode::CatalogCapacityExceeded);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(package.textureId), *oldTexture);
    EXPECT_EQ(system->find(package.materialId), *oldMaterial);
    EXPECT_EQ(system->store().activeCount(), 2U);

    ASSERT_TRUE(system->unload(*oldMaterial).has_value());
    ASSERT_TRUE(system->unload(*oldTexture).has_value());
    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, DoubleResidencyHeadroomFailurePreservesResidentHandles)
{
    TrackingMemoryResource resource;
    const auto package = writeTextureMaterialPackage("tina_asset_system_reload_headroom");
    auto replacementPackage = writeTextureMaterialPackage("tina_asset_system_reload_headroom_new");
    rewriteTexturePayload(replacementPackage,
                          {std::byte{0x31}, std::byte{0x42}, std::byte{0x53}, std::byte{0x64}});
    auto system = makeSystem(resource, 2U);
    ASSERT_TRUE(system.has_value()) << system.error().message;

    auto initialCatalog = openCatalog(resource, package);
    ASSERT_TRUE(initialCatalog.has_value()) << initialCatalog.error().message;
    ASSERT_TRUE(system->bindCatalog(toUtf8(package.root), std::move(*initialCatalog)).has_value());
    auto oldMaterial = system->loadOne(package.materialId);
    ASSERT_TRUE(oldMaterial.has_value()) << oldMaterial.error().message;
    const auto oldTexture = system->find(package.textureId);
    ASSERT_TRUE(oldTexture.has_value());

    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root));
    ASSERT_FALSE(reload.has_value());
    EXPECT_EQ(reload.error().code, AssetErrorCode::CatalogCapacityExceeded);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(package.textureId), *oldTexture);
    EXPECT_EQ(system->find(package.materialId), *oldMaterial);
    EXPECT_EQ(system->store().activeCount(), 2U);

    ASSERT_TRUE(system->unload(*oldMaterial).has_value());
    ASSERT_TRUE(system->unload(*oldTexture).has_value());
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

    std::optional<Core::Error> reloadError;
    std::thread worker([&]() {
        auto reload = system->reloadCatalog(toUtf8(package.root));
        if (!reload)
        {
            reloadError = std::move(reload.error());
        }
    });
    worker.join();

    ASSERT_TRUE(reloadError.has_value());
    EXPECT_EQ(reloadError->code, AssetErrorCode::WrongOwnerThread);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    removePackage(package);
}

} // namespace
} // namespace Tina::Asset
