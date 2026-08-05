#pragma once

#include <tina/asset/AssetStore.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>

#include <array>
#include <limits>
#include <memory_resource>
#include <span>
#include <thread>
#include <vector>

namespace Tina::Asset {

class AssetSystem;
struct CatalogResidentMigration;

inline constexpr Core::usize DefaultMesh3DBindingCapacity = 64;
inline constexpr Core::usize MaximumMesh3DBindingCapacity = 4096;
inline constexpr Core::usize DefaultMesh3DTextureCapacity = DefaultMesh3DBindingCapacity * 3U;
inline constexpr Core::usize MaximumMesh3DTextureCapacity = MaximumMesh3DBindingCapacity * 3U;

struct Mesh3DBindingRegistryConfig final {
    Core::usize meshCapacity = DefaultMesh3DBindingCapacity;
    Core::usize materialCapacity = DefaultMesh3DBindingCapacity;
    Core::usize textureCapacity = DefaultMesh3DTextureCapacity;
    // Borrowed when non-null and must outlive the registry.
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Owner-thread registry for StaticMesh/Material bindings and the Texture2D
// resources shared by Material entries. AssetSystem, device, and configured
// memory resource are borrowed. Successful Mesh/Texture registration transfers
// the GPU owner into this registry and retains the corresponding CPU payload with
// an AssetLease. Scene extraction only receives packet-local FrameResourceRef
// values; no persistent device binding key escapes this owner.
class Mesh3DBindingRegistry final {
  public:
    ~Mesh3DBindingRegistry() noexcept;

    Mesh3DBindingRegistry(const Mesh3DBindingRegistry&) = delete;
    Mesh3DBindingRegistry& operator=(const Mesh3DBindingRegistry&) = delete;
    Mesh3DBindingRegistry(Mesh3DBindingRegistry&& other) noexcept;
    Mesh3DBindingRegistry& operator=(Mesh3DBindingRegistry&&) = delete;

