#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset_format/SpritePayload.hpp>
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

    [[nodiscard]] Core::Status setSprite2DTextureBinding(Core::u32 bindingKey,
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

    void rejectNextUpdate() noexcept
    {
        m_rejectNext = true;
    }

    [[nodiscard]] Core::usize callCount() const noexcept
    {
        return m_callCount;
    }

    [[nodiscard]] const Call& call(Core::usize index) const noexcept
    {
        return m_calls[index];
    }

  private:
    std::array<Call, 1024> m_calls{};
    Core::usize m_callCount = 0;
    bool m_rejectNext = false;
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

[[nodiscard]] Core::Result<AssetStore> makeStore(std::pmr::memory_resource& memory, Core::usize capacity = 16U)
{
    return AssetStore::Create(AssetStoreConfig{.capacity = capacity, .memoryResource = &memory});
}

TEST(Sprite2DBindingRegistryTests, CreateValidatesCapacityBounds)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value()) << store.error().message;
    FixedBindingRenderDevice device;

    auto zero = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 0, .memoryResource = &memory});
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto excessive = Sprite2DBindingRegistry::Create(*store, device,
                                                     Sprite2DBindingRegistryConfig{
                                                         .textureCapacity = MaximumSprite2DBindingCapacity + 1U,
                                                         .memoryResource = &memory,
                                                     });
    ASSERT_FALSE(excessive.has_value());
    EXPECT_EQ(excessive.error().code, AssetErrorCode::InvalidCatalogConfig);

    // MSVC Debug allocates a small iterator proxy from the PMR inside vector's
    // noexcept allocator constructor. Reject only the actual fixed entry storage.
    ThrowingMemoryResource throwingMemory{64U};
    auto allocationFailure = Sprite2DBindingRegistry::Create(
        *store, device,
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

TEST(Sprite2DBindingRegistryTests, SharedAtlasResolvesToOneIdempotentTextureBinding)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value()) << store.error().message;
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto spriteA = store->publish(makeSprite(memory, 2U, textureId));
    auto spriteB = store->publish(makeSprite(memory, 3U, textureId));
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    ASSERT_TRUE(spriteA.has_value()) << spriteA.error().message;
    ASSERT_TRUE(spriteB.has_value()) << spriteB.error().message;

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    constexpr Render::GpuTextureId GpuTexture{7U, 3U};

    auto registered = registry->registerTextureBinding(*texture, GpuTexture);
    ASSERT_TRUE(registered.has_value()) << registered.error().message;
    EXPECT_NE(*registered, 0U);
    EXPECT_EQ(registry->capacity(), 2U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *registered);
    EXPECT_EQ(registry->resolveSprite(*spriteA), *registered);
    EXPECT_EQ(registry->resolveSprite(*spriteB), *registered);
    ASSERT_EQ(device.callCount(), 1U);
    EXPECT_EQ(device.call(0).bindingKey, *registered);
    EXPECT_EQ(device.call(0).texture, GpuTexture);

    auto duplicate = registry->registerTextureBinding(*texture, GpuTexture);
    ASSERT_TRUE(duplicate.has_value()) << duplicate.error().message;
    EXPECT_EQ(*duplicate, *registered);
    EXPECT_EQ(device.callCount(), 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, RegistrationRejectsInvalidStaleWrongKindAndNotReadyHandles)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value()) << store.error().message;
    auto wrongKind = store->publish(makeSprite(memory, 2U, assetId(1U)));
    auto stale = store->publish(makeTexture(memory, 3U));
    auto notReady = store->beginQueued(assetId(4U), AssetFormat::AssetKind::Texture2D);
    ASSERT_TRUE(wrongKind.has_value());
    ASSERT_TRUE(stale.has_value());
    ASSERT_TRUE(notReady.has_value());
    ASSERT_TRUE(store->unload(*stale).has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    constexpr Render::GpuTextureId GpuTexture{1U, 1U};

    const auto empty = registry->registerTextureBinding({}, GpuTexture);
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, AssetErrorCode::InvalidHandle);
    const auto wrong = registry->registerTextureBinding(*wrongKind, GpuTexture);
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error().code, AssetErrorCode::InvalidHandle);
    const auto staleResult = registry->registerTextureBinding(*stale, GpuTexture);
    ASSERT_FALSE(staleResult.has_value());
    EXPECT_EQ(staleResult.error().code, AssetErrorCode::InvalidHandle);
    const auto queued = registry->registerTextureBinding(*notReady, GpuTexture);
    ASSERT_FALSE(queued.has_value());
    EXPECT_EQ(queued.error().code, AssetErrorCode::AssetNotReady);

    auto ready = store->publish(makeTexture(memory, 5U));
    ASSERT_TRUE(ready.has_value());
    const auto invalidGpu = registry->registerTextureBinding(*ready, {});
    ASSERT_FALSE(invalidGpu.has_value());
    EXPECT_EQ(invalidGpu.error().code, Render::RenderErrorCode::InvalidTextureUpload);
    EXPECT_EQ(device.callCount(), 0U);
    EXPECT_EQ(registry->bindingCount(), 0U);
}

