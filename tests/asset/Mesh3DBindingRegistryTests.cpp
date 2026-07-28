#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <thread>
#include <utility>
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

    [[nodiscard]] Core::usize allocationAttempts() const noexcept { return m_allocationAttempts; }
    [[nodiscard]] Core::usize rejectedAllocations() const noexcept { return m_rejectedAllocations; }
    [[nodiscard]] Core::usize outstandingAllocations() const noexcept { return m_outstandingAllocations; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++m_allocationAttempts;
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
    Core::usize m_allocationAttempts = 0;
    Core::usize m_rejectedAllocations = 0;
    Core::usize m_outstandingAllocations = 0;
};

class RecordingRenderDevice final : public Render::IRenderDevice {
  public:
    struct MeshBindingCall final {
        Core::u32 bindingKey = 0;
        Render::GpuMeshId mesh{};
    };

    struct MaterialBindingCall final {
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
        m_meshBindingCalls[m_meshBindingCallCount++] = {.bindingKey = bindingKey, .mesh = mesh};
        if (std::exchange(m_rejectNextMeshBinding, false))
        {
            return Core::failure(Render::RenderErrorCode::MeshUploadUnsupported,
                                 "recording device rejected Mesh3D binding");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DMaterialBinding(
        Core::u32 bindingKey,
        const Render::Mesh3DMaterialBindingDesc& binding) noexcept override
    {
        m_materialBindingCalls[m_materialBindingCallCount++] = {
            .bindingKey = bindingKey,
            .binding = binding,
        };
        if (std::exchange(m_rejectNextMaterialBinding, false))
        {
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "recording device rejected Mesh3D material binding");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status clearMesh3DMaterialBinding(Core::u32 bindingKey) noexcept override
    {
        m_materialClears[m_materialClearCount++] = bindingKey;
        if (std::exchange(m_rejectNextMaterialClear, false))
        {
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "recording device rejected Mesh3D material clear");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status validateTexture2D(Render::GpuTextureId texture) const noexcept override
    {
        if (!texture)
        {
            return Core::failure(Render::RenderErrorCode::TextureNotFound,
                                 "recording device rejected invalid Texture2D owner");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status retireTexture2D(
        Render::GpuTextureId texture,
        Render::FramePin& completionPin) noexcept override
    {
        ++m_textureRetirementAttempts;
        if (m_rejectTextureRetirementAttempt == m_textureRetirementAttempts)
        {
            m_rejectTextureRetirementAttempt = 0;
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "recording device rejected Texture2D retirement");
        }
        m_retiredTextures[m_textureRetirementCount++] = texture;
        completionPin.release();
        return Core::success();
    }

    [[nodiscard]] Core::Status retireStaticMesh(
        Render::GpuMeshId mesh,
        Render::FramePin& completionPin) noexcept override
    {
        ++m_meshRetirementAttempts;
        if (m_rejectMeshRetirementAttempt == m_meshRetirementAttempts)
        {
            m_rejectMeshRetirementAttempt = 0;
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "recording device rejected StaticMesh retirement");
        }
        m_retiredMeshes[m_meshRetirementCount++] = mesh;
        completionPin.release();
        return Core::success();
    }

    void rejectNextMeshBinding() noexcept { m_rejectNextMeshBinding = true; }
    void rejectNextMaterialBinding() noexcept { m_rejectNextMaterialBinding = true; }
    void rejectNextMaterialClear() noexcept { m_rejectNextMaterialClear = true; }
    void rejectNextTextureRetirement() noexcept
    {
        m_rejectTextureRetirementAttempt = m_textureRetirementAttempts + 1U;
    }
    void rejectNextMeshRetirement() noexcept
    {
        m_rejectMeshRetirementAttempt = m_meshRetirementAttempts + 1U;
    }
    void rejectTextureRetirementOnAttempt(Core::usize attempt) noexcept
    {
        m_rejectTextureRetirementAttempt = attempt;
    }

    [[nodiscard]] Core::usize meshBindingCallCount() const noexcept { return m_meshBindingCallCount; }
    [[nodiscard]] Core::usize materialBindingCallCount() const noexcept { return m_materialBindingCallCount; }
    [[nodiscard]] Core::usize materialClearCount() const noexcept { return m_materialClearCount; }
    [[nodiscard]] Core::usize textureRetirementAttempts() const noexcept { return m_textureRetirementAttempts; }
    [[nodiscard]] Core::usize textureRetirementCount() const noexcept { return m_textureRetirementCount; }
    [[nodiscard]] Core::usize meshRetirementAttempts() const noexcept { return m_meshRetirementAttempts; }
    [[nodiscard]] Core::usize meshRetirementCount() const noexcept { return m_meshRetirementCount; }
    [[nodiscard]] const MeshBindingCall& meshBindingCall(Core::usize index) const noexcept
    {
        return m_meshBindingCalls[index];
    }
    [[nodiscard]] const MaterialBindingCall& materialBindingCall(Core::usize index) const noexcept
    {
        return m_materialBindingCalls[index];
    }
    [[nodiscard]] Render::GpuTextureId retiredTexture(Core::usize index) const noexcept
    {
        return m_retiredTextures[index];
    }
    [[nodiscard]] Render::GpuMeshId retiredMesh(Core::usize index) const noexcept
    {
        return m_retiredMeshes[index];
    }

  private:
    std::array<MeshBindingCall, 64> m_meshBindingCalls{};
    std::array<MaterialBindingCall, 64> m_materialBindingCalls{};
    std::array<Core::u32, 64> m_materialClears{};
    std::array<Render::GpuTextureId, 64> m_retiredTextures{};
    std::array<Render::GpuMeshId, 64> m_retiredMeshes{};
    Core::usize m_meshBindingCallCount = 0;
    Core::usize m_materialBindingCallCount = 0;
    Core::usize m_materialClearCount = 0;
    Core::usize m_textureRetirementAttempts = 0;
    Core::usize m_textureRetirementCount = 0;
    Core::usize m_meshRetirementAttempts = 0;
    Core::usize m_meshRetirementCount = 0;
    Core::usize m_rejectTextureRetirementAttempt = 0;
    Core::usize m_rejectMeshRetirementAttempt = 0;
    bool m_rejectNextMeshBinding = false;
    bool m_rejectNextMaterialBinding = false;
    bool m_rejectNextMaterialClear = false;
};

class RejectingFrameResourceSink final : public Render::FrameResourceSink {
  public:
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    intern(Render::FrameResourceDescriptor, Render::FramePin&&) noexcept override
    {
        ++m_callCount;
        return Core::failure(Render::RenderErrorCode::InvalidFrameResource,
                             "test sink rejected frame resource");
    }

    [[nodiscard]] Core::u32 resourceCount() const noexcept override { return 0; }
    [[nodiscard]] Core::usize callCount() const noexcept { return m_callCount; }

  private:
    Core::usize m_callCount = 0;
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
    auto file = makeCookedAssetFileFromBytes(
        std::move(owned), CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    return file ? std::move(*file) : CookedAssetFile{};
}

[[nodiscard]] CookedAssetFile makeMesh(std::pmr::memory_resource& memory, Core::u8 seed)
{
    constexpr std::array payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    return loadCooked(memory, AssetFormat::writeCookedAssetBytes(
                                  AssetFormat::CookedAssetWriteDesc{
                                      .assetKind = AssetFormat::AssetKind::StaticMesh,
                                      .assetTypeVersion = 1,
                                      .assetId = assetId(seed),
                                      .payload = payload,
                                  }));
}

[[nodiscard]] CookedAssetFile makeTexture(std::pmr::memory_resource& memory, Core::u8 seed)
{
    constexpr std::array pixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    return loadCooked(
        memory,
        AssetFormat::writeCookedTexture2DAsset(
            assetId(seed),
            AssetFormat::Texture2DPayloadDesc{.width = 1, .height = 1, .pixels = pixels}));
}

[[nodiscard]] CookedAssetFile makeMaterial(
    std::pmr::memory_resource& memory,
    Core::u8 seed,
    AssetFormat::MaterialPayloadDesc desc = {})
{
    return loadCooked(memory, AssetFormat::writeCookedMaterialAsset(assetId(seed), desc));
}

[[nodiscard]] Core::Result<AssetSystem> makeAssetSystem(
    std::pmr::memory_resource& memory,
    Core::usize capacity = 32U)
{
    return AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = capacity,
        .memoryResource = &memory,
        .batch = CookedAssetBatchLoadConfig{
            .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
            .memoryResource = &memory,
        },
    });
}

class AutoRetiringRegistry final {
  public:
    explicit AutoRetiringRegistry(Core::Result<Mesh3DBindingRegistry> registry) noexcept
        : m_registry(std::move(registry))
    {
    }

    ~AutoRetiringRegistry() noexcept
    {
        if (m_registry &&
            (m_registry->meshBindingCount() != 0 || m_registry->materialBindingCount() != 0 ||
             m_registry->textureOwnerCount() != 0) &&
            !m_registry->retireAllBindings())
        {
            std::terminate();
        }
    }

    AutoRetiringRegistry(const AutoRetiringRegistry&) = delete;
    AutoRetiringRegistry& operator=(const AutoRetiringRegistry&) = delete;
    AutoRetiringRegistry(AutoRetiringRegistry&&) noexcept = default;
    AutoRetiringRegistry& operator=(AutoRetiringRegistry&&) = delete;

    [[nodiscard]] bool has_value() const noexcept { return m_registry.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return m_registry.has_value(); }
    [[nodiscard]] decltype(auto) error() const { return m_registry.error(); }
    [[nodiscard]] Mesh3DBindingRegistry& operator*() noexcept { return *m_registry; }
    [[nodiscard]] const Mesh3DBindingRegistry& operator*() const noexcept { return *m_registry; }
    [[nodiscard]] Mesh3DBindingRegistry* operator->() noexcept { return &*m_registry; }
    [[nodiscard]] const Mesh3DBindingRegistry* operator->() const noexcept { return &*m_registry; }

  private:
    Core::Result<Mesh3DBindingRegistry> m_registry;
};

[[nodiscard]] AutoRetiringRegistry makeRegistry(
    AssetSystem& assets,
    Render::IRenderDevice& device,
    Mesh3DBindingRegistryConfig config = {})
{
    return AutoRetiringRegistry{Mesh3DBindingRegistry::Create(assets, device, config)};
}

[[nodiscard]] Core::usize retirementRecordCount(
    const AssetSystem& assets,
    AssetRetirementKind kind) noexcept
{
    return static_cast<Core::usize>(std::ranges::count_if(
        assets.retirement().records(),
        [kind](const AssetRetirementRecord& record) noexcept { return record.kind == kind; }));
}

void destroyMesh3DRegistryWithOwnedBinding()
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    if (!assets)
    {
        std::abort();
    }
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    if (!mesh)
    {
        std::abort();
    }
    RecordingRenderDevice device;
    auto registry = Mesh3DBindingRegistry::Create(*assets, device);
    if (!registry)
    {
        std::abort();
    }
    Render::GpuMeshId gpuMesh{1U, 1U};
    if (!registry->registerMeshBinding(*mesh, gpuMesh))
    {
        std::abort();
    }
}

TEST(Mesh3DBindingRegistryTests, CreateValidatesAllCapacitiesAndMapsPmrFailure)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    RecordingRenderDevice device;

