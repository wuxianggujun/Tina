#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>

#include "support/CatalogPackageTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <exception>
#include <limits>
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

    [[nodiscard]] Core::usize allocationAttempts() const noexcept
    {
        return m_allocationAttempts;
    }

    [[nodiscard]] Core::usize rejectedAllocations() const noexcept
    {
        return m_rejectedAllocations;
    }

    [[nodiscard]] Core::usize outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

    void rejectAllocationsAtOrAbove(std::size_t minimumBytes) noexcept
    {
        m_rejectedAllocationMinimumBytes = minimumBytes;
    }

    void allowAllocations() noexcept
    {
        m_rejectedAllocationMinimumBytes = (std::numeric_limits<std::size_t>::max)();
    }

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

class FixedBindingRenderDevice final : public Render::IRenderDevice {
  public:
    struct Call final {
        Core::u32 bindingKey = 0;
        Render::GpuTextureId texture{};

        [[nodiscard]] friend bool operator==(const Call&, const Call&) = default;
    };

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
        return {};
    }

    void shutdown() noexcept override
    {
    }

    [[nodiscard]] Core::Status setTexture2DBinding(Core::u32 bindingKey,
                                                   Render::GpuTextureId texture) noexcept override
    {
        if (m_callCount >= m_calls.size())
        {
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "fixed binding test device call capacity exceeded");
        }
        m_calls[m_callCount++] = Call{.bindingKey = bindingKey, .texture = texture};
        if (m_rejectNext)
        {
            m_rejectNext = false;
            return Core::failure(Render::RenderErrorCode::TextureUploadUnsupported,
                                 "fixed binding test device rejected update");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status retireTexture2D(Render::GpuTextureId texture,
                                               Render::FramePin& completionPin) noexcept override
    {
        ++m_retirementAttempts;
        if (m_rejectRetirementAttempt == m_retirementAttempts)
        {
            m_rejectRetirementAttempt = 0;
            return Core::failure(Render::RenderErrorCode::GpuRetirementUnsupported,
                                 "fixed binding test device rejected retirement");
        }
        m_lastRetiredTexture = texture;
        ++m_retirementCount;
        if (m_delayRetirement)
        {
            m_pendingRetirementPin = std::move(completionPin);
        } else
        {
            completionPin.release();
        }
        return Core::success();
    }

    void rejectNextUpdate() noexcept
    {
        m_rejectNext = true;
    }

    void rejectNextRetirement() noexcept
    {
        m_rejectRetirementAttempt = m_retirementAttempts + 1U;
    }

    void rejectRetirementOnAttempt(Core::usize attempt) noexcept
    {
        m_rejectRetirementAttempt = attempt;
    }

    void delayRetirement() noexcept
    {
        m_delayRetirement = true;
    }

    void completeRetirement() noexcept
    {
        m_pendingRetirementPin.release();
    }

    [[nodiscard]] Core::usize callCount() const noexcept
    {
        return m_callCount;
    }

    [[nodiscard]] const Call& call(Core::usize index) const noexcept
    {
        return m_calls[index];
    }

    [[nodiscard]] Core::usize retirementAttempts() const noexcept
    {
        return m_retirementAttempts;
    }

    [[nodiscard]] Core::usize retirementCount() const noexcept
    {
        return m_retirementCount;
    }

    [[nodiscard]] Render::GpuTextureId lastRetiredTexture() const noexcept
    {
        return m_lastRetiredTexture;
    }

    [[nodiscard]] bool hasPendingRetirement() const noexcept
    {
        return m_pendingRetirementPin.hasValue();
    }

  private:
    std::array<Call, 1024> m_calls{};
    Core::usize m_callCount = 0;
    Core::usize m_retirementAttempts = 0;
    Core::usize m_retirementCount = 0;
    Render::GpuTextureId m_lastRetiredTexture{};
    Render::FramePin m_pendingRetirementPin{};
    bool m_rejectNext = false;
    Core::usize m_rejectRetirementAttempt = 0;
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

    [[nodiscard]] Core::u32 resourceCount() const noexcept override
    {
        return 0;
    }

    [[nodiscard]] Core::usize callCount() const noexcept
    {
        return m_callCount;
    }

  private:
    Core::usize m_callCount = 0;
};

[[nodiscard]] CookedAssetFile makeCookedFile(std::pmr::memory_resource& memory, AssetFormat::AssetKind kind,
                                             Core::u16 typeVersion, Core::AssetId id,
                                             std::span<const AssetFormat::CookedAssetWriteDependency> dependencies,
                                             std::span<const std::byte> payload)
{
    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = kind,
        .assetTypeVersion = typeVersion,
        .assetId = id,
        .dependencies = dependencies,
        .payload = payload,
    });
    EXPECT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);
    if (!cooked)
    {
        return {};
    }

    std::pmr::vector<std::byte> owned{&memory};
    owned.assign(cooked->begin(), cooked->end());
    auto file = makeCookedAssetFileFromBytes(std::move(owned), CookedAssetFileLoadConfig{.memoryResource = &memory});
    EXPECT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    return file ? std::move(*file) : CookedAssetFile{};
}

[[nodiscard]] CookedAssetFile makeTexture(std::pmr::memory_resource& memory, Core::u8 seed)
{
    constexpr std::array<std::byte, 4> Pixel{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF}};
    auto payload = AssetFormat::writeTexture2DPayloadBytes(
        AssetFormat::Texture2DPayloadDesc{.width = 1, .height = 1, .pixels = Pixel});
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    if (!payload)
    {
        return {};
    }
    return makeCookedFile(memory, AssetFormat::AssetKind::Texture2D, AssetFormat::Texture2DWire::SchemaVersion,
                          assetId(seed), {}, *payload);
}

