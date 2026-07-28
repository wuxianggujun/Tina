#include <tina/asset/Mesh3DBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>

#include <exception>
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

} // namespace

Mesh3DBindingRegistry::~Mesh3DBindingRegistry() noexcept
{
    if (m_meshBindingCount != 0 || m_materialBindingCount != 0 || m_textureOwnerCount != 0)
    {
        // Releasing CPU leases here would silently leak their GPU owners.
        std::terminate();
    }
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.frameBorrowCount != 0)
        {
            std::terminate();
        }
    }
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.frameBorrowCount != 0)
        {
            std::terminate();
        }
    }
}

Mesh3DBindingRegistry::Mesh3DBindingRegistry(
    AssetSystem& assets, Render::IRenderDevice& device,
    std::pmr::vector<MeshEntry> meshEntries,
    std::pmr::vector<MaterialEntry> materialEntries,
    std::pmr::vector<TextureEntry> textureEntries,
    Core::usize meshCapacity, Core::usize materialCapacity,
    Core::usize textureCapacity) noexcept
    : m_assets(&assets), m_store(&assets.store()), m_device(&device),
      m_meshEntries(std::move(meshEntries)), m_materialEntries(std::move(materialEntries)),
      m_textureEntries(std::move(textureEntries)), m_meshCapacity(meshCapacity),
      m_materialCapacity(materialCapacity), m_textureCapacity(textureCapacity),
      m_ownerThread(std::this_thread::get_id())
{
}

Mesh3DBindingRegistry::Mesh3DBindingRegistry(Mesh3DBindingRegistry&& other) noexcept
    : m_assets(std::exchange(other.m_assets, nullptr)),
      m_store(std::exchange(other.m_store, nullptr)),
      m_device(std::exchange(other.m_device, nullptr)),
      m_meshEntries(std::move(other.m_meshEntries)),
      m_materialEntries(std::move(other.m_materialEntries)),
      m_textureEntries(std::move(other.m_textureEntries)),
      m_meshCapacity(std::exchange(other.m_meshCapacity, 0)),
      m_materialCapacity(std::exchange(other.m_materialCapacity, 0)),
      m_textureCapacity(std::exchange(other.m_textureCapacity, 0)),
      m_meshBindingCount(std::exchange(other.m_meshBindingCount, 0)),
      m_materialBindingCount(std::exchange(other.m_materialBindingCount, 0)),
      m_textureOwnerCount(std::exchange(other.m_textureOwnerCount, 0)),
      m_ownerThread(std::exchange(other.m_ownerThread, {}))
{
}

Core::Result<Mesh3DBindingRegistry> Mesh3DBindingRegistry::Create(
    AssetSystem& assets, Render::IRenderDevice& device, Mesh3DBindingRegistryConfig config)
{
    if (config.meshCapacity == 0 || config.meshCapacity > MaximumMesh3DBindingCapacity ||
        config.materialCapacity == 0 || config.materialCapacity > MaximumMesh3DBindingCapacity ||
        config.textureCapacity == 0 || config.textureCapacity > MaximumMesh3DTextureCapacity)
    {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "Mesh3DBindingRegistry capacities are outside their supported ranges");
    }
    std::pmr::memory_resource* memoryResource =
        config.memoryResource != nullptr ? config.memoryResource : std::pmr::get_default_resource();
    try
    {
        std::pmr::vector<MeshEntry> meshEntries{memoryResource};
        std::pmr::vector<MaterialEntry> materialEntries{memoryResource};
        std::pmr::vector<TextureEntry> textureEntries{memoryResource};
        meshEntries.resize(config.meshCapacity);
        materialEntries.resize(config.materialCapacity);
        textureEntries.resize(config.textureCapacity);
        return Mesh3DBindingRegistry{
            assets,
            device,
            std::move(meshEntries),
            std::move(materialEntries),
            std::move(textureEntries),
            config.meshCapacity,
            config.materialCapacity,
            config.textureCapacity,
        };
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "Mesh3DBindingRegistry storage allocation failed");
    }
}

Mesh3DBindingRegistry::operator bool() const noexcept
{
    return m_assets != nullptr && m_store != nullptr && m_device != nullptr &&
           m_meshCapacity != 0 && m_materialCapacity != 0 && m_textureCapacity != 0;
}

