#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/render/RenderErrors.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace Tina::Asset {
namespace {

using TestSupport::assetId;
using TestSupport::TrackingMemoryResource;

class ThrowingMemoryResource final : public std::pmr::memory_resource {
  public:
    explicit ThrowingMemoryResource(std::size_t rejectedAllocationMinimumBytes) noexcept
        : m_rejectedAllocationMinimumBytes(rejectedAllocationMinimumBytes)
    {
    }

    [[nodiscard]] Core::usize rejectedAllocations() const noexcept { return m_rejectedAllocations; }
    [[nodiscard]] Core::usize outstandingAllocations() const noexcept { return m_outstandingAllocations; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (bytes >= m_rejectedAllocationMinimumBytes)
        {
            ++m_rejectedAllocations;
            throw std::bad_alloc{};
        }
        void* allocation = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_outstandingAllocations;
        return allocation;
    }

    void do_deallocate(void* allocation, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(allocation, bytes, alignment);
        --m_outstandingAllocations;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::size_t m_rejectedAllocationMinimumBytes = 0;
    Core::usize m_rejectedAllocations = 0;
    Core::usize m_outstandingAllocations = 0;
};

class RecordingRenderDevice final : public Render::IRenderDevice {
  public:
    struct MeshCall final {
        Core::u32 bindingKey = 0;
        Render::GpuMeshId mesh{};
    };

    struct MaterialCall final {
        Core::u32 bindingKey = 0;
        Render::Mesh3DMaterialBindingDesc binding{};
    };

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame&) override
    {
        return Render::RenderFrameSubmission::SkippedSuspendedSurface();
    }

    [[nodiscard]] Core::Status present() override { return Core::success(); }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override { return {}; }
    void shutdown() noexcept override {}

    [[nodiscard]] Core::Status setMesh3DBinding(Core::u32 bindingKey,
                                                Render::GpuMeshId mesh) noexcept override
    {
        if (m_meshCallCount >= m_meshCalls.size())
        {
            return Core::failure(Render::RenderErrorCode::MeshUploadUnsupported,
                                 "recording Mesh3D test device mesh call capacity exceeded");
        }
        m_meshCalls[m_meshCallCount++] = MeshCall{.bindingKey = bindingKey, .mesh = mesh};
        if (m_rejectNextMeshUpdate)
        {
            m_rejectNextMeshUpdate = false;
            return Core::failure(Render::RenderErrorCode::MeshUploadUnsupported,
                                 "recording Mesh3D test device rejected mesh update");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DMaterialBinding(
        Core::u32 bindingKey, const Render::Mesh3DMaterialBindingDesc& binding) noexcept override
    {
        if (m_materialCallCount >= m_materialCalls.size())
        {
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "recording Mesh3D test device material call capacity exceeded");
        }
        m_materialCalls[m_materialCallCount++] = MaterialCall{.bindingKey = bindingKey, .binding = binding};
        if (m_rejectNextMaterialUpdate)
        {
            m_rejectNextMaterialUpdate = false;
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "recording Mesh3D test device rejected material update");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status clearMesh3DMaterialBinding(Core::u32 bindingKey) noexcept override
    {
        if (m_materialClearCount >= m_materialClears.size())
        {
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "recording Mesh3D test device clear call capacity exceeded");
        }
        m_materialClears[m_materialClearCount++] = bindingKey;
        if (m_rejectNextMaterialClear)
        {
            m_rejectNextMaterialClear = false;
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "recording Mesh3D test device rejected material clear");
        }
        return Core::success();
    }

    void rejectNextMeshUpdate() noexcept { m_rejectNextMeshUpdate = true; }
    void rejectNextMaterialUpdate() noexcept { m_rejectNextMaterialUpdate = true; }
    void rejectNextMaterialClear() noexcept { m_rejectNextMaterialClear = true; }

    [[nodiscard]] Core::usize meshCallCount() const noexcept { return m_meshCallCount; }
    [[nodiscard]] Core::usize materialCallCount() const noexcept { return m_materialCallCount; }
    [[nodiscard]] Core::usize materialClearCount() const noexcept { return m_materialClearCount; }
    [[nodiscard]] const MeshCall& meshCall(Core::usize index) const noexcept { return m_meshCalls[index]; }
    [[nodiscard]] const MaterialCall& materialCall(Core::usize index) const noexcept
    {
        return m_materialCalls[index];
    }
    [[nodiscard]] Core::u32 materialClear(Core::usize index) const noexcept
    {
        return m_materialClears[index];
    }