[[nodiscard]] CookedAssetFile makeSprite(std::pmr::memory_resource& memory, Core::u8 seed, Core::AssetId textureId,
                                         std::span<const AssetFormat::CookedAssetWriteDependency> dependencies)
{
    auto payload = AssetFormat::writeSpritePayloadBytes(AssetFormat::SpritePayloadDesc{
        .u0 = 0.0F,
        .v0 = 0.0F,
        .u1 = 1.0F,
        .v1 = 1.0F,
        .pivotX = 0.5F,
        .pivotY = 0.5F,
        .pixelsPerUnit = 16.0F,
        .textureId = textureId,
    });
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    if (!payload)
    {
        return {};
    }
    return makeCookedFile(memory, AssetFormat::AssetKind::Sprite, AssetFormat::SpriteWire::SchemaVersion, assetId(seed),
                          dependencies, *payload);
}

[[nodiscard]] CookedAssetFile makeSprite(std::pmr::memory_resource& memory, Core::u8 seed, Core::AssetId textureId)
{
    const std::array dependencies{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    return makeSprite(memory, seed, textureId, dependencies);
}

[[nodiscard]] CookedAssetFile makeTileset(
    std::pmr::memory_resource& memory,
    Core::u8 seed,
    Core::AssetId textureId,
    std::span<const AssetFormat::CookedAssetWriteDependency> dependencies)
{
    constexpr std::array tiles{
        AssetFormat::TilesetTileDesc{
            .localId = 1,
            .u0 = 0.0F,
            .v0 = 0.0F,
            .u1 = 1.0F,
            .v1 = 1.0F,
        },
    };
    auto payload = AssetFormat::writeTilesetPayloadBytes(AssetFormat::TilesetPayloadDesc{
        .tilePixelWidth = 16,
        .tilePixelHeight = 16,
        .tiles = tiles,
        .textureId = textureId,
    });
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    if (!payload)
    {
        return {};
    }
    return makeCookedFile(
        memory,
        AssetFormat::AssetKind::Tileset,
        AssetFormat::TilesetWire::SchemaVersion,
        assetId(seed),
        dependencies,
        *payload);
}

[[nodiscard]] CookedAssetFile makeTileset(
    std::pmr::memory_resource& memory,
    Core::u8 seed,
    Core::AssetId textureId)
{
    const std::array dependencies{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    return makeTileset(memory, seed, textureId, dependencies);
}

[[nodiscard]] Core::Result<AssetSystem> makeAssetSystem(std::pmr::memory_resource& memory,
                                                       Core::usize capacity = 16U)
{
    return AssetSystem::Create(AssetSystemConfig{
        .storeCapacity = capacity,
        .memoryResource = &memory,
        .batch =
            CookedAssetBatchLoadConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &memory},
                .memoryResource = &memory,
            },
    });
}

class AutoRetiringRegistry final {
  public:
    explicit AutoRetiringRegistry(Core::Result<Sprite2DBindingRegistry> registry) noexcept
        : m_registry(std::move(registry))
    {
    }

    ~AutoRetiringRegistry() noexcept
    {
        if (m_registry && m_registry->bindingCount() != 0 && !m_registry->retireAllTextureBindings())
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
    [[nodiscard]] Sprite2DBindingRegistry& operator*() noexcept { return *m_registry; }
    [[nodiscard]] const Sprite2DBindingRegistry& operator*() const noexcept { return *m_registry; }
    [[nodiscard]] Sprite2DBindingRegistry* operator->() noexcept { return &*m_registry; }
    [[nodiscard]] const Sprite2DBindingRegistry* operator->() const noexcept { return &*m_registry; }

  private:
    Core::Result<Sprite2DBindingRegistry> m_registry;
};

[[nodiscard]] AutoRetiringRegistry makeRegistry(AssetSystem& assets, Render::IRenderDevice& device,
                                                Sprite2DBindingRegistryConfig config = {})
{
    return AutoRetiringRegistry{Sprite2DBindingRegistry::Create(assets, device, config)};
}

[[nodiscard]] Core::Result<Core::u32> registerTexture(Sprite2DBindingRegistry& registry,
                                                      AssetHandle texture,
                                                      Render::GpuTextureId gpuTexture) noexcept
{
    return registry.registerTextureBinding(texture, gpuTexture);
}

void destroyRegistryWithOwnedBinding()
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    if (!assets)
    {
        std::abort();
    }
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    if (!texture)
    {
        std::abort();
    }
    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*assets, device);
    if (!registry)
    {
        std::abort();
    }
    Render::GpuTextureId gpuTexture{1U, 1U};
    if (!registry->registerTextureBinding(*texture, gpuTexture))
    {
        std::abort();
    }
}

TEST(Sprite2DBindingRegistryTests, CreateValidatesCapacityBounds)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    FixedBindingRenderDevice device;

    auto zero = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 0, .memoryResource = &memory});
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto excessive = makeRegistry(*assets, device,
                                                     Sprite2DBindingRegistryConfig{
                                                         .textureCapacity = MaximumSprite2DBindingCapacity + 1U,
                                                         .memoryResource = &memory,
                                                     });
    ASSERT_FALSE(excessive.has_value());
    EXPECT_EQ(excessive.error().code, AssetErrorCode::InvalidCatalogConfig);

    // MSVC Debug allocates a small iterator proxy from the PMR inside vector's
    // noexcept allocator constructor. Reject only the actual fixed entry storage.
    ThrowingMemoryResource throwingMemory{64U};
    auto allocationFailure = makeRegistry(
        *assets, device,
        Sprite2DBindingRegistryConfig{
            .textureCapacity = DefaultSprite2DBindingCapacity,
            .memoryResource = &throwingMemory,
        });
    ASSERT_FALSE(allocationFailure.has_value());
    EXPECT_EQ(allocationFailure.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_GE(throwingMemory.allocationAttempts(), 1U);
    EXPECT_EQ(throwingMemory.rejectedAllocations(), 1U);
    EXPECT_EQ(throwingMemory.outstandingAllocations(), 0U);
}