    [[nodiscard]] static Core::Result<Mesh3DBindingRegistry> Create(
        AssetSystem& assets, Render::IRenderDevice& device, Mesh3DBindingRegistryConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::usize meshCapacity() const noexcept;
    [[nodiscard]] Core::usize materialCapacity() const noexcept;
    [[nodiscard]] Core::usize textureCapacity() const noexcept;
    [[nodiscard]] Core::usize meshBindingCount() const noexcept;
    [[nodiscard]] Core::usize materialBindingCount() const noexcept;
    [[nodiscard]] Core::usize textureOwnerCount() const noexcept;

    // Old owners replaced by AssetSystem::reloadCatalog() remain retryable until
    // backend retirement succeeds.
    [[nodiscard]] Core::usize pendingRetirementCount() const noexcept;
    [[nodiscard]] Core::Status drainPendingRetirements() noexcept;

    // Success clears the caller's GPU owner. Every failure preserves it.
    [[nodiscard]] Core::Result<Core::u32> registerMeshBinding(
        AssetHandle meshAsset, Render::GpuMeshId& gpuMesh) noexcept;
    [[nodiscard]] Core::Status registerMaterialTexture(
        AssetHandle textureAsset, Render::GpuTextureId& gpuTexture) noexcept;

    // Required texture dependencies must already have one registered owner.
    // Material factors and texture roles are derived from the cooked payload.
    [[nodiscard]] Core::Result<Core::u32> registerMaterialBinding(
        AssetHandle materialAsset) noexcept;

    [[nodiscard]] bool hasMaterialTexture(AssetHandle textureAsset) const noexcept;

    // Retirement is retryable per owned entry. Active frame borrows reject before
    // mutation. Shared Texture2D owners cannot retire while referenced by a live
    // Material binding.
    [[nodiscard]] Core::Status retireMeshBinding(AssetHandle meshAsset) noexcept;
    [[nodiscard]] Core::Status retireMaterialBinding(AssetHandle materialAsset) noexcept;
    [[nodiscard]] Core::Status retireMaterialTexture(AssetHandle textureAsset) noexcept;
    [[nodiscard]] Core::Status retireAllBindings() noexcept;

    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internMeshFrameResource(AssetHandle meshAsset, Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internMaterialFrameResource(AssetHandle materialAsset, Render::FrameResourceSink& sink) noexcept;

  private:
    friend class AssetSystem;

    inline static constexpr Core::u32 InvalidTextureIndex = (std::numeric_limits<Core::u32>::max)();
    inline static constexpr Core::u32 InvalidEntryIndex = (std::numeric_limits<Core::u32>::max)();

    struct MeshEntry final {
        AssetHandle asset{};
        Core::AssetId assetId{};
        AssetLease lease{};
        Render::GpuMeshId gpuMesh{};
        Core::u32 bindingKey = 0;
        Core::u32 frameBorrowCount = 0;
    };

    struct TextureEntry final {
        AssetHandle asset{};
        Core::AssetId assetId{};
        AssetLease lease{};
        Render::GpuTextureId gpuTexture{};
        Core::u32 materialReferenceCount = 0;
    };

    struct MaterialEntry final {
        AssetHandle asset{};
        Core::AssetId assetId{};
        AssetLease lease{};
        std::array<Core::u32, 3> textureIndices{
            InvalidTextureIndex,
            InvalidTextureIndex,
            InvalidTextureIndex,
        };
        Core::u32 textureCount = 0;
        Core::u32 bindingKey = 0;
        Core::u32 frameBorrowCount = 0;
    };

    struct ValidatedMaterialBinding final {
        Render::Mesh3DMaterialBindingDesc renderBinding{};
        std::array<Core::u32, 3> textureIndices{
            InvalidTextureIndex,
            InvalidTextureIndex,
            InvalidTextureIndex,
        };
        Core::u32 textureCount = 0;
    };

    struct CandidateTextureEntry final {
        const TextureEntry* entry = nullptr;
        Core::u32 entryIndex = InvalidTextureIndex;
    };

    struct PreparedMeshEntry final {
        Core::u32 entryIndex = InvalidEntryIndex;
        MeshEntry replacement{};
        bool remove = false;
    };

    struct PreparedTextureEntry final {
        Core::u32 entryIndex = InvalidEntryIndex;
        TextureEntry replacement{};
        bool remove = false;
    };

    struct PreparedMaterialEntry final {
        Core::u32 entryIndex = InvalidEntryIndex;
        MaterialEntry replacement{};
        bool remove = false;
    };

    struct PendingMeshRetirement final {
        AssetLease lease{};
        Render::GpuMeshId gpuMesh{};
    };

    struct PendingTextureRetirement final {
        AssetLease lease{};
        Render::GpuTextureId gpuTexture{};
    };

    struct PendingMaterialRetirement final {
        AssetLease lease{};
        Core::u32 bindingKey = 0;
    };

    Mesh3DBindingRegistry(AssetSystem& assets, Render::IRenderDevice& device,
                          std::pmr::vector<MeshEntry> meshEntries,
                          std::pmr::vector<MaterialEntry> materialEntries,
                          std::pmr::vector<TextureEntry> textureEntries,
                          std::pmr::vector<PreparedMeshEntry> preparedMeshes,
                          std::pmr::vector<PreparedMaterialEntry> preparedMaterials,
                          std::pmr::vector<PreparedTextureEntry> preparedTextures,
                          std::pmr::vector<PendingMeshRetirement> pendingMeshes,
                          std::pmr::vector<PendingMaterialRetirement> pendingMaterials,
                          std::pmr::vector<PendingTextureRetirement> pendingTextures,
                          Core::usize meshCapacity, Core::usize materialCapacity,
                          Core::usize textureCapacity) noexcept;

    [[nodiscard]] Core::Status prepareCatalogReload(
        AssetSystem& owner, std::span<const CatalogResidentMigration> migrations) noexcept;
    void commitPreparedCatalogReload() noexcept;
    void abortPreparedCatalogReload() noexcept;

    [[nodiscard]] bool isOwnerThread() const noexcept;
    [[nodiscard]] bool isLiveMeshEntry(const MeshEntry& entry) const noexcept;
    [[nodiscard]] bool isLiveMaterialEntry(const MaterialEntry& entry) const noexcept;
    [[nodiscard]] bool isLiveTextureEntry(const TextureEntry& entry) const noexcept;
    [[nodiscard]] MeshEntry* findMeshExact(AssetHandle asset) noexcept;
    [[nodiscard]] const MeshEntry* findMeshExact(AssetHandle asset) const noexcept;
    [[nodiscard]] MeshEntry* findMeshByAssetId(Core::AssetId assetId) noexcept;
    [[nodiscard]] const MeshEntry* findMeshByGpu(Render::GpuMeshId gpuMesh) const noexcept;
    [[nodiscard]] MeshEntry* findFreeMesh() noexcept;
    [[nodiscard]] MaterialEntry* findMaterialExact(AssetHandle asset) noexcept;
    [[nodiscard]] const MaterialEntry* findMaterialExact(AssetHandle asset) const noexcept;
    [[nodiscard]] MaterialEntry* findMaterialByAssetId(Core::AssetId assetId) noexcept;
    [[nodiscard]] MaterialEntry* findFreeMaterial() noexcept;
    [[nodiscard]] TextureEntry* findTextureExact(AssetHandle asset) noexcept;
    [[nodiscard]] const TextureEntry* findTextureExact(AssetHandle asset) const noexcept;
    [[nodiscard]] TextureEntry* findTextureByAssetId(Core::AssetId assetId) noexcept;
    [[nodiscard]] const TextureEntry* findTextureByAssetId(Core::AssetId assetId) const noexcept;
    [[nodiscard]] const TextureEntry* findTextureByGpu(Render::GpuTextureId gpuTexture) const noexcept;
    [[nodiscard]] TextureEntry* findFreeTexture() noexcept;
    [[nodiscard]] Core::u32 textureIndex(const TextureEntry& entry) const noexcept;
    [[nodiscard]] Core::Result<ValidatedMaterialBinding> validateMaterialBinding(
        AssetHandle materialAsset) const noexcept;
    [[nodiscard]] Core::Result<ValidatedMaterialBinding> validatePreparedMaterialBinding(
        AssetHandle materialAsset) const noexcept;
    [[nodiscard]] Core::Result<ValidatedMaterialBinding> validateMaterialBindingImpl(
        AssetHandle materialAsset, bool usePreparedTextures) const noexcept;
    [[nodiscard]] CandidateTextureEntry findCandidateTextureByAssetId(Core::AssetId assetId) const noexcept;
    [[nodiscard]] Core::u32 findPreparedTextureByAssetId(Core::AssetId assetId) const noexcept;
    [[nodiscard]] Core::u32 findFreePreparedTextureSlot() const noexcept;
    [[nodiscard]] const CatalogResidentMigration* findMigration(
        std::span<const CatalogResidentMigration> migrations, Core::AssetId assetId) const noexcept;
    static void releaseMeshFrameBorrow(void* userData) noexcept;
    static void releaseMaterialFrameBorrow(void* userData) noexcept;

    AssetSystem* m_assets = nullptr;
    AssetStore* m_store = nullptr;
    Render::IRenderDevice* m_device = nullptr;
    std::pmr::vector<MeshEntry> m_meshEntries{};
    std::pmr::vector<MaterialEntry> m_materialEntries{};
    std::pmr::vector<TextureEntry> m_textureEntries{};
    std::pmr::vector<PreparedMeshEntry> m_preparedMeshes{};
    std::pmr::vector<PreparedMaterialEntry> m_preparedMaterials{};
    std::pmr::vector<PreparedTextureEntry> m_preparedTextures{};
    std::pmr::vector<PendingMeshRetirement> m_pendingMeshes{};
    std::pmr::vector<PendingMaterialRetirement> m_pendingMaterials{};
    std::pmr::vector<PendingTextureRetirement> m_pendingTextures{};
    Core::usize m_meshCapacity = 0;
    Core::usize m_materialCapacity = 0;
    Core::usize m_textureCapacity = 0;
    Core::usize m_meshBindingCount = 0;
    Core::usize m_materialBindingCount = 0;
    Core::usize m_textureOwnerCount = 0;
    Core::usize m_preparedMeshCount = 0;
    Core::usize m_preparedMaterialCount = 0;
    Core::usize m_preparedTextureCount = 0;
    Core::usize m_pendingMeshCount = 0;
    Core::usize m_pendingMaterialCount = 0;
    Core::usize m_pendingTextureCount = 0;
    std::thread::id m_ownerThread{};
};

} // namespace Tina::Asset
