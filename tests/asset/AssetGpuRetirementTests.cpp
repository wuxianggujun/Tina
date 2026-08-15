#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::assetId;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

[[nodiscard]] Core::Result<AssetHandle> publishStaticMesh(
    AssetSystem& system,
    std::pmr::memory_resource& memory,
    Core::u8 seed)
{
    constexpr std::array payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    auto bytes = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::StaticMesh,
        .assetTypeVersion = 1,
        .assetId = assetId(seed),
        .payload = payload,
    });
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()));
    }
    std::pmr::vector<std::byte> owned{&memory};
    owned.assign(bytes->begin(), bytes->end());
    auto file = makeCookedAssetFileFromBytes(
        std::move(owned), CookedAssetFileLoadConfig{.memoryResource = &memory});
    if (!file)
    {
        return Core::failure(std::move(file.error()));
    }
    return system.store().publish(std::move(*file));
}

[[nodiscard]] Core::Result<AssetHandle> publishSkinnedMesh(
    AssetSystem& system,
    std::pmr::memory_resource& memory,
    Core::u8 seed)
{
    const std::array<AssetFormat::SkinnedMeshJointDesc, 1> joints{
        AssetFormat::SkinnedMeshJointDesc{
            .parentJoint = AssetFormat::SkinnedMeshWire::JointIndexNone,
        },
    };
    const std::array<float, 16> inverseBind{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    const std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{
        AssetFormat::StaticMeshSubmeshDesc{.indexCount = 3},
    };
    const std::array<float, 3 * AssetFormat::SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1,
    };
    const std::array<Core::u16, 12> jointIndices{};
    const std::array<Core::u16, 12> jointWeights{
        65535, 0, 0, 0,
        65535, 0, 0, 0,
        65535, 0, 0, 0,
    };
    const std::array<Core::u16, 3> indices{0, 1, 2};
    auto bytes = AssetFormat::writeCookedSkinnedMeshAsset(
        assetId(seed),
        AssetFormat::SkinnedMeshPayloadDesc{
            .boundsRadius = 1.0F,
            .joints = joints,
            .inverseBindMatrices = inverseBind,
            .submeshes = submeshes,
            .vertices = vertices,
            .jointIndices = jointIndices,
            .jointWeights = jointWeights,
            .indices = indices,
        });
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()));
    }
    std::pmr::vector<std::byte> owned{&memory};
    owned.assign(bytes->begin(), bytes->end());
    auto file = makeCookedAssetFileFromBytes(
        std::move(owned), CookedAssetFileLoadConfig{.memoryResource = &memory});
    if (!file)
    {
        return Core::failure(std::move(file.error()));
    }
    return system.store().publish(std::move(*file));
}

class SwitchableFailMemoryResource final : public std::pmr::memory_resource {
  public:
    void failAllocations(bool fail) noexcept
    {
        m_failAllocations = fail;
    }

    [[nodiscard]] std::size_t outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (m_failAllocations)
        {
            throw std::bad_alloc{};
        }
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_outstandingAllocations;
        return pointer;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --m_outstandingAllocations;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    bool m_failAllocations = false;
    std::size_t m_outstandingAllocations = 0;
};

class DelayedRetirementRenderDevice final : public Render::IRenderDevice {
  public:
    enum class Acceptance {
        Accept,
        CompleteSynchronously,
        Reject,
    };

    explicit DelayedRetirementRenderDevice(Acceptance acceptance = Acceptance::Accept) noexcept
        : m_acceptance(acceptance)
    {
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame&) override
    {
        return Render::RenderFrameSubmission::SkippedSuspendedSurface();
    }

    [[nodiscard]] Core::Status present() override
    {
        return Core::success();
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return m_statistics;
    }

    void shutdown() noexcept override
    {
        (void)drainGpuRetirements();
    }