TEST(Sprite2DBindingRegistryTests, RegistrationAdoptsGpuAndLeaseWhileDuplicatePreservesCandidate)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto spriteA = assets->store().publish(makeSprite(memory, 2U, textureId));
    auto spriteB = assets->store().publish(makeSprite(memory, 3U, textureId));
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    ASSERT_TRUE(spriteA.has_value()) << spriteA.error().message;
    ASSERT_TRUE(spriteB.has_value()) << spriteB.error().message;

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    constexpr Render::GpuTextureId OriginalGpuTexture{7U, 3U};
    Render::GpuTextureId gpuTexture = OriginalGpuTexture;

    auto registered = registry->registerTextureBinding(*texture, gpuTexture);
    ASSERT_TRUE(registered.has_value()) << registered.error().message;
    EXPECT_FALSE(gpuTexture);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_NE(*registered, 0U);
    EXPECT_EQ(registry->capacity(), 2U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *registered);
    EXPECT_EQ(registry->resolveSprite(*spriteA), *registered);
    EXPECT_EQ(registry->resolveSprite(*spriteB), *registered);
    ASSERT_EQ(device.callCount(), 1U);
    EXPECT_EQ(device.call(0).bindingKey, *registered);
    EXPECT_EQ(device.call(0).texture, OriginalGpuTexture);

    Render::GpuTextureId duplicateGpu{8U, 4U};
    const Render::GpuTextureId expectedDuplicateGpu = duplicateGpu;
    auto duplicate = registry->registerTextureBinding(*texture, duplicateGpu);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, AssetErrorCode::SpriteBindingConflict);
    EXPECT_EQ(duplicateGpu, expectedDuplicateGpu);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_EQ(device.callCount(), 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, RegistrationRejectsGpuOwnerAlreadyHeldByAnotherEntry)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 2U));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    constexpr Render::GpuTextureId SharedGpuTexture{7U, 3U};
    Render::GpuTextureId firstGpuTexture = SharedGpuTexture;
    ASSERT_TRUE(registry->registerTextureBinding(*firstTexture, firstGpuTexture).has_value());
    EXPECT_FALSE(firstGpuTexture);

    Render::GpuTextureId duplicateGpuTexture = SharedGpuTexture;
    const auto duplicate = registry->registerTextureBinding(*secondTexture, duplicateGpuTexture);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, AssetErrorCode::SpriteBindingConflict);
    EXPECT_EQ(duplicateGpuTexture, SharedGpuTexture);
    EXPECT_EQ(assets->store().leaseCount(*firstTexture), 1U);
    EXPECT_EQ(assets->store().leaseCount(*secondTexture), 0U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, TilesetResolvesItsRequiredTextureBindingFailClosed)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto tileset = assets->store().publish(makeTileset(memory, 2U, textureId));
    auto unboundTileset = assets->store().publish(makeTileset(memory, 3U, assetId(9U)));
    auto queuedTileset = assets->store().beginQueued(assetId(4U), AssetFormat::AssetKind::Tileset);
    auto staleTileset = assets->store().publish(makeTileset(memory, 5U, textureId));
    auto noDependencyTileset = assets->store().publish(makeTileset(memory, 6U, textureId, {}));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(tileset.has_value());
    ASSERT_TRUE(unboundTileset.has_value());
    ASSERT_TRUE(queuedTileset.has_value());
    ASSERT_TRUE(staleTileset.has_value());
    ASSERT_TRUE(noDependencyTileset.has_value());
    ASSERT_TRUE(assets->store().unload(*staleTileset).has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{7U, 1U});
    ASSERT_TRUE(binding.has_value()) << binding.error().message;

    EXPECT_EQ(registry->resolveTileset(*tileset), *binding);
    EXPECT_EQ(registry->resolveTileset({}), 0U);
    EXPECT_EQ(registry->resolveTileset(*texture), 0U);
    EXPECT_EQ(registry->resolveTileset(*unboundTileset), 0U);
    EXPECT_EQ(registry->resolveTileset(*queuedTileset), 0U);
    EXPECT_EQ(registry->resolveTileset(*staleTileset), 0U);
    EXPECT_EQ(registry->resolveTileset(*noDependencyTileset), 0U);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, FrameResourcesDeduplicateAndBlockRetirementUntilPacketRelease)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    auto tileset = assets->store().publish(makeTileset(memory, 3U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());
    ASSERT_TRUE(tileset.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{7U, 1U});
    ASSERT_TRUE(binding.has_value()) << binding.error().message;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());

    auto firstSprite = registry->internSpriteFrameResource(*sprite, packet.resourceSink());
    auto duplicateSprite = registry->internSpriteFrameResource(*sprite, packet.resourceSink());
    auto sharedTileset = registry->internTilesetFrameResource(*tileset, packet.resourceSink());
    ASSERT_TRUE(firstSprite.has_value()) << firstSprite.error().message;
    ASSERT_TRUE(duplicateSprite.has_value()) << duplicateSprite.error().message;
    ASSERT_TRUE(sharedTileset.has_value()) << sharedTileset.error().message;
    EXPECT_TRUE(static_cast<bool>(*firstSprite));
    EXPECT_EQ(*duplicateSprite, *firstSprite);
    EXPECT_EQ(*sharedTileset, *firstSprite);
    EXPECT_EQ(packet.resourceCount(), 1U);
    const auto* descriptor = packet.resourceTableView().resolve(
        *firstSprite, Render::FrameResourceKind::Texture2D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->deviceBindingKey, *binding);

    const auto borrowedRetirement = registry->retireTextureBinding(*texture);
    ASSERT_FALSE(borrowedRetirement.has_value());
    EXPECT_EQ(borrowedRetirement.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(registry->bindingCount(), 1U);

    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireTextureBinding(*texture).has_value());
    EXPECT_EQ(registry->bindingCount(), 0U);
}

