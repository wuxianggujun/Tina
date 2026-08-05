#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::TrackingMemoryResource;
using TestSupport::writeCookedPackage;
using TestSupport::writeTextureMaterialPackage;

[[nodiscard]] TestSupport::Bytes takeBytes(Core::Result<std::vector<std::byte>> result)
{
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : TestSupport::Bytes{};
}

[[nodiscard]] TestSupport::CookedPackageAsset makeTexturePackageAsset(Core::u8 seed,
                                                                      Core::u8 colorSeed)
{
    const std::array pixels{
        static_cast<std::byte>(colorSeed),
        static_cast<std::byte>(colorSeed + 1U),
        static_cast<std::byte>(colorSeed + 2U),
        std::byte{0xFF},
    };
    return TestSupport::CookedPackageAsset{
        .assetId = TestSupport::assetId(seed),
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .cookedBytes = takeBytes(AssetFormat::writeCookedTexture2DAsset(
            TestSupport::assetId(seed),
            AssetFormat::Texture2DPayloadDesc{.width = 1, .height = 1, .pixels = pixels})),
    };
}

[[nodiscard]] TestSupport::CookedPackageAsset makeMaterialPackageAsset(
    Core::u8 seed, AssetFormat::MaterialPayloadDesc desc = {})
{
    std::vector<AssetFormat::CookedAssetWriteDependency> dependencies;
    const auto addDependency = [&dependencies](Core::AssetId assetId) {
        if (assetId)
        {
            dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                .assetId = assetId,
                .expectedKind = AssetFormat::AssetKind::Texture2D,
                .flags = AssetFormat::DependencyFlags::Required,
            });
        }
    };
    addDependency(desc.baseColorTextureId);
    addDependency(desc.metallicRoughnessTextureId);
    addDependency(desc.normalTextureId);
    return TestSupport::CookedPackageAsset{
        .assetId = TestSupport::assetId(seed),
        .assetKind = AssetFormat::AssetKind::Material,
        .cookedBytes = takeBytes(
            AssetFormat::writeCookedMaterialAsset(TestSupport::assetId(seed), desc)),
        .dependencies = std::move(dependencies),
    };
}

[[nodiscard]] TestSupport::CookedPackageAsset makeSpritePackageAsset(
    Core::u8 seed, Core::AssetId textureId)
{
    std::vector dependencies{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    return TestSupport::CookedPackageAsset{
        .assetId = TestSupport::assetId(seed),
        .assetKind = AssetFormat::AssetKind::Sprite,
        .cookedBytes = takeBytes(AssetFormat::writeCookedSpriteAsset(
            TestSupport::assetId(seed),
            AssetFormat::SpritePayloadDesc{
                .u0 = 0.0F,
                .v0 = 0.0F,
                .u1 = 1.0F,
                .v1 = 1.0F,
                .pivotX = 0.5F,
                .pivotY = 0.5F,
                .pixelsPerUnit = 16.0F,
                .textureId = textureId,
            })),
        .dependencies = std::move(dependencies),
    };
}

[[nodiscard]] TestSupport::CookedPackageAsset makeMeshPackageAsset(Core::u8 seed,
                                                                   float positionOffset)
{
    std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{};
    std::array<float, 24U * AssetFormat::StaticMeshWire::FloatsPerVertex> vertices{};
    std::array<Core::u16, 36> indices{};
    const auto desc = AssetFormat::makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);
    vertices[0] += positionOffset;
    return TestSupport::CookedPackageAsset{
        .assetId = TestSupport::assetId(seed),
        .assetKind = AssetFormat::AssetKind::StaticMesh,
        .cookedBytes = takeBytes(
            AssetFormat::writeCookedStaticMeshAsset(TestSupport::assetId(seed), desc)),
    };
}

class CatalogReloadRenderDevice final : public Render::IRenderDevice {
  public:
    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(
        const Render::RenderFrame&) override
    {
        return Render::RenderFrameSubmission::SkippedSuspendedSurface();
    }