Core::usize Mesh3DBindingRegistry::meshCapacity() const noexcept { return m_meshCapacity; }
Core::usize Mesh3DBindingRegistry::materialCapacity() const noexcept { return m_materialCapacity; }
Core::usize Mesh3DBindingRegistry::textureCapacity() const noexcept { return m_textureCapacity; }
Core::usize Mesh3DBindingRegistry::meshBindingCount() const noexcept { return m_meshBindingCount; }
Core::usize Mesh3DBindingRegistry::materialBindingCount() const noexcept { return m_materialBindingCount; }
Core::usize Mesh3DBindingRegistry::textureOwnerCount() const noexcept { return m_textureOwnerCount; }

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMeshBinding(
    AssetHandle meshAsset, Render::GpuMeshId& gpuMesh) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh register must run on its owner thread");
    }
    if (!meshAsset || m_store->assetKind(meshAsset) != AssetFormat::AssetKind::StaticMesh)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a StaticMesh handle from its AssetSystem");
    }
    if (!hasRenderableCpuPayload(*m_store, meshAsset) || m_store->tryGet(meshAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry StaticMesh CPU payload is not ready");
    }
    if (!gpuMesh)
    {
        return Core::failure(Render::RenderErrorCode::InvalidMeshUpload,
                             "Mesh3DBindingRegistry requires a live GPU mesh owner");
    }
    if (findMeshExact(meshAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "StaticMesh handle already owns a Mesh3D binding");
    }

    const Core::AssetId assetId = m_store->assetId(meshAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "StaticMesh handle has no AssetId");
    }
    if (findMeshByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "StaticMesh AssetId already has another owned binding");
    }
    if (findMeshByGpu(gpuMesh) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "GPU mesh already belongs to another Mesh3D binding");
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

    auto lease = m_assets->acquire(meshAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext(
            "Mesh3DBindingRegistry::registerMeshBinding", "lease"));
    }
    auto bindingKey = m_device->createMesh3DBinding(gpuMesh);
    if (!bindingKey)
    {
        return Core::failure(std::move(bindingKey.error()).withContext(
            "Mesh3DBindingRegistry::registerMeshBinding", "device"));
    }

    const Core::u32 candidateKey = *bindingKey;
    *freeEntry = MeshEntry{
        .asset = meshAsset,
        .assetId = assetId,
        .lease = std::move(*lease),
        .gpuMesh = gpuMesh,
        .bindingKey = candidateKey,
    };
    gpuMesh = {};
    ++m_meshBindingCount;
    return candidateKey;
}