    for (const Mesh3DBindingRegistryConfig config : std::array{
             Mesh3DBindingRegistryConfig{.meshCapacity = 0, .materialCapacity = 1, .textureCapacity = 1},
             Mesh3DBindingRegistryConfig{.meshCapacity = 1, .materialCapacity = 0, .textureCapacity = 1},
             Mesh3DBindingRegistryConfig{.meshCapacity = 1, .materialCapacity = 1, .textureCapacity = 0},
         })
    {
        auto rejected = Mesh3DBindingRegistry::Create(*assets, device, config);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_EQ(rejected.error().code, AssetErrorCode::InvalidCatalogConfig);
    }

    for (const Mesh3DBindingRegistryConfig config : std::array{
             Mesh3DBindingRegistryConfig{
                 .meshCapacity = MaximumMesh3DBindingCapacity + 1U,
                 .materialCapacity = 1,
                 .textureCapacity = 1,
             },
             Mesh3DBindingRegistryConfig{
                 .meshCapacity = 1,
                 .materialCapacity = MaximumMesh3DBindingCapacity + 1U,
                 .textureCapacity = 1,
             },
             Mesh3DBindingRegistryConfig{
                 .meshCapacity = 1,
                 .materialCapacity = 1,
                 .textureCapacity = MaximumMesh3DTextureCapacity + 1U,
             },
         })
    {
        auto rejected = Mesh3DBindingRegistry::Create(*assets, device, config);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_EQ(rejected.error().code, AssetErrorCode::InvalidCatalogConfig);
    }