TEST(Sprite2DBindingRegistryTests, Texture2DResolverInternsBindingAndReportsCookedExtent)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    ASSERT_TRUE(texture.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{7U, 1U});
    ASSERT_TRUE(binding.has_value()) << binding.error().message;
    const Render::Texture2DFrameResourceResolver resolver = registry->texture2DFrameResourceResolver();
    ASSERT_TRUE(resolver.hasValue());

    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    auto resolved = resolver.resolve(resolver.userData, textureId, packet.resourceSink());
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_TRUE(resolved->has_value());
    EXPECT_EQ((**resolved).pixelWidth, 1U);
    EXPECT_EQ((**resolved).pixelHeight, 1U);
    EXPECT_TRUE((**resolved).resource.hasValue());
    const Render::FrameResourceDescriptor* descriptor = packet.resourceTableView().resolve(
        (**resolved).resource, Render::FrameResourceKind::Texture2D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->deviceBindingKey, *binding);

    auto missing = resolver.resolve(resolver.userData, assetId(99U), packet.resourceSink());
    ASSERT_TRUE(missing.has_value()) << missing.error().message;
    EXPECT_FALSE(missing->has_value());
    EXPECT_EQ(packet.resourceCount(), 1U);

    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireTextureBinding(*texture).has_value());
}

TEST(Sprite2DBindingRegistryTests, UnresolvedFrameResourcesReturnEmptyWithoutTouchingSink)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto unboundSprite = assets->store().publish(makeSprite(memory, 2U, assetId(9U)));
    auto queuedSprite = assets->store().beginQueued(assetId(3U), AssetFormat::AssetKind::Sprite);
    auto staleSprite = assets->store().publish(makeSprite(memory, 4U, textureId));
    auto noDependencySprite = assets->store().publish(makeSprite(memory, 5U, textureId, {}));
    auto noDependencyTileset = assets->store().publish(makeTileset(memory, 6U, textureId, {}));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(unboundSprite.has_value());
    ASSERT_TRUE(queuedSprite.has_value());
    ASSERT_TRUE(staleSprite.has_value());
    ASSERT_TRUE(noDependencySprite.has_value());
    ASSERT_TRUE(noDependencyTileset.has_value());
    ASSERT_TRUE(assets->store().unload(*staleSprite).has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    ASSERT_TRUE(registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U}).has_value());
    RejectingFrameResourceSink sink;

    const auto expectUnresolved = [&](auto resource) {
        ASSERT_TRUE(resource.has_value()) << resource.error().message;
        EXPECT_FALSE(static_cast<bool>(*resource));
    };
    expectUnresolved(registry->internSpriteFrameResource({}, sink));
    auto directTextureResource = registry->internSpriteFrameResource(*texture, sink);
    ASSERT_FALSE(directTextureResource.has_value());
    EXPECT_EQ(directTextureResource.error().code,
              Render::RenderErrorCode::InvalidFrameResource);
    expectUnresolved(registry->internSpriteFrameResource(*unboundSprite, sink));
    expectUnresolved(registry->internSpriteFrameResource(*queuedSprite, sink));
    expectUnresolved(registry->internSpriteFrameResource(*staleSprite, sink));
    expectUnresolved(registry->internSpriteFrameResource(*noDependencySprite, sink));
    expectUnresolved(registry->internTilesetFrameResource(*noDependencyTileset, sink));
    EXPECT_EQ(sink.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, SinkFailuresReleaseBorrowAndLeaveBindingRetirable)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    ASSERT_TRUE(registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U}).has_value());

    Render::RenderFramePacket idlePacket;
    const auto idleFailure = registry->internSpriteFrameResource(*sprite, idlePacket.resourceSink());
    ASSERT_FALSE(idleFailure.has_value());
    EXPECT_EQ(idleFailure.error().code, Render::RenderErrorCode::InvalidFrameResource);
    RejectingFrameResourceSink rejectingSink;
    const auto rejected = registry->internSpriteFrameResource(*sprite, rejectingSink);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(rejectingSink.callCount(), 1U);
    ASSERT_TRUE(registry->retireTextureBinding(*texture).has_value());
}