    [[nodiscard]] Core::Status retireTexture2D(Render::GpuTextureId texture,
                                               Render::FramePin& completionPin) noexcept override
    {
        if (m_acceptance == Acceptance::Reject)
        {
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "test render device rejected texture retirement");
        }
        if (!texture || m_texturePin.hasValue())
        {
            return Core::failure(Render::RenderErrorCode::GpuRetirementDrainFailed,
                                 "test render device cannot queue texture retirement");
        }
        if (m_acceptance == Acceptance::CompleteSynchronously)
        {
            completionPin.release();
            ++m_statistics.completedGpuRetirements;
            return Core::success();
        }
        m_texturePin = std::move(completionPin);
        ++m_statistics.pendingGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status retireStaticMesh(Render::GpuMeshId mesh,
                                                Render::FramePin& completionPin) noexcept override
    {
        if (m_acceptance == Acceptance::Reject)
        {
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "test render device rejected mesh retirement");
        }
        if (!mesh || m_meshPin.hasValue())
        {
            return Core::failure(Render::RenderErrorCode::GpuRetirementDrainFailed,
                                 "test render device cannot queue mesh retirement");
        }
        m_meshPin = std::move(completionPin);
        ++m_statistics.pendingGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status drainGpuRetirements() noexcept override
    {
        ++m_drainCalls;
        if (m_completeOnDrain)
        {
            (void)completeTexture();
            (void)completeMesh();
        }
        return Core::success();
    }

    void setCompleteOnDrain(bool complete) noexcept
    {
        m_completeOnDrain = complete;
    }

    [[nodiscard]] bool completeTexture() noexcept
    {
        return complete(m_texturePin);
    }

    [[nodiscard]] bool completeMesh() noexcept
    {
        return complete(m_meshPin);
    }

    [[nodiscard]] bool hasPendingTexture() const noexcept
    {
        return m_texturePin.hasValue();
    }

    [[nodiscard]] bool hasPendingMesh() const noexcept
    {
        return m_meshPin.hasValue();
    }

    [[nodiscard]] Core::u32 drainCalls() const noexcept
    {
        return m_drainCalls;
    }

  private:
    [[nodiscard]] bool complete(Render::FramePin& pin) noexcept
    {
        if (!pin.hasValue())
        {
            return false;
        }
        pin.release();
        --m_statistics.pendingGpuRetirements;
        ++m_statistics.completedGpuRetirements;
        return true;
    }

    Acceptance m_acceptance = Acceptance::Accept;
    Render::FramePin m_texturePin{};
    Render::FramePin m_meshPin{};
    Render::RenderStatistics m_statistics{};
    Core::u32 m_drainCalls = 0;
    bool m_completeOnDrain = true;
};

class AssetGpuRetirementTests : public ::testing::Test {
  protected:
    void SetUp() override
    {
        const auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_package = writeTextureMaterialPackage(std::string{"tina_gpu_retirement_"} + testInfo->name());
    }

    void TearDown() override
    {
        removePackage(m_package);
    }

    [[nodiscard]] Core::Result<AssetSystem>
    createSystem(Render::NullUploadLedger* uploadLedger = nullptr,
                 AssetGpuUploadConfig gpuUpload = {},
                 std::pmr::memory_resource* memoryResource = nullptr)
    {
        if (memoryResource == nullptr)
        {
            memoryResource = &m_memory;
        }
        auto system = AssetSystem::Create(AssetSystemConfig{
            .storeCapacity = 8,
            .memoryResource = memoryResource,
            .batch =
                CookedAssetBatchLoadConfig{
                    .file = CookedAssetFileLoadConfig{.memoryResource = memoryResource},
                    .memoryResource = memoryResource,
                },
            .uploadLedger = uploadLedger,
            .gpuUpload = gpuUpload,
        });
        if (!system)
        {
            return Core::failure(std::move(system.error()).withContext("AssetGpuRetirementTests", "create"));
        }
        if (auto status = system->openAndBindCatalog(toUtf8(m_package.root)); !status)
        {
            return Core::failure(std::move(status.error()).withContext("AssetGpuRetirementTests", "catalog"));
        }
        return std::move(*system);
    }

    TrackingMemoryResource m_memory{};
    TestSupport::TextureMaterialPackage m_package{};
};

TEST_F(AssetGpuRetirementTests, ExistingTextureLeaseAndGpuOwnerTransferOnlyAfterBackendAccepts)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    ASSERT_EQ(system->store().leaseCount(*loaded), 1U);

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuTextureId ExpectedTexture{17U, 4U};
    Render::GpuTextureId texture = ExpectedTexture;
    ASSERT_TRUE(system->retireTexture2D(device, *lease, texture).has_value());

    EXPECT_FALSE(static_cast<bool>(*lease));
    EXPECT_FALSE(static_cast<bool>(texture));
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    ASSERT_TRUE(device.hasPendingTexture());
    ASSERT_EQ(system->retirement().records().size(), 1U);
    EXPECT_EQ(system->retirement().records()[0].texture, ExpectedTexture);

    EXPECT_TRUE(device.completeTexture());
    EXPECT_FALSE(device.completeTexture());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
    EXPECT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_EQ(device.drainCalls(), 0U);
}