    ThrowingMemoryResource throwingMemory{64U};
    auto allocationFailure = Mesh3DBindingRegistry::Create(
        *assets, device,
        Mesh3DBindingRegistryConfig{.memoryResource = &throwingMemory});
    ASSERT_FALSE(allocationFailure.has_value());
    EXPECT_EQ(allocationFailure.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_GE(throwingMemory.allocationAttempts(), 1U);
    EXPECT_EQ(throwingMemory.rejectedAllocations(), 1U);
    EXPECT_EQ(throwingMemory.outstandingAllocations(), 0U);
}

TEST(Mesh3DBindingRegistryTests, CapacityFailuresPreserveCandidatesLeasesAndExistingEntries)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto firstMesh = assets->store().publish(makeMesh(memory, 1U));
    auto overflowMesh = assets->store().publish(makeMesh(memory, 2U));
    auto firstTexture = assets->store().publish(makeTexture(memory, 3U));
    auto overflowTexture = assets->store().publish(makeTexture(memory, 4U));
    auto firstMaterial = assets->store().publish(makeMaterial(
        memory, 5U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = assetId(3U)}));
    auto overflowMaterial = assets->store().publish(makeMaterial(memory, 6U));
    ASSERT_TRUE(firstMesh.has_value());
    ASSERT_TRUE(overflowMesh.has_value());
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(overflowTexture.has_value());
    ASSERT_TRUE(firstMaterial.has_value());
    ASSERT_TRUE(overflowMaterial.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device,
        Mesh3DBindingRegistryConfig{
            .meshCapacity = 1,
            .materialCapacity = 1,
            .textureCapacity = 1,
            .memoryResource = &memory,
        });
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId firstGpuMesh{1U, 1U};
    Render::GpuTextureId firstGpuTexture{2U, 1U};
    auto firstMeshKey = registry->registerMeshBinding(*firstMesh, firstGpuMesh);
    ASSERT_TRUE(firstMeshKey.has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*firstTexture, firstGpuTexture).has_value());
    auto firstMaterialKey = registry->registerMaterialBinding(*firstMaterial);
    ASSERT_TRUE(firstMaterialKey.has_value());

    constexpr Render::GpuMeshId OverflowGpuMesh{3U, 1U};
    Render::GpuMeshId overflowGpuMesh = OverflowGpuMesh;
    const auto rejectedMesh = registry->registerMeshBinding(*overflowMesh, overflowGpuMesh);
    ASSERT_FALSE(rejectedMesh.has_value());
    EXPECT_EQ(rejectedMesh.error().code, AssetErrorCode::Mesh3DBindingCapacityExceeded);
    EXPECT_EQ(overflowGpuMesh, OverflowGpuMesh);

    constexpr Render::GpuTextureId OverflowGpuTexture{4U, 1U};
    Render::GpuTextureId overflowGpuTexture = OverflowGpuTexture;
    const auto rejectedTexture =
        registry->registerMaterialTexture(*overflowTexture, overflowGpuTexture);
    ASSERT_FALSE(rejectedTexture.has_value());
    EXPECT_EQ(rejectedTexture.error().code, AssetErrorCode::Mesh3DBindingCapacityExceeded);
    EXPECT_EQ(overflowGpuTexture, OverflowGpuTexture);

    const auto rejectedMaterial = registry->registerMaterialBinding(*overflowMaterial);
    ASSERT_FALSE(rejectedMaterial.has_value());
    EXPECT_EQ(rejectedMaterial.error().code, AssetErrorCode::Mesh3DBindingCapacityExceeded);

    EXPECT_EQ(assets->store().leaseCount(*firstMesh), 1U);
    EXPECT_EQ(assets->store().leaseCount(*overflowMesh), 0U);
    EXPECT_EQ(assets->store().leaseCount(*firstTexture), 1U);
    EXPECT_EQ(assets->store().leaseCount(*overflowTexture), 0U);
    EXPECT_EQ(assets->store().leaseCount(*firstMaterial), 1U);
    EXPECT_EQ(assets->store().leaseCount(*overflowMaterial), 0U);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    EXPECT_TRUE(registry->hasMaterialTexture(*firstTexture));
    EXPECT_FALSE(registry->hasMaterialTexture(*overflowTexture));
    ASSERT_EQ(device.meshBindingCallCount(), 1U);
    EXPECT_EQ(device.meshBindingCall(0).bindingKey, *firstMeshKey);
    ASSERT_EQ(device.materialBindingCallCount(), 1U);
    EXPECT_EQ(device.materialBindingCall(0).bindingKey, *firstMaterialKey);

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    auto meshResource = registry->internMeshFrameResource(*firstMesh, packet.resourceSink());
    auto materialResource =
        registry->internMaterialFrameResource(*firstMaterial, packet.resourceSink());
    ASSERT_TRUE(meshResource.has_value());
    ASSERT_TRUE(materialResource.has_value());
    const auto* meshDescriptor = packet.resourceTableView().resolve(
        *meshResource, Render::FrameResourceKind::Mesh3DGeometry);
    const auto* materialDescriptor = packet.resourceTableView().resolve(
        *materialResource, Render::FrameResourceKind::Mesh3DMaterial);
    ASSERT_NE(meshDescriptor, nullptr);
    ASSERT_NE(materialDescriptor, nullptr);
    EXPECT_EQ(meshDescriptor->deviceBindingKey, *firstMeshKey);
    EXPECT_EQ(materialDescriptor->deviceBindingKey, *firstMaterialKey);
    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireAllBindings().has_value());
}