Core::Status Mesh3DBindingRegistry::registerMaterialTexture(
    AssetHandle textureAsset, Render::GpuTextureId& gpuTexture) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry texture register must run on its owner thread");
    }
    if (!textureAsset || m_store->assetKind(textureAsset) != AssetFormat::AssetKind::Texture2D)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a Texture2D handle from its AssetSystem");
    }
    if (!hasRenderableCpuPayload(*m_store, textureAsset) || m_store->tryGet(textureAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry Texture2D CPU payload is not ready");
    }
    if (!gpuTexture)
    {
        return Core::failure(Render::RenderErrorCode::InvalidTextureUpload,
                             "Mesh3DBindingRegistry requires a live GPU texture owner");
    }
    if (findTextureExact(textureAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Texture2D handle already has an owned Material texture");
    }

    const Core::AssetId assetId = m_store->assetId(textureAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Texture2D handle has no AssetId");
    }
    if (findTextureByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Texture2D AssetId already has another owned Material texture");
    }
    if (findTextureByGpu(gpuTexture) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "GPU texture already belongs to another Material texture owner");
    }
    if (m_textureOwnerCount >= m_textureCapacity)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free Material texture slot");
    }
    TextureEntry* freeEntry = findFreeTexture();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free Material texture slot");
    }

    auto lease = m_assets->acquire(textureAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialTexture", "lease"));
    }
    if (auto status = m_device->validateTexture2D(gpuTexture); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialTexture", "device"));
    }
    *freeEntry = TextureEntry{
        .asset = textureAsset,
        .assetId = assetId,
        .lease = std::move(*lease),
        .gpuTexture = gpuTexture,
    };
    gpuTexture = {};
    ++m_textureOwnerCount;
    return Core::success();
}

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMaterialBinding(
    AssetHandle materialAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material register must run on its owner thread");
    }
    if (findMaterialExact(materialAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Material handle already owns a Mesh3D binding");
    }

    auto validated = validateMaterialBinding(materialAsset);
    if (!validated)
    {
        return Core::failure(std::move(validated.error()));
    }
    const Core::AssetId assetId = m_store->assetId(materialAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Material handle has no AssetId");
    }
    if (findMaterialByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Material AssetId already has another owned binding");
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

    auto lease = m_assets->acquire(materialAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialBinding", "lease"));
    }
    auto bindingKey = m_device->createMesh3DMaterialBinding(validated->renderBinding);
    if (!bindingKey)
    {
        return Core::failure(std::move(bindingKey.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialBinding", "device"));
    }

    *freeEntry = MaterialEntry{
        .asset = materialAsset,
        .assetId = assetId,
        .lease = std::move(*lease),
        .textureIndices = validated->textureIndices,
        .textureCount = validated->textureCount,
        .bindingKey = *bindingKey,
    };
    for (Core::u32 roleIndex = 0; roleIndex < validated->textureCount; ++roleIndex)
    {
        TextureEntry& texture = m_textureEntries[validated->textureIndices[roleIndex]];
        if (texture.materialReferenceCount == (std::numeric_limits<Core::u32>::max)())
        {
            std::terminate();
        }
        ++texture.materialReferenceCount;
    }
    ++m_materialBindingCount;
    return *bindingKey;
}

bool Mesh3DBindingRegistry::hasMaterialTexture(AssetHandle textureAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return false;
    }
    const TextureEntry* entry = findTextureExact(textureAsset);
    return entry != nullptr && isLiveTextureEntry(*entry);
}

Core::Status Mesh3DBindingRegistry::retireMeshBinding(AssetHandle meshAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh retirement must run on its owner thread");
    }
    MeshEntry* entry = findMeshExact(meshAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound,
                             "StaticMesh handle has no owned Mesh3D binding");
    }
    if (entry->frameBorrowCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D geometry binding is still borrowed by an active frame resource");
    }
    if (auto status = m_assets->retireStaticMesh(*m_device, entry->lease, entry->gpuMesh); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::retireMeshBinding", "assets"));
    }
    *entry = MeshEntry{};
    --m_meshBindingCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::retireMaterialBinding(AssetHandle materialAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material retirement must run on its owner thread");
    }
    MaterialEntry* entry = findMaterialExact(materialAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound,
                             "Material handle has no owned Mesh3D binding");
    }
    if (entry->frameBorrowCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D material binding is still borrowed by an active frame resource");
    }
    if (auto status = m_device->clearMesh3DMaterialBinding(entry->bindingKey); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::retireMaterialBinding", "device"));
    }
    if (auto status = m_assets->unload(entry->asset); !status)
    {
        // The retained exact lease makes unload infallible after the device
        // binding has been cleared. Continuing would strand a split owner.
        std::terminate();
    }
    for (Core::u32 roleIndex = 0; roleIndex < entry->textureCount; ++roleIndex)
    {
        TextureEntry& texture = m_textureEntries[entry->textureIndices[roleIndex]];
        if (texture.materialReferenceCount == 0)
        {
            std::terminate();
        }
        --texture.materialReferenceCount;
    }
    *entry = MaterialEntry{};
    --m_materialBindingCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::retireMaterialTexture(AssetHandle textureAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry texture retirement must run on its owner thread");
    }
    TextureEntry* entry = findTextureExact(textureAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound,
                             "Texture2D handle has no owned Material texture");
    }
    if (entry->materialReferenceCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Material texture is still referenced by a live Material binding");
    }
    if (auto status = m_assets->retireTexture2D(*m_device, entry->lease, entry->gpuTexture); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::retireMaterialTexture", "assets"));
    }
    *entry = TextureEntry{};
    --m_textureOwnerCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::retireAllBindings() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry retirement must run on its owner thread");
    }
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Mesh3D geometry binding is still borrowed by an active frame resource");
        }
    }
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Mesh3D material binding is still borrowed by an active frame resource");
        }
    }

    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey == 0)
        {
            continue;
        }
        if (auto status = retireMaterialBinding(entry.asset); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::retireAllBindings", "material"));
        }
    }
    for (TextureEntry& entry : m_textureEntries)
    {
        if (!entry.gpuTexture)
        {
            continue;
        }
        if (auto status = retireMaterialTexture(entry.asset); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::retireAllBindings", "texture"));
        }
    }
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey == 0)
        {
            continue;
        }
        if (auto status = retireMeshBinding(entry.asset); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::retireAllBindings", "mesh"));
        }
    }
    return Core::success();
}

