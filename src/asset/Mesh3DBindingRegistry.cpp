#include <tina/asset/Mesh3DBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/render/RenderErrors.hpp>

#include <array>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool hasRenderableCpuPayload(const AssetStore& store, AssetHandle handle) noexcept
{
    const AssetLogicalState state = store.state(handle);
    return state == AssetLogicalState::ReadyCpu || state == AssetLogicalState::UploadQueued ||
           state == AssetLogicalState::ReadyGpu;
}

[[nodiscard]] bool isLiveTextureBinding(const AssetStore& store,
                                        const Mesh3DMaterialTextureBinding& binding) noexcept
{
    if (!binding.textureAsset)
    {
        return !binding.gpuTexture;
    }
    return binding.gpuTexture &&
           store.assetKind(binding.textureAsset) == AssetFormat::AssetKind::Texture2D &&
           hasRenderableCpuPayload(store, binding.textureAsset) &&
           store.tryGet(binding.textureAsset) != nullptr && store.assetId(binding.textureAsset).hasValue();
}

} // namespace

Mesh3DBindingRegistry::Mesh3DBindingRegistry(AssetStore& store, Render::IRenderDevice& device,
                                             std::pmr::vector<MeshEntry> meshEntries,
                                             std::pmr::vector<MaterialEntry> materialEntries,
                                             Core::usize meshCapacity, Core::usize materialCapacity) noexcept
    : m_store(&store), m_device(&device), m_meshEntries(std::move(meshEntries)),
      m_materialEntries(std::move(materialEntries)), m_meshCapacity(meshCapacity),
      m_materialCapacity(materialCapacity), m_ownerThread(std::this_thread::get_id())
{
}

Mesh3DBindingRegistry::Mesh3DBindingRegistry(Mesh3DBindingRegistry&& other) noexcept
    : m_store(std::exchange(other.m_store, nullptr)), m_device(std::exchange(other.m_device, nullptr)),
      m_meshEntries(std::move(other.m_meshEntries)), m_materialEntries(std::move(other.m_materialEntries)),
      m_meshCapacity(std::exchange(other.m_meshCapacity, 0)),
      m_materialCapacity(std::exchange(other.m_materialCapacity, 0)),
      m_meshBindingCount(std::exchange(other.m_meshBindingCount, 0)),
      m_materialBindingCount(std::exchange(other.m_materialBindingCount, 0)),
      m_ownerThread(std::exchange(other.m_ownerThread, {}))
{
}

Core::Result<Mesh3DBindingRegistry> Mesh3DBindingRegistry::Create(
    AssetStore& store, Render::IRenderDevice& device, Mesh3DBindingRegistryConfig config)
{
    if (config.meshCapacity == 0 || config.meshCapacity > MaximumMesh3DBindingCapacity ||
        config.materialCapacity == 0 || config.materialCapacity > MaximumMesh3DBindingCapacity)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Mesh3DBindingRegistry capacities must be in [1, 4096]");
    }
    auto* memoryResource = config.memoryResource != nullptr ? config.memoryResource : std::pmr::get_default_resource();
    try
    {
        std::pmr::vector<MeshEntry> meshEntries{memoryResource};
        std::pmr::vector<MaterialEntry> materialEntries{memoryResource};
        meshEntries.resize(config.meshCapacity);
        materialEntries.resize(config.materialCapacity);
        return Mesh3DBindingRegistry{store, device, std::move(meshEntries), std::move(materialEntries),
                                     config.meshCapacity, config.materialCapacity};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "Mesh3DBindingRegistry storage allocation failed");
    }
}

Mesh3DBindingRegistry::operator bool() const noexcept
{
    return m_store != nullptr && m_device != nullptr && m_meshCapacity != 0 && m_materialCapacity != 0;
}