TEST(Mesh3DBindingRegistryTests, ExactHandleAndAssetIdConflictsPreserveUniqueOwners)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto sameIdMesh = assets->store().publish(makeMesh(memory, 1U));
    auto texture = assets->store().publish(makeTexture(memory, 2U));
    auto sameIdTexture = assets->store().publish(makeTexture(memory, 2U));
    auto material = assets->store().publish(makeMaterial(memory, 3U));
    auto sameIdMaterial = assets->store().publish(makeMaterial(memory, 3U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(sameIdMesh.has_value());
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sameIdTexture.has_value());
    ASSERT_TRUE(material.has_value());
    ASSERT_TRUE(sameIdMaterial.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId gpuMesh{1U, 1U};
    Render::GpuTextureId gpuTexture{2U, 1U};
    ASSERT_TRUE(registry->registerMeshBinding(*mesh, gpuMesh).has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*texture, gpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*material).has_value());

    constexpr Render::GpuMeshId ExactMeshCandidate{3U, 1U};
    Render::GpuMeshId exactMeshCandidate = ExactMeshCandidate;
    const auto exactMesh = registry->registerMeshBinding(*mesh, exactMeshCandidate);
    ASSERT_FALSE(exactMesh.has_value());
    EXPECT_EQ(exactMesh.error().code, AssetErrorCode::Mesh3DBindingConflict);
    EXPECT_EQ(exactMeshCandidate, ExactMeshCandidate);

    constexpr Render::GpuMeshId SameIdMeshCandidate{4U, 1U};
    Render::GpuMeshId sameIdMeshCandidate = SameIdMeshCandidate;
    const auto duplicateMesh = registry->registerMeshBinding(*sameIdMesh, sameIdMeshCandidate);
    ASSERT_FALSE(duplicateMesh.has_value());
    EXPECT_EQ(duplicateMesh.error().code, AssetErrorCode::Mesh3DBindingConflict);
    EXPECT_EQ(sameIdMeshCandidate, SameIdMeshCandidate);

    constexpr Render::GpuTextureId ExactTextureCandidate{5U, 1U};
    Render::GpuTextureId exactTextureCandidate = ExactTextureCandidate;
    const auto exactTexture =
        registry->registerMaterialTexture(*texture, exactTextureCandidate);
    ASSERT_FALSE(exactTexture.has_value());
    EXPECT_EQ(exactTexture.error().code, AssetErrorCode::Mesh3DBindingConflict);
    EXPECT_EQ(exactTextureCandidate, ExactTextureCandidate);

    constexpr Render::GpuTextureId SameIdTextureCandidate{6U, 1U};
    Render::GpuTextureId sameIdTextureCandidate = SameIdTextureCandidate;
    const auto duplicateTexture =
        registry->registerMaterialTexture(*sameIdTexture, sameIdTextureCandidate);
    ASSERT_FALSE(duplicateTexture.has_value());
    EXPECT_EQ(duplicateTexture.error().code, AssetErrorCode::Mesh3DBindingConflict);
    EXPECT_EQ(sameIdTextureCandidate, SameIdTextureCandidate);

    const auto exactMaterial = registry->registerMaterialBinding(*material);
    ASSERT_FALSE(exactMaterial.has_value());
    EXPECT_EQ(exactMaterial.error().code, AssetErrorCode::Mesh3DBindingConflict);
    const auto duplicateMaterial = registry->registerMaterialBinding(*sameIdMaterial);
    ASSERT_FALSE(duplicateMaterial.has_value());
    EXPECT_EQ(duplicateMaterial.error().code, AssetErrorCode::Mesh3DBindingConflict);

    EXPECT_EQ(assets->store().leaseCount(*mesh), 1U);
    EXPECT_EQ(assets->store().leaseCount(*sameIdMesh), 0U);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_EQ(assets->store().leaseCount(*sameIdTexture), 0U);
    EXPECT_EQ(assets->store().leaseCount(*material), 1U);
    EXPECT_EQ(assets->store().leaseCount(*sameIdMaterial), 0U);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    EXPECT_EQ(device.meshBindingCallCount(), 1U);
    EXPECT_EQ(device.materialBindingCallCount(), 1U);
    ASSERT_TRUE(registry->retireAllBindings().has_value());
}

TEST(Mesh3DBindingRegistryTests, RegistrationTransfersGpuAndLeaseOwnersAndDerivesMaterial)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(2U);
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto texture = assets->store().publish(makeTexture(memory, 2U));
    auto material = assets->store().publish(makeMaterial(
        memory, 3U,
        AssetFormat::MaterialPayloadDesc{
            .metallicFactor = 0.25F,
            .roughnessFactor = 0.75F,
            .baseColorTextureId = textureId,
        }));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device,
        Mesh3DBindingRegistryConfig{
            .meshCapacity = 2,
            .materialCapacity = 2,
            .textureCapacity = 2,
            .memoryResource = &memory,
        });
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    constexpr Render::GpuMeshId ExpectedMesh{7U, 3U};
    constexpr Render::GpuTextureId ExpectedTexture{8U, 4U};
    Render::GpuMeshId gpuMesh = ExpectedMesh;
    Render::GpuTextureId gpuTexture = ExpectedTexture;

    auto meshKey = registry->registerMeshBinding(*mesh, gpuMesh);
    ASSERT_TRUE(meshKey.has_value()) << meshKey.error().message;
    ASSERT_TRUE(registry->registerMaterialTexture(*texture, gpuTexture).has_value());
    auto materialKey = registry->registerMaterialBinding(*material);
    ASSERT_TRUE(materialKey.has_value()) << materialKey.error().message;

    EXPECT_FALSE(gpuMesh);
    EXPECT_FALSE(gpuTexture);
    EXPECT_EQ(assets->store().leaseCount(*mesh), 1U);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_EQ(assets->store().leaseCount(*material), 1U);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    ASSERT_EQ(device.meshBindingCallCount(), 1U);
    EXPECT_EQ(device.meshBindingCall(0).mesh, ExpectedMesh);
    ASSERT_EQ(device.materialBindingCallCount(), 1U);
    const auto& binding = device.materialBindingCall(0).binding;
    EXPECT_EQ(binding.baseColorTexture, ExpectedTexture);
    EXPECT_FALSE(binding.metallicRoughnessTexture);
    EXPECT_FALSE(binding.normalTexture);
    EXPECT_FLOAT_EQ(binding.metallicFactor, 0.25F);
    EXPECT_FLOAT_EQ(binding.roughnessFactor, 0.75F);

    ASSERT_TRUE(registry->retireAllBindings().has_value());
    EXPECT_EQ(assets->state(*mesh), AssetLogicalState::Unloaded);
    EXPECT_EQ(assets->state(*texture), AssetLogicalState::Unloaded);
    EXPECT_EQ(assets->state(*material), AssetLogicalState::Unloaded);
    EXPECT_EQ(device.meshRetirementCount(), 1U);
    EXPECT_EQ(device.textureRetirementCount(), 1U);
    EXPECT_EQ(device.retiredMesh(0), ExpectedMesh);
    EXPECT_EQ(device.retiredTexture(0), ExpectedTexture);
    ASSERT_EQ(assets->retirement().records().size(), 3U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::Logical), 1U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuTexture2D), 1U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuStaticMesh), 1U);
    EXPECT_EQ(assets->retirementStats().released, 3U);
    EXPECT_EQ(assets->retirementStats().live, 0U);
}