  private:
    std::array<MeshCall, 1024> m_meshCalls{};
    std::array<MaterialCall, 1024> m_materialCalls{};
    std::array<Core::u32, 1024> m_materialClears{};
    Core::usize m_meshCallCount = 0;
    Core::usize m_materialCallCount = 0;
    Core::usize m_materialClearCount = 0;
    bool m_rejectNextMeshUpdate = false;
    bool m_rejectNextMaterialUpdate = false;
    bool m_rejectNextMaterialClear = false;
};

[[nodiscard]] CookedAssetFile loadCooked(std::pmr::memory_resource& memory,
                                         Core::Result<std::vector<std::byte>> bytes)
{
    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error().message);
    if (!bytes)
    {
        return {};
    }
    std::pmr::vector<std::byte> owned{&memory};
    owned.assign(bytes->begin(), bytes->end());
    auto file = makeCookedAssetFileFromBytes(std::move(owned), CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    return file ? std::move(*file) : CookedAssetFile{};
}

[[nodiscard]] CookedAssetFile makeMesh(std::pmr::memory_resource& memory, Core::u8 seed)
{
    constexpr std::array payload{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    return loadCooked(memory, AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
                                  .assetKind = AssetFormat::AssetKind::StaticMesh,
                                  .assetTypeVersion = 1,
                                  .assetId = assetId(seed),
                                  .payload = payload,
                              }));
}

[[nodiscard]] CookedAssetFile makeTexture(std::pmr::memory_resource& memory, Core::u8 seed)
{
    constexpr std::array pixels{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    return loadCooked(memory,
                      AssetFormat::writeCookedTexture2DAsset(
                          assetId(seed), AssetFormat::Texture2DPayloadDesc{.width = 1, .height = 1, .pixels = pixels}));
}

[[nodiscard]] CookedAssetFile makeMaterial(std::pmr::memory_resource& memory, Core::u8 seed,
                                           AssetFormat::MaterialPayloadDesc desc = {})
{
    return loadCooked(memory, AssetFormat::writeCookedMaterialAsset(assetId(seed), desc));
}

[[nodiscard]] CookedAssetFile makeMaterialWithDependencies(
    std::pmr::memory_resource& memory, Core::u8 seed, const AssetFormat::MaterialPayloadDesc& desc,
    std::span<const AssetFormat::CookedAssetWriteDependency> dependencies)
{
    auto payload = AssetFormat::writeMaterialPayloadBytes(desc);
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    if (!payload)
    {
        return {};
    }
    return loadCooked(memory, AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
                                  .assetKind = AssetFormat::AssetKind::Material,
                                  .assetTypeVersion = AssetFormat::MaterialWire::SchemaVersion,
                                  .assetId = assetId(seed),
                                  .dependencies = dependencies,
                                  .payload = *payload,
                              }));
}

[[nodiscard]] Core::Result<AssetStore> makeStore(std::pmr::memory_resource& memory, Core::usize capacity = 32U)
{
    return AssetStore::Create(AssetStoreConfig{.capacity = capacity, .memoryResource = &memory});
}

[[nodiscard]] Mesh3DMaterialGpuBindingDesc oneTextureBinding(AssetHandle texture,
                                                             Render::GpuTextureId gpuTexture) noexcept
{
    return Mesh3DMaterialGpuBindingDesc{
        .baseColor = Mesh3DMaterialTextureBinding{.textureAsset = texture, .gpuTexture = gpuTexture},
    };
}

TEST(Mesh3DBindingRegistryTests, CreateValidatesCapacityBoundsAndReportsPmrAllocationFailure)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    RecordingRenderDevice device;

    auto zeroMesh = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.meshCapacity = 0, .materialCapacity = 1,
                                                    .memoryResource = &memory});
    ASSERT_FALSE(zeroMesh.has_value());
    EXPECT_EQ(zeroMesh.error().code, AssetErrorCode::InvalidCatalogConfig);
    auto zeroMaterial = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.meshCapacity = 1, .materialCapacity = 0,
                                                    .memoryResource = &memory});
    ASSERT_FALSE(zeroMaterial.has_value());
    EXPECT_EQ(zeroMaterial.error().code, AssetErrorCode::InvalidCatalogConfig);
    auto excessive = Mesh3DBindingRegistry::Create(
        *store, device,
        Mesh3DBindingRegistryConfig{.meshCapacity = MaximumMesh3DBindingCapacity + 1U,
                                    .materialCapacity = MaximumMesh3DBindingCapacity + 1U,
                                    .memoryResource = &memory});
    ASSERT_FALSE(excessive.has_value());
    EXPECT_EQ(excessive.error().code, AssetErrorCode::InvalidCatalogConfig);

    ThrowingMemoryResource throwingMemory{64U};
    auto allocationFailure = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.memoryResource = &throwingMemory});
    ASSERT_FALSE(allocationFailure.has_value());
    EXPECT_EQ(allocationFailure.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_EQ(throwingMemory.rejectedAllocations(), 1U);
    EXPECT_EQ(throwingMemory.outstandingAllocations(), 0U);
}

