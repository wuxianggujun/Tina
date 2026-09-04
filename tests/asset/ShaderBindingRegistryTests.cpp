#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/ShaderBindingRegistry.hpp>
#include <tina/asset_format/ShaderPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
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

    std::size_t m_rejectedAllocationMinimumBytes;
    Core::usize m_allocationAttempts = 0;
    Core::usize m_rejectedAllocations = 0;
    Core::usize m_outstandingAllocations = 0;
};

class ShaderBindingRenderDevice final : public Render::IRenderDevice {
  public:
    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(const Render::RenderFrame&) override
    {
        return Render::RenderFrameSubmission::SkippedSuspendedSurface();
    }

    [[nodiscard]] Core::Status present() override { return Core::success(); }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override { return {}; }
    void shutdown() noexcept override {}

    [[nodiscard]] Core::Result<Render::GpuShaderId> createShader(const Render::GpuShaderUploadDesc& desc) override
    {
        ++m_createAttempts;
        if (auto status = Render::validateShaderUploadDesc(desc); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (m_rejectCreateAttempt == m_createAttempts)
        {
            m_rejectCreateAttempt = 0;
            return Core::failure(Render::RenderErrorCode::ShaderUploadUnsupported,
                                 "shader binding test device rejected shader upload");
        }
        return Render::GpuShaderId{m_nextShaderIndex++, 1U};
    }

    [[nodiscard]] Core::Status validateShader(Render::GpuShaderId shader) const noexcept override
    {
        return shader ? Core::success()
                      : Core::failure(Render::RenderErrorCode::ShaderNotFound,
                                      "shader binding test device received invalid shader");
    }

    [[nodiscard]] Core::Status setShaderBinding(Core::u32 bindingKey,
                                                Render::GpuShaderId shader) noexcept override
    {
        ++m_bindingAttempts;
        if (m_rejectBindingAttempt == m_bindingAttempts)
        {
            m_rejectBindingAttempt = 0;
            return Core::failure(Render::RenderErrorCode::ShaderUploadUnsupported,
                                 "shader binding test device rejected shader binding");
        }
        m_lastBindingKey = bindingKey;
        m_lastBoundShader = shader;
        return Core::success();
    }

    [[nodiscard]] Core::Status
    setShaderUniformBinding(Core::u32 bindingKey,
                            const Render::GpuShaderUniformBindingDesc& desc) noexcept override
    {
        ++m_uniformBindingAttempts;
        if (m_rejectUniformBindingAttempt == m_uniformBindingAttempts)
        {
            m_rejectUniformBindingAttempt = 0;
            return Core::failure(Render::RenderErrorCode::ShaderUploadUnsupported,
                                 "shader binding test device rejected uniform binding");
        }
        m_lastUniformBindingKey = bindingKey;
        if (desc.values.empty())
        {
            m_clearedUniformBindingKeys.push_back(bindingKey);
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status retireShader(Render::GpuShaderId shader,
                                            Render::FramePin& completionPin) noexcept override
    {
        ++m_retirementAttempts;
        if (m_rejectRetirementAttempt == m_retirementAttempts)
        {
            m_rejectRetirementAttempt = 0;
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "shader binding test device rejected shader retirement");
        }
        m_lastRetiredShader = shader;
        ++m_retirementCount;
        if (m_delayRetirement)
        {
            m_pendingRetirementPin = std::move(completionPin);
        }
        else
        {
            completionPin.release();
        }
        return Core::success();
    }

    void rejectNextCreate() noexcept { m_rejectCreateAttempt = m_createAttempts + 1U; }
    void rejectNextBinding() noexcept { m_rejectBindingAttempt = m_bindingAttempts + 1U; }
    void rejectNextUniformBinding() noexcept { m_rejectUniformBindingAttempt = m_uniformBindingAttempts + 1U; }
    void rejectNextRetirement() noexcept { m_rejectRetirementAttempt = m_retirementAttempts + 1U; }
    void rejectRetirementOnAttempt(Core::usize attempt) noexcept { m_rejectRetirementAttempt = attempt; }
    void delayRetirement() noexcept { m_delayRetirement = true; }
    void completeRetirement() noexcept { m_pendingRetirementPin.release(); }

    [[nodiscard]] Core::usize bindingAttempts() const noexcept { return m_bindingAttempts; }
    [[nodiscard]] Render::GpuShaderId lastBoundShader() const noexcept { return m_lastBoundShader; }
    [[nodiscard]] Core::usize uniformBindingAttempts() const noexcept { return m_uniformBindingAttempts; }
    [[nodiscard]] Core::u32 lastUniformBindingKey() const noexcept { return m_lastUniformBindingKey; }
    [[nodiscard]] bool clearedUniformBinding(Core::u32 key) const noexcept
    {
        return std::find(m_clearedUniformBindingKeys.begin(), m_clearedUniformBindingKeys.end(), key) !=
               m_clearedUniformBindingKeys.end();
    }
    [[nodiscard]] Core::usize retirementAttempts() const noexcept { return m_retirementAttempts; }
    [[nodiscard]] Core::usize retirementCount() const noexcept { return m_retirementCount; }
    [[nodiscard]] bool hasPendingRetirement() const noexcept { return m_pendingRetirementPin.hasValue(); }
    [[nodiscard]] Render::GpuShaderId lastRetiredShader() const noexcept { return m_lastRetiredShader; }

  private:
    Core::usize m_createAttempts = 0;
    Core::usize m_bindingAttempts = 0;
    Core::usize m_uniformBindingAttempts = 0;
    Core::usize m_retirementAttempts = 0;
    Core::usize m_retirementCount = 0;
    Core::usize m_rejectCreateAttempt = 0;
    Core::usize m_rejectBindingAttempt = 0;
    Core::usize m_rejectUniformBindingAttempt = 0;
    Core::usize m_rejectRetirementAttempt = 0;
    Core::u32 m_nextShaderIndex = 100U;
    Core::u32 m_lastBindingKey = 0;
    Core::u32 m_lastUniformBindingKey = 0;
    std::vector<Core::u32> m_clearedUniformBindingKeys{};
    Render::GpuShaderId m_lastBoundShader{};
    Render::GpuShaderId m_lastRetiredShader{};
    Render::FramePin m_pendingRetirementPin{};
    bool m_delayRetirement = false;
};

class RejectingFrameResourceSink final : public Render::FrameResourceSink {
  public:
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    intern(Render::FrameResourceDescriptor, Render::FramePin&&) noexcept override
    {
        ++m_callCount;
        return Core::failure(Render::RenderErrorCode::InvalidFrameResource,
                             "test frame resource sink rejected intern");
    }

    [[nodiscard]] Core::u32 resourceCount() const noexcept override { return 0U; }
    [[nodiscard]] Core::usize callCount() const noexcept { return m_callCount; }

  private:
    Core::usize m_callCount = 0;
};

[[nodiscard]] CookedAssetFile makeShader(std::pmr::memory_resource& memory, Core::u8 seed)
{
    static constexpr std::array bytes{std::byte{0x01}, std::byte{0x02}};
    const std::array blobs{
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::Glsl120, .bytes = bytes},
    };
    auto cooked = AssetFormat::writeCookedShaderAsset(
        assetId(seed), AssetFormat::ShaderPayloadDesc{.shaderKind = AssetFormat::ShaderKind::Sprite2D, .blobs = blobs});
    EXPECT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);
    if (!cooked)
    {
        return {};
    }
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    return file ? std::move(*file) : CookedAssetFile{};
}