    [[nodiscard]] Core::Status present() override { return Core::success(); }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override { return {}; }
    void shutdown() noexcept override {}

    [[nodiscard]] Core::Result<Render::GpuTextureId> createTexture2DRgba8(
        const Render::Texture2DUploadDesc&) override
    {
        ++m_textureUploadAttempts;
        if (m_rejectTextureUploadAttempt == m_textureUploadAttempts)
        {
            m_rejectTextureUploadAttempt = 0;
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "catalog reload test rejected Texture2D upload");
        }
        return Render::GpuTextureId{m_nextTextureIndex++, 1U};
    }

    [[nodiscard]] Core::Status validateTexture2D(Render::GpuTextureId texture) const noexcept override
    {
        return texture
                   ? Core::success()
                   : Core::failure(Render::RenderErrorCode::TextureNotFound,
                                   "catalog reload test received an invalid Texture2D owner");
    }

    [[nodiscard]] Core::Status setTexture2DBinding(Core::u32 bindingKey,
                                                   Render::GpuTextureId texture) noexcept override
    {
        ++m_textureBindingAttempts;
        if (m_rejectTextureBindingAttempt == m_textureBindingAttempts)
        {
            m_rejectTextureBindingAttempt = 0;
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "catalog reload test rejected Texture2D binding");
        }
        m_lastTextureBindingKey = bindingKey;
        m_lastBoundTexture = texture;
        return Core::success();
    }

    [[nodiscard]] Core::Status retireTexture2D(
        Render::GpuTextureId texture, Render::FramePin& completionPin) noexcept override
    {
        ++m_textureRetirementAttempts;
        if (m_rejectTextureRetirementAttempt == m_textureRetirementAttempts)
        {
            m_rejectTextureRetirementAttempt = 0;
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "catalog reload test rejected Texture2D retirement");
        }
        if (m_retiredTextureCount < m_retiredTextures.size())
        {
            m_retiredTextures[m_retiredTextureCount++] = texture;
        }
        completionPin.release();
        return Core::success();
    }

    [[nodiscard]] Core::Result<Render::GpuMeshId> createStaticMesh(
        const Render::StaticMeshUploadDesc&) override
    {
        return Render::GpuMeshId{m_nextMeshIndex++, 1U};
    }

    [[nodiscard]] Core::Status setMesh3DBinding(Core::u32 bindingKey,
                                                Render::GpuMeshId mesh) noexcept override
    {
        ++m_meshBindingAttempts;
        m_lastMeshBindingKey = bindingKey;
        m_lastBoundMesh = mesh;
        return Core::success();
    }

    [[nodiscard]] Core::Status retireStaticMesh(
        Render::GpuMeshId mesh, Render::FramePin& completionPin) noexcept override
    {
        if (m_retiredMeshCount < m_retiredMeshes.size())
        {
            m_retiredMeshes[m_retiredMeshCount++] = mesh;
        }
        completionPin.release();
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DMaterialBinding(
        Core::u32 bindingKey,
        const Render::Mesh3DMaterialBindingDesc& binding) noexcept override
    {
        ++m_materialBindingAttempts;
        if (m_rejectMaterialBindingAttempt == m_materialBindingAttempts)
        {
            m_rejectMaterialBindingAttempt = 0;
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "catalog reload test rejected Material binding");
        }
        m_lastMaterialBindingKey = bindingKey;
        m_lastMaterialBinding = binding;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearMesh3DMaterialBinding(Core::u32) noexcept override
    {
        ++m_materialClearCount;
        return Core::success();
    }

    void rejectNextTextureUpload() noexcept
    {
        m_rejectTextureUploadAttempt = m_textureUploadAttempts + 1U;
    }

    void rejectTextureBindingOnAttempt(Core::usize attempt) noexcept
    {
        m_rejectTextureBindingAttempt = attempt;
    }

    void rejectNextTextureBinding() noexcept
    {
        rejectTextureBindingOnAttempt(m_textureBindingAttempts + 1U);
    }

    void rejectNextTextureRetirement() noexcept
    {
        m_rejectTextureRetirementAttempt = m_textureRetirementAttempts + 1U;
    }

    void rejectNextMaterialBinding() noexcept
    {
        m_rejectMaterialBindingAttempt = m_materialBindingAttempts + 1U;
    }

    void allowAll() noexcept
    {
        m_rejectTextureUploadAttempt = 0;
        m_rejectTextureBindingAttempt = 0;
        m_rejectTextureRetirementAttempt = 0;
        m_rejectMaterialBindingAttempt = 0;
    }

    [[nodiscard]] Core::usize textureBindingAttempts() const noexcept
    {
        return m_textureBindingAttempts;
    }
    [[nodiscard]] Core::usize textureRetirementAttempts() const noexcept
    {
        return m_textureRetirementAttempts;
    }
    [[nodiscard]] Core::usize retiredTextureCount() const noexcept
    {
        return m_retiredTextureCount;
    }
    [[nodiscard]] Render::GpuTextureId retiredTexture(Core::usize index) const noexcept
    {
        return m_retiredTextures[index];
    }
    [[nodiscard]] Core::usize retiredMeshCount() const noexcept { return m_retiredMeshCount; }
    [[nodiscard]] Core::usize materialClearCount() const noexcept { return m_materialClearCount; }
    [[nodiscard]] Core::usize materialBindingAttempts() const noexcept
    {
        return m_materialBindingAttempts;
    }
    [[nodiscard]] const Render::Mesh3DMaterialBindingDesc& lastMaterialBinding() const noexcept
    {
        return m_lastMaterialBinding;
    }

  private:
    std::array<Render::GpuTextureId, 32> m_retiredTextures{};
    std::array<Render::GpuMeshId, 32> m_retiredMeshes{};
    Render::Mesh3DMaterialBindingDesc m_lastMaterialBinding{};
    Render::GpuTextureId m_lastBoundTexture{};
    Render::GpuMeshId m_lastBoundMesh{};
    Core::usize m_textureUploadAttempts = 0;
    Core::usize m_textureBindingAttempts = 0;
    Core::usize m_textureRetirementAttempts = 0;
    Core::usize m_retiredTextureCount = 0;
    Core::usize m_meshBindingAttempts = 0;
    Core::usize m_retiredMeshCount = 0;
    Core::usize m_materialBindingAttempts = 0;
    Core::usize m_materialClearCount = 0;
    Core::usize m_rejectTextureUploadAttempt = 0;
    Core::usize m_rejectTextureBindingAttempt = 0;
    Core::usize m_rejectTextureRetirementAttempt = 0;
    Core::usize m_rejectMaterialBindingAttempt = 0;
    Core::u32 m_nextTextureIndex = 100U;
    Core::u32 m_nextMeshIndex = 100U;
    Core::u32 m_lastTextureBindingKey = 0;
    Core::u32 m_lastMeshBindingKey = 0;
    Core::u32 m_lastMaterialBindingKey = 0;
};

