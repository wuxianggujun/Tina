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

inline constexpr Core::usize DefaultMesh3DBindingCapacity = 64;
inline constexpr Core::usize MaximumMesh3DBindingCapacity = 4096;

struct Mesh3DMaterialTextureBinding final {
    AssetHandle textureAsset{};
    Render::GpuTextureId gpuTexture{};

    [[nodiscard]] friend constexpr bool operator==(const Mesh3DMaterialTextureBinding&,
                                                   const Mesh3DMaterialTextureBinding&) noexcept = default;
};

struct Mesh3DMaterialGpuBindingDesc final {
    Mesh3DMaterialTextureBinding baseColor{};
    Mesh3DMaterialTextureBinding metallicRoughness{};
    Mesh3DMaterialTextureBinding normal{};

    [[nodiscard]] friend constexpr bool operator==(const Mesh3DMaterialGpuBindingDesc&,
                                                   const Mesh3DMaterialGpuBindingDesc&) noexcept = default;
};

struct Mesh3DBindingRegistryConfig final {
    Core::usize meshCapacity = DefaultMesh3DBindingCapacity;
    Core::usize materialCapacity = DefaultMesh3DBindingCapacity;
    // Borrowed when non-null and must outlive the registry.
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Owner-thread registry for StaticMesh/Material AssetHandle -> backend-neutral
// Mesh3D binding keys. Store, device, and configured memory resource are borrowed.
// The caller owns every GPU resource and must successfully unbind before destroy
// or retirement. This registry never acquires AssetLease or retires GPU resources.
class Mesh3DBindingRegistry final {
  public:
    Mesh3DBindingRegistry(const Mesh3DBindingRegistry&) = delete;
    Mesh3DBindingRegistry& operator=(const Mesh3DBindingRegistry&) = delete;
    Mesh3DBindingRegistry(Mesh3DBindingRegistry&& other) noexcept;
    Mesh3DBindingRegistry& operator=(Mesh3DBindingRegistry&&) = delete;

    [[nodiscard]] static Core::Result<Mesh3DBindingRegistry> Create(
        AssetStore& store, Render::IRenderDevice& device, Mesh3DBindingRegistryConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::usize meshCapacity() const noexcept;
    [[nodiscard]] Core::usize materialCapacity() const noexcept;
    [[nodiscard]] Core::usize meshBindingCount() const noexcept;
    [[nodiscard]] Core::usize materialBindingCount() const noexcept;

    [[nodiscard]] Core::Result<Core::u32> registerMeshBinding(
        AssetHandle meshAsset, Render::GpuMeshId gpuMesh) noexcept;
    [[nodiscard]] Core::Result<Core::u32> registerMaterialBinding(
        AssetHandle materialAsset, Mesh3DMaterialGpuBindingDesc gpuBinding) noexcept;

    // Exact handles remain usable for unbind after their Store records become stale.
    // Backend failure preserves the entry and allows retry.
    [[nodiscard]] Core::Status unbindMeshBinding(AssetHandle meshAsset) noexcept;
    [[nodiscard]] Core::Status unbindMaterialBinding(AssetHandle materialAsset) noexcept;

    [[nodiscard]] Core::u32 resolveMesh(AssetHandle meshAsset) const noexcept;
    [[nodiscard]] Core::u32 resolveMaterial(AssetHandle materialAsset) const noexcept;

  private:
    struct MeshEntry final {
        AssetHandle asset{};
        Core::AssetId assetId{};
        Render::GpuMeshId gpuMesh{};
        Core::u32 bindingKey = 0;
    };

    struct MaterialEntry final {
        AssetHandle asset{};
        Core::AssetId assetId{};
        Mesh3DMaterialGpuBindingDesc gpuBinding{};
        Core::u32 bindingKey = 0;
    };

    Mesh3DBindingRegistry(AssetStore& store, Render::IRenderDevice& device,
                          std::pmr::vector<MeshEntry> meshEntries,
                          std::pmr::vector<MaterialEntry> materialEntries,
                          Core::usize meshCapacity, Core::usize materialCapacity) noexcept;

    [[nodiscard]] bool isOwnerThread() const noexcept;
    [[nodiscard]] bool isLiveMeshEntry(const MeshEntry& entry) const noexcept;
    [[nodiscard]] bool isLiveMaterialEntry(const MaterialEntry& entry) const noexcept;
    [[nodiscard]] MeshEntry* findMeshExact(AssetHandle asset) noexcept;
    [[nodiscard]] const MeshEntry* findMeshExact(AssetHandle asset) const noexcept;
    [[nodiscard]] MeshEntry* findMeshByAssetId(Core::AssetId assetId) noexcept;
    [[nodiscard]] MeshEntry* findFreeMesh() noexcept;
    [[nodiscard]] MaterialEntry* findMaterialExact(AssetHandle asset) noexcept;
    [[nodiscard]] const MaterialEntry* findMaterialExact(AssetHandle asset) const noexcept;
    [[nodiscard]] MaterialEntry* findMaterialByAssetId(Core::AssetId assetId) noexcept;
    [[nodiscard]] MaterialEntry* findFreeMaterial() noexcept;
    [[nodiscard]] Core::Result<Render::Mesh3DMaterialBindingDesc> validateMaterialBinding(
        AssetHandle materialAsset, const Mesh3DMaterialGpuBindingDesc& gpuBinding) const noexcept;

    AssetStore* m_store = nullptr;
    Render::IRenderDevice* m_device = nullptr;
    std::pmr::vector<MeshEntry> m_meshEntries{};
    std::pmr::vector<MaterialEntry> m_materialEntries{};
    Core::usize m_meshCapacity = 0;
    Core::usize m_materialCapacity = 0;
    Core::usize m_meshBindingCount = 0;
    Core::usize m_materialBindingCount = 0;
    std::thread::id m_ownerThread{};
};

} // namespace Tina::Asset