Core::usize Mesh3DBindingRegistry::meshCapacity() const noexcept { return m_meshCapacity; }
Core::usize Mesh3DBindingRegistry::materialCapacity() const noexcept { return m_materialCapacity; }
Core::usize Mesh3DBindingRegistry::meshBindingCount() const noexcept { return m_meshBindingCount; }
Core::usize Mesh3DBindingRegistry::materialBindingCount() const noexcept { return m_materialBindingCount; }

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMeshBinding(
    AssetHandle meshAsset, Render::GpuMeshId gpuMesh) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh register must run on its owner thread");
    }
    if (!meshAsset || m_store->assetKind(meshAsset) != AssetFormat::AssetKind::StaticMesh)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a StaticMesh handle from its AssetStore");
    }
    if (!hasRenderableCpuPayload(*m_store, meshAsset) || m_store->tryGet(meshAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry StaticMesh CPU payload is not ready");
    }
    if (!gpuMesh)
    {
        return Core::failure(Render::RenderErrorCode::InvalidMeshUpload,
                             "Mesh3DBindingRegistry requires a live GPU mesh");
    }
    if (MeshEntry* exact = findMeshExact(meshAsset); exact != nullptr)
    {
        if (exact->gpuMesh == gpuMesh)
        {
            return exact->bindingKey;
        }
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "StaticMesh handle is already registered with another GPU mesh");
    }
    const Core::AssetId assetId = m_store->assetId(meshAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "StaticMesh handle has no AssetId");
    }
    if (findMeshByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "StaticMesh AssetId already has another registered handle");
    }
    if (m_meshBindingCount >= m_meshCapacity)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free mesh slot");
    }
    MeshEntry* freeEntry = findFreeMesh();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free mesh slot");
    }
    auto bindingKey = m_device->createMesh3DBinding(gpuMesh);
    if (!bindingKey)
    {
        return Core::failure(std::move(bindingKey.error()).withContext(
            "Mesh3DBindingRegistry::registerMeshBinding", "device"));
    }
    *freeEntry = MeshEntry{.asset = meshAsset, .assetId = assetId, .gpuMesh = gpuMesh, .bindingKey = *bindingKey};
    ++m_meshBindingCount;
    return *bindingKey;
}

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMaterialBinding(
    AssetHandle materialAsset, Mesh3DMaterialGpuBindingDesc gpuBinding) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material register must run on its owner thread");
    }
    auto renderBinding = validateMaterialBinding(materialAsset, gpuBinding);
    if (!renderBinding)
    {
        return Core::failure(std::move(renderBinding.error()));
    }
    if (MaterialEntry* exact = findMaterialExact(materialAsset); exact != nullptr)
    {
        if (exact->gpuBinding == gpuBinding)
        {
            return exact->bindingKey;
        }
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Material handle is already registered with another GPU binding");
    }
    const Core::AssetId assetId = m_store->assetId(materialAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Material handle has no AssetId");
    }
    if (findMaterialByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Material AssetId already has another registered handle");
    }
    if (m_materialBindingCount >= m_materialCapacity)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free material slot");
    }
    MaterialEntry* freeEntry = findFreeMaterial();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free material slot");
    }
    auto bindingKey = m_device->createMesh3DMaterialBinding(*renderBinding);
    if (!bindingKey)
    {
        return Core::failure(std::move(bindingKey.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialBinding", "device"));
    }
    *freeEntry = MaterialEntry{.asset = materialAsset, .assetId = assetId,
                               .gpuBinding = gpuBinding, .bindingKey = *bindingKey};
    ++m_materialBindingCount;
    return *bindingKey;
}

Core::Status Mesh3DBindingRegistry::unbindMeshBinding(AssetHandle meshAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh unbind must run on its owner thread");
    }
    MeshEntry* entry = findMeshExact(meshAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound, "StaticMesh handle has no Mesh3D binding");
    }
    if (auto status = m_device->setMesh3DBinding(entry->bindingKey, {}); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::unbindMeshBinding", "device"));
    }
    *entry = MeshEntry{};
    --m_meshBindingCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::unbindMaterialBinding(AssetHandle materialAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material unbind must run on its owner thread");
    }
    MaterialEntry* entry = findMaterialExact(materialAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound, "Material handle has no Mesh3D binding");
    }
    if (auto status = m_device->clearMesh3DMaterialBinding(entry->bindingKey); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::unbindMaterialBinding", "device"));
    }
    *entry = MaterialEntry{};
    --m_materialBindingCount;
    return Core::success();
}

Core::u32 Mesh3DBindingRegistry::resolveMesh(AssetHandle meshAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return 0;
    }
    const MeshEntry* entry = findMeshExact(meshAsset);
    return entry == nullptr || !isLiveMeshEntry(*entry) ? 0U : entry->bindingKey;
}

Core::u32 Mesh3DBindingRegistry::resolveMaterial(AssetHandle materialAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return 0;
    }
    const MaterialEntry* entry = findMaterialExact(materialAsset);
    return entry == nullptr || !isLiveMaterialEntry(*entry) ? 0U : entry->bindingKey;
}

bool Mesh3DBindingRegistry::isOwnerThread() const noexcept
{
    return *this && std::this_thread::get_id() == m_ownerThread;
}

bool Mesh3DBindingRegistry::isLiveMeshEntry(const MeshEntry& entry) const noexcept
{
    return entry.bindingKey != 0 && entry.gpuMesh &&
           m_store->assetKind(entry.asset) == AssetFormat::AssetKind::StaticMesh &&
           hasRenderableCpuPayload(*m_store, entry.asset) && m_store->tryGet(entry.asset) != nullptr &&
           m_store->assetId(entry.asset) == entry.assetId;
}