TEST(Sprite2DBindingRegistryTests, RegistrationRejectsInvalidStaleWrongKindAndNotReadyHandles)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value()) << assets.error().message;
    auto wrongKind = assets->store().publish(makeSprite(memory, 2U, assetId(1U)));
    auto stale = assets->store().publish(makeTexture(memory, 3U));
    auto notReady = assets->store().beginQueued(assetId(4U), AssetFormat::AssetKind::Texture2D);
    ASSERT_TRUE(wrongKind.has_value());
    ASSERT_TRUE(stale.has_value());
    ASSERT_TRUE(notReady.has_value());
    ASSERT_TRUE(assets->store().unload(*stale).has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    Render::GpuTextureId gpuTexture{1U, 1U};
    const Render::GpuTextureId expectedGpuTexture = gpuTexture;

    const auto empty = registry->registerTextureBinding({}, gpuTexture);
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_EQ(gpuTexture, expectedGpuTexture);
    const auto wrong = registry->registerTextureBinding(*wrongKind, gpuTexture);
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_EQ(gpuTexture, expectedGpuTexture);
    const auto staleResult = registry->registerTextureBinding(*stale, gpuTexture);
    ASSERT_FALSE(staleResult.has_value());
    EXPECT_EQ(staleResult.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_EQ(gpuTexture, expectedGpuTexture);
    const auto queued = registry->registerTextureBinding(*notReady, gpuTexture);
    ASSERT_FALSE(queued.has_value());
    EXPECT_EQ(queued.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(gpuTexture, expectedGpuTexture);

    auto ready = assets->store().publish(makeTexture(memory, 5U));
    ASSERT_TRUE(ready.has_value());
    Render::GpuTextureId invalidGpuCandidate{};
    const auto invalidGpu = registry->registerTextureBinding(*ready, invalidGpuCandidate);
    ASSERT_FALSE(invalidGpu.has_value());
    EXPECT_EQ(invalidGpu.error().code, Render::RenderErrorCode::InvalidTextureUpload);
    EXPECT_EQ(device.callCount(), 0U);
    EXPECT_EQ(registry->bindingCount(), 0U);
}

TEST(Sprite2DBindingRegistryTests, CrossStoreHandlesFailClosedEvenWhenSlotsCollide)
{
    TrackingMemoryResource localMemory;
    TrackingMemoryResource foreignMemory;
    auto localAssets = makeAssetSystem(localMemory, 4U);
    auto foreignAssets = makeAssetSystem(foreignMemory, 4U);
    ASSERT_TRUE(localAssets.has_value());
    ASSERT_TRUE(foreignAssets.has_value());

    const Core::AssetId localTextureId = assetId(1U);
    auto localTexture = localAssets->store().publish(makeTexture(localMemory, 1U));
    auto localSprite = localAssets->store().publish(makeSprite(localMemory, 2U, localTextureId));
    auto foreignTexture = foreignAssets->store().publish(makeTexture(foreignMemory, 7U));
    auto foreignSprite = foreignAssets->store().publish(makeSprite(foreignMemory, 8U, assetId(7U)));
    ASSERT_TRUE(localTexture.has_value());
    ASSERT_TRUE(localSprite.has_value());
    ASSERT_TRUE(foreignTexture.has_value());
    ASSERT_TRUE(foreignSprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*localAssets, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    auto localBinding = registerTexture(*registry, *localTexture, Render::GpuTextureId{3U, 1U});
    ASSERT_TRUE(localBinding.has_value()) << localBinding.error().message;
    ASSERT_EQ(registry->resolveSprite(*localSprite), *localBinding);

    Render::GpuTextureId foreignGpu{4U, 1U};
    const auto foreignRegistration = registry->registerTextureBinding(*foreignTexture, foreignGpu);
    ASSERT_FALSE(foreignRegistration.has_value());
    EXPECT_EQ(foreignRegistration.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_EQ(foreignGpu, (Render::GpuTextureId{4U, 1U}));
    EXPECT_EQ(foreignAssets->store().leaseCount(*foreignTexture), 0U);
    EXPECT_EQ(registry->bindingKey(*foreignTexture), 0U);
    EXPECT_EQ(registry->resolveSprite(*foreignSprite), 0U);
    EXPECT_EQ(device.callCount(), 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, BackendRegisterFailureRollsBackRecordAndBindingKey)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 2U));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    device.rejectNextUpdate();

    Render::GpuTextureId rejectedGpu{1U, 1U};
    const auto rejected = registry->registerTextureBinding(*firstTexture, rejectedGpu);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(registry->bindingKey(*firstTexture), 0U);
    EXPECT_EQ(rejectedGpu, (Render::GpuTextureId{1U, 1U}));
    EXPECT_EQ(assets->store().leaseCount(*firstTexture), 0U);

    Render::GpuTextureId acceptedGpu{2U, 1U};
    auto accepted = registry->registerTextureBinding(*secondTexture, acceptedGpu);
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_EQ(*accepted, 1U);
    EXPECT_FALSE(acceptedGpu);
    EXPECT_EQ(assets->store().leaseCount(*secondTexture), 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(device.callCount(), 2U);
    EXPECT_EQ(device.call(0).bindingKey, 1U);
    EXPECT_EQ(device.call(1).bindingKey, 1U);
}

TEST(Sprite2DBindingRegistryTests, ConcurrentLiveTexturesReceiveDistinctNonzeroKeys)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 2U));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto firstKey = registerTexture(*registry, *firstTexture, Render::GpuTextureId{1U, 1U});
    auto secondKey = registerTexture(*registry, *secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(firstKey.has_value());
    ASSERT_TRUE(secondKey.has_value());

    EXPECT_NE(*firstKey, 0U);
    EXPECT_NE(*secondKey, 0U);
    EXPECT_NE(*firstKey, *secondKey);
    EXPECT_EQ(registry->bindingKey(*firstTexture), *firstKey);
    EXPECT_EQ(registry->bindingKey(*secondTexture), *secondKey);
    EXPECT_EQ(registry->bindingCount(), 2U);
    EXPECT_EQ(device.callCount(), 2U);
}

TEST(Sprite2DBindingRegistryTests, RegistriesSharingDeviceReceiveDistinctKeysAndRetireIndependently)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId firstTextureId = assetId(1U);
    const Core::AssetId secondTextureId = assetId(2U);
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 2U));
    auto firstSprite = assets->store().publish(makeSprite(memory, 3U, firstTextureId));
    auto secondSprite = assets->store().publish(makeSprite(memory, 4U, secondTextureId));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());
    ASSERT_TRUE(firstSprite.has_value());
    ASSERT_TRUE(secondSprite.has_value());

    FixedBindingRenderDevice device;
    auto firstRegistry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    auto secondRegistry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(firstRegistry.has_value());
    ASSERT_TRUE(secondRegistry.has_value());

    auto firstKey = registerTexture(*firstRegistry, *firstTexture, Render::GpuTextureId{1U, 1U});
    auto secondKey = registerTexture(*secondRegistry, *secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(firstKey.has_value());
    ASSERT_TRUE(secondKey.has_value());
    EXPECT_NE(*firstKey, *secondKey);
    EXPECT_EQ(firstRegistry->resolveSprite(*firstSprite), *firstKey);
    EXPECT_EQ(secondRegistry->resolveSprite(*secondSprite), *secondKey);

    ASSERT_TRUE(firstRegistry->retireTextureBinding(*firstTexture).has_value());
    EXPECT_EQ(firstRegistry->resolveSprite(*firstSprite), 0U);
    EXPECT_EQ(secondRegistry->resolveSprite(*secondSprite), *secondKey);
    ASSERT_TRUE(secondRegistry->retireTextureBinding(*secondTexture).has_value());
    EXPECT_EQ(device.callCount(), 2U);
    EXPECT_EQ(device.retirementCount(), 2U);
}

