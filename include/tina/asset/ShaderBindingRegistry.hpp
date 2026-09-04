#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <thread>
#include <vector>

namespace Tina::Asset {

class AssetSystem;
struct CatalogResidentMigration;

inline constexpr Core::usize DefaultShaderBindingCapacity = 32;
inline constexpr Core::usize MaximumShaderBindingCapacity = 512;

struct ShaderBindingRegistryConfig final {
    Core::usize shaderCapacity = DefaultShaderBindingCapacity;
    std::pmr::memory_resource* memoryResource = nullptr;
};

class ShaderBindingRegistry final {
  public:
    ~ShaderBindingRegistry() noexcept;

    ShaderBindingRegistry(const ShaderBindingRegistry&) = delete;
    ShaderBindingRegistry& operator=(const ShaderBindingRegistry&) = delete;
    ShaderBindingRegistry(ShaderBindingRegistry&& other) noexcept;
    ShaderBindingRegistry& operator=(ShaderBindingRegistry&&) = delete;

    [[nodiscard]] static Core::Result<ShaderBindingRegistry>
    Create(AssetSystem& assets, Render::IRenderDevice& device, ShaderBindingRegistryConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::usize capacity() const noexcept;
    [[nodiscard]] Core::usize bindingCount() const noexcept;
    [[nodiscard]] bool hasActiveFrameBorrows() const noexcept;
    [[nodiscard]] Core::usize pendingRetirementCount() const noexcept;
    [[nodiscard]] Core::Status drainPendingRetirements() noexcept;

    [[nodiscard]] Core::Result<Core::u32>
    registerShaderBinding(AssetHandle shaderAsset, Render::GpuShaderId& gpuShader) noexcept;
    [[nodiscard]] Core::Status retireShaderBinding(AssetHandle shaderAsset) noexcept;
    [[nodiscard]] Core::Status retireAllShaderBindings() noexcept;

    [[nodiscard]] Core::u32 bindingKey(AssetHandle shaderAsset) const noexcept;
    [[nodiscard]] Core::u32 uniformBindingKey(AssetHandle shaderAsset) const noexcept;
    [[nodiscard]] Core::Status
    setShaderUniformValues(AssetHandle shaderAsset,
                           const Render::GpuShaderUniformBindingDesc& desc) noexcept;
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internShaderFrameResource(AssetHandle shaderAsset, Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internShaderUniformFrameResource(AssetHandle shaderAsset, Render::FrameResourceSink& sink) noexcept;

  private:
    friend class AssetSystem;

    inline static constexpr Core::u32 InvalidEntryIndex = (std::numeric_limits<Core::u32>::max)();

    struct Entry final {
        AssetHandle shaderAsset{};
        Core::AssetId shaderAssetId{};
        AssetLease lease{};
        Render::GpuShaderId gpuShader{};
        Core::u32 bindingKey = 0;
        Core::u32 uniformBindingKey = 0;
        Core::u32 frameBorrowCount = 0;
    };

    struct PreparedEntry final {
        Core::u32 entryIndex = InvalidEntryIndex;
        Entry replacement{};
        bool remove = false;
    };

    struct PendingRetirement final {
        AssetLease lease{};
        Render::GpuShaderId gpuShader{};
        Core::u32 uniformBindingKey = 0;
    };

    ShaderBindingRegistry(AssetSystem& assets, Render::IRenderDevice& device,
                          std::pmr::vector<Entry> entries,
                          std::pmr::vector<PreparedEntry> preparedEntries,
                          std::pmr::vector<PendingRetirement> pendingRetirements,
                          Core::usize capacity) noexcept;

    [[nodiscard]] Core::Status prepareCatalogReload(
        AssetSystem& owner, std::span<const CatalogResidentMigration> migrations) noexcept;
    void commitPreparedCatalogReload() noexcept;
    void abortPreparedCatalogReload() noexcept;

    [[nodiscard]] bool isOwnerThread() const noexcept;
    [[nodiscard]] Entry* findExact(AssetHandle shaderAsset) noexcept;
    [[nodiscard]] const Entry* findExact(AssetHandle shaderAsset) const noexcept;
    [[nodiscard]] Entry* findByAssetId(Core::AssetId shaderAssetId) noexcept;
    [[nodiscard]] const Entry* findByGpuShader(Render::GpuShaderId gpuShader) const noexcept;
    [[nodiscard]] Entry* findFree() noexcept;
    [[nodiscard]] bool isLiveShaderEntry(const Entry& entry) const noexcept;
    [[nodiscard]] Core::Result<Render::FrameResourceRef>
    internOwnedFrameResource(Entry& entry, Render::FrameResourceSink& sink,
                             Render::FrameResourceKind kind, Core::u32 deviceBindingKey) noexcept;
    [[nodiscard]] Core::Status clearUniformBinding(Core::u32 uniformBindingKey) noexcept;
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