TEST_F(AssetGpuRetirementTests, SynchronousCompletionOfLastUnloadPendingLeaseFinishesWithoutTerminate)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    ASSERT_TRUE(system->unload(*loaded).has_value());
    ASSERT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);

    DelayedRetirementRenderDevice device{
        DelayedRetirementRenderDevice::Acceptance::CompleteSynchronously};
    Render::GpuTextureId texture{22U, 9U};

    ASSERT_TRUE(system->retireTexture2D(device, *lease, texture).has_value());
    EXPECT_FALSE(static_cast<bool>(*lease));
    EXPECT_FALSE(static_cast<bool>(texture));
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_FALSE(system->find(m_package.textureId).has_value());
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
    EXPECT_EQ(device.statistics().completedGpuRetirements, 1U);
    EXPECT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_EQ(device.drainCalls(), 0U);
}

TEST_F(AssetGpuRetirementTests, BackendRejectionRestoresCallerLeaseAndGpuForRetry)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    const CookedAssetFile* expectedPayload = lease->get();
    constexpr Render::GpuTextureId ExpectedTexture{18U, 5U};
    Render::GpuTextureId texture = ExpectedTexture;

    DelayedRetirementRenderDevice rejectingDevice{DelayedRetirementRenderDevice::Acceptance::Reject};
    const auto rejected = system->retireTexture2D(rejectingDevice, *lease, texture);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_TRUE(static_cast<bool>(*lease));
    EXPECT_EQ(lease->handle(), *loaded);
    EXPECT_EQ(lease->get(), expectedPayload);
    EXPECT_EQ(texture, ExpectedTexture);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    EXPECT_EQ(system->find(m_package.textureId), std::optional<AssetHandle>{*loaded});
    EXPECT_TRUE(system->retirement().records().empty());

    DelayedRetirementRenderDevice acceptingDevice;
    ASSERT_TRUE(system->retireTexture2D(acceptingDevice, *lease, texture).has_value());
    EXPECT_FALSE(static_cast<bool>(*lease));
    EXPECT_FALSE(static_cast<bool>(texture));
    EXPECT_TRUE(acceptingDevice.completeTexture());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
}

TEST_F(AssetGpuRetirementTests, PayloadAllocationFailureRollsBackLedgerAndPreservesCallerOwners)
{
    SwitchableFailMemoryResource memory;
    auto system = createSystem(nullptr, {}, &memory);
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    auto residencyLease = system->acquire(*loaded);
    ASSERT_TRUE(residencyLease.has_value()) << residencyLease.error().message;
    const CookedAssetFile* expectedPayload = lease->get();
    constexpr Render::GpuTextureId ExpectedTexture{19U, 6U};
    Render::GpuTextureId texture = ExpectedTexture;
    DelayedRetirementRenderDevice device;
    const std::size_t baselineAllocations = memory.outstandingAllocations();

    memory.failAllocations(true);
    const auto failed = system->retireTexture2D(device, *lease, texture);
    memory.failAllocations(false);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_TRUE(static_cast<bool>(*lease));
    EXPECT_EQ(lease->handle(), *loaded);
    EXPECT_EQ(lease->get(), expectedPayload);
    EXPECT_EQ(texture, ExpectedTexture);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->store().leaseCount(*loaded), 2U);
    EXPECT_EQ(system->find(m_package.textureId), std::optional<AssetHandle>{*loaded});
    EXPECT_TRUE(system->retirement().records().empty());
    EXPECT_FALSE(device.hasPendingTexture());
    EXPECT_EQ(memory.outstandingAllocations(), baselineAllocations);

    ASSERT_TRUE(system->retireTexture2D(device, *lease, texture).has_value());
    EXPECT_EQ(memory.outstandingAllocations(), baselineAllocations + 1U);
    EXPECT_TRUE(device.completeTexture());
    EXPECT_EQ(memory.outstandingAllocations(), baselineAllocations);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);
    *residencyLease = AssetLease{};
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
}