TEST(Mesh3DBindingRegistryTests, RegistersMeshAndDerivesMaterialFactorsFromCookedPayload)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto mesh = store->publish(makeMesh(memory, 1U));
    auto material = store->publish(makeMaterial(memory, 2U, AssetFormat::MaterialPayloadDesc{
                                                                .metallicFactor = 0.25F,
                                                                .roughnessFactor = 0.75F,
                                                            }));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.meshCapacity = 2, .materialCapacity = 2,
                                                    .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value()) << registry.error().message;

    auto meshKey = registry->registerMeshBinding(*mesh, Render::GpuMeshId{7U, 3U});
    auto materialKey = registry->registerMaterialBinding(*material, {});
    ASSERT_TRUE(meshKey.has_value()) << meshKey.error().message;
    ASSERT_TRUE(materialKey.has_value()) << materialKey.error().message;
    EXPECT_EQ(*meshKey, 2U);
    EXPECT_EQ(*materialKey, 2U);
    EXPECT_EQ(registry->meshCapacity(), 2U);
    EXPECT_EQ(registry->materialCapacity(), 2U);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(registry->resolveMesh(*mesh), *meshKey);
    EXPECT_EQ(registry->resolveMaterial(*material), *materialKey);
    ASSERT_EQ(device.meshCallCount(), 1U);
    EXPECT_EQ(device.meshCall(0).bindingKey, *meshKey);
    EXPECT_EQ(device.meshCall(0).mesh, (Render::GpuMeshId{7U, 3U}));
    ASSERT_EQ(device.materialCallCount(), 1U);
    const auto& renderBinding = device.materialCall(0).binding;
    EXPECT_FALSE(renderBinding.baseColorTexture);
    EXPECT_FALSE(renderBinding.metallicRoughnessTexture);
    EXPECT_FALSE(renderBinding.normalTexture);
    EXPECT_FLOAT_EQ(renderBinding.metallicFactor, 0.25F);
    EXPECT_FLOAT_EQ(renderBinding.roughnessFactor, 0.75F);
}

TEST(Mesh3DBindingRegistryTests, MaterialRolesRequireExactCookedTextureDependencies)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId baseColorTextureId = assetId(1U);
    const Core::AssetId normalTextureId = assetId(2U);
    auto baseColorTexture = store->publish(makeTexture(memory, 1U));
    auto normalTexture = store->publish(makeTexture(memory, 2U));
    const AssetFormat::MaterialPayloadDesc materialDesc{
        .metallicFactor = 0.4F,
        .roughnessFactor = 0.6F,
        .baseColorTextureId = baseColorTextureId,
        .normalTextureId = normalTextureId,
    };
    auto material = store->publish(makeMaterial(memory, 3U, materialDesc));
    ASSERT_TRUE(baseColorTexture.has_value());
    ASSERT_TRUE(normalTexture.has_value());
    ASSERT_TRUE(material.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuTextureId BaseColorGpuTexture{8U, 2U};
    constexpr Render::GpuTextureId NormalGpuTexture{9U, 2U};
    const Mesh3DMaterialTextureBinding baseColorRole{
        .textureAsset = *baseColorTexture,
        .gpuTexture = BaseColorGpuTexture,
    };
    const Mesh3DMaterialTextureBinding normalRole{
        .textureAsset = *normalTexture,
        .gpuTexture = NormalGpuTexture,
    };

    const auto swapped = registry->registerMaterialBinding(
        *material, Mesh3DMaterialGpuBindingDesc{.baseColor = normalRole, .normal = baseColorRole});
    ASSERT_FALSE(swapped.has_value());
    EXPECT_EQ(swapped.error().code, AssetErrorCode::InvalidCatalogConfig);
    EXPECT_EQ(device.materialCallCount(), 0U);
    auto key = registry->registerMaterialBinding(
        *material, Mesh3DMaterialGpuBindingDesc{.baseColor = baseColorRole, .normal = normalRole});
    ASSERT_TRUE(key.has_value()) << key.error().message;
    ASSERT_EQ(device.materialCallCount(), 1U);
    const auto& renderBinding = device.materialCall(0).binding;
    EXPECT_EQ(renderBinding.baseColorTexture, BaseColorGpuTexture);
    EXPECT_FALSE(renderBinding.metallicRoughnessTexture);
    EXPECT_EQ(renderBinding.normalTexture, NormalGpuTexture);
    EXPECT_FLOAT_EQ(renderBinding.metallicFactor, 0.4F);
    EXPECT_FLOAT_EQ(renderBinding.roughnessFactor, 0.6F);

    auto undeclaredMaterial = store->publish(makeMaterial(memory, 4U));
    ASSERT_TRUE(undeclaredMaterial.has_value());
    const auto undeclared = registry->registerMaterialBinding(
        *undeclaredMaterial, oneTextureBinding(*baseColorTexture, BaseColorGpuTexture));
    ASSERT_FALSE(undeclared.has_value());
    EXPECT_EQ(undeclared.error().code, AssetErrorCode::InvalidCatalogConfig);

    const std::array wrongFlags{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = baseColorTextureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required | AssetFormat::DependencyFlags::Deferred,
        },
    };
    auto malformed = store->publish(makeMaterialWithDependencies(
        memory, 5U, AssetFormat::MaterialPayloadDesc{.baseColorTextureId = baseColorTextureId}, wrongFlags));
    ASSERT_TRUE(malformed.has_value());
    const auto wrongDependency = registry->registerMaterialBinding(
        *malformed, oneTextureBinding(*baseColorTexture, BaseColorGpuTexture));
    ASSERT_FALSE(wrongDependency.has_value());
    EXPECT_EQ(wrongDependency.error().code, AssetErrorCode::InvalidCatalogConfig);
    EXPECT_EQ(device.materialCallCount(), 1U);
}