TEST(Sprite2DBindingRegistryTests, CrossStoreHandlesFailClosedEvenWhenSlotsCollide)
{
    TrackingMemoryResource localMemory;
    TrackingMemoryResource foreignMemory;
    auto localStore = makeStore(localMemory, 4U);
    auto foreignStore = makeStore(foreignMemory, 4U);
    ASSERT_TRUE(localStore.has_value());
    ASSERT_TRUE(foreignStore.has_value());

    const Core::AssetId localTextureId = assetId(1U);
    auto localTexture = localStore->publish(makeTexture(localMemory, 1U));
    auto localSprite = localStore->publish(makeSprite(localMemory, 2U, localTextureId));
    auto foreignTexture = foreignStore->publish(makeTexture(foreignMemory, 7U));
    auto foreignSprite = foreignStore->publish(makeSprite(foreignMemory, 8U, assetId(7U)));
    ASSERT_TRUE(localTexture.has_value());
    ASSERT_TRUE(localSprite.has_value());
    ASSERT_TRUE(foreignTexture.has_value());
    ASSERT_TRUE(foreignSprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*localStore, device);
    ASSERT_TRUE(registry.has_value()) << registry.error().message;
    auto localBinding = registry->registerTextureBinding(*localTexture, Render::GpuTextureId{3U, 1U});
    ASSERT_TRUE(localBinding.has_value()) << localBinding.error().message;
    ASSERT_EQ(registry->resolveSprite(*localSprite), *localBinding);

    const auto foreignRegistration = registry->registerTextureBinding(*foreignTexture, Render::GpuTextureId{4U, 1U});
    ASSERT_FALSE(foreignRegistration.has_value());
    EXPECT_EQ(foreignRegistration.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_EQ(registry->bindingKey(*foreignTexture), 0U);
    EXPECT_EQ(registry->resolveSprite(*foreignSprite), 0U);
    EXPECT_EQ(device.callCount(), 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, BackendRegisterFailureRollsBackRecordAndBindingKey)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto firstTexture = store->publish(makeTexture(memory, 1U));
    auto secondTexture = store->publish(makeTexture(memory, 2U));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    device.rejectNextUpdate();

    const auto rejected = registry->registerTextureBinding(*firstTexture, Render::GpuTextureId{1U, 1U});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(registry->bindingKey(*firstTexture), 0U);

    auto accepted = registry->registerTextureBinding(*secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_EQ(*accepted, 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(device.callCount(), 2U);
    EXPECT_EQ(device.call(0).bindingKey, 1U);
    EXPECT_EQ(device.call(1).bindingKey, 1U);
}

TEST(Sprite2DBindingRegistryTests, ConcurrentLiveTexturesReceiveDistinctNonzeroKeys)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto firstTexture = store->publish(makeTexture(memory, 1U));
    auto secondTexture = store->publish(makeTexture(memory, 2U));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto firstKey = registry->registerTextureBinding(*firstTexture, Render::GpuTextureId{1U, 1U});
    auto secondKey = registry->registerTextureBinding(*secondTexture, Render::GpuTextureId{2U, 1U});
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

TEST(Sprite2DBindingRegistryTests, RegistriesSharingDeviceReceiveDistinctKeysAndUnbindIndependently)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId firstTextureId = assetId(1U);
    const Core::AssetId secondTextureId = assetId(2U);
    auto firstTexture = store->publish(makeTexture(memory, 1U));
    auto secondTexture = store->publish(makeTexture(memory, 2U));
    auto firstSprite = store->publish(makeSprite(memory, 3U, firstTextureId));
    auto secondSprite = store->publish(makeSprite(memory, 4U, secondTextureId));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(secondTexture.has_value());
    ASSERT_TRUE(firstSprite.has_value());
    ASSERT_TRUE(secondSprite.has_value());

    FixedBindingRenderDevice device;
    auto firstRegistry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    auto secondRegistry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(firstRegistry.has_value());
    ASSERT_TRUE(secondRegistry.has_value());

    auto firstKey = firstRegistry->registerTextureBinding(*firstTexture, Render::GpuTextureId{1U, 1U});
    auto secondKey = secondRegistry->registerTextureBinding(*secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(firstKey.has_value());
    ASSERT_TRUE(secondKey.has_value());
    EXPECT_NE(*firstKey, *secondKey);
    EXPECT_EQ(firstRegistry->resolveSprite(*firstSprite), *firstKey);
    EXPECT_EQ(secondRegistry->resolveSprite(*secondSprite), *secondKey);

    ASSERT_TRUE(firstRegistry->unbindTextureBinding(*firstTexture).has_value());
    EXPECT_EQ(firstRegistry->resolveSprite(*firstSprite), 0U);
    EXPECT_EQ(secondRegistry->resolveSprite(*secondSprite), *secondKey);
    ASSERT_TRUE(secondRegistry->unbindTextureBinding(*secondTexture).has_value());
    ASSERT_EQ(device.callCount(), 4U);
    EXPECT_EQ(device.call(2), (FixedBindingRenderDevice::Call{.bindingKey = *firstKey, .texture = {}}));
    EXPECT_EQ(device.call(3), (FixedBindingRenderDevice::Call{.bindingKey = *secondKey, .texture = {}}));
}

TEST(Sprite2DBindingRegistryTests, ConflictsAndCapacityFailurePreserveExistingBinding)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    auto texture = store->publish(makeTexture(memory, 1U));
    auto sameIdOtherHandle = store->publish(makeTexture(memory, 1U));
    auto overflowTexture = store->publish(makeTexture(memory, 2U));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sameIdOtherHandle.has_value());
    ASSERT_TRUE(overflowTexture.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    constexpr Render::GpuTextureId FirstGpu{1U, 1U};
    auto first = registry->registerTextureBinding(*texture, FirstGpu);
    ASSERT_TRUE(first.has_value());

    const auto changedGpu = registry->registerTextureBinding(*texture, Render::GpuTextureId{2U, 1U});
    ASSERT_FALSE(changedGpu.has_value());
    EXPECT_EQ(changedGpu.error().code, AssetErrorCode::SpriteBindingConflict);
    const auto duplicateAssetId = registry->registerTextureBinding(*sameIdOtherHandle, Render::GpuTextureId{3U, 1U});
    ASSERT_FALSE(duplicateAssetId.has_value());
    EXPECT_EQ(duplicateAssetId.error().code, AssetErrorCode::SpriteBindingConflict);
    const auto capacity = registry->registerTextureBinding(*overflowTexture, Render::GpuTextureId{4U, 1U});
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, AssetErrorCode::SpriteBindingCapacityExceeded);

    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *first);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, UnbindFailureIsRetryableAndPreservesResolution)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto sprite = store->publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registry->registerTextureBinding(*texture, Render::GpuTextureId{5U, 2U});
    ASSERT_TRUE(binding.has_value());
    device.rejectNextUpdate();

    const auto rejected = registry->unbindTextureBinding(*texture);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::TextureUploadUnsupported);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *binding);
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);

    const auto retried = registry->unbindTextureBinding(*texture);
    ASSERT_TRUE(retried.has_value()) << retried.error().message;
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(registry->bindingKey(*texture), 0U);
    EXPECT_EQ(registry->resolveSprite(*sprite), 0U);
    ASSERT_EQ(device.callCount(), 3U);
    const FixedBindingRenderDevice::Call clearCall{.bindingKey = *binding, .texture = {}};
    EXPECT_EQ(device.call(1), clearCall);
    EXPECT_EQ(device.call(2), clearCall);

    const auto missing = registry->unbindTextureBinding(*texture);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, AssetErrorCode::SpriteBindingNotFound);
    EXPECT_EQ(device.callCount(), 3U);
}