TEST(Mesh3DBindingRegistryTests, RegistrationFailuresPreserveCandidateGpuOwners)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId firstTextureId = assetId(3U);
    auto firstMesh = assets->store().publish(makeMesh(memory, 1U));
    auto secondMesh = assets->store().publish(makeMesh(memory, 2U));
    auto firstTexture = assets->store().publish(makeTexture(memory, 3U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 4U));
    auto material = assets->store().publish(makeMaterial(
        memory, 5U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = firstTextureId}));
    ASSERT_TRUE(firstMesh.has_value());
    ASSERT_TRUE(secondMesh.has_value());
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuMeshId SharedMesh{11U, 2U};
    constexpr Render::GpuTextureId SharedTexture{12U, 2U};

    Render::GpuMeshId rejectedMesh = SharedMesh;
    device.rejectNextMeshBinding();
    const auto backendRejected = registry->registerMeshBinding(*firstMesh, rejectedMesh);
    ASSERT_FALSE(backendRejected.has_value());
    EXPECT_EQ(backendRejected.error().code, Render::RenderErrorCode::MeshUploadUnsupported);
    EXPECT_EQ(rejectedMesh, SharedMesh);
    EXPECT_EQ(assets->store().leaseCount(*firstMesh), 0U);

    Render::GpuMeshId adoptedMesh = SharedMesh;
    ASSERT_TRUE(registry->registerMeshBinding(*firstMesh, adoptedMesh).has_value());
    EXPECT_FALSE(adoptedMesh);
    Render::GpuMeshId duplicateMesh = SharedMesh;
    const auto meshOwnerConflict = registry->registerMeshBinding(*secondMesh, duplicateMesh);
    ASSERT_FALSE(meshOwnerConflict.has_value());
    EXPECT_EQ(meshOwnerConflict.error().code, AssetErrorCode::Mesh3DBindingConflict);
    EXPECT_EQ(duplicateMesh, SharedMesh);
    EXPECT_EQ(assets->store().leaseCount(*secondMesh), 0U);

    Render::GpuTextureId adoptedTexture = SharedTexture;
    ASSERT_TRUE(registry->registerMaterialTexture(*firstTexture, adoptedTexture).has_value());
    EXPECT_FALSE(adoptedTexture);
    Render::GpuTextureId duplicateTexture = SharedTexture;
    const auto textureOwnerConflict =
        registry->registerMaterialTexture(*secondTexture, duplicateTexture);
    ASSERT_FALSE(textureOwnerConflict.has_value());
    EXPECT_EQ(textureOwnerConflict.error().code, AssetErrorCode::Mesh3DBindingConflict);
    EXPECT_EQ(duplicateTexture, SharedTexture);
    EXPECT_EQ(assets->store().leaseCount(*secondTexture), 0U);

    device.rejectNextMaterialBinding();
    const auto materialRejected = registry->registerMaterialBinding(*material);
    ASSERT_FALSE(materialRejected.has_value());
    EXPECT_EQ(materialRejected.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(assets->store().leaseCount(*material), 0U);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    ASSERT_TRUE(registry->retireMaterialTexture(*firstTexture).has_value());
    ASSERT_TRUE(registry->retireMeshBinding(*firstMesh).has_value());
}

TEST(Mesh3DBindingRegistryTests, MaterialTextureRegistrationRejectsStaleAndCollidingForeignGpuOwners)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    ASSERT_TRUE(texture.has_value());

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    auto foreignDevice = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(foreignDevice.has_value());
    auto registry = makeRegistry(*assets, **device);
    ASSERT_TRUE(registry.has_value());

    constexpr std::array<std::byte, 4> Pixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto localTexture = (*device)->createTexture2DRgba8(
        Render::Texture2DUploadDesc{.width = 1, .height = 1, .rgba8Pixels = Pixels});
    ASSERT_TRUE(localTexture.has_value());
    auto foreignTexture = (*foreignDevice)->createTexture2DRgba8(
        Render::Texture2DUploadDesc{.width = 1, .height = 1, .rgba8Pixels = Pixels});
    ASSERT_TRUE(foreignTexture.has_value());
    EXPECT_EQ(localTexture->index, foreignTexture->index);
    EXPECT_EQ(localTexture->generation, foreignTexture->generation);
    EXPECT_NE(localTexture->owner, foreignTexture->owner);
    Render::GpuTextureId foreignCandidate = *foreignTexture;
    const auto foreign = registry->registerMaterialTexture(*texture, foreignCandidate);
    ASSERT_FALSE(foreign.has_value());
    EXPECT_EQ(foreign.error().code, Render::RenderErrorCode::TextureNotFound);
    EXPECT_EQ(foreignCandidate, *foreignTexture);
    EXPECT_EQ(assets->store().leaseCount(*texture), 0U);
    EXPECT_EQ(registry->textureOwnerCount(), 0U);

    ASSERT_TRUE((*device)->destroyTexture2D(*localTexture).has_value());
    Render::GpuTextureId staleCandidate = *localTexture;
    const auto stale = registry->registerMaterialTexture(*texture, staleCandidate);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::TextureNotFound);
    EXPECT_EQ(staleCandidate, *localTexture);
    EXPECT_EQ(assets->store().leaseCount(*texture), 0U);
    EXPECT_EQ(registry->textureOwnerCount(), 0U);

    ASSERT_TRUE((*foreignDevice)->destroyTexture2D(*foreignTexture).has_value());
    (*foreignDevice)->shutdown();
    (*device)->shutdown();
}