bool Mesh3DBindingRegistry::isLiveMaterialEntry(const MaterialEntry& entry) const noexcept
{
    return entry.bindingKey != 0 && m_store->assetKind(entry.asset) == AssetFormat::AssetKind::Material &&
           hasRenderableCpuPayload(*m_store, entry.asset) && m_store->tryGet(entry.asset) != nullptr &&
           m_store->assetId(entry.asset) == entry.assetId &&
           isLiveTextureBinding(*m_store, entry.gpuBinding.baseColor) &&
           isLiveTextureBinding(*m_store, entry.gpuBinding.metallicRoughness) &&
           isLiveTextureBinding(*m_store, entry.gpuBinding.normal);
}

Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshExact(AssetHandle asset) noexcept
{
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshExact(AssetHandle asset) const noexcept
{
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshByAssetId(Core::AssetId assetId) noexcept
{
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findFreeMesh() noexcept
{
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey == 0) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialExact(AssetHandle asset) noexcept
{
    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialExact(AssetHandle asset) const noexcept
{
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialByAssetId(Core::AssetId assetId) noexcept
{
    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findFreeMaterial() noexcept
{
    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey == 0) return &entry;
    }
    return nullptr;
}

Core::Result<Render::Mesh3DMaterialBindingDesc> Mesh3DBindingRegistry::validateMaterialBinding(
    AssetHandle materialAsset, const Mesh3DMaterialGpuBindingDesc& gpuBinding) const noexcept
{
    if (!materialAsset || m_store->assetKind(materialAsset) != AssetFormat::AssetKind::Material)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a Material handle from its AssetStore");
    }
    if (!hasRenderableCpuPayload(*m_store, materialAsset))
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry Material CPU payload is not ready");
    }
    const CookedAssetFile* file = m_store->tryGet(materialAsset);
    if (file == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry Material CPU payload is unavailable");
    }
    auto material = parseMaterialFromCooked(*file);
    if (!material)
    {
        return Core::failure(std::move(material.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialBinding", "Material payload"));
    }

    std::array<Core::AssetId, 3> dependencyIds{};
    Core::usize dependencyCount = 0;
    for (Core::u32 index = 0; index < file->header().dependencyCount; ++index)
    {
        const auto dependency = file->dependency(index);
        if (!dependency || dependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
            dependency->flags != AssetFormat::DependencyFlags::Required || dependencyCount >= dependencyIds.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "Material dependencies must be at most three required Texture2D assets");
        }
        dependencyIds[dependencyCount++] = dependency->assetId;
    }

    const Core::usize expectedDependencyCount =
        static_cast<Core::usize>(material->hasBaseColorTexture) +
        static_cast<Core::usize>(material->hasMetallicRoughnessTexture) +
        static_cast<Core::usize>(material->hasNormalTexture);
    if (dependencyCount != expectedDependencyCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Material texture flags and cooked dependencies do not match");
    }

    Core::usize dependencyIndex = 0;
    const auto validateRole = [&](bool required, const Mesh3DMaterialTextureBinding& binding,
                                  const char* role) -> Core::Status {
        if (!required)
        {
            if (binding.textureAsset || binding.gpuTexture)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "Material GPU binding supplies an undeclared texture role");
            }
            return Core::success();
        }
        if (!binding.textureAsset || !binding.gpuTexture ||
            m_store->assetKind(binding.textureAsset) != AssetFormat::AssetKind::Texture2D)
        {
            auto failure = Core::failure(AssetErrorCode::InvalidHandle,
                                         "Material texture role requires a live Texture2D handle and GPU texture");
            return Core::failure(std::move(failure.error()).withContext(
                "Mesh3DBindingRegistry::registerMaterialBinding", role));
        }
        if (!hasRenderableCpuPayload(*m_store, binding.textureAsset) ||
            m_store->tryGet(binding.textureAsset) == nullptr)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Material texture role CPU payload is not ready");
        }
        const Core::AssetId roleId = m_store->assetId(binding.textureAsset);
        const Core::AssetId expectedRoleId = dependencyIds[dependencyIndex++];
        if (!roleId || roleId != expectedRoleId)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "Material texture role does not match its cooked required dependency");
        }
        return Core::success();
    };

    if (auto status = validateRole(material->hasBaseColorTexture, gpuBinding.baseColor, "baseColor"); !status)
        return Core::failure(std::move(status.error()));
    if (auto status = validateRole(material->hasMetallicRoughnessTexture, gpuBinding.metallicRoughness,
                                   "metallicRoughness"); !status)
        return Core::failure(std::move(status.error()));
    if (auto status = validateRole(material->hasNormalTexture, gpuBinding.normal, "normal"); !status)
        return Core::failure(std::move(status.error()));

    return Render::Mesh3DMaterialBindingDesc{
        .baseColorTexture = gpuBinding.baseColor.gpuTexture,
        .metallicRoughnessTexture = gpuBinding.metallicRoughness.gpuTexture,
        .normalTexture = gpuBinding.normal.gpuTexture,
        .metallicFactor = material->metallicFactor,
        .roughnessFactor = material->roughnessFactor,
    };
}

} // namespace Tina::Asset