Core::Result<Render::FrameResourceRef> Mesh3DBindingRegistry::internMeshFrameResource(
    AssetHandle meshAsset, Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh intern must run on its owner thread");
    }
    MeshEntry* entry = findMeshExact(meshAsset);
    if (entry == nullptr || !isLiveMeshEntry(*entry))
    {
        return Render::FrameResourceRef{};
    }
    if (entry->frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D geometry binding cannot acquire another frame borrow");
    }
    ++entry->frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, entry->bindingKey, entry,
                         &Mesh3DBindingRegistry::releaseMeshFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Mesh3DGeometry,
            .deviceBindingKey = entry->bindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(std::move(resource.error()).withContext(
            "Mesh3DBindingRegistry::internMeshFrameResource", "sink"));
    }
    return *resource;
}

Core::Result<Render::FrameResourceRef> Mesh3DBindingRegistry::internMaterialFrameResource(
    AssetHandle materialAsset, Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material intern must run on its owner thread");
    }
    MaterialEntry* entry = findMaterialExact(materialAsset);
    if (entry == nullptr || !isLiveMaterialEntry(*entry))
    {
        return Render::FrameResourceRef{};
    }
    if (entry->frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D material binding cannot acquire another frame borrow");
    }
    ++entry->frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, entry->bindingKey, entry,
                         &Mesh3DBindingRegistry::releaseMaterialFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Mesh3DMaterial,
            .deviceBindingKey = entry->bindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(std::move(resource.error()).withContext(
            "Mesh3DBindingRegistry::internMaterialFrameResource", "sink"));
    }
    return *resource;
}

void Mesh3DBindingRegistry::releaseMeshFrameBorrow(void* userData) noexcept
{
    auto* entry = static_cast<MeshEntry*>(userData);
    if (entry == nullptr || entry->frameBorrowCount == 0)
    {
        std::terminate();
    }
    --entry->frameBorrowCount;
}

void Mesh3DBindingRegistry::releaseMaterialFrameBorrow(void* userData) noexcept
{
    auto* entry = static_cast<MaterialEntry*>(userData);
    if (entry == nullptr || entry->frameBorrowCount == 0)
    {
        std::terminate();
    }
    --entry->frameBorrowCount;
}

bool Mesh3DBindingRegistry::isOwnerThread() const noexcept
{
    return *this && std::this_thread::get_id() == m_ownerThread;
}

bool Mesh3DBindingRegistry::isLiveMeshEntry(const MeshEntry& entry) const noexcept
{
    return entry.bindingKey != 0 && entry.lease && entry.gpuMesh &&
           m_store->assetKind(entry.asset) == AssetFormat::AssetKind::StaticMesh &&
           hasRenderableCpuPayload(*m_store, entry.asset) && m_store->tryGet(entry.asset) != nullptr &&
           m_store->assetId(entry.asset) == entry.assetId;
}

bool Mesh3DBindingRegistry::isLiveMaterialEntry(const MaterialEntry& entry) const noexcept
{
    if (entry.bindingKey == 0 || !entry.lease ||
        m_store->assetKind(entry.asset) != AssetFormat::AssetKind::Material ||
        !hasRenderableCpuPayload(*m_store, entry.asset) || m_store->tryGet(entry.asset) == nullptr ||
        m_store->assetId(entry.asset) != entry.assetId)
    {
        return false;
    }
    for (Core::u32 roleIndex = 0; roleIndex < entry.textureCount; ++roleIndex)
    {
        if (entry.textureIndices[roleIndex] >= m_textureEntries.size() ||
            !isLiveTextureEntry(m_textureEntries[entry.textureIndices[roleIndex]]))
        {
            return false;
        }
    }
    return true;
}