TEST(Mesh3DBindingRegistryTests, InvalidAndUnreadyAssetsFailBeforeConsumingGpuOwners)
{
    TrackingMemoryResource memory;
    TrackingMemoryResource foreignMemory;
    auto assets = makeAssetSystem(memory);
    auto foreignAssets = makeAssetSystem(foreignMemory);
    ASSERT_TRUE(assets.has_value());
    ASSERT_TRUE(foreignAssets.has_value());
    auto wrongKind = assets->store().publish(makeTexture(memory, 1U));
    auto staleMesh = assets->store().publish(makeMesh(memory, 2U));
    auto queuedMesh = assets->store().beginQueued(assetId(3U), AssetFormat::AssetKind::StaticMesh);
    auto foreignMesh = foreignAssets->store().publish(makeMesh(foreignMemory, 4U));
    auto missingTextureMaterial = assets->store().publish(makeMaterial(
        memory, 5U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = assetId(9U)}));
    ASSERT_TRUE(wrongKind.has_value());
    ASSERT_TRUE(staleMesh.has_value());
    ASSERT_TRUE(queuedMesh.has_value());
    ASSERT_TRUE(foreignMesh.has_value());
    ASSERT_TRUE(missingTextureMaterial.has_value());
    ASSERT_TRUE(assets->store().unload(*staleMesh).has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuMeshId Candidate{1U, 1U};
    for (const AssetHandle handle :
         std::array{AssetHandle{}, *wrongKind, *staleMesh, *queuedMesh, *foreignMesh})
    {
        Render::GpuMeshId gpuMesh = Candidate;
        const auto rejected = registry->registerMeshBinding(handle, gpuMesh);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_EQ(gpuMesh, Candidate);
    }
    const auto missingTexture = registry->registerMaterialBinding(*missingTextureMaterial);
    ASSERT_FALSE(missingTexture.has_value());
    EXPECT_EQ(missingTexture.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(device.meshBindingCallCount(), 0U);
    EXPECT_EQ(device.materialBindingCallCount(), 0U);
}

TEST(Mesh3DBindingRegistryTests, SharedTextureRemainsOwnedUntilBothMaterialsRetire)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto firstMaterial = assets->store().publish(makeMaterial(
        memory, 2U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = textureId}));
    auto secondMaterial = assets->store().publish(makeMaterial(
        memory, 3U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = textureId}));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(firstMaterial.has_value());
    ASSERT_TRUE(secondMaterial.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    Render::GpuTextureId gpuTexture{9U, 3U};
    ASSERT_TRUE(registry->registerMaterialTexture(*texture, gpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*firstMaterial).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*secondMaterial).has_value());
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    EXPECT_EQ(device.materialBindingCallCount(), 2U);

    auto referenced = registry->retireMaterialTexture(*texture);
    ASSERT_FALSE(referenced.has_value());
    EXPECT_EQ(referenced.error().code, AssetErrorCode::AssetNotReady);
    ASSERT_TRUE(registry->retireMaterialBinding(*firstMaterial).has_value());
    referenced = registry->retireMaterialTexture(*texture);
    ASSERT_FALSE(referenced.has_value());
    EXPECT_EQ(referenced.error().code, AssetErrorCode::AssetNotReady);
    ASSERT_TRUE(registry->retireMaterialBinding(*secondMaterial).has_value());
    ASSERT_TRUE(registry->retireMaterialTexture(*texture).has_value());

    EXPECT_EQ(device.materialClearCount(), 2U);
    EXPECT_EQ(device.textureRetirementCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    EXPECT_EQ(registry->textureOwnerCount(), 0U);
    ASSERT_EQ(assets->retirement().records().size(), 3U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::Logical), 2U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuTexture2D), 1U);
    EXPECT_EQ(assets->retirementStats().released, 3U);
    EXPECT_EQ(assets->retirementStats().live, 0U);
}

