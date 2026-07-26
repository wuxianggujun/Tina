#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory_resource>
#include <thread>
#include <vector>

namespace Tina::Asset {

class AssetStore;

inline constexpr Core::usize DefaultSprite2DBindingCapacity = 64;
inline constexpr Core::usize MaximumSprite2DBindingCapacity = 4096;

struct Sprite2DBindingRegistryConfig final {
    Core::usize textureCapacity = DefaultSprite2DBindingCapacity;
    // Borrowed when non-null and must outlive the registry.
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Owner-thread registry for Texture2D AssetHandle -> backend-neutral Sprite2D
// binding key. Store, device, and any configured memory resource are borrowed
// and must outlive the registry.
// The caller owns GpuTextureId lifetime and must successfully unbind before
// destroying or retiring a registered texture.
class Sprite2DBindingRegistry final {
  public:
    Sprite2DBindingRegistry(const Sprite2DBindingRegistry&) = delete;
    Sprite2DBindingRegistry& operator=(const Sprite2DBindingRegistry&) = delete;
    Sprite2DBindingRegistry(Sprite2DBindingRegistry&& other) noexcept;
    Sprite2DBindingRegistry& operator=(Sprite2DBindingRegistry&&) = delete;

    // Must run on the shared owner thread of the borrowed AssetStore and
    // RenderDevice. That thread becomes the registry owner and must perform
    // every subsequent registry operation.
    [[nodiscard]] static Core::Result<Sprite2DBindingRegistry> Create(AssetStore& store, Render::IRenderDevice& device,
                                                                      Sprite2DBindingRegistryConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::usize capacity() const noexcept;
    [[nodiscard]] Core::usize bindingCount() const noexcept;

    // Exact duplicate registration of a live handle is idempotent. A different GPU
    // texture or a second live handle for the same AssetId is a conflict and must be
    // unbound first.
    [[nodiscard]] Core::Result<Core::u32> registerTextureBinding(AssetHandle textureAsset,
                                                                 Render::GpuTextureId gpuTexture) noexcept;

    // Uses the exact stored handle even after the AssetStore handle becomes stale.
    // Backend failure preserves the registry record so the caller can retry.
    [[nodiscard]] Core::Status unbindTextureBinding(AssetHandle textureAsset) noexcept;

    [[nodiscard]] Core::u32 bindingKey(AssetHandle textureAsset) const noexcept;
    // Fail-closed Sprite -> unique Texture2D dependency -> live binding lookup.
    [[nodiscard]] Core::u32 resolveSprite(AssetHandle spriteAsset) const noexcept;

  private:
    struct Entry final {
        AssetHandle textureAsset{};
        Core::AssetId textureAssetId{};
        Render::GpuTextureId gpuTexture{};
        Core::u32 bindingKey = 0;
    };

    Sprite2DBindingRegistry(AssetStore& store, Render::IRenderDevice& device, std::pmr::vector<Entry> entries,
                            Core::usize capacity) noexcept;

    [[nodiscard]] bool isOwnerThread() const noexcept;
    [[nodiscard]] Entry* findExact(AssetHandle textureAsset) noexcept;
    [[nodiscard]] const Entry* findExact(AssetHandle textureAsset) const noexcept;
    [[nodiscard]] Entry* findByAssetId(Core::AssetId textureAssetId) noexcept;
    [[nodiscard]] const Entry* findByAssetId(Core::AssetId textureAssetId) const noexcept;
    [[nodiscard]] Entry* findFree() noexcept;
    [[nodiscard]] bool isLiveTextureEntry(const Entry& entry) const noexcept;

    AssetStore* m_store = nullptr;
    Render::IRenderDevice* m_device = nullptr;
    std::pmr::vector<Entry> m_entries{};
    Core::usize m_capacity = 0;
    Core::usize m_bindingCount = 0;
    std::thread::id m_ownerThread{};
};

} // namespace Tina::Asset