TEST(Sprite2DBindingRegistryTests, StaleBindingCanBeUnboundAndReleasedKeyIsNeverReused)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory, 3U);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId firstTextureId = assetId(1U);
    auto firstTexture = store->publish(makeTexture(memory, 1U));
    auto sprite = store->publish(makeSprite(memory, 2U, firstTextureId));
    ASSERT_TRUE(firstTexture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    auto firstKey = registry->registerTextureBinding(*firstTexture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(firstKey.has_value());
    ASSERT_TRUE(store->unload(*firstTexture).has_value());
    EXPECT_EQ(registry->bindingKey(*firstTexture), 0U);
    EXPECT_EQ(registry->resolveSprite(*sprite), 0U);

    ASSERT_TRUE(registry->unbindTextureBinding(*firstTexture).has_value());
    auto secondTexture = store->publish(makeTexture(memory, 3U));
    ASSERT_TRUE(secondTexture.has_value());
    auto secondKey = registry->registerTextureBinding(*secondTexture, Render::GpuTextureId{2U, 1U});
    ASSERT_TRUE(secondKey.has_value());
    EXPECT_GT(*secondKey, *firstKey);
    EXPECT_EQ(*secondKey, *firstKey + 1U);
    EXPECT_EQ(registry->bindingCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, ResolveSpriteFailsClosedForMalformedUnboundAndNotReadyInputs)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory, 16U);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto unboundSprite = store->publish(makeSprite(memory, 2U, assetId(9U)));
    auto queuedSprite = store->beginQueued(assetId(3U), AssetFormat::AssetKind::Sprite);
    auto staleSprite = store->publish(makeSprite(memory, 4U, textureId));
    auto noDependencySprite = store->publish(makeSprite(memory, 5U, textureId, {}));
    const std::array wrongKindDependency{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Material,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    auto wrongDependencySprite = store->publish(makeSprite(memory, 6U, textureId, wrongKindDependency));
    const std::array deferredRequiredDependency{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required | AssetFormat::DependencyFlags::Deferred,
        },
    };
    auto deferredRequiredDependencySprite =
        store->publish(makeSprite(memory, 10U, textureId, deferredRequiredDependency));
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
    auto multipleDependencySprite = store->publish(makeSprite(memory, 7U, textureId, multipleDependencies));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(unboundSprite.has_value());
    ASSERT_TRUE(queuedSprite.has_value());
    ASSERT_TRUE(staleSprite.has_value());
    ASSERT_TRUE(noDependencySprite.has_value());
    ASSERT_TRUE(wrongDependencySprite.has_value());
    ASSERT_TRUE(deferredRequiredDependencySprite.has_value());
    ASSERT_TRUE(multipleDependencySprite.has_value());
    ASSERT_TRUE(store->unload(*staleSprite).has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto textureBinding = registry->registerTextureBinding(*texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(textureBinding.has_value());

    EXPECT_EQ(registry->resolveSprite({}), 0U);
    EXPECT_EQ(registry->resolveSprite(*texture), 0U);
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
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto sprite = store->publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registry->registerTextureBinding(*texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);

    ASSERT_TRUE(store->beginUpload(*texture).has_value());
    ASSERT_TRUE(store->beginUpload(*sprite).has_value());
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    ASSERT_TRUE(store->completeGpu(*texture).has_value());
    ASSERT_TRUE(store->completeGpu(*sprite).has_value());
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
}

TEST(Sprite2DBindingRegistryTests, WrongOwnerThreadOperationsFailWithoutMutation)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto sprite = store->publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());

    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registry->registerTextureBinding(*texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());

    std::optional<Core::ErrorCode> registerError;
    std::optional<Core::ErrorCode> unbindError;
    Core::u32 foreignBindingKey = *binding;
    Core::u32 foreignSpriteKey = *binding;
    std::thread foreignThread([&] {
        const auto registration = registry->registerTextureBinding(*texture, Render::GpuTextureId{2U, 1U});
        if (!registration)
        {
            registerError = registration.error().code;
        }
        const auto unbind = registry->unbindTextureBinding(*texture);
        if (!unbind)
        {
            unbindError = unbind.error().code;
        }
        foreignBindingKey = registry->bindingKey(*texture);
        foreignSpriteKey = registry->resolveSprite(*sprite);
    });
    foreignThread.join();

    ASSERT_TRUE(registerError.has_value());
    EXPECT_EQ(*registerError, Render::RenderErrorCode::WrongOwnerThread);
    ASSERT_TRUE(unbindError.has_value());
    EXPECT_EQ(*unbindError, Render::RenderErrorCode::WrongOwnerThread);
    EXPECT_EQ(foreignBindingKey, 0U);
    EXPECT_EQ(foreignSpriteKey, 0U);
    EXPECT_EQ(registry->bindingCount(), 1U);
    EXPECT_EQ(registry->bindingKey(*texture), *binding);
    EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, MoveTransfersOwnershipAndInvalidatesSourceRegistry)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto sprite = store->publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());
    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(*store, device);
    ASSERT_TRUE(registry.has_value());
    auto binding = registry->registerTextureBinding(*texture, Render::GpuTextureId{1U, 1U});
    ASSERT_TRUE(binding.has_value());

    Sprite2DBindingRegistry moved = std::move(*registry);

    EXPECT_FALSE(static_cast<bool>(*registry));
    EXPECT_TRUE(static_cast<bool>(moved));
    EXPECT_EQ(registry->capacity(), 0U);
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(registry->bindingKey(*texture), 0U);
    EXPECT_EQ(registry->resolveSprite(*sprite), 0U);
    const auto movedFromRegister = registry->registerTextureBinding(*texture, Render::GpuTextureId{2U, 1U});
    ASSERT_FALSE(movedFromRegister.has_value());
    EXPECT_EQ(movedFromRegister.error().code, Render::RenderErrorCode::WrongOwnerThread);

    EXPECT_EQ(moved.bindingKey(*texture), *binding);
    EXPECT_EQ(moved.resolveSprite(*sprite), *binding);
    EXPECT_EQ(moved.bindingCount(), 1U);
    EXPECT_EQ(device.callCount(), 1U);
}