[[nodiscard]] CookedAssetFile makeWrongKind(std::pmr::memory_resource& memory, Core::u8 seed)
{
    constexpr std::array pixels{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto cooked = AssetFormat::writeCookedTexture2DAssetRgba8(assetId(seed), 1U, 1U, pixels);
    EXPECT_TRUE(cooked.has_value());
    if (!cooked)
    {
        return {};
    }
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value());
    return file ? std::move(*file) : CookedAssetFile{};
}

[[nodiscard]] Core::Result<AssetSystem> makeAssetSystem(std::pmr::memory_resource& memory,
                                                         Core::usize capacity = 16U)
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
    explicit AutoRetiringRegistry(Core::Result<ShaderBindingRegistry> registry) noexcept
        : m_registry(std::move(registry))
    {
    }

    ~AutoRetiringRegistry() noexcept
    {
        if (m_registry && m_registry->bindingCount() != 0 && !m_registry->retireAllShaderBindings())
        {
            std::terminate();
        }
    }

    [[nodiscard]] bool has_value() const noexcept { return m_registry.has_value(); }
    [[nodiscard]] decltype(auto) error() const { return m_registry.error(); }
    [[nodiscard]] ShaderBindingRegistry& operator*() noexcept { return *m_registry; }
    [[nodiscard]] const ShaderBindingRegistry& operator*() const noexcept { return *m_registry; }
    [[nodiscard]] ShaderBindingRegistry* operator->() noexcept { return &*m_registry; }

  private:
    Core::Result<ShaderBindingRegistry> m_registry;
};