TEST(Mesh3DBindingRegistryTests, RegistrationRejectsInvalidStaleCrossStoreWrongKindAndNotReadyHandles)
{
    TrackingMemoryResource memory;
    TrackingMemoryResource foreignMemory;
    auto store = makeStore(memory);
    auto foreignStore = makeStore(foreignMemory);
    ASSERT_TRUE(store.has_value());
    ASSERT_TRUE(foreignStore.has_value());
    auto mesh = store->publish(makeMesh(memory, 1U));
    auto material = store->publish(makeMaterial(memory, 2U));
    auto staleMesh = store->publish(makeMesh(memory, 3U));
    auto staleMaterial = store->publish(makeMaterial(memory, 4U));
    auto queuedMesh = store->beginQueued(assetId(5U), AssetFormat::AssetKind::StaticMesh);
    auto queuedMaterial = store->beginQueued(assetId(6U), AssetFormat::AssetKind::Material);
    auto foreignMesh = foreignStore->publish(makeMesh(foreignMemory, 7U));
    auto foreignMaterial = foreignStore->publish(makeMaterial(foreignMemory, 8U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());
    ASSERT_TRUE(staleMesh.has_value());
    ASSERT_TRUE(staleMaterial.has_value());
    ASSERT_TRUE(queuedMesh.has_value());
    ASSERT_TRUE(queuedMaterial.has_value());
    ASSERT_TRUE(foreignMesh.has_value());
    ASSERT_TRUE(foreignMaterial.has_value());
    ASSERT_TRUE(store->unload(*staleMesh).has_value());
    ASSERT_TRUE(store->unload(*staleMaterial).has_value());

    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuMeshId GpuMesh{1U, 1U};

    const auto emptyMesh = registry->registerMeshBinding({}, GpuMesh);
    ASSERT_FALSE(emptyMesh.has_value());
    EXPECT_EQ(emptyMesh.error().code, AssetErrorCode::InvalidHandle);
    const auto wrongMeshKind = registry->registerMeshBinding(*material, GpuMesh);
    ASSERT_FALSE(wrongMeshKind.has_value());
    EXPECT_EQ(wrongMeshKind.error().code, AssetErrorCode::InvalidHandle);
    const auto staleMeshResult = registry->registerMeshBinding(*staleMesh, GpuMesh);
    ASSERT_FALSE(staleMeshResult.has_value());
    EXPECT_EQ(staleMeshResult.error().code, AssetErrorCode::InvalidHandle);
    const auto foreignMeshResult = registry->registerMeshBinding(*foreignMesh, GpuMesh);
    ASSERT_FALSE(foreignMeshResult.has_value());
    EXPECT_EQ(foreignMeshResult.error().code, AssetErrorCode::InvalidHandle);
    const auto queuedMeshResult = registry->registerMeshBinding(*queuedMesh, GpuMesh);
    ASSERT_FALSE(queuedMeshResult.has_value());
    EXPECT_EQ(queuedMeshResult.error().code, AssetErrorCode::AssetNotReady);
    const auto invalidGpuMesh = registry->registerMeshBinding(*mesh, {});
    ASSERT_FALSE(invalidGpuMesh.has_value());
    EXPECT_EQ(invalidGpuMesh.error().code, Render::RenderErrorCode::InvalidMeshUpload);

    const auto emptyMaterial = registry->registerMaterialBinding({}, {});
    ASSERT_FALSE(emptyMaterial.has_value());
    EXPECT_EQ(emptyMaterial.error().code, AssetErrorCode::InvalidHandle);
    const auto wrongMaterialKind = registry->registerMaterialBinding(*mesh, {});
    ASSERT_FALSE(wrongMaterialKind.has_value());
    EXPECT_EQ(wrongMaterialKind.error().code, AssetErrorCode::InvalidHandle);
    const auto staleMaterialResult = registry->registerMaterialBinding(*staleMaterial, {});
    ASSERT_FALSE(staleMaterialResult.has_value());
    EXPECT_EQ(staleMaterialResult.error().code, AssetErrorCode::InvalidHandle);
    const auto foreignMaterialResult = registry->registerMaterialBinding(*foreignMaterial, {});
    ASSERT_FALSE(foreignMaterialResult.has_value());
    EXPECT_EQ(foreignMaterialResult.error().code, AssetErrorCode::InvalidHandle);
    const auto queuedMaterialResult = registry->registerMaterialBinding(*queuedMaterial, {});
    ASSERT_FALSE(queuedMaterialResult.has_value());
    EXPECT_EQ(queuedMaterialResult.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(device.meshCallCount(), 0U);
    EXPECT_EQ(device.materialCallCount(), 0U);
}

TEST(Mesh3DBindingRegistryTests, MaterialRegistrationRejectsInvalidTextureRoleInputs)
{
    TrackingMemoryResource memory;
    TrackingMemoryResource foreignMemory;
    auto store = makeStore(memory);
    auto foreignStore = makeStore(foreignMemory);
    ASSERT_TRUE(store.has_value());
    ASSERT_TRUE(foreignStore.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto material = store->publish(makeMaterial(
        memory, 2U, AssetFormat::MaterialPayloadDesc{.baseColorTextureId = textureId}));
    auto staleTexture = store->publish(makeTexture(memory, 3U));
    auto queuedTexture = store->beginQueued(assetId(4U), AssetFormat::AssetKind::Texture2D);
    auto wrongKind = store->publish(makeMesh(memory, 5U));
    auto foreignTexture = foreignStore->publish(makeTexture(foreignMemory, 6U));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());
    ASSERT_TRUE(staleTexture.has_value());
    ASSERT_TRUE(queuedTexture.has_value());
    ASSERT_TRUE(wrongKind.has_value());
    ASSERT_TRUE(foreignTexture.has_value());
    ASSERT_TRUE(store->unload(*staleTexture).has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuTextureId GpuTexture{1U, 1U};

    const auto missingGpu = registry->registerMaterialBinding(
        *material, Mesh3DMaterialGpuBindingDesc{.baseColor = {.textureAsset = *texture}});
    ASSERT_FALSE(missingGpu.has_value());
    EXPECT_EQ(missingGpu.error().code, AssetErrorCode::InvalidHandle);
    const auto wrongKindResult = registry->registerMaterialBinding(
        *material, oneTextureBinding(*wrongKind, GpuTexture));
    ASSERT_FALSE(wrongKindResult.has_value());
    EXPECT_EQ(wrongKindResult.error().code, AssetErrorCode::InvalidHandle);
    const auto stale = registry->registerMaterialBinding(
        *material, oneTextureBinding(*staleTexture, GpuTexture));
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, AssetErrorCode::InvalidHandle);
    const auto queued = registry->registerMaterialBinding(
        *material, oneTextureBinding(*queuedTexture, GpuTexture));
    ASSERT_FALSE(queued.has_value());
    EXPECT_EQ(queued.error().code, AssetErrorCode::AssetNotReady);
    const auto foreign = registry->registerMaterialBinding(
        *material, oneTextureBinding(*foreignTexture, GpuTexture));
    ASSERT_FALSE(foreign.has_value());
    EXPECT_EQ(foreign.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_EQ(device.materialCallCount(), 0U);
}

TEST(Mesh3DBindingRegistryTests, IdempotencyConflictsAndCapacityPreserveExistingBindings)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto mesh = store->publish(makeMesh(memory, 1U));
    auto sameMeshId = store->publish(makeMesh(memory, 1U));
    auto overflowMesh = store->publish(makeMesh(memory, 2U));
    const Core::AssetId textureId = assetId(5U);
    auto texture = store->publish(makeTexture(memory, 5U));
    const AssetFormat::MaterialPayloadDesc texturedMaterialDesc{.baseColorTextureId = textureId};
    auto material = store->publish(makeMaterial(memory, 3U, texturedMaterialDesc));
    auto sameMaterialId = store->publish(makeMaterial(memory, 3U, texturedMaterialDesc));
    auto overflowMaterial = store->publish(makeMaterial(memory, 4U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(sameMeshId.has_value());
    ASSERT_TRUE(overflowMesh.has_value());
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());
    ASSERT_TRUE(sameMaterialId.has_value());
    ASSERT_TRUE(overflowMaterial.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.meshCapacity = 1, .materialCapacity = 1,
                                                    .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuMeshId GpuMesh{1U, 1U};
    constexpr Render::GpuTextureId GpuTexture{1U, 1U};
    const auto gpuBinding = oneTextureBinding(*texture, GpuTexture);
    auto meshKey = registry->registerMeshBinding(*mesh, GpuMesh);
    auto materialKey = registry->registerMaterialBinding(*material, gpuBinding);
    ASSERT_TRUE(meshKey.has_value());
    ASSERT_TRUE(materialKey.has_value());

    auto duplicateMesh = registry->registerMeshBinding(*mesh, GpuMesh);
    auto duplicateMaterial = registry->registerMaterialBinding(*material, gpuBinding);
    ASSERT_TRUE(duplicateMesh.has_value());
    ASSERT_TRUE(duplicateMaterial.has_value());
    EXPECT_EQ(*duplicateMesh, *meshKey);
    EXPECT_EQ(*duplicateMaterial, *materialKey);
    const auto changedMesh = registry->registerMeshBinding(*mesh, Render::GpuMeshId{2U, 1U});
    ASSERT_FALSE(changedMesh.has_value());
    EXPECT_EQ(changedMesh.error().code, AssetErrorCode::Mesh3DBindingConflict);
    const auto duplicateMeshId = registry->registerMeshBinding(*sameMeshId, Render::GpuMeshId{3U, 1U});
    ASSERT_FALSE(duplicateMeshId.has_value());
    EXPECT_EQ(duplicateMeshId.error().code, AssetErrorCode::Mesh3DBindingConflict);
    const auto meshCapacity = registry->registerMeshBinding(*overflowMesh, Render::GpuMeshId{4U, 1U});
    ASSERT_FALSE(meshCapacity.has_value());
    EXPECT_EQ(meshCapacity.error().code, AssetErrorCode::Mesh3DBindingCapacityExceeded);
    const auto changedMaterial = registry->registerMaterialBinding(
        *material, oneTextureBinding(*texture, Render::GpuTextureId{2U, 1U}));
    ASSERT_FALSE(changedMaterial.has_value());
    EXPECT_EQ(changedMaterial.error().code, AssetErrorCode::Mesh3DBindingConflict);
    const auto duplicateMaterialId = registry->registerMaterialBinding(*sameMaterialId, gpuBinding);
    ASSERT_FALSE(duplicateMaterialId.has_value());
    EXPECT_EQ(duplicateMaterialId.error().code, AssetErrorCode::Mesh3DBindingConflict);
    const auto materialCapacity = registry->registerMaterialBinding(*overflowMaterial, {});
    ASSERT_FALSE(materialCapacity.has_value());
    EXPECT_EQ(materialCapacity.error().code, AssetErrorCode::Mesh3DBindingCapacityExceeded);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(device.meshCallCount(), 1U);
    EXPECT_EQ(device.materialCallCount(), 1U);
}

TEST(Mesh3DBindingRegistryTests, BackendRegistrationFailureRollsBackWithoutConsumingKeys)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto firstMesh = store->publish(makeMesh(memory, 1U));
    auto secondMesh = store->publish(makeMesh(memory, 2U));
    auto firstMaterial = store->publish(makeMaterial(memory, 3U));
    auto secondMaterial = store->publish(makeMaterial(memory, 4U));
    ASSERT_TRUE(firstMesh.has_value());
    ASSERT_TRUE(secondMesh.has_value());
    ASSERT_TRUE(firstMaterial.has_value());
    ASSERT_TRUE(secondMaterial.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());

    device.rejectNextMeshUpdate();
    const auto rejectedMesh = registry->registerMeshBinding(*firstMesh, Render::GpuMeshId{1U, 1U});
    ASSERT_FALSE(rejectedMesh.has_value());
    EXPECT_EQ(rejectedMesh.error().code, Render::RenderErrorCode::MeshUploadUnsupported);
    EXPECT_EQ(registry->meshBindingCount(), 0U);
    auto acceptedMesh = registry->registerMeshBinding(*secondMesh, Render::GpuMeshId{2U, 1U});
    ASSERT_TRUE(acceptedMesh.has_value());
    EXPECT_EQ(*acceptedMesh, 2U);

    device.rejectNextMaterialUpdate();
    const auto rejectedMaterial = registry->registerMaterialBinding(*firstMaterial, {});
    ASSERT_FALSE(rejectedMaterial.has_value());
    EXPECT_EQ(rejectedMaterial.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    auto acceptedMaterial = registry->registerMaterialBinding(*secondMaterial, {});
    ASSERT_TRUE(acceptedMaterial.has_value());
    EXPECT_EQ(*acceptedMaterial, 2U);
}

TEST(Mesh3DBindingRegistryTests, UnbindFailuresAreRetryableAndPreserveResolution)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto mesh = store->publish(makeMesh(memory, 1U));
    auto material = store->publish(makeMaterial(memory, 2U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto meshKey = registry->registerMeshBinding(*mesh, Render::GpuMeshId{1U, 1U});
    auto materialKey = registry->registerMaterialBinding(*material, {});
    ASSERT_TRUE(meshKey.has_value());
    ASSERT_TRUE(materialKey.has_value());

    device.rejectNextMeshUpdate();
    const auto rejectedMesh = registry->unbindMeshBinding(*mesh);
    ASSERT_FALSE(rejectedMesh.has_value());
    EXPECT_EQ(rejectedMesh.error().code, Render::RenderErrorCode::MeshUploadUnsupported);
    EXPECT_EQ(registry->resolveMesh(*mesh), *meshKey);
    ASSERT_TRUE(registry->unbindMeshBinding(*mesh).has_value());
    EXPECT_EQ(registry->resolveMesh(*mesh), 0U);
    const auto missingMesh = registry->unbindMeshBinding(*mesh);
    ASSERT_FALSE(missingMesh.has_value());
    EXPECT_EQ(missingMesh.error().code, AssetErrorCode::Mesh3DBindingNotFound);

    device.rejectNextMaterialClear();
    const auto rejectedMaterial = registry->unbindMaterialBinding(*material);
    ASSERT_FALSE(rejectedMaterial.has_value());
    EXPECT_EQ(rejectedMaterial.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(registry->resolveMaterial(*material), *materialKey);
    ASSERT_TRUE(registry->unbindMaterialBinding(*material).has_value());
    EXPECT_EQ(registry->resolveMaterial(*material), 0U);
    const auto missingMaterial = registry->unbindMaterialBinding(*material);
    ASSERT_FALSE(missingMaterial.has_value());
    EXPECT_EQ(missingMaterial.error().code, AssetErrorCode::Mesh3DBindingNotFound);
    EXPECT_EQ(registry->meshBindingCount(), 0U);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    ASSERT_EQ(device.materialClearCount(), 2U);
    EXPECT_EQ(device.materialClear(0), *materialKey);
    EXPECT_EQ(device.materialClear(1), *materialKey);
}

TEST(Mesh3DBindingRegistryTests, StaleExactHandlesCanUnbindAndReleasedKeysAreNotReused)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory, 12U);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto mesh = store->publish(makeMesh(memory, 2U));
    auto material = store->publish(makeMaterial(
        memory, 3U, AssetFormat::MaterialPayloadDesc{.baseColorTextureId = textureId}));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.meshCapacity = 1, .materialCapacity = 1,
                                                    .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto firstMeshKey = registry->registerMeshBinding(*mesh, Render::GpuMeshId{1U, 1U});
    auto firstMaterialKey = registry->registerMaterialBinding(
        *material, oneTextureBinding(*texture, Render::GpuTextureId{1U, 1U}));
    ASSERT_TRUE(firstMeshKey.has_value());
    ASSERT_TRUE(firstMaterialKey.has_value());

    ASSERT_TRUE(store->unload(*texture).has_value());
    EXPECT_EQ(registry->resolveMaterial(*material), 0U);
    ASSERT_TRUE(store->unload(*mesh).has_value());
    ASSERT_TRUE(store->unload(*material).has_value());
    EXPECT_EQ(registry->resolveMesh(*mesh), 0U);
    EXPECT_EQ(registry->resolveMaterial(*material), 0U);
    ASSERT_TRUE(registry->unbindMaterialBinding(*material).has_value());
    ASSERT_TRUE(registry->unbindMeshBinding(*mesh).has_value());

    auto nextMesh = store->publish(makeMesh(memory, 4U));
    auto nextMaterial = store->publish(makeMaterial(memory, 5U));
    ASSERT_TRUE(nextMesh.has_value());
    ASSERT_TRUE(nextMaterial.has_value());
    auto nextMeshKey = registry->registerMeshBinding(*nextMesh, Render::GpuMeshId{2U, 1U});
    auto nextMaterialKey = registry->registerMaterialBinding(*nextMaterial, {});
    ASSERT_TRUE(nextMeshKey.has_value());
    ASSERT_TRUE(nextMaterialKey.has_value());
    EXPECT_GT(*nextMeshKey, *firstMeshKey);
    EXPECT_GT(*nextMaterialKey, *firstMaterialKey);
}

TEST(Mesh3DBindingRegistryTests, RegistriesSharingOneDeviceReceiveDistinctKeysAndUnbindIndependently)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto firstMesh = store->publish(makeMesh(memory, 1U));
    auto secondMesh = store->publish(makeMesh(memory, 2U));
    auto firstMaterial = store->publish(makeMaterial(memory, 3U));
    auto secondMaterial = store->publish(makeMaterial(memory, 4U));
    ASSERT_TRUE(firstMesh.has_value());
    ASSERT_TRUE(secondMesh.has_value());
    ASSERT_TRUE(firstMaterial.has_value());
    ASSERT_TRUE(secondMaterial.has_value());
    RecordingRenderDevice device;
    auto firstRegistry = Mesh3DBindingRegistry::Create(*store, device);
    auto secondRegistry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(firstRegistry.has_value());
    ASSERT_TRUE(secondRegistry.has_value());

    auto firstMeshKey = firstRegistry->registerMeshBinding(*firstMesh, Render::GpuMeshId{1U, 1U});
    auto secondMeshKey = secondRegistry->registerMeshBinding(*secondMesh, Render::GpuMeshId{2U, 1U});
    auto firstMaterialKey = firstRegistry->registerMaterialBinding(*firstMaterial, {});
    auto secondMaterialKey = secondRegistry->registerMaterialBinding(*secondMaterial, {});
    ASSERT_TRUE(firstMeshKey.has_value());
    ASSERT_TRUE(secondMeshKey.has_value());
    ASSERT_TRUE(firstMaterialKey.has_value());
    ASSERT_TRUE(secondMaterialKey.has_value());
    EXPECT_NE(*firstMeshKey, *secondMeshKey);
    EXPECT_NE(*firstMaterialKey, *secondMaterialKey);

    ASSERT_TRUE(firstRegistry->unbindMeshBinding(*firstMesh).has_value());
    ASSERT_TRUE(firstRegistry->unbindMaterialBinding(*firstMaterial).has_value());
    EXPECT_EQ(secondRegistry->resolveMesh(*secondMesh), *secondMeshKey);
    EXPECT_EQ(secondRegistry->resolveMaterial(*secondMaterial), *secondMaterialKey);
}

TEST(Mesh3DBindingRegistryTests, WrongOwnerThreadOperationsFailWithoutMutation)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto mesh = store->publish(makeMesh(memory, 1U));
    auto material = store->publish(makeMaterial(memory, 2U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto meshKey = registry->registerMeshBinding(*mesh, Render::GpuMeshId{1U, 1U});
    auto materialKey = registry->registerMaterialBinding(*material, {});
    ASSERT_TRUE(meshKey.has_value());
    ASSERT_TRUE(materialKey.has_value());

    std::optional<Core::ErrorCode> meshRegisterError;
    std::optional<Core::ErrorCode> materialRegisterError;
    std::optional<Core::ErrorCode> meshUnbindError;
    std::optional<Core::ErrorCode> materialUnbindError;
    Core::u32 foreignMeshKey = *meshKey;
    Core::u32 foreignMaterialKey = *materialKey;
    std::thread foreign([&] {
        auto meshRegister = registry->registerMeshBinding(*mesh, Render::GpuMeshId{2U, 1U});
        if (!meshRegister) meshRegisterError = meshRegister.error().code;
        auto materialRegister = registry->registerMaterialBinding(*material, {});
        if (!materialRegister) materialRegisterError = materialRegister.error().code;
        auto meshUnbind = registry->unbindMeshBinding(*mesh);
        if (!meshUnbind) meshUnbindError = meshUnbind.error().code;
        auto materialUnbind = registry->unbindMaterialBinding(*material);
        if (!materialUnbind) materialUnbindError = materialUnbind.error().code;
        foreignMeshKey = registry->resolveMesh(*mesh);
        foreignMaterialKey = registry->resolveMaterial(*material);
    });
    foreign.join();

    ASSERT_TRUE(meshRegisterError.has_value());
    EXPECT_EQ(*meshRegisterError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(materialRegisterError.has_value());
    EXPECT_EQ(*materialRegisterError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(meshUnbindError.has_value());
    EXPECT_EQ(*meshUnbindError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(materialUnbindError.has_value());
    EXPECT_EQ(*materialUnbindError, Render::RenderErrorCode::WrongOwnerThread);
    EXPECT_EQ(foreignMeshKey, 0U);
    EXPECT_EQ(foreignMaterialKey, 0U);
    EXPECT_EQ(registry->resolveMesh(*mesh), *meshKey);
    EXPECT_EQ(registry->resolveMaterial(*material), *materialKey);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
}

TEST(Mesh3DBindingRegistryTests, SteadyStateOperationsPerformNoPmrAllocations)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto mesh = store->publish(makeMesh(memory, 1U));
    auto material = store->publish(makeMaterial(memory, 2U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(
        *store, device, Mesh3DBindingRegistryConfig{.meshCapacity = 1, .materialCapacity = 1,
                                                    .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    const Core::usize allocationBaseline = memory.allocationCalls();

    Core::u32 previousMeshKey = 0;
    Core::u32 previousMaterialKey = 0;
    for (Core::usize iteration = 0; iteration < 300U; ++iteration)
    {
        auto meshKey = registry->registerMeshBinding(*mesh, Render::GpuMeshId{1U, 1U});
        auto materialKey = registry->registerMaterialBinding(*material, {});
        ASSERT_TRUE(meshKey.has_value());
        ASSERT_TRUE(materialKey.has_value());
        EXPECT_GT(*meshKey, previousMeshKey);
        EXPECT_GT(*materialKey, previousMaterialKey);
        EXPECT_EQ(registry->resolveMesh(*mesh), *meshKey);
        EXPECT_EQ(registry->resolveMaterial(*material), *materialKey);
        ASSERT_TRUE(registry->unbindMeshBinding(*mesh).has_value());
        ASSERT_TRUE(registry->unbindMaterialBinding(*material).has_value());
        previousMeshKey = *meshKey;
        previousMaterialKey = *materialKey;
    }

    EXPECT_EQ(memory.allocationCalls(), allocationBaseline);
    EXPECT_EQ(registry->meshBindingCount(), 0U);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
}

} // namespace
} // namespace Tina::Asset