TEST(Sprite2DBindingRegistryTests, SteadyStateOperationsPerformNoPmrAllocations)
{
    TrackingMemoryResource memory;
    auto store = makeStore(memory);
    ASSERT_TRUE(store.has_value());
    const Core::AssetId textureId = assetId(1U);
    auto texture = store->publish(makeTexture(memory, 1U));
    auto sprite = store->publish(makeSprite(memory, 2U, textureId));
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(sprite.has_value());
    FixedBindingRenderDevice device;
    auto registry = Sprite2DBindingRegistry::Create(
        *store, device, Sprite2DBindingRegistryConfig{.textureCapacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(registry.has_value());
    const Core::usize allocationBaseline = memory.allocationCalls();

    Core::u32 previousBindingKey = 0;
    for (Core::usize iteration = 0; iteration < 300U; ++iteration)
    {
        auto binding = registry->registerTextureBinding(*texture, Render::GpuTextureId{1U, 1U});
        ASSERT_TRUE(binding.has_value());
        EXPECT_GT(*binding, previousBindingKey);
        EXPECT_EQ(registry->bindingKey(*texture), *binding);
        EXPECT_EQ(registry->resolveSprite(*sprite), *binding);
        ASSERT_TRUE(registry->unbindTextureBinding(*texture).has_value());
        previousBindingKey = *binding;
    }

    EXPECT_EQ(memory.allocationCalls(), allocationBaseline);
    EXPECT_EQ(registry->bindingCount(), 0U);
    EXPECT_EQ(device.callCount(), 600U);
}

} // namespace
} // namespace Tina::Asset