TEST(Sprite2DBindingRegistryTests, ConflictsAndCapacityFailurePreserveExistingBinding)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sameIdOtherHandle = assets->store().publish(makeTexture(memory, 1U));
    auto overflowTexture = assets->store().publish(makeTexture(memory, 2U));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sameIdOtherHandle.has_value());
    ASSERT_TRUE(overflowTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    Render::GpuTextureId firstGpu{1U, 1U};
    auto first = registry->registerTextureBinding(*texture, firstGpu);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(firstGpu);

    Render::GpuTextureId changedGpuCandidate{2U, 1U};
    const auto changedGpu = registry->registerTextureBinding(*texture, changedGpuCandidate);
    ASSERT_FALSE(changedGpu.has_value());
    EXPECT_EQ(changedGpu.error().code, AssetErrorCode::SpriteBindingConflict);
    EXPECT_EQ(changedGpuCandidate, (Render::GpuTextureId{2U, 1U}));
    Render::GpuTextureId duplicateAssetIdCandidate{3U, 1U};
    const auto duplicateAssetId =
        registry->registerTextureBinding(*sameIdOtherHandle, duplicateAssetIdCandidate);
    ASSERT_FALSE(duplicateAssetId.has_value());
    EXPECT_EQ(duplicateAssetId.error().code, AssetErrorCode::SpriteBindingConflict);
    EXPECT_EQ(duplicateAssetIdCandidate, (Render::GpuTextureId{3U, 1U}));
    Render::GpuTextureId capacityCandidate{4U, 1U};
    const auto capacity = registry->registerTextureBinding(*overflowTexture, capacityCandidate);
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, AssetErrorCode::SpriteBindingCapacityExceeded);
    EXPECT_EQ(capacityCandidate, (Render::GpuTextureId{4U, 1U}));
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_EQ(assets->store().leaseCount(*sameIdOtherHandle), 0U);
    EXPECT_EQ(assets->store().leaseCount(*overflowTexture), 0U);

    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *first);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, RetirementFailureIsRetryableAndPreservesOwnersAndResolution)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{5U, 2U});
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    device.rejectNextRetirement();

    const auto rejected = registry->retireTextureBinding(*texture);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *binding);
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_EQ(assets->retirement().records().size(), 0U);

    const auto retried = registry->retireTextureBinding(*texture);
    ASSERT_TRUE(retried.has_value()) << retried.error().message;
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(registry->bindingKey(*texture), 0U);
    EXPECT_EQ(registry->resolveSprite(*sprite), 0U);
    EXPECT_EQ(device.callCount(), 1U);
    EXPECT_EQ(device.retirementAttempts(), 2U);
    EXPECT_EQ(device.retirementCount(), 1U);
    EXPECT_EQ(assets->store().state(*texture), AssetLogicalState::Unloaded);
    ASSERT_EQ(assets->retirement().records().size(), 1U);
    EXPECT_EQ(assets->retirement().records().front().state, AssetRetirementState::Released);

    const auto missing = registry->retireTextureBinding(*texture);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, AssetErrorCode::SpriteBindingNotFound);
    EXPECT_EQ(device.retirementAttempts(), 2U);
}

TEST(Sprite2DBindingRegistryTests, DelayedCompletionOutlivesRegistryAndReleasesLeaseExactlyOnce)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    device.delayRetirement();
    {
        auto registry = makeRegistry(*assets, device);
        ASSERT_TRUE(registry.has_value());
        Render::GpuTextureId gpuTexture{9U, 3U};
        auto binding = registry->registerTextureBinding(*texture, gpuTexture);
        ASSERT_TRUE(binding.has_value());
        EXPECT_FALSE(gpuTexture);
        EXPECT_EQ(assets->store().leaseCount(*texture), 1U);

        ASSERT_TRUE(registry->retireTextureBinding(*texture).has_value());
        EXPECT_EQ(registry->bindingCount(), 0U);
        EXPECT_EQ(registry->resolveSprite(*sprite), 0U);
        EXPECT_TRUE(device.hasPendingRetirement());
        EXPECT_EQ(assets->store().state(*texture), AssetLogicalState::UnloadPending);
        EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
        ASSERT_EQ(assets->retirement().records().size(), 1U);
        EXPECT_EQ(assets->retirement().records().front().state, AssetRetirementState::Retiring);
    }

    EXPECT_TRUE(device.hasPendingRetirement());
    device.completeRetirement();
    EXPECT_FALSE(device.hasPendingRetirement());
    EXPECT_EQ(assets->store().state(*texture), AssetLogicalState::Unloaded);
    EXPECT_EQ(assets->store().leaseCount(*texture), 0U);
    ASSERT_EQ(assets->retirement().records().size(), 1U);
    EXPECT_EQ(assets->retirement().records().front().state, AssetRetirementState::Released);
}