TEST(Mesh3DBindingRegistryTests, FrameResourcesDeduplicateAndBlockRetirementUntilRelease)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto material = assets->store().publish(makeMaterial(memory, 2U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId gpuMesh{7U, 1U};
    auto meshKey = registry->registerMeshBinding(*mesh, gpuMesh);
    auto materialKey = registry->registerMaterialBinding(*material);
    ASSERT_TRUE(meshKey.has_value());
    ASSERT_TRUE(materialKey.has_value());

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    auto firstMesh = registry->internMeshFrameResource(*mesh, packet.resourceSink());
    auto duplicateMesh = registry->internMeshFrameResource(*mesh, packet.resourceSink());
    auto firstMaterial = registry->internMaterialFrameResource(*material, packet.resourceSink());
    auto duplicateMaterial = registry->internMaterialFrameResource(*material, packet.resourceSink());
    ASSERT_TRUE(firstMesh.has_value());
    ASSERT_TRUE(duplicateMesh.has_value());
    ASSERT_TRUE(firstMaterial.has_value());
    ASSERT_TRUE(duplicateMaterial.has_value());
    EXPECT_EQ(*duplicateMesh, *firstMesh);
    EXPECT_EQ(*duplicateMaterial, *firstMaterial);
    EXPECT_EQ(packet.resourceCount(), 2U);
    const auto* meshDescriptor = packet.resourceTableView().resolve(
        *firstMesh, Render::FrameResourceKind::Mesh3DGeometry);
    const auto* materialDescriptor = packet.resourceTableView().resolve(
        *firstMaterial, Render::FrameResourceKind::Mesh3DMaterial);
    ASSERT_NE(meshDescriptor, nullptr);
    ASSERT_NE(materialDescriptor, nullptr);
    EXPECT_EQ(meshDescriptor->deviceBindingKey, *meshKey);
    EXPECT_EQ(materialDescriptor->deviceBindingKey, *materialKey);

    const auto meshBlocked = registry->retireMeshBinding(*mesh);
    const auto materialBlocked = registry->retireMaterialBinding(*material);
    const auto allBlocked = registry->retireAllBindings();
    ASSERT_FALSE(meshBlocked.has_value());
    ASSERT_FALSE(materialBlocked.has_value());
    ASSERT_FALSE(allBlocked.has_value());
    EXPECT_EQ(meshBlocked.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(materialBlocked.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(allBlocked.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(device.materialClearCount(), 0U);
    EXPECT_EQ(device.meshRetirementAttempts(), 0U);

    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireAllBindings().has_value());
    EXPECT_EQ(device.materialClearCount(), 1U);
    EXPECT_EQ(device.meshRetirementCount(), 1U);
}

TEST(Mesh3DBindingRegistryTests, SinkRejectionRollsBackMeshAndMaterialFrameBorrows)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto material = assets->store().publish(makeMaterial(memory, 2U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId gpuMesh{1U, 1U};
    ASSERT_TRUE(registry->registerMeshBinding(*mesh, gpuMesh).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*material).has_value());

    Render::RenderFramePacket idlePacket;
    auto idleMesh = registry->internMeshFrameResource(*mesh, idlePacket.resourceSink());
    auto idleMaterial = registry->internMaterialFrameResource(*material, idlePacket.resourceSink());
    ASSERT_FALSE(idleMesh.has_value());
    ASSERT_FALSE(idleMaterial.has_value());
    RejectingFrameResourceSink sink;
    auto rejectedMesh = registry->internMeshFrameResource(*mesh, sink);
    auto rejectedMaterial = registry->internMaterialFrameResource(*material, sink);
    ASSERT_FALSE(rejectedMesh.has_value());
    ASSERT_FALSE(rejectedMaterial.has_value());
    EXPECT_EQ(sink.callCount(), 2U);

    ASSERT_TRUE(registry->retireAllBindings().has_value());
    EXPECT_EQ(device.materialClearCount(), 1U);
    EXPECT_EQ(device.meshRetirementCount(), 1U);
}

TEST(Mesh3DBindingRegistryTests, MoveWithActiveFrameBorrowsPreservesPinTargetsAndOwnership)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto texture = assets->store().publish(makeTexture(memory, 2U));
    auto material = assets->store().publish(makeMaterial(
        memory, 3U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = assetId(2U)}));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device,
        Mesh3DBindingRegistryConfig{
            .meshCapacity = 1,
            .materialCapacity = 1,
            .textureCapacity = 1,
            .memoryResource = &memory,
        });
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId gpuMesh{1U, 1U};
    Render::GpuTextureId gpuTexture{2U, 1U};
    ASSERT_TRUE(registry->registerMeshBinding(*mesh, gpuMesh).has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*texture, gpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*material).has_value());

    std::optional<AutoRetiringRegistry> movedOwner;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    ASSERT_TRUE(registry->internMeshFrameResource(*mesh, packet.resourceSink()).has_value());
    ASSERT_TRUE(registry->internMaterialFrameResource(*material, packet.resourceSink()).has_value());
    ASSERT_EQ(packet.resourceCount(), 2U);

    movedOwner.emplace(Core::Result<Mesh3DBindingRegistry>{std::move(*registry)});
    Mesh3DBindingRegistry& moved = **movedOwner;

    EXPECT_FALSE(static_cast<bool>(*registry));
    EXPECT_EQ(registry->meshCapacity(), 0U);
    EXPECT_EQ(registry->materialCapacity(), 0U);
    EXPECT_EQ(registry->textureCapacity(), 0U);
    EXPECT_EQ(registry->meshBindingCount(), 0U);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    EXPECT_EQ(registry->textureOwnerCount(), 0U);
    Render::GpuMeshId movedFromCandidate{3U, 1U};
    const auto movedFromRegister = registry->registerMeshBinding(*mesh, movedFromCandidate);
    EXPECT_FALSE(movedFromRegister.has_value());
    if (!movedFromRegister)
    {
        EXPECT_EQ(movedFromRegister.error().code, Render::RenderErrorCode::WrongOwnerThread);
    }
    EXPECT_EQ(movedFromCandidate, (Render::GpuMeshId{3U, 1U}));

    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved.meshBindingCount(), 1U);
    EXPECT_EQ(moved.materialBindingCount(), 1U);
    EXPECT_EQ(moved.textureOwnerCount(), 1U);
    const auto blocked = moved.retireAllBindings();
    EXPECT_FALSE(blocked.has_value());
    if (!blocked)
    {
        EXPECT_EQ(blocked.error().code, AssetErrorCode::AssetNotReady);
    }
    EXPECT_EQ(device.materialClearCount(), 0U);
    EXPECT_EQ(device.textureRetirementAttempts(), 0U);
    EXPECT_EQ(device.meshRetirementAttempts(), 0U);

    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(moved.retireAllBindings().has_value());
    EXPECT_EQ(moved.meshBindingCount(), 0U);
    EXPECT_EQ(moved.materialBindingCount(), 0U);
    EXPECT_EQ(moved.textureOwnerCount(), 0U);
    EXPECT_EQ(device.materialClearCount(), 1U);
    EXPECT_EQ(device.textureRetirementCount(), 1U);
    EXPECT_EQ(device.meshRetirementCount(), 1U);
    EXPECT_EQ(assets->store().leaseCount(*mesh), 0U);
    EXPECT_EQ(assets->store().leaseCount(*texture), 0U);
    EXPECT_EQ(assets->store().leaseCount(*material), 0U);
}

TEST(Mesh3DBindingRegistryTests, RetirementRejectionsPreserveOwnersAndCanBeRetried)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(2U);
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto texture = assets->store().publish(makeTexture(memory, 2U));
    auto material = assets->store().publish(makeMaterial(
        memory, 3U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = textureId}));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId gpuMesh{4U, 2U};
    Render::GpuTextureId gpuTexture{5U, 2U};
    ASSERT_TRUE(registry->registerMeshBinding(*mesh, gpuMesh).has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*texture, gpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*material).has_value());

    device.rejectNextMaterialClear();
    auto rejectedMaterial = registry->retireMaterialBinding(*material);
    ASSERT_FALSE(rejectedMaterial.has_value());
    EXPECT_EQ(rejectedMaterial.error().code,
              Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(assets->store().leaseCount(*material), 1U);
    ASSERT_FALSE(registry->retireMaterialTexture(*texture).has_value());
    ASSERT_TRUE(registry->retireMaterialBinding(*material).has_value());

    device.rejectNextTextureRetirement();
    auto rejectedTexture = registry->retireMaterialTexture(*texture);
    ASSERT_FALSE(rejectedTexture.has_value());
    EXPECT_EQ(rejectedTexture.error().code,
              Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    ASSERT_EQ(assets->retirement().records().size(), 1U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::Logical), 1U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuTexture2D), 0U);
    ASSERT_TRUE(registry->retireMaterialTexture(*texture).has_value());

    device.rejectNextMeshRetirement();
    auto rejectedMesh = registry->retireMeshBinding(*mesh);
    ASSERT_FALSE(rejectedMesh.has_value());
    EXPECT_EQ(rejectedMesh.error().code,
              Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(assets->store().leaseCount(*mesh), 1U);
    ASSERT_EQ(assets->retirement().records().size(), 2U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuTexture2D), 1U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuStaticMesh), 0U);
    ASSERT_TRUE(registry->retireMeshBinding(*mesh).has_value());

    EXPECT_EQ(device.textureRetirementAttempts(), 2U);
    EXPECT_EQ(device.textureRetirementCount(), 1U);
    EXPECT_EQ(device.meshRetirementAttempts(), 2U);
    EXPECT_EQ(device.meshRetirementCount(), 1U);
    ASSERT_EQ(assets->retirement().records().size(), 3U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuStaticMesh), 1U);
    EXPECT_EQ(assets->retirementStats().released, 3U);
    EXPECT_EQ(assets->retirementStats().live, 0U);
}

