#include <tina/asset/AssetSystem.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::removePackage;
using TestSupport::toUtf8;
using TestSupport::writeTextureMaterialPackage;

class DelayedRetirementRenderDevice final : public Render::IRenderDevice {
  public:
    enum class Acceptance {
        Accept,
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
                 AssetGpuUploadConfig gpuUpload = {})
    {
        auto system = AssetSystem::Create(AssetSystemConfig{
            .storeCapacity = 8,
            .memoryResource = &m_memory,
            .batch =
                CookedAssetBatchLoadConfig{
                    .file = CookedAssetFileLoadConfig{.memoryResource = &m_memory},
                    .memoryResource = &m_memory,
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

TEST_F(AssetGpuRetirementTests, StaticMeshDrainReleasesLeaseAndLedgerRecord)
{
    auto system = createSystem();
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto loaded = system->loadOne(m_package.materialId);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    DelayedRetirementRenderDevice device;
    constexpr Render::GpuMeshId Mesh{11U, 5U};
    ASSERT_TRUE(system->retireStaticMesh(device, *loaded, Mesh).has_value());
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::UnloadPending);
    EXPECT_EQ(system->store().leaseCount(*loaded), 1U);
    ASSERT_TRUE(device.hasPendingMesh());
    ASSERT_EQ(system->retirement().records().size(), 1U);
    EXPECT_EQ(system->retirement().records()[0].kind, AssetRetirementKind::GpuStaticMesh);
    EXPECT_EQ(system->retirement().records()[0].mesh, Mesh);

    ASSERT_TRUE(system->drainGpuRetirements().has_value());
    EXPECT_FALSE(device.hasPendingMesh());
    EXPECT_EQ(device.drainCalls(), 1U);
    EXPECT_EQ(system->state(*loaded), AssetLogicalState::Unloaded);
    EXPECT_EQ(system->retirementStats().released, 1U);
    EXPECT_EQ(system->retirementStats().live, 0U);
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