TEST(Sprite2DBindingRegistryTests, RetirementPayloadAllocationFailurePreservesEntryForRetry)
{
    ThrowingMemoryResource memory{(std::numeric_limits<std::size_t>::max)()};
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{4U, 2U});
    ASSERT_TRUE(binding.has_value());
    memory.rejectAllocationsAtOrAbove(1U);

    const auto rejected = registry->retireTextureBinding(*texture);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *binding);
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    EXPECT_EQ(assets->store().leaseCount(*texture), 1U);
    EXPECT_TRUE(assets->retirement().records().empty());
    EXPECT_EQ(device.retirementAttempts(), 0U);

    memory.allowAllocations();
    ASSERT_TRUE(registry->retireTextureBinding(*texture).has_value());
    EXPECT_EQ(registry->bindingCount(), 0U);
}

TEST(Sprite2DBindingRegistryTests, RetireAllAllowsCommittedPrefixAndRetriesRemainingEntry)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 2U));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto firstKey = registerTexture(*registry, *firstTexture, Render::GpuTextureId{1U, 1U});
    auto secondKey = registerTexture(*registry, *secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(firstKey.has_value());
    ASSERT_TRUE(secondKey.has_value());
    device.rejectRetirementOnAttempt(2U);

    const auto partial = registry->retireAllTextureBindings();
    ASSERT_FALSE(partial.has_value());
    EXPECT_EQ(partial.error().code, Render::RenderErrorCode::GpuRetirementUnsupported);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*firstTexture), 0U);
    EXPECT_EQ(registry->bindingKey(*secondTexture), *secondKey);
    EXPECT_EQ(assets->store().leaseCount(*firstTexture), 0U);
    EXPECT_EQ(assets->store().leaseCount(*secondTexture), 1U);
    ASSERT_EQ(assets->retirement().records().size(), 1U);
    EXPECT_EQ(assets->retirement().records().front().state, AssetRetirementState::Released);

    ASSERT_TRUE(registry->retireAllTextureBindings().has_value());
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(device.retirementAttempts(), 3U);
    EXPECT_EQ(device.retirementCount(), 2U);
    ASSERT_EQ(assets->retirement().records().size(), 2U);
}

TEST(Sprite2DBindingRegistryTests, RetireAllPreflightsActiveFrameBorrowBeforeAnyCommit)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId secondTextureId = assetId(2U);
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto secondTexture = assets->store().publish(makeTexture(memory, 2U));
    auto secondSprite = assets->store().publish(makeSprite(memory, 3U, secondTextureId));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());
    ASSERT_TRUE(secondSprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    ASSERT_TRUE(registerTexture(*registry, *firstTexture, Render::GpuTextureId{1U, 1U}).has_value());
    ASSERT_TRUE(registerTexture(*registry, *secondTexture, Render::GpuTextureId{2U, 1U}).has_value());
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1U).has_value());
    ASSERT_TRUE(registry->internSpriteFrameResource(*secondSprite, packet.resourceSink()).has_value());

    const auto blocked = registry->retireAllTextureBindings();
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code, AssetErrorCode::AssetNotReady);
    EXPECT_EQ(registry->bindingCount(), 2U);
    EXPECT_EQ(device.retirementAttempts(), 0U);

    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(registry->retireAllTextureBindings().has_value());
    EXPECT_EQ(device.retirementCount(), 2U);
}

TEST(Sprite2DBindingRegistryTests, DestructionWithOwnedBindingFailsFast)
{
    EXPECT_DEATH(destroyRegistryWithOwnedBinding(), "");
}