TEST(Mesh3DBindingRegistryTests, RetireAllCommitsPrefixAndRetriesRemainingOwnersInOrder)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto firstMesh = assets->store().publish(makeMesh(memory, 1U));
    auto secondMesh = assets->store().publish(makeMesh(memory, 2U));
    auto firstTexture = assets->store().publish(makeTexture(memory, 3U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 4U));
    auto firstMaterial = assets->store().publish(makeMaterial(
        memory, 5U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = assetId(3U)}));
    auto secondMaterial = assets->store().publish(makeMaterial(
        memory, 6U,
        AssetFormat::MaterialPayloadDesc{.baseColorTextureId = assetId(4U)}));
    ASSERT_TRUE(firstMesh.has_value());
    ASSERT_TRUE(secondMesh.has_value());
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());
    ASSERT_TRUE(firstMaterial.has_value());
    ASSERT_TRUE(secondMaterial.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device,
        Mesh3DBindingRegistryConfig{
            .meshCapacity = 2,
            .materialCapacity = 2,
            .textureCapacity = 2,
            .memoryResource = &memory,
        });
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId firstGpuMesh{1U, 1U};
    Render::GpuMeshId secondGpuMesh{2U, 1U};
    Render::GpuTextureId firstGpuTexture{3U, 1U};
    Render::GpuTextureId secondGpuTexture{4U, 1U};
    ASSERT_TRUE(registry->registerMeshBinding(*firstMesh, firstGpuMesh).has_value());
    ASSERT_TRUE(registry->registerMeshBinding(*secondMesh, secondGpuMesh).has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*firstTexture, firstGpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*secondTexture, secondGpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*firstMaterial).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*secondMaterial).has_value());
    device.rejectTextureRetirementOnAttempt(2U);

    auto partial = registry->retireAllBindings();
    ASSERT_FALSE(partial.has_value());
    EXPECT_EQ(partial.error().code,
              Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    EXPECT_EQ(registry->meshBindingCount(), 2U);
    EXPECT_EQ(device.materialClearCount(), 2U);
    EXPECT_EQ(device.textureRetirementAttempts(), 2U);
    EXPECT_EQ(device.textureRetirementCount(), 1U);
    EXPECT_EQ(device.meshRetirementAttempts(), 0U);
    ASSERT_EQ(assets->retirement().records().size(), 3U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::Logical), 2U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuTexture2D), 1U);
    EXPECT_EQ(assets->retirementStats().released, 3U);

    ASSERT_TRUE(registry->retireAllBindings().has_value());
    EXPECT_EQ(registry->materialBindingCount(), 0U);
    EXPECT_EQ(registry->textureOwnerCount(), 0U);
    EXPECT_EQ(registry->meshBindingCount(), 0U);
    EXPECT_EQ(device.textureRetirementAttempts(), 3U);
    EXPECT_EQ(device.textureRetirementCount(), 2U);
    EXPECT_EQ(device.meshRetirementCount(), 2U);
    ASSERT_EQ(assets->retirement().records().size(), 6U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::Logical), 2U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuTexture2D), 2U);
    EXPECT_EQ(retirementRecordCount(*assets, AssetRetirementKind::GpuStaticMesh), 2U);
    EXPECT_EQ(assets->retirementStats().released, 6U);
    EXPECT_EQ(assets->retirementStats().live, 0U);
}

TEST(Mesh3DBindingRegistryTests, DestructionWithOwnedBindingFailsFast)
{
    EXPECT_DEATH(destroyMesh3DRegistryWithOwnedBinding(), "");
}

TEST(Mesh3DBindingRegistryTests, WrongOwnerThreadOperationsFailWithoutMutation)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto mesh = assets->store().publish(makeMesh(memory, 1U));
    auto texture = assets->store().publish(makeTexture(memory, 2U));
    auto material = assets->store().publish(makeMaterial(memory, 3U));
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(material.has_value());

    RecordingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    Render::GpuMeshId gpuMesh{1U, 1U};
    Render::GpuTextureId gpuTexture{2U, 1U};
    ASSERT_TRUE(registry->registerMeshBinding(*mesh, gpuMesh).has_value());
    ASSERT_TRUE(registry->registerMaterialTexture(*texture, gpuTexture).has_value());
    ASSERT_TRUE(registry->registerMaterialBinding(*material).has_value());

    std::array<std::optional<Core::ErrorCode>, 7> errors{};
    Render::GpuMeshId foreignMeshCandidate{3U, 1U};
    Render::GpuTextureId foreignTextureCandidate{4U, 1U};
    RejectingFrameResourceSink sink;
    bool foreignHasTexture = true;
    std::thread foreign([&] {
        const auto record = [&](Core::usize index, const auto& result) {
            if (!result)
            {
                errors[index] = result.error().code;
            }
        };
        record(0, registry->registerMeshBinding(*mesh, foreignMeshCandidate));
        record(1, registry->registerMaterialTexture(*texture, foreignTextureCandidate));
        record(2, registry->registerMaterialBinding(*material));
        record(3, registry->retireMeshBinding(*mesh));
        record(4, registry->retireMaterialBinding(*material));
        record(5, registry->internMeshFrameResource(*mesh, sink));
        record(6, registry->internMaterialFrameResource(*material, sink));
        foreignHasTexture = registry->hasMaterialTexture(*texture);
    });
    foreign.join();

    for (const auto& error : errors)
    {
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(*error, Render::RenderErrorCode::WrongOwnerThread);
    }
    EXPECT_FALSE(foreignHasTexture);
    EXPECT_EQ(foreignMeshCandidate, (Render::GpuMeshId{3U, 1U}));
    EXPECT_EQ(foreignTextureCandidate, (Render::GpuTextureId{4U, 1U}));
    EXPECT_EQ(sink.callCount(), 0U);
    EXPECT_EQ(registry->meshBindingCount(), 1U);
    EXPECT_EQ(registry->materialBindingCount(), 1U);
    EXPECT_EQ(registry->textureOwnerCount(), 1U);
    ASSERT_TRUE(registry->retireAllBindings().has_value());
}

} // namespace
} // namespace Tina::Asset