TEST_F(AssetGpuRetirementTests, LeaseRetirementRejectsWrongKindCrossStoreInvalidAndWrongThreadWithoutMutation)
{
    auto system = createSystem();
    auto foreignSystem = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(foreignSystem.has_value()) << foreignSystem.error().message;

    auto textureHandle = system->loadOne(m_package.textureId);
    auto materialHandle = system->loadOne(m_package.materialId);
    auto foreignTextureHandle = foreignSystem->loadOne(m_package.textureId);
    ASSERT_TRUE(textureHandle.has_value()) << textureHandle.error().message;
    ASSERT_TRUE(materialHandle.has_value()) << materialHandle.error().message;
    ASSERT_TRUE(foreignTextureHandle.has_value()) << foreignTextureHandle.error().message;

    auto textureLease = system->acquire(*textureHandle);
    auto materialLease = system->acquire(*materialHandle);
    auto foreignTextureLease = foreignSystem->acquire(*foreignTextureHandle);
    ASSERT_TRUE(textureLease.has_value()) << textureLease.error().message;
    ASSERT_TRUE(materialLease.has_value()) << materialLease.error().message;
    ASSERT_TRUE(foreignTextureLease.has_value()) << foreignTextureLease.error().message;

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuTextureId ExpectedTexture{20U, 7U};

    Render::GpuTextureId wrongKindTexture = ExpectedTexture;
    const auto wrongKind = system->retireTexture2D(device, *materialLease, wrongKindTexture);
    ASSERT_FALSE(wrongKind.has_value());
    EXPECT_EQ(wrongKind.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_TRUE(static_cast<bool>(*materialLease));
    EXPECT_EQ(wrongKindTexture, ExpectedTexture);

    Render::GpuTextureId crossStoreTexture = ExpectedTexture;
    const auto crossStore = system->retireTexture2D(device, *foreignTextureLease, crossStoreTexture);
    ASSERT_FALSE(crossStore.has_value());
    EXPECT_EQ(crossStore.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_TRUE(static_cast<bool>(*foreignTextureLease));
    EXPECT_EQ(crossStoreTexture, ExpectedTexture);

    AssetLease invalidLease;
    Render::GpuTextureId invalidLeaseTexture = ExpectedTexture;
    const auto invalid = system->retireTexture2D(device, invalidLease, invalidLeaseTexture);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_FALSE(static_cast<bool>(invalidLease));
    EXPECT_EQ(invalidLeaseTexture, ExpectedTexture);

    Render::GpuTextureId wrongThreadTexture = ExpectedTexture;
    std::optional<Core::ErrorCode> wrongThreadError;
    std::thread otherThread([&] {
        const auto status = system->retireTexture2D(device, *textureLease, wrongThreadTexture);
        if (!status)
        {
            wrongThreadError = status.error().code;
        }
    });
    otherThread.join();
    ASSERT_TRUE(wrongThreadError.has_value());
    EXPECT_EQ(*wrongThreadError, Render::RenderErrorCode::WrongOwnerThread);
    EXPECT_TRUE(static_cast<bool>(*textureLease));
    EXPECT_EQ(wrongThreadTexture, ExpectedTexture);

    EXPECT_EQ(system->state(*textureHandle), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->state(*materialHandle), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(foreignSystem->state(*foreignTextureHandle), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->store().leaseCount(*textureHandle), 1U);
    EXPECT_EQ(system->store().leaseCount(*materialHandle), 1U);
    EXPECT_EQ(foreignSystem->store().leaseCount(*foreignTextureHandle), 1U);
    EXPECT_TRUE(system->retirement().records().empty());
    EXPECT_TRUE(foreignSystem->retirement().records().empty());
    EXPECT_FALSE(device.hasPendingTexture());
}

TEST_F(AssetGpuRetirementTests, LeaseRetirementDrainCompletesAndReleasesExactlyOnce)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;

    DelayedRetirementRenderDevice device;
    Render::GpuTextureId texture{21U, 8U};
    ASSERT_TRUE(system->retireTexture2D(device, *lease, texture).has_value());
    ASSERT_TRUE(device.hasPendingTexture());

    ASSERT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_EQ(device.drainCalls(), 1U);
    EXPECT_FALSE(device.hasPendingTexture());
    EXPECT_EQ(device.statistics().completedGpuRetirements, 1U);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);

    EXPECT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_EQ(device.drainCalls(), 1U);
    EXPECT_FALSE(device.completeTexture());
    EXPECT_EQ(device.statistics().completedGpuRetirements, 1U);
}

TEST_F(AssetGpuRetirementTests, TextureLeaseStaysPinnedUntilBackendCompletionExactlyOnce)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuTextureId Texture{7U, 3U};
    ASSERT_TRUE(system->retireTexture2D(device, *loaded, Texture).has_value());

    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    EXPECT_EQ(system->find(m_package.textureId), std::nullopt);
    ASSERT_TRUE(device.hasPendingTexture());
    ASSERT_EQ(system->retirement().records().size(), 1U);
    EXPECT_EQ(system->retirement().records()[0].kind, AssetRetirementKind::GpuTexture2D);
    EXPECT_EQ(system->retirement().records()[0].texture, Texture);
    EXPECT_EQ(system->retirement().records()[0].state, AssetRetirementState::Retiring);

    EXPECT_TRUE(device.completeTexture());
    EXPECT_FALSE(device.completeTexture());
    EXPECT_EQ(device.statistics().completedGpuRetirements, 1U);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
    EXPECT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_EQ(device.drainCalls(), 0U);
}