struct RegistryCleanup final {
    CatalogReloadRenderDevice* device = nullptr;
    Sprite2DBindingRegistry* sprite = nullptr;
    Mesh3DBindingRegistry* mesh = nullptr;

    ~RegistryCleanup() noexcept
    {
        if (device != nullptr)
        {
            device->allowAll();
        }
        if (sprite != nullptr)
        {
            if (!sprite->drainPendingRetirements() || !sprite->retireAllTextureBindings())
            {
                std::terminate();
            }
        }
        if (mesh != nullptr)
        {
            if (!mesh->drainPendingRetirements() || !mesh->retireAllBindings())
            {
                std::terminate();
            }
        }
    }
};

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

TEST(AssetSystemCatalogReloadTests, SpriteParticipantCommitsReplacementAndRetriesRejectedRetirement)
{
    TrackingMemoryResource resource;
    const auto package = writeCookedPackage(
        "tina_asset_reload_sprite_gpu_owner",
        {makeTexturePackageAsset(0x41U, 0x10U)});
    const auto replacementPackage = writeCookedPackage(
        "tina_asset_reload_sprite_gpu_owner_new",
        {makeTexturePackageAsset(0x41U, 0x70U)});
    auto system = makeSystem(resource, 8U);
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(package.root)).has_value());
    const auto oldTexture = system->loadOne(TestSupport::assetId(0x41U));
    ASSERT_TRUE(oldTexture.has_value()) << oldTexture.error().message;

    CatalogReloadRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*system, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    RegistryCleanup cleanup{.device = &device, .sprite = &*registry};
    Render::GpuTextureId initialGpu{1U, 1U};
    const auto oldBinding = registry->registerTextureBinding(*oldTexture, initialGpu);
    ASSERT_TRUE(oldBinding.has_value()) << oldBinding.error().message;
    EXPECT_FALSE(initialGpu);

    device.rejectNextTextureRetirement();
    std::array participants{&*registry};
    CatalogReloadConfig config{};
    config.bindings.sprite2D = participants;
    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_TRUE(reload.has_value()) << reload.error().message;
    const auto currentTexture = system->find(TestSupport::assetId(0x41U));
    ASSERT_TRUE(currentTexture.has_value());
    EXPECT_NE(*currentTexture, *oldTexture);
    const Core::u32 currentBinding = registry->bindingKey(*currentTexture);
    EXPECT_NE(currentBinding, 0U);
    EXPECT_NE(currentBinding, *oldBinding);
    EXPECT_EQ(registry->bindingKey(*oldTexture), 0U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->pendingRetirementCount(), 1U);
    EXPECT_EQ(device.retiredTextureCount(), 0U);

    ASSERT_TRUE(registry->drainPendingRetirements().has_value());
    EXPECT_EQ(registry->pendingRetirementCount(), 0U);
    ASSERT_EQ(device.retiredTextureCount(), 1U);
    EXPECT_EQ(device.retiredTexture(0U), Render::GpuTextureId(1U, 1U));

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, SpritePrepareFailuresPreserveCatalogAndActiveOwner)
{
    TrackingMemoryResource resource;
    const auto package = writeCookedPackage(
        "tina_asset_reload_sprite_prepare_failure",
        {makeTexturePackageAsset(0x42U, 0x10U)});
    const auto replacementPackage = writeCookedPackage(
        "tina_asset_reload_sprite_prepare_failure_new",
        {makeTexturePackageAsset(0x42U, 0x60U)});
    auto system = makeSystem(resource, 8U);
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(package.root)).has_value());
    const auto oldTexture = system->loadOne(TestSupport::assetId(0x42U));
    ASSERT_TRUE(oldTexture.has_value());

    CatalogReloadRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*system, device);
    ASSERT_TRUE(registry.has_value());
    RegistryCleanup cleanup{.device = &device, .sprite = &*registry};
    Render::GpuTextureId initialGpu{2U, 1U};
    const auto oldBinding = registry->registerTextureBinding(*oldTexture, initialGpu);
    ASSERT_TRUE(oldBinding.has_value());
    std::array participants{&*registry};
    CatalogReloadConfig config{};
    config.bindings.sprite2D = participants;

    device.rejectNextTextureUpload();
    auto uploadFailure = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_FALSE(uploadFailure.has_value());
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(TestSupport::assetId(0x42U)), *oldTexture);
    EXPECT_EQ(registry->bindingKey(*oldTexture), *oldBinding);
    EXPECT_EQ(registry->pendingRetirementCount(), 0U);
    EXPECT_EQ(device.retiredTextureCount(), 0U);

    device.rejectNextTextureBinding();
    auto bindingFailure = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_FALSE(bindingFailure.has_value());
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(TestSupport::assetId(0x42U)), *oldTexture);
    EXPECT_EQ(registry->bindingKey(*oldTexture), *oldBinding);
    EXPECT_EQ(registry->pendingRetirementCount(), 0U);
    EXPECT_EQ(device.retiredTextureCount(), 1U);

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, ActiveFrameBorrowRejectsGpuOwnerTransactionBeforePublish)
{
    TrackingMemoryResource resource;
    const Core::AssetId textureId = TestSupport::assetId(0x43U);
    const Core::AssetId spriteId = TestSupport::assetId(0x44U);
    const auto package = writeCookedPackage(
        "tina_asset_reload_active_frame",
        {makeTexturePackageAsset(0x43U, 0x10U), makeSpritePackageAsset(0x44U, textureId)});
    const auto replacementPackage = writeCookedPackage(
        "tina_asset_reload_active_frame_new",
        {makeTexturePackageAsset(0x43U, 0x50U), makeSpritePackageAsset(0x44U, textureId)});
    auto system = makeSystem(resource, 8U);
    ASSERT_TRUE(system.has_value());
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(package.root)).has_value());
    const auto oldSprite = system->loadOne(spriteId);
    const auto oldTexture = system->find(textureId);
    ASSERT_TRUE(oldSprite.has_value());
    ASSERT_TRUE(oldTexture.has_value());

    CatalogReloadRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*system, device);
    ASSERT_TRUE(registry.has_value());
    RegistryCleanup cleanup{.device = &device, .sprite = &*registry};
    Render::GpuTextureId initialGpu{3U, 1U};
    ASSERT_TRUE(registry->registerTextureBinding(*oldTexture, initialGpu).has_value());

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    const auto frameTexture = registry->internSpriteFrameResource(*oldSprite, packet.resourceSink());
    ASSERT_TRUE(frameTexture.has_value());
    ASSERT_TRUE(static_cast<bool>(*frameTexture));
    std::array participants{&*registry};
    CatalogReloadConfig config{};
    config.bindings.sprite2D = participants;

    auto blocked = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(textureId), *oldTexture);
    EXPECT_EQ(device.retiredTextureCount(), 0U);

    ASSERT_TRUE(packet.abandon().has_value());
    auto committed = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(system->catalogRoot(), toUtf8(replacementPackage.root));

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, LaterParticipantFailureRollsBackEarlierPreparedGpuOwner)
{
    TrackingMemoryResource resource;
    const Core::AssetId textureId = TestSupport::assetId(0x45U);
    const auto package = writeCookedPackage(
        "tina_asset_reload_participant_rollback",
        {makeTexturePackageAsset(0x45U, 0x10U)});
    const auto replacementPackage = writeCookedPackage(
        "tina_asset_reload_participant_rollback_new",
        {makeTexturePackageAsset(0x45U, 0x40U)});
    auto system = makeSystem(resource, 8U);
    ASSERT_TRUE(system.has_value());
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(package.root)).has_value());
    const auto oldTexture = system->loadOne(textureId);
    ASSERT_TRUE(oldTexture.has_value());

    CatalogReloadRenderDevice device;
    auto firstRegistry = Sprite2DBindingRegistry::Create(*system, device);
    auto secondRegistry = Sprite2DBindingRegistry::Create(*system, device);
    ASSERT_TRUE(firstRegistry.has_value());
    ASSERT_TRUE(secondRegistry.has_value());
    RegistryCleanup firstCleanup{.device = &device, .sprite = &*firstRegistry};
    RegistryCleanup secondCleanup{.device = &device, .sprite = &*secondRegistry};
    Render::GpuTextureId firstGpu{4U, 1U};
    Render::GpuTextureId secondGpu{5U, 1U};
    const auto firstBinding = firstRegistry->registerTextureBinding(*oldTexture, firstGpu);
    const auto secondBinding = secondRegistry->registerTextureBinding(*oldTexture, secondGpu);
    ASSERT_TRUE(firstBinding.has_value());
    ASSERT_TRUE(secondBinding.has_value());

    device.rejectTextureBindingOnAttempt(device.textureBindingAttempts() + 2U);
    std::array participants{&*firstRegistry, &*secondRegistry};
    CatalogReloadConfig config{};
    config.bindings.sprite2D = participants;
    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_FALSE(reload.has_value());
    EXPECT_EQ(system->catalogRoot(), toUtf8(package.root));
    EXPECT_EQ(system->find(textureId), *oldTexture);
    EXPECT_EQ(firstRegistry->bindingKey(*oldTexture), *firstBinding);
    EXPECT_EQ(secondRegistry->bindingKey(*oldTexture), *secondBinding);
    EXPECT_EQ(firstRegistry->pendingRetirementCount(), 0U);
    EXPECT_EQ(secondRegistry->pendingRetirementCount(), 0U);
    EXPECT_EQ(device.retiredTextureCount(), 2U);

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, MeshParticipantMigratesMeshMaterialAndSharedTextureTogether)
{
    TrackingMemoryResource resource;
    const Core::AssetId textureId = TestSupport::assetId(0x51U);
    const Core::AssetId materialId = TestSupport::assetId(0x52U);
    const Core::AssetId meshId = TestSupport::assetId(0x53U);
    const AssetFormat::MaterialPayloadDesc materialDesc{.baseColorTextureId = textureId};
    const auto package = writeCookedPackage(
        "tina_asset_reload_mesh_graph",
        {makeTexturePackageAsset(0x51U, 0x10U),
         makeMaterialPackageAsset(0x52U, materialDesc),
         makeMeshPackageAsset(0x53U, 0.0F)});
    const auto replacementPackage = writeCookedPackage(
        "tina_asset_reload_mesh_graph_new",
        {makeTexturePackageAsset(0x51U, 0x60U),
         makeMaterialPackageAsset(0x52U, materialDesc),
         makeMeshPackageAsset(0x53U, 0.25F)});
    auto system = makeSystem(resource, 16U);
    ASSERT_TRUE(system.has_value());
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(package.root)).has_value());
    const auto oldMaterial = system->loadOne(materialId);
    const auto oldTexture = system->find(textureId);
    const auto oldMesh = system->loadOne(meshId);
    ASSERT_TRUE(oldMaterial.has_value());
    ASSERT_TRUE(oldTexture.has_value());
    ASSERT_TRUE(oldMesh.has_value());

    CatalogReloadRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*system, device);
    ASSERT_TRUE(registry.has_value());
    RegistryCleanup cleanup{.device = &device, .mesh = &*registry};
    Render::GpuTextureId initialTexture{6U, 1U};
    Render::GpuMeshId initialMesh{7U, 1U};
    ASSERT_TRUE(registry->registerMaterialTexture(*oldTexture, initialTexture).has_value());
    const auto oldMaterialBinding = registry->registerMaterialBinding(*oldMaterial);
    const auto oldMeshBinding = registry->registerMeshBinding(*oldMesh, initialMesh);
    ASSERT_TRUE(oldMaterialBinding.has_value());
    ASSERT_TRUE(oldMeshBinding.has_value());

    std::array participants{&*registry};
    CatalogReloadConfig config{};
    config.bindings.mesh3D = participants;
    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_TRUE(reload.has_value()) << reload.error().message;
    const auto currentTexture = system->find(textureId);
    const auto currentMaterial = system->find(materialId);
    const auto currentMesh = system->find(meshId);
    ASSERT_TRUE(currentTexture.has_value());
    ASSERT_TRUE(currentMaterial.has_value());
    ASSERT_TRUE(currentMesh.has_value());
    EXPECT_TRUE(registry->hasMaterialTexture(*currentTexture));
    EXPECT_FALSE(registry->hasMaterialTexture(*oldTexture));
    EXPECT_TRUE(static_cast<bool>(device.lastMaterialBinding().baseColorTexture));
    EXPECT_NE(device.lastMaterialBinding().baseColorTexture, Render::GpuTextureId(6U, 1U));
    EXPECT_EQ(device.retiredTextureCount(), 1U);
    EXPECT_EQ(device.retiredMeshCount(), 1U);
    EXPECT_EQ(device.materialClearCount(), 1U);

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(2U).has_value());
    const auto meshResource = registry->internMeshFrameResource(*currentMesh, packet.resourceSink());
    const auto materialResource = registry->internMaterialFrameResource(*currentMaterial, packet.resourceSink());
    ASSERT_TRUE(meshResource.has_value());
    ASSERT_TRUE(materialResource.has_value());
    const auto* meshDescriptor = packet.resourceTableView().resolve(
        *meshResource, Render::FrameResourceKind::Mesh3DGeometry);
    const auto* materialDescriptor = packet.resourceTableView().resolve(
        *materialResource, Render::FrameResourceKind::Mesh3DMaterial);
    ASSERT_NE(meshDescriptor, nullptr);
    ASSERT_NE(materialDescriptor, nullptr);
    EXPECT_NE(meshDescriptor->deviceBindingKey, *oldMeshBinding);
    EXPECT_NE(materialDescriptor->deviceBindingKey, *oldMaterialBinding);
    ASSERT_TRUE(packet.abandon().has_value());

    removePackage(package);
    removePackage(replacementPackage);
}