[[nodiscard]] AutoRetiringRegistry makeRegistry(AssetSystem& assets, Render::IRenderDevice& device,
                                                ShaderBindingRegistryConfig config = {})
{
    return AutoRetiringRegistry{ShaderBindingRegistry::Create(assets, device, config)};
}

[[nodiscard]] Core::Result<Core::u32> registerShader(ShaderBindingRegistry& registry, AssetHandle shader,
                                                     Render::GpuShaderId gpuShader) noexcept
{
    return registry.registerShaderBinding(shader, gpuShader);
}

void destroyRegistryWithOwnedBinding()
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    if (!assets)
    {
        std::abort();
    }
    auto shader = assets->store().publish(makeShader(memory, 1U));
    if (!shader)
    {
        std::abort();
    }
    ShaderBindingRenderDevice device;
    auto registry = ShaderBindingRegistry::Create(*assets, device);
    if (!registry || !registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U}))
    {
        std::abort();
    }
}

TEST(ShaderBindingRegistryTests, CreateValidatesCapacityBoundsAndAllocationFailure)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    ShaderBindingRenderDevice device;

    auto zero = makeRegistry(*assets, device, ShaderBindingRegistryConfig{.shaderCapacity = 0U, .memoryResource = &memory});
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto excessive = makeRegistry(*assets, device,
                                  ShaderBindingRegistryConfig{.shaderCapacity = MaximumShaderBindingCapacity + 1U,
                                                              .memoryResource = &memory});
    ASSERT_FALSE(excessive.has_value());
    EXPECT_EQ(excessive.error().code, AssetErrorCode::InvalidCatalogConfig);

    ThrowingMemoryResource throwingMemory{64U};
    auto allocationFailure = makeRegistry(
        *assets, device,
        ShaderBindingRegistryConfig{.shaderCapacity = DefaultShaderBindingCapacity, .memoryResource = &throwingMemory});
    ASSERT_FALSE(allocationFailure.has_value());
    EXPECT_EQ(allocationFailure.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_GE(throwingMemory.allocationAttempts(), 1U);
    EXPECT_EQ(throwingMemory.rejectedAllocations(), 1U);
    EXPECT_EQ(throwingMemory.outstandingAllocations(), 0U);
}

TEST(ShaderBindingRegistryTests, RegistrationTransfersOwnersAndRejectsConflictsAndCapacity)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto first = assets->store().publish(makeShader(memory, 1U));
    auto sameId = assets->store().publish(makeShader(memory, 1U));
    auto second = assets->store().publish(makeShader(memory, 2U));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(sameId.has_value());
    ASSERT_TRUE(second.has_value());

    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device, ShaderBindingRegistryConfig{.shaderCapacity = 1U, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    Render::GpuShaderId gpu{7U, 3U};
    const auto binding = registry->registerShaderBinding(*first, gpu);
    ASSERT_TRUE(binding.has_value()) << binding.error().message;
    EXPECT_FALSE(gpu);
    EXPECT_EQ(assets->store().leaseCount(*first), 1U);
    EXPECT_EQ(registry->bindingKey(*first), *binding);
    // Registration allocates an empty uniform slot from a namespace independent of
    // the shader binding keys, so the draw can name it before any value exists.
    EXPECT_NE(registry->uniformBindingKey(*first), 0U);
    EXPECT_EQ(device.uniformBindingAttempts(), 1U);
    EXPECT_EQ(device.lastUniformBindingKey(), registry->uniformBindingKey(*first));

    Render::GpuShaderId sameHandleGpu{8U, 1U};
    auto sameHandle = registry->registerShaderBinding(*first, sameHandleGpu);
    ASSERT_FALSE(sameHandle.has_value());
    EXPECT_EQ(sameHandle.error().code, AssetErrorCode::ShaderBindingConflict);
    EXPECT_EQ(sameHandleGpu, (Render::GpuShaderId{8U, 1U}));

    Render::GpuShaderId sameIdGpu{9U, 1U};
    auto duplicateId = registry->registerShaderBinding(*sameId, sameIdGpu);
    ASSERT_FALSE(duplicateId.has_value());
    EXPECT_EQ(duplicateId.error().code, AssetErrorCode::ShaderBindingConflict);

    Render::GpuShaderId sharedGpu{7U, 3U};
    auto duplicateGpu = registry->registerShaderBinding(*second, sharedGpu);
    ASSERT_FALSE(duplicateGpu.has_value());
    EXPECT_EQ(duplicateGpu.error().code, AssetErrorCode::ShaderBindingConflict);

    Render::GpuShaderId capacityGpu{10U, 1U};
    auto capacity = registry->registerShaderBinding(*second, capacityGpu);
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, AssetErrorCode::ShaderBindingCapacityExceeded);
    EXPECT_EQ(assets->store().leaseCount(*second), 0U);
}