TEST_F(AssetGpuRetirementTests, NullUploadCleanupPreservesCompletedGpuTextureRetirement)
{
    auto uploadLedger = Render::NullUploadLedger::Create(
        Render::UploadLedgerConfig{.capacity = 8, .memoryResource = &m_memory});
    ASSERT_TRUE(uploadLedger.has_value()) << uploadLedger.error().message;

    auto system = createSystem(&*uploadLedger,
                               AssetGpuUploadConfig{
                                   .submitBudget = 8,
                                   .pollBudget = 8,
                                   .retireOnGpuReady = false,
                               });
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(system->state(*loaded), AssetLogicalState::ReadyGpu);
    ASSERT_EQ(uploadLedger->liveCount(), 1U);

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value()) << device.error().message;
    constexpr std::array<std::byte, 4> Pixel{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto texture = (*device)->createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = 1,
        .height = 1,
        .rgba8Pixels = Pixel,
    });
    ASSERT_TRUE(texture.has_value()) << texture.error().message;

    ASSERT_TRUE(system->retireTexture2D(**device, *loaded, *texture).has_value());

    EXPECT_EQ(uploadLedger->liveCount(), 0U);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    ASSERT_EQ(system->retirement().records().size(), 1U);
    const auto& record = system->retirement().records()[0];
    EXPECT_EQ(record.kind, AssetRetirementKind::GpuTexture2D);
    EXPECT_EQ(record.state, AssetRetirementState::Released);
    EXPECT_EQ(system->retirementStats().live, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);
}

TEST_F(AssetGpuRetirementTests, ExistingStaticMeshLeaseAndGpuOwnerTransferUntilDrain)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = publishStaticMesh(*system, m_memory, 0xE1U);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuMeshId ExpectedMesh{11U, 5U};
    Render::GpuMeshId mesh = ExpectedMesh;
    ASSERT_TRUE(system->retireStaticMesh(device, *lease, mesh).has_value());
    EXPECT_FALSE(static_cast<bool>(*lease));
    EXPECT_FALSE(static_cast<bool>(mesh));
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    ASSERT_TRUE(device.hasPendingMesh());
    ASSERT_EQ(system->retirement().records().size(), 1U);
    EXPECT_EQ(system->retirement().records()[0].kind, AssetRetirementKind::GpuStaticMesh);
    EXPECT_EQ(system->retirement().records()[0].mesh, ExpectedMesh);

    ASSERT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_FALSE(device.hasPendingMesh());
    EXPECT_EQ(device.drainCalls(), 1U);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
}

TEST_F(AssetGpuRetirementTests, SkinnedMeshLeaseUsesTheSharedGpuMeshRetirementContract)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = publishSkinnedMesh(*system, m_memory, 0xE5U);
    ASSERT_TRUE(loaded.has_value()) << (loaded ? "" : loaded.error().message);
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << (lease ? "" : lease.error().message);

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuMeshId ExpectedMesh{14U, 8U};
    Render::GpuMeshId mesh = ExpectedMesh;
    ASSERT_TRUE(system->retireStaticMesh(device, *lease, mesh));
    EXPECT_FALSE(static_cast<bool>(*lease));
    EXPECT_FALSE(static_cast<bool>(mesh));
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    ASSERT_TRUE(device.hasPendingMesh());
    ASSERT_EQ(system->retirement().records().size(), 1U);
    EXPECT_EQ(system->retirement().records()[0].kind, AssetRetirementKind::GpuStaticMesh);
    EXPECT_EQ(system->retirement().records()[0].mesh, ExpectedMesh);

    ASSERT_TRUE(system->drainGpuRetirements());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
}