TEST(AssetSystemCatalogReloadTests, MaterialReloadCanAddNewResidentTextureDependency)
{
    TrackingMemoryResource resource;
    const Core::AssetId textureId = TestSupport::assetId(0x61U);
    const Core::AssetId materialId = TestSupport::assetId(0x62U);
    const auto package = writeCookedPackage(
        "tina_asset_reload_material_dependency",
        {makeMaterialPackageAsset(0x62U)});
    const auto replacementPackage = writeCookedPackage(
        "tina_asset_reload_material_dependency_new",
        {makeTexturePackageAsset(0x61U, 0x20U),
         makeMaterialPackageAsset(
             0x62U, AssetFormat::MaterialPayloadDesc{.baseColorTextureId = textureId})});
    auto system = makeSystem(resource, 8U);
    ASSERT_TRUE(system.has_value());
    ASSERT_TRUE(system->openAndBindCatalog(toUtf8(package.root)).has_value());
    const auto oldMaterial = system->loadOne(materialId);
    ASSERT_TRUE(oldMaterial.has_value());

    CatalogReloadRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(
        *system, device,
        Mesh3DBindingRegistryConfig{.meshCapacity = 1U,
                                    .materialCapacity = 1U,
                                    .textureCapacity = 1U});
    ASSERT_TRUE(registry.has_value());
    RegistryCleanup cleanup{.device = &device, .mesh = &*registry};
    ASSERT_TRUE(registry->registerMaterialBinding(*oldMaterial).has_value());
    EXPECT_EQ(registry->textureOwnerCount(), 0U);

    std::array participants{&*registry};
    CatalogReloadConfig config{};
    config.bindings.mesh3D = participants;
    auto reload = system->reloadCatalog(toUtf8(replacementPackage.root), config);
    ASSERT_TRUE(reload.has_value()) << reload.error().message;
    const auto currentTexture = system->find(textureId);
    const auto currentMaterial = system->find(materialId);
    ASSERT_TRUE(currentTexture.has_value());
    ASSERT_TRUE(currentMaterial.has_value());
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    EXPECT_TRUE(registry->hasMaterialTexture(*currentTexture));
    EXPECT_TRUE(static_cast<bool>(device.lastMaterialBinding().baseColorTexture));
    const auto* textureMigration = findMigration(*reload, textureId);
    ASSERT_NE(textureMigration, nullptr);
    EXPECT_EQ(textureMigration->kind, CatalogResidentMigrationKind::LoadedDependency);

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(3U).has_value());
    const auto materialResource = registry->internMaterialFrameResource(
        *currentMaterial, packet.resourceSink());
    ASSERT_TRUE(materialResource.has_value());
    EXPECT_TRUE(static_cast<bool>(*materialResource));
    ASSERT_TRUE(packet.abandon().has_value());

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