TEST(ShaderBindingRegistryTests, RegistrationFailsClosedForInvalidStaleWrongKindNotReadyAndForeignHandles)
{
    TrackingMemoryResource memory;
    TrackingMemoryResource foreignMemory;
    auto assets = makeAssetSystem(memory);
    auto foreignAssets = makeAssetSystem(foreignMemory);
    ASSERT_TRUE(assets.has_value());
    ASSERT_TRUE(foreignAssets.has_value());
    auto wrongKind = assets->store().publish(makeWrongKind(memory, 1U));
    auto stale = assets->store().publish(makeShader(memory, 2U));
    auto queued = assets->store().beginQueued(assetId(3U), AssetFormat::AssetKind::Shader);
    auto foreign = foreignAssets->store().publish(makeShader(foreignMemory, 4U));
    ASSERT_TRUE(wrongKind.has_value());
    ASSERT_TRUE(stale.has_value());
    ASSERT_TRUE(queued.has_value());
    ASSERT_TRUE(foreign.has_value());
    ASSERT_TRUE(assets->store().unload(*stale).has_value());

    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    const auto expectInvalid = [&](AssetHandle handle) {
        Render::GpuShaderId gpu{1U, 1U};
        auto result = registry->registerShaderBinding(handle, gpu);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, AssetErrorCode::InvalidHandle);
        EXPECT_EQ(gpu, (Render::GpuShaderId{1U, 1U}));
    };
    expectInvalid({});
    expectInvalid(*wrongKind);
    expectInvalid(*stale);
    expectInvalid(*foreign);

    Render::GpuShaderId queuedGpu{1U, 1U};
    auto notReady = registry->registerShaderBinding(*queued, queuedGpu);
    ASSERT_FALSE(notReady.has_value());
    EXPECT_EQ(notReady.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(queuedGpu, (Render::GpuShaderId{1U, 1U}));
}

TEST(ShaderBindingRegistryTests, BackendBindingFailureReleasesLeaseAndPreservesGpuForRetry)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    device.rejectNextBinding();

    Render::GpuShaderId gpu{1U, 1U};
    auto rejected = registry->registerShaderBinding(*shader, gpu);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::ShaderUploadUnsupported);
    EXPECT_EQ(gpu, (Render::GpuShaderId{1U, 1U}));
    EXPECT_EQ(assets->store().leaseCount(*shader), 0U);
    EXPECT_EQ(registry->bindingCount(), 0U);

    auto accepted = registry->registerShaderBinding(*shader, gpu);
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_FALSE(gpu);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(ShaderBindingRegistryTests, UniformBindingFailureReleasesLeaseAndUnbindsTheShaderKey)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    device.rejectNextUniformBinding();

    Render::GpuShaderId gpu{1U, 1U};
    auto rejected = registry->registerShaderBinding(*shader, gpu);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::ShaderUploadUnsupported);
    EXPECT_EQ(gpu, (Render::GpuShaderId{1U, 1U}));
    EXPECT_EQ(assets->store().leaseCount(*shader), 0U);
    EXPECT_EQ(registry->bindingCount(), 0U);
    // The shader key was already published when uniform allocation failed, so it
    // must be unbound or the device keeps a binding no entry owns.
    EXPECT_EQ(device.bindingAttempts(), 2U);
    EXPECT_FALSE(device.lastBoundShader());

    auto accepted = registry->registerShaderBinding(*shader, gpu);
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_NE(registry->uniformBindingKey(*shader), 0U);
}