TEST_F(AssetGpuRetirementTests, StaticMeshBackendRejectionRestoresCallerOwnersForRetry)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = publishStaticMesh(*system, m_memory, 0xE2U);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    auto lease = system->acquire(*loaded);
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    const CookedAssetFile* expectedPayload = lease->get();
    constexpr Render::GpuMeshId ExpectedMesh{12U, 6U};
    Render::GpuMeshId mesh = ExpectedMesh;

    DelayedRetirementRenderDevice rejectingDevice{
        DelayedRetirementRenderDevice::Acceptance::Reject};
    const auto rejected = system->retireStaticMesh(rejectingDevice, *lease, mesh);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_TRUE(static_cast<bool>(*lease));
    EXPECT_EQ(lease->handle(), *loaded);
    EXPECT_EQ(lease->get(), expectedPayload);
    EXPECT_EQ(mesh, ExpectedMesh);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    EXPECT_TRUE(system->retirement().records().empty());
    EXPECT_FALSE(rejectingDevice.hasPendingMesh());

    DelayedRetirementRenderDevice acceptingDevice;
    ASSERT_TRUE(system->retireStaticMesh(acceptingDevice, *lease, mesh).has_value());
    EXPECT_FALSE(static_cast<bool>(*lease));
    EXPECT_FALSE(static_cast<bool>(mesh));
    EXPECT_TRUE(acceptingDevice.completeMesh());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
}

TEST_F(AssetGpuRetirementTests, StaticMeshLeaseRejectsWrongKindCrossStoreAndWrongThread)
{
    auto system = createSystem();
    auto foreignSystem = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_TRUE(foreignSystem.has_value()) << foreignSystem.error().message;
    auto meshHandle = publishStaticMesh(*system, m_memory, 0xE3U);
    auto foreignMeshHandle = publishStaticMesh(*foreignSystem, m_memory, 0xE4U);
    auto materialHandle = system->loadOne(m_package.materialId);
    ASSERT_TRUE(meshHandle.has_value()) << meshHandle.error().message;
    ASSERT_TRUE(foreignMeshHandle.has_value()) << foreignMeshHandle.error().message;
    ASSERT_TRUE(materialHandle.has_value()) << materialHandle.error().message;
    auto meshLease = system->acquire(*meshHandle);
    auto foreignMeshLease = foreignSystem->acquire(*foreignMeshHandle);
    auto materialLease = system->acquire(*materialHandle);
    ASSERT_TRUE(meshLease.has_value());
    ASSERT_TRUE(foreignMeshLease.has_value());
    ASSERT_TRUE(materialLease.has_value());

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuMeshId ExpectedMesh{13U, 7U};
    Render::GpuMeshId wrongKindMesh = ExpectedMesh;
    const auto wrongKind = system->retireStaticMesh(device, *materialLease, wrongKindMesh);
    ASSERT_FALSE(wrongKind.has_value());
    EXPECT_EQ(wrongKind.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_TRUE(static_cast<bool>(*materialLease));
    EXPECT_EQ(wrongKindMesh, ExpectedMesh);

    Render::GpuMeshId crossStoreMesh = ExpectedMesh;
    const auto crossStore = system->retireStaticMesh(device, *foreignMeshLease, crossStoreMesh);
    ASSERT_FALSE(crossStore.has_value());
    EXPECT_EQ(crossStore.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_TRUE(static_cast<bool>(*foreignMeshLease));
    EXPECT_EQ(crossStoreMesh, ExpectedMesh);

    Render::GpuMeshId wrongThreadMesh = ExpectedMesh;
    std::optional<Core::ErrorCode> wrongThreadError;
    std::thread otherThread([&] {
        const auto status = system->retireStaticMesh(device, *meshLease, wrongThreadMesh);
        if (!status)
        {
            wrongThreadError = status.error().code;
        }
    });
    otherThread.join();
    ASSERT_TRUE(wrongThreadError.has_value());
    EXPECT_EQ(*wrongThreadError, Render::RenderErrorCode::WrongOwnerThread);
    EXPECT_TRUE(static_cast<bool>(*meshLease));
    EXPECT_EQ(wrongThreadMesh, ExpectedMesh);
    EXPECT_TRUE(system->retirement().records().empty());
    EXPECT_TRUE(foreignSystem->retirement().records().empty());
    EXPECT_FALSE(device.hasPendingMesh());
}

TEST_F(AssetGpuRetirementTests, DrainRejectsFalseBackendCompletionWhileLeaseRemainsLive)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    DelayedRetirementRenderDevice device;
    device.setCompleteOnDrain(false);
    ASSERT_TRUE(system->retireTexture2D(device, *loaded, Render::GpuTextureId{12U, 2U}).has_value());

    const auto incompleteDrain = system->drainGpuRetirements();
    ASSERT_FALSE(incompleteDrain.has_value());
    EXPECT_EQ(incompleteDrain.error().code, Render::RenderErrorCode::GpuRetirementDrainFailed);
    EXPECT_TRUE(device.hasPendingTexture());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);

    EXPECT_TRUE(device.completeTexture());
    EXPECT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
}

