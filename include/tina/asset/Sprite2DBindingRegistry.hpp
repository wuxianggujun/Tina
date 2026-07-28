#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory_resource>
#include <thread>
#include <vector>

namespace Tina::Asset {

class AssetSystem;

inline constexpr Core::usize DefaultSprite2DBindingCapacity = 64;
inline constexpr Core::usize MaximumSprite2DBindingCapacity = 4096;

struct Sprite2DBindingRegistryConfig final {
    Core::usize textureCapacity = DefaultSprite2DBindingCapacity;
    // Borrowed when non-null and must outlive the registry.
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Owner-thread registry for Texture2D AssetHandle -> backend-neutral Sprite2D
// binding key. AssetSystem, device, and any configured memory resource are
// borrowed and must outlive the registry. A successful registration transfers
// one GpuTextureId owner into the registry and keeps the Texture2D payload alive
// with an AssetLease until retireTextureBinding() transfers both owners to the
// AssetSystem retirement path.
class Sprite2DBindingRegistry final {
  public:
    ~Sprite2DBindingRegistry() noexcept;

    Sprite2DBindingRegistry(const Sprite2DBindingRegistry&) = delete;
    Sprite2DBindingRegistry& operator=(const Sprite2DBindingRegistry&) = delete;
    Sprite2DBindingRegistry(Sprite2DBindingRegistry&& other) noexcept;
    Sprite2DBindingRegistry& operator=(Sprite2DBindingRegistry&&) = delete;

    // Must run on the shared owner thread of the borrowed AssetSystem and
    // RenderDevice. That thread becomes the registry owner and must perform
    // every subsequent registry operation. Both borrowed owners must remain at
    // stable addresses until the registry and every handed-off retirement end.
    [[nodiscard]] static Core::Result<Sprite2DBindingRegistry> Create(AssetSystem& assets,
                                                                      Render::IRenderDevice& device,
                                                                      Sprite2DBindingRegistryConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::usize capacity() const noexcept;
    [[nodiscard]] Core::usize bindingCount() const noexcept;

    // On success gpuTexture is cleared and the registry owns its GPU lifetime.
    // Every failure preserves gpuTexture and releases any temporary AssetLease.
    // A live handle, AssetId, or GPU texture may have only one owned
    // registration in this registry. Callers must not alias a GPU owner across
    // registries.
    [[nodiscard]] Core::Result<Core::u32> registerTextureBinding(AssetHandle textureAsset,
                                                                 Render::GpuTextureId& gpuTexture) noexcept;

    // Transfers the owned AssetLease and GPU texture to AssetSystem retirement.
    // The RenderDevice retirement commit atomically invalidates the texture and
    // clears its bindings. An active frame borrow or any retirement failure
    // preserves the complete record for retry.
    [[nodiscard]] Core::Status retireTextureBinding(AssetHandle textureAsset) noexcept;
    [[nodiscard]] Core::Status retireAllTextureBindings() noexcept;

    [[nodiscard]] Core::u32 bindingKey(AssetHandle textureAsset) const noexcept;
    // Fail-closed Sprite -> unique Texture2D dependency -> live binding lookup.
    [[nodiscard]] Core::u32 resolveSprite(AssetHandle spriteAsset) const noexcept;
    // Fail-closed Tileset -> unique Texture2D dependency -> live binding lookup.
    [[nodiscard]] Core::u32 resolveTileset(AssetHandle tilesetAsset) const noexcept;

    // Resolves the asset's unique Texture2D dependency and interns its current
    // backend binding into a frame-local resource table. The retained FramePin
    // prevents this binding entry from being retired until the sink releases it.
    // An unresolved asset returns a successful empty ref without invoking the sink.
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internSpriteFrameResource(AssetHandle spriteAsset, Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internTilesetFrameResource(AssetHandle tilesetAsset, Render::FrameResourceSink& sink) noexcept;

  private:
    struct Entry final {
        AssetHandle textureAsset{};
        Core::AssetId textureAssetId{};
        AssetLease lease{};
        Render::GpuTextureId gpuTexture{};
        Core::u32 bindingKey = 0;
        Core::u32 frameBorrowCount = 0;
    };

    Sprite2DBindingRegistry(AssetSystem& assets, Render::IRenderDevice& device, std::pmr::vector<Entry> entries,
                            Core::usize capacity) noexcept;

    [[nodiscard]] bool isOwnerThread() const noexcept;
    [[nodiscard]] Entry* findExact(AssetHandle textureAsset) noexcept;
    [[nodiscard]] const Entry* findExact(AssetHandle textureAsset) const noexcept;
    [[nodiscard]] Entry* findByAssetId(Core::AssetId textureAssetId) noexcept;
    [[nodiscard]] const Entry* findByAssetId(Core::AssetId textureAssetId) const noexcept;
    [[nodiscard]] const Entry* findByGpuTexture(Render::GpuTextureId gpuTexture) const noexcept;
    [[nodiscard]] Entry* findFree() noexcept;
    [[nodiscard]] bool isLiveTextureEntry(const Entry& entry) const noexcept;
    [[nodiscard]] const Entry* resolveSingleTextureDependencyEntry(
        AssetHandle asset,
        AssetFormat::AssetKind expectedKind) const noexcept;
    [[nodiscard]] Core::u32 resolveSingleTextureDependency(AssetHandle asset,
                                                           AssetFormat::AssetKind expectedKind) const noexcept;
    [[nodiscard]] Core::Result<Render::FrameResourceRef> internSingleTextureDependency(
        AssetHandle asset,
        AssetFormat::AssetKind expectedKind,
        Render::FrameResourceSink& sink) noexcept;
    static void releaseFrameBorrow(void* userData) noexcept;

    AssetSystem* m_assets = nullptr;
    AssetStore* m_store = nullptr;
    Render::IRenderDevice* m_device = nullptr;
    std::pmr::vector<Entry> m_entries{};
    Core::usize m_capacity = 0;
    Core::usize m_bindingCount = 0;
    std::thread::id m_ownerThread{};
};

} // namespace Tina::Asset