TEST(ShaderBindingRegistryTests, UniformValuesPublishThroughTheOwnedBindingKey)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    auto unregistered = assets->store().publish(makeShader(memory, 2U));
    ASSERT_TRUE(shader.has_value());
    ASSERT_TRUE(unregistered.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device, ShaderBindingRegistryConfig{.shaderCapacity = 2U, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    ASSERT_TRUE(registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U}).has_value());
    const Core::u32 uniformKey = registry->uniformBindingKey(*shader);
    ASSERT_NE(uniformKey, 0U);

    Render::GpuShaderUniformValue pulse{};
    pulse.name[0] = 'u';
    pulse.name[1] = '_';
    pulse.name[2] = 'p';
    pulse.value = {0.25F, 0.0F, 0.0F, 1.0F};
    const std::array values{pulse};
    ASSERT_TRUE(registry->setShaderUniformValues(*shader, Render::GpuShaderUniformBindingDesc{.values = values})
                    .has_value());
    EXPECT_EQ(device.lastUniformBindingKey(), uniformKey);

    auto missing = registry->setShaderUniformValues(*unregistered,
                                                    Render::GpuShaderUniformBindingDesc{.values = values});
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, AssetErrorCode::ShaderBindingNotFound);
}

TEST(ShaderBindingRegistryTests, UniformFrameResourcesUseTheirOwnKindKeyAndBorrow)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U});
    ASSERT_TRUE(binding.has_value());
    const Core::u32 uniformKey = registry->uniformBindingKey(*shader);
    ASSERT_NE(uniformKey, 0U);

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    auto uniforms = registry->internShaderUniformFrameResource(*shader, packet.resourceSink());
    auto duplicate = registry->internShaderUniformFrameResource(*shader, packet.resourceSink());
    auto program = registry->internShaderFrameResource(*shader, packet.resourceSink());
    ASSERT_TRUE(uniforms.has_value());
    ASSERT_TRUE(duplicate.has_value());
    ASSERT_TRUE(program.has_value());
    EXPECT_EQ(*uniforms, *duplicate);
    EXPECT_NE(*uniforms, *program);
    EXPECT_EQ(packet.resourceCount(), 2U);
    const auto* descriptor =
        packet.resourceTableView().resolve(*uniforms, Render::FrameResourceKind::ShaderUniforms);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->deviceBindingKey, uniformKey);
    // Swapping the two kinds must not resolve: the batch key would name a program
    // where the backend expects a value table.
    EXPECT_EQ(packet.resourceTableView().resolve(*uniforms, Render::FrameResourceKind::Shader), nullptr);

    auto blocked = registry->retireShaderBinding(*shader);
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, AssetErrorCode::AssetNotReady);
    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireShaderBinding(*shader).has_value());
    EXPECT_TRUE(device.clearedUniformBinding(uniformKey));
}

TEST(ShaderBindingRegistryTests, UniformSinkFailureReleasesBorrowAndLeavesBindingRetirable)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    ASSERT_TRUE(registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U}).has_value());

    RejectingFrameResourceSink sink;
    auto rejected = registry->internShaderUniformFrameResource(*shader, sink);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(sink.callCount(), 1U);
    ASSERT_TRUE(registry->retireShaderBinding(*shader).has_value());
}

TEST(ShaderBindingRegistryTests, FrameResourcesUseShaderDescriptorDeduplicateAndBlockRetirement)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U});
    ASSERT_TRUE(binding.has_value());

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    auto first = registry->internShaderFrameResource(*shader, packet.resourceSink());
    auto duplicate = registry->internShaderFrameResource(*shader, packet.resourceSink());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_EQ(*first, *duplicate);
    EXPECT_EQ(packet.resourceCount(), 1U);
    const auto* descriptor = packet.resourceTableView().resolve(*first, Render::FrameResourceKind::Shader);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->deviceBindingKey, *binding);

    auto blocked = registry->retireShaderBinding(*shader);
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, AssetErrorCode::AssetNotReady);
    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireShaderBinding(*shader).has_value());
}

TEST(ShaderBindingRegistryTests, SinkFailureReleasesBorrowAndLeavesBindingRetirable)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    ASSERT_TRUE(registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U}).has_value());

    RejectingFrameResourceSink sink;
    auto rejected = registry->internShaderFrameResource(*shader, sink);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(sink.callCount(), 1U);
    ASSERT_TRUE(registry->retireShaderBinding(*shader).has_value());
}