TEST_F(AssetGpuRetirementTests, RejectedRetirementDoesNotConsumeAssetOrLeaveLedgerEntry)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    DelayedRetirementRenderDevice device{DelayedRetirementRenderDevice::Acceptance::Reject};
    const auto status = system->retireTexture2D(device, *loaded, Render::GpuTextureId{2U, 1U});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_FALSE(device.hasPendingTexture());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::ReadyCpu);
    EXPECT_EQ(system->store().leaseCount(*loaded), 0U);
    EXPECT_EQ(system->find(m_package.textureId), std::optional<AssetHandle>{*loaded});
    EXPECT_TRUE(system->acquire(*loaded).has_value());
    EXPECT_EQ(system->retirementStats().live, 0U);
    EXPECT_TRUE(system->retirement().records().empty());
}

TEST_F(AssetGpuRetirementTests, RejectedRetirementKeepsExistingNullUploadTicketTracked)
{
    auto uploadLedger = Render::NullUploadLedger::Create(
        Render::UploadLedgerConfig{.capacity = 8, .memoryResource = &m_memory});
    ASSERT_TRUE(uploadLedger.has_value()) << uploadLedger.error().message;
    auto system = createSystem(&*uploadLedger,
                               AssetGpuUploadConfig{
                                   .submitBudget = 8,
                                   .pollBudget = 8,
                                   .retireOnGpuReady = false,
                               });
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.textureId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    ASSERT_EQ(system->state(*loaded), AssetLogicalState::ReadyGpu);
    ASSERT_EQ(uploadLedger->liveCount(), 1U);

    DelayedRetirementRenderDevice device{DelayedRetirementRenderDevice::Acceptance::Reject};
    const auto status = system->retireTexture2D(device, *loaded, Render::GpuTextureId{2U, 1U});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::ReadyGpu);
    EXPECT_EQ(system->store().leaseCount(*loaded), 0U);
    EXPECT_EQ(system->find(m_package.textureId), std::optional<AssetHandle>{*loaded});
    EXPECT_EQ(uploadLedger->liveCount(), 1U);
    EXPECT_TRUE(system->retirement().records().empty());

    ASSERT_TRUE(system->unload(*loaded).has_value());
    EXPECT_EQ(uploadLedger->liveCount(), 0U);
}

TEST_F(AssetGpuRetirementTests, AssetSystemDestructorDrainsOutstandingGpuPins)
{
    DelayedRetirementRenderDevice device;
    {
        auto system = createSystem();
        ASSERT_TRUE(system.has_value()) << system.error().message;
        auto loaded = system->loadOne(m_package.textureId);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        ASSERT_TRUE(system->retireTexture2D(device, *loaded, Render::GpuTextureId{3U, 1U}).has_value());
        ASSERT_TRUE(device.hasPendingTexture());
    }

    EXPECT_FALSE(device.hasPendingTexture());
    EXPECT_EQ(device.drainCalls(), 1U);
    EXPECT_EQ(device.statistics().completedGpuRetirements, 1U);
}

} // namespace
} // namespace Tina::Asset