bool Mesh3DBindingRegistry::isLiveTextureEntry(const TextureEntry& entry) const noexcept
{
    return entry.lease && entry.gpuTexture &&
           m_store->assetKind(entry.asset) == AssetFormat::AssetKind::Texture2D &&
           hasRenderableCpuPayload(*m_store, entry.asset) && m_store->tryGet(entry.asset) != nullptr &&
           m_store->assetId(entry.asset) == entry.assetId;
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

const Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshByGpu(
    Render::GpuMeshId gpuMesh) const noexcept
{
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.gpuMesh == gpuMesh) return &entry;
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

const Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialExact(
    AssetHandle asset) const noexcept
{
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialByAssetId(
    Core::AssetId assetId) noexcept
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

Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureExact(AssetHandle asset) noexcept
{
    for (TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.asset == asset) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureExact(
    AssetHandle asset) const noexcept
{
    for (const TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureByAssetId(
    Core::AssetId assetId) noexcept
{
    for (TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureByAssetId(
    Core::AssetId assetId) const noexcept
{
    for (const TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureByGpu(
    Render::GpuTextureId gpuTexture) const noexcept
{
    for (const TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture == gpuTexture) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findFreeTexture() noexcept
{
    for (TextureEntry& entry : m_textureEntries)
    {
        if (!entry.gpuTexture) return &entry;
    }
    return nullptr;
}

Core::u32 Mesh3DBindingRegistry::textureIndex(const TextureEntry& entry) const noexcept
{
    return static_cast<Core::u32>(&entry - m_textureEntries.data());
}

Core::Result<Mesh3DBindingRegistry::ValidatedMaterialBinding>
Mesh3DBindingRegistry::validateMaterialBinding(AssetHandle materialAsset) const noexcept
{
    if (!materialAsset || m_store->assetKind(materialAsset) != AssetFormat::AssetKind::Material)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a Material handle from its AssetSystem");
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
    Core::u32 dependencyCount = 0;
    for (Core::u32 index = 0; index < file->header().dependencyCount; ++index)
    {
        const auto dependency = file->dependency(index);
        if (!dependency || dependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
            dependency->flags != AssetFormat::DependencyFlags::Required ||
            dependencyCount >= dependencyIds.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "Material dependencies must be at most three required Texture2D assets");
        }
        dependencyIds[dependencyCount++] = dependency->assetId;
    }

    const Core::u32 expectedDependencyCount =
        static_cast<Core::u32>(material->hasBaseColorTexture) +
        static_cast<Core::u32>(material->hasMetallicRoughnessTexture) +
        static_cast<Core::u32>(material->hasNormalTexture);
    if (dependencyCount != expectedDependencyCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Material texture flags and cooked dependencies do not match");
    }

    ValidatedMaterialBinding validated{
        .renderBinding = Render::Mesh3DMaterialBindingDesc{
            .metallicFactor = material->metallicFactor,
            .roughnessFactor = material->roughnessFactor,
        },
    };
    Core::u32 dependencyIndex = 0;
    const auto resolveRole = [&](bool required, Render::GpuTextureId& gpuTexture,
                                 const char* role) -> Core::Status {
        if (!required)
        {
            return Core::success();
        }
        const TextureEntry* texture = findTextureByAssetId(dependencyIds[dependencyIndex++]);
        if (texture == nullptr || !isLiveTextureEntry(*texture))
        {
            auto failure = Core::failure(
                AssetErrorCode::AssetNotReady,
                "Material texture dependency has no live shared GPU owner");
            return Core::failure(std::move(failure.error()).withContext(
                "Mesh3DBindingRegistry::registerMaterialBinding", role));
        }
        const Core::u32 index = textureIndex(*texture);
        for (Core::u32 prior = 0; prior < validated.textureCount; ++prior)
        {
            if (validated.textureIndices[prior] == index)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "Material texture roles must use distinct cooked dependencies");
            }
        }
        validated.textureIndices[validated.textureCount++] = index;
        gpuTexture = texture->gpuTexture;
        return Core::success();
    };

    if (auto status = resolveRole(material->hasBaseColorTexture,
                                  validated.renderBinding.baseColorTexture, "baseColor");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = resolveRole(material->hasMetallicRoughnessTexture,
                                  validated.renderBinding.metallicRoughnessTexture,
                                  "metallicRoughness");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = resolveRole(material->hasNormalTexture,
                                  validated.renderBinding.normalTexture, "normal");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return validated;
}

} // namespace Tina::Asset