TEST(ShaderBindingRegistryTests, RetirementFailureIsRetryableAndDelayedCompletionOwnsLease)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto first = assets->store().publish(makeShader(memory, 1U));
    auto second = assets->store().publish(makeShader(memory, 2U));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device, ShaderBindingRegistryConfig{.shaderCapacity = 2U, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    ASSERT_TRUE(registerShader(*registry, *first, Render::GpuShaderId{1U, 1U}).has_value());
    ASSERT_TRUE(registerShader(*registry, *second, Render::GpuShaderId{2U, 1U}).has_value());

    device.rejectNextRetirement();
    auto rejected = registry->retireShaderBinding(*first);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(registry->bindingCount(), 2U);
    ASSERT_TRUE(registry->retireShaderBinding(*first).has_value());
    EXPECT_EQ(registry->bindingCount(), 1U);

    device.delayRetirement();
    ASSERT_TRUE(registry->retireShaderBinding(*second).has_value());
    EXPECT_TRUE(device.hasPendingRetirement());
    EXPECT_EQ(assets->store().state(*second), AssetLogicalState::UnloadPending);
    EXPECT_EQ(assets->store().leaseCount(*second), 1U);
    device.completeRetirement();
    EXPECT_EQ(assets->store().state(*second), AssetLogicalState::Unloaded);
    EXPECT_EQ(assets->store().leaseCount(*second), 0U);
}

TEST(ShaderBindingRegistryTests, RetireAllPreflightsActiveBorrowAndRetriesCommittedPrefix)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto first = assets->store().publish(makeShader(memory, 1U));
    auto second = assets->store().publish(makeShader(memory, 2U));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device, ShaderBindingRegistryConfig{.shaderCapacity = 2U, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    ASSERT_TRUE(registerShader(*registry, *first, Render::GpuShaderId{1U, 1U}).has_value());
    ASSERT_TRUE(registerShader(*registry, *second, Render::GpuShaderId{2U, 1U}).has_value());

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    ASSERT_TRUE(registry->internShaderFrameResource(*second, packet.resourceSink()).has_value());
    auto blocked = registry->retireAllShaderBindings();
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(device.retirementAttempts(), 0U);
    ASSERT_TRUE(packet.abandon().has_value());

    device.rejectRetirementOnAttempt(2U);
    auto partial = registry->retireAllShaderBindings();
    ASSERT_FALSE(partial.has_value());
    EXPECT_EQ(registry->bindingCount(), 1U);
    ASSERT_TRUE(registry->retireAllShaderBindings().has_value());
    EXPECT_EQ(registry->bindingCount(), 0U);
}

TEST(ShaderBindingRegistryTests, WrongOwnerThreadAndMoveFailClosed)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto shader = assets->store().publish(makeShader(memory, 1U));
    ASSERT_TRUE(shader.has_value());
    ShaderBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerShader(*registry, *shader, Render::GpuShaderId{1U, 1U});
    ASSERT_TRUE(binding.has_value());

    std::optional<Core::ErrorCode> registerError;
    std::optional<Core::ErrorCode> uniformInternError;
    std::optional<Core::ErrorCode> uniformPublishError;
    Core::u32 foreignKey = *binding;
    Core::u32 foreignUniformKey = registry->uniformBindingKey(*shader);
    ASSERT_NE(foreignUniformKey, 0U);
    std::thread foreign([&] {
        Render::GpuShaderId candidate{2U, 1U};
        auto registration = registry->registerShaderBinding(*shader, candidate);
        if (!registration)
        {
            registerError = registration.error().code;
        }
        RejectingFrameResourceSink sink;
        auto uniformIntern = registry->internShaderUniformFrameResource(*shader, sink);
        if (!uniformIntern)
        {
            uniformInternError = uniformIntern.error().code;
        }
        auto uniformPublish = registry->setShaderUniformValues(*shader, {});
        if (!uniformPublish)
        {
            uniformPublishError = uniformPublish.error().code;
        }
        foreignKey = registry->bindingKey(*shader);
        foreignUniformKey = registry->uniformBindingKey(*shader);
    });
    foreign.join();
    ASSERT_TRUE(registerError.has_value());
    EXPECT_EQ(*registerError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(uniformInternError.has_value());
    EXPECT_EQ(*uniformInternError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(uniformPublishError.has_value());
    EXPECT_EQ(*uniformPublishError, Render::RenderErrorCode::WrongOwnerThread);
    EXPECT_EQ(foreignKey, 0U);
    EXPECT_EQ(foreignUniformKey, 0U);

    ShaderBindingRegistry moved = std::move(*registry);
    EXPECT_FALSE(static_cast<bool>(*registry));
    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(moved.bindingKey(*shader), *binding);
    ASSERT_TRUE(moved.retireAllShaderBindings().has_value());
}

TEST(ShaderBindingRegistryTests, DestructionWithOwnedBindingFailsFast)
{
    EXPECT_DEATH(destroyRegistryWithOwnedBinding(), "");
}

} // namespace
} // namespace Tina::Asset