TEST(Sprite2DBindingRegistryTests, UnloadPendingBindingCanBeRetiredAndReleasedKeyIsNeverReused)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory, 3U);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId firstTextureId = assetId(1U);
    auto firstTexture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, firstTextureId));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto firstKey = registerTexture(*registry, *firstTexture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(firstKey.has_value());
    ASSERT_TRUE(assets->store().unload(*firstTexture).has_value());
    EXPECT_EQ(registry->bindingKey(*firstTexture), 0U);
    EXPECT_EQ(registry->resolveSprite(*sprite), 0U);

    ASSERT_TRUE(registry->retireTextureBinding(*firstTexture).has_value());
    auto secondTexture = assets->store().publish(makeTexture(memory, 3U));
    ASSERT_TRUE(secondTexture.has_value());
    auto secondKey = registerTexture(*registry, *secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(secondKey.has_value());
    EXPECT_GT(*secondKey, *firstKey);
    EXPECT_EQ(*secondKey, *firstKey + 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, ResolveSpriteFailsClosedForMalformedUnboundAndNotReadyInputs)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory, 16U);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto unboundSprite = assets->store().publish(makeSprite(memory, 2U, assetId(9U)));
    auto queuedSprite = assets->store().beginQueued(assetId(3U), AssetFormat::AssetKind::Sprite);
    auto staleSprite = assets->store().publish(makeSprite(memory, 4U, textureId));
    auto noDependencySprite = assets->store().publish(makeSprite(memory, 5U, textureId, {}));
    const std::array wrongKindDependency{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Material,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    auto wrongDependencySprite = assets->store().publish(makeSprite(memory, 6U, textureId, wrongKindDependency));
    const std::array deferredRequiredDependency{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required | AssetFormat::DependencyFlags::Deferred,
        },
    };
    auto deferredRequiredDependencySprite =
        assets->store().publish(makeSprite(memory, 10U, textureId, deferredRequiredDependency));
    const std::array multipleDependencies{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
        AssetFormat::CookedAssetWriteDependency{
            .assetId = assetId(8U),
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    auto multipleDependencySprite = assets->store().publish(makeSprite(memory, 7U, textureId, multipleDependencies));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(unboundSprite.has_value());
    ASSERT_TRUE(queuedSprite.has_value());
    ASSERT_TRUE(staleSprite.has_value());
    ASSERT_TRUE(noDependencySprite.has_value());
    ASSERT_TRUE(wrongDependencySprite.has_value());
    ASSERT_TRUE(deferredRequiredDependencySprite.has_value());
    ASSERT_TRUE(multipleDependencySprite.has_value());
    ASSERT_TRUE(assets->store().unload(*staleSprite).has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto textureBinding = registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(textureBinding.has_value());

    EXPECT_EQ(registry->resolveSprite({}), 0U);
    EXPECT_EQ(registry->resolveSprite(*texture), *textureBinding);
    EXPECT_EQ(registry->resolveSprite(*unboundSprite), 0U);
    EXPECT_EQ(registry->resolveSprite(*queuedSprite), 0U);
    EXPECT_EQ(registry->resolveSprite(*staleSprite), 0U);
    EXPECT_EQ(registry->resolveSprite(*noDependencySprite), 0U);
    EXPECT_EQ(registry->resolveSprite(*wrongDependencySprite), 0U);
    EXPECT_EQ(registry->resolveSprite(*deferredRequiredDependencySprite), 0U);
    EXPECT_EQ(registry->resolveSprite(*multipleDependencySprite), 0U);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, UploadQueuedAndReadyGpuPayloadsRemainResolvable)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);

    ASSERT_TRUE(assets->store().beginUpload(*texture).has_value());
    ASSERT_TRUE(assets->store().beginUpload(*sprite).has_value());
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    ASSERT_TRUE(assets->store().completeGpu(*texture).has_value());
    ASSERT_TRUE(assets->store().completeGpu(*sprite).has_value());
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
}

TEST(Sprite2DBindingRegistryTests, WrongOwnerThreadOperationsFailWithoutMutation)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());

    std::optional<Core::ErrorCode> registerError;
    std::optional<Core::ErrorCode> retirementError;
    std::optional<Core::ErrorCode> internError;
    std::optional<Core::ErrorCode> resolverError;
    Render::GpuTextureId foreignGpu{2U, 1U};
    Core::u32 foreignBindingKey = *binding;
    Core::u32 foreignSpriteKey = *binding;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    std::thread foreignThread([&] {
        const auto registration = registry->registerTextureBinding(*texture, foreignGpu);
        if (!registration)
        {
            registerError = registration.error().code;
        }
        const auto retirement = registry->retireTextureBinding(*texture);
        if (!retirement)
        {
            retirementError = retirement.error().code;
        }
        const auto intern = registry->internSpriteFrameResource(*sprite, packet.resourceSink());
        if (!intern)
        {
            internError = intern.error().code;
        }
        const auto resolver = registry->texture2DFrameResourceResolver();
        const auto resolved = resolver.resolve(resolver.userData, textureId, packet.resourceSink());
        if (!resolved)
        {
            resolverError = resolved.error().code;
        }
        foreignBindingKey = registry->bindingKey(*texture);
        foreignSpriteKey = registry->resolveSprite(*sprite);
    });
    foreignThread.join();

    ASSERT_TRUE(registerError.has_value());
    EXPECT_EQ(*registerError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(retirementError.has_value());
    EXPECT_EQ(*retirementError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(internError.has_value());
    EXPECT_EQ(*internError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(resolverError.has_value());
    EXPECT_EQ(*resolverError, Render::RenderErrorCode::WrongOwnerThread);
    EXPECT_EQ(packet.resourceCount(), 0U);
    EXPECT_EQ(foreignBindingKey, 0U);
    EXPECT_EQ(foreignSpriteKey, 0U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *binding);
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    EXPECT_EQ(device.callCount(), 1U);
    EXPECT_EQ(device.retirementAttempts(), 0U);
    EXPECT_EQ(foreignGpu, (Render::GpuTextureId{2U, 1U}));
}

TEST(Sprite2DBindingRegistryTests, MoveTransfersOwnershipAndInvalidatesSourceRegistry)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());
    FixedBindingRenderDevice device;
    auto registry = makeRegistry(*assets, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());

    Sprite2DBindingRegistry moved = std::move(*registry);

    EXPECT_FALSE(static_cast<bool>(*registry));
    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(registry->capacity(), 0U);
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(registry->bindingKey(*texture), 0U);
    EXPECT_EQ(registry->resolveSprite(*sprite), 0U);
    const auto movedFromRegister = registerTexture(*registry, *texture, Render::GpuTextureId{2U, 1U});
    ASSERT_FALSE(movedFromRegister.has_value());
    EXPECT_EQ(movedFromRegister.error().code, Render::RenderErrorCode::WrongOwnerThread);

    EXPECT_EQ(moved.bindingKey(*texture), *binding);
    EXPECT_EQ(moved.resolveSprite(*sprite), *binding);
    EXPECT_EQ(moved.bindingCount(), 1U);
    EXPECT_EQ(device.callCount(), 1U);
    ASSERT_TRUE(moved.retireAllTextureBindings().has_value());
}

TEST(Sprite2DBindingRegistryTests, SteadyStateResolutionPerformsNoPmrAllocations)
{
    TrackingMemoryResource memory;
    auto assets = makeAssetSystem(memory);
    ASSERT_TRUE(assets.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = assets->store().publish(makeTexture(memory, 1U));
    auto sprite = assets->store().publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());
    FixedBindingRenderDevice device;
    auto registry = makeRegistry(
        *assets, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto binding = registerTexture(*registry, *texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());
    const Core::usize allocationBaseline = memory.allocationCalls();
    for (Core::usize iteration = 0; iteration < 300U; ++iteration)
    {
        EXPECT_EQ(registry->bindingKey(*texture), *binding);
        EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    }

    EXPECT_EQ(memory.allocationCalls(), allocationBaseline);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(device.callCount(), 1U);
    ASSERT_TRUE(registry->retireTextureBinding(*texture).has_value());
}

} // namespace
} // namespace Tina::Asset
