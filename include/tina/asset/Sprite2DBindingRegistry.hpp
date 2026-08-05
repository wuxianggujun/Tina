#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/Texture2DFrameResourceResolver.hpp>

#include <memory_resource>
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace Tina::Asset {

class AssetSystem;
struct CatalogResidentMigration;

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
    // Old owners replaced by AssetSystem::reloadCatalog() remain retryable until
    // backend retirement succeeds.
    [[nodiscard]] Core::usize pendingRetirementCount() const noexcept;
    [[nodiscard]] Core::Status drainPendingRetirements() noexcept;

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

    // Root-scoped UI and other backend-neutral consumers can bind this adapter
    // without retaining AssetHandle values. The registry remains the sole
    // binding/lifetime owner and must outlive the returned resolver.
    [[nodiscard]] Render::Texture2DFrameResourceResolver texture2DFrameResourceResolver() noexcept;

  private:
    friend class AssetSystem;

    inline static constexpr Core::u32 InvalidEntryIndex = (std::numeric_limits<Core::u32>::max)();

    struct Entry final {
        AssetHandle textureAsset{};
        Core::AssetId textureAssetId{};
        AssetLease lease{};
        Render::GpuTextureId gpuTexture{};
        Core::u32 bindingKey = 0;
        Core::u32 frameBorrowCount = 0;
    };

    struct PreparedEntry final {
        Core::u32 entryIndex = InvalidEntryIndex;
        Entry replacement{};
        bool remove = false;
    };

    struct PendingRetirement final {
        AssetLease lease{};
        Render::GpuTextureId gpuTexture{};
    };

    Sprite2DBindingRegistry(AssetSystem& assets, Render::IRenderDevice& device, std::pmr::vector<Entry> entries,
                            std::pmr::vector<PreparedEntry> preparedEntries,
                            std::pmr::vector<PendingRetirement> pendingRetirements,
                            Core::usize capacity) noexcept;

    [[nodiscard]] Core::Status prepareCatalogReload(
        AssetSystem& owner, std::span<const CatalogResidentMigration> migrations) noexcept;
    void commitPreparedCatalogReload() noexcept;
    void abortPreparedCatalogReload() noexcept;

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
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internTextureEntry(Entry& entry, Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
    resolveTexture2DFrameResource(Core::AssetId asset, Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
    resolveTexture2DFrameResourceCallback(
        void* userData, Core::AssetId asset, Render::FrameResourceSink& sink) noexcept;
    static void releaseFrameBorrow(void* userData) noexcept;

    AssetSystem* m_assets = nullptr;
    AssetStore* m_store = nullptr;
    Render::IRenderDevice* m_device = nullptr;
    std::pmr::vector<Entry> m_entries{};
    std::pmr::vector<PreparedEntry> m_preparedEntries{};
    std::pmr::vector<PendingRetirement> m_pendingRetirements{};
    Core::usize m_capacity = 0;
    Core::usize m_bindingCount = 0;
    Core::usize m_preparedCount = 0;
    Core::usize m_pendingRetirementCount = 0;
    std::thread::id m_ownerThread{};
};

} // namespace Tina::Asset
