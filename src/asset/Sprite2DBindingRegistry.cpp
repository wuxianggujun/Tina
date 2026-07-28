#include <tina/asset/Sprite2DBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>

#include <exception>
#include <limits>
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

Sprite2DBindingRegistry::~Sprite2DBindingRegistry() noexcept
{
    if (m_bindingCount != 0)
    {
        // Entry destruction would release the CPU lease while silently leaking
        // its GPU owner. Callers must explicitly retire every binding first.
        std::terminate();
    }
    for (const Entry& entry : m_entries)
    {
        if (entry.frameBorrowCount != 0)
        {
            // FramePin callbacks point at fixed registry entries. Destroying that
            // storage while a packet still owns a borrow would turn completion
            // into a use-after-free.
            std::terminate();
        }
    }
}

Sprite2DBindingRegistry::Sprite2DBindingRegistry(AssetSystem& assets, Render::IRenderDevice& device,
                                                 std::pmr::vector<Entry> entries, Core::usize capacity) noexcept
    : m_assets(&assets), m_store(&assets.store()), m_device(&device), m_entries(std::move(entries)), m_capacity(capacity),
      m_ownerThread(std::this_thread::get_id())
{
}

Sprite2DBindingRegistry::Sprite2DBindingRegistry(Sprite2DBindingRegistry&& other) noexcept
    : m_assets(std::exchange(other.m_assets, nullptr)), m_store(std::exchange(other.m_store, nullptr)),
      m_device(std::exchange(other.m_device, nullptr)),
      m_entries(std::move(other.m_entries)), m_capacity(std::exchange(other.m_capacity, 0)),
      m_bindingCount(std::exchange(other.m_bindingCount, 0)), m_ownerThread(std::exchange(other.m_ownerThread, {}))
{
}

Core::Result<Sprite2DBindingRegistry> Sprite2DBindingRegistry::Create(AssetSystem& assets,
                                                                      Render::IRenderDevice& device,
                                                                      Sprite2DBindingRegistryConfig config)
{
    if (config.textureCapacity == 0 || config.textureCapacity > MaximumSprite2DBindingCapacity)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Sprite2DBindingRegistry textureCapacity must be in [1, 4096]");
    }
    std::pmr::memory_resource* memoryResource =
        config.memoryResource != nullptr ? config.memoryResource : std::pmr::get_default_resource();
    try
    {
        std::pmr::vector<Entry> entries{memoryResource};
        entries.resize(config.textureCapacity);
        return Sprite2DBindingRegistry{assets, device, std::move(entries), config.textureCapacity};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "Sprite2DBindingRegistry storage allocation failed");
    }
}

Sprite2DBindingRegistry::operator bool() const noexcept
{
    return m_assets != nullptr && m_store != nullptr && m_device != nullptr && m_capacity != 0;
}

Core::usize Sprite2DBindingRegistry::capacity() const noexcept
{
    return m_capacity;
}

Core::usize Sprite2DBindingRegistry::bindingCount() const noexcept
{
    return m_bindingCount;
}

Core::Result<Core::u32> Sprite2DBindingRegistry::registerTextureBinding(AssetHandle textureAsset,
                                                                        Render::GpuTextureId& gpuTexture) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry register must run on its owner thread");
    }
    if (!textureAsset || m_store->assetKind(textureAsset) != AssetFormat::AssetKind::Texture2D)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Sprite2DBindingRegistry requires a Texture2D handle from its AssetStore");
    }
    if (!hasRenderableCpuPayload(*m_store, textureAsset) || m_store->tryGet(textureAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Sprite2DBindingRegistry Texture2D CPU payload is not ready");
    }
    if (!gpuTexture)
    {
        return Core::failure(Render::RenderErrorCode::InvalidTextureUpload,
                             "Sprite2DBindingRegistry requires a live GPU texture");
    }

    if (findExact(textureAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::SpriteBindingConflict,
                             "Texture2D handle already owns a Sprite2D binding");
    }

    const Core::AssetId textureAssetId = m_store->assetId(textureAsset);
    if (!textureAssetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Texture2D handle has no AssetId");
    }
    if (findByAssetId(textureAssetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::SpriteBindingConflict,
                             "Texture2D AssetId already has another registered handle");
    }
    if (findByGpuTexture(gpuTexture) != nullptr)
    {
        return Core::failure(AssetErrorCode::SpriteBindingConflict,
                             "GPU texture already belongs to another Sprite2D binding");
    }
    if (m_bindingCount >= m_capacity)
    {
        return Core::failure(AssetErrorCode::SpriteBindingCapacityExceeded,
                             "Sprite2DBindingRegistry has no free texture slot");
    }
    Entry* freeEntry = findFree();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::SpriteBindingCapacityExceeded,
                             "Sprite2DBindingRegistry has no free texture slot");
    }

    auto lease = m_assets->acquire(textureAsset);
    if (!lease)
    {
        return Core::failure(
            std::move(lease.error()).withContext("Sprite2DBindingRegistry::registerTextureBinding", "lease"));
    }

    auto bindingKey = m_device->createSprite2DTextureBinding(gpuTexture);
    if (!bindingKey)
    {
        if (bindingKey.error().code == Render::RenderErrorCode::SpriteBindingKeyExhausted)
        {
            return Core::failure(AssetErrorCode::SpriteBindingKeyExhausted,
                                 "Sprite2DBindingRegistry exhausted device binding keys");
        }
        return Core::failure(
            std::move(bindingKey.error()).withContext("Sprite2DBindingRegistry::registerTextureBinding", "device"));
    }

    const Core::u32 candidateKey = *bindingKey;
    *freeEntry = Entry{
        .textureAsset = textureAsset,
        .textureAssetId = textureAssetId,
        .lease = std::move(*lease),
        .gpuTexture = gpuTexture,
        .bindingKey = candidateKey,
    };
    gpuTexture = {};
    ++m_bindingCount;
    return candidateKey;
}

Core::Status Sprite2DBindingRegistry::retireTextureBinding(AssetHandle textureAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry retirement must run on its owner thread");
    }
    Entry* entry = findExact(textureAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::SpriteBindingNotFound, "Texture2D handle has no Sprite2D binding");
    }
    if (entry->frameBorrowCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Sprite2D binding is still borrowed by an active frame resource");
    }
    if (auto status = m_assets->retireTexture2D(*m_device, entry->lease, entry->gpuTexture); !status)
    {
        return Core::failure(
            std::move(status.error()).withContext("Sprite2DBindingRegistry::retireTextureBinding", "assets"));
    }
    *entry = Entry{};
    --m_bindingCount;
    return Core::success();
}

Core::Status Sprite2DBindingRegistry::retireAllTextureBindings() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry retirement must run on its owner thread");
    }
    for (const Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Sprite2D binding is still borrowed by an active frame resource");
        }
    }
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey == 0)
        {
            continue;
        }
        if (auto status = m_assets->retireTexture2D(*m_device, entry.lease, entry.gpuTexture); !status)
        {
            return Core::failure(
                std::move(status.error()).withContext("Sprite2DBindingRegistry::retireAllTextureBindings", "assets"));
        }
        entry = Entry{};
        --m_bindingCount;
    }
    return Core::success();
}

Core::u32 Sprite2DBindingRegistry::bindingKey(AssetHandle textureAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return 0;
    }
    const Entry* entry = findExact(textureAsset);
    return entry == nullptr || !isLiveTextureEntry(*entry) ? 0U : entry->bindingKey;
}

Core::u32 Sprite2DBindingRegistry::resolveSprite(AssetHandle spriteAsset) const noexcept
{
    return resolveSingleTextureDependency(spriteAsset, AssetFormat::AssetKind::Sprite);
}

Core::u32 Sprite2DBindingRegistry::resolveTileset(AssetHandle tilesetAsset) const noexcept
{
    return resolveSingleTextureDependency(tilesetAsset, AssetFormat::AssetKind::Tileset);
}

Core::Result<Render::FrameResourceRef>
Sprite2DBindingRegistry::internSpriteFrameResource(AssetHandle spriteAsset,
                                                   Render::FrameResourceSink& sink) noexcept
{
    return internSingleTextureDependency(spriteAsset, AssetFormat::AssetKind::Sprite, sink);
}

Core::Result<Render::FrameResourceRef>
Sprite2DBindingRegistry::internTilesetFrameResource(AssetHandle tilesetAsset,
                                                    Render::FrameResourceSink& sink) noexcept
{
    return internSingleTextureDependency(tilesetAsset, AssetFormat::AssetKind::Tileset, sink);
}

const Sprite2DBindingRegistry::Entry* Sprite2DBindingRegistry::resolveSingleTextureDependencyEntry(
    AssetHandle asset,
    AssetFormat::AssetKind expectedKind) const noexcept
{
    if (!isOwnerThread() || !asset || m_store->assetKind(asset) != expectedKind ||
        !hasRenderableCpuPayload(*m_store, asset))
    {
        return nullptr;
    }
    const CookedAssetFile* file = m_store->tryGet(asset);
    if (file == nullptr || file->header().dependencyCount != 1U)
    {
        return nullptr;
    }
    const auto textureDependency = file->dependency(0);
    if (!textureDependency || textureDependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
        textureDependency->flags != AssetFormat::DependencyFlags::Required)
    {
        return nullptr;
    }
    const Entry* entry = findByAssetId(textureDependency->assetId);
    if (entry == nullptr || !isLiveTextureEntry(*entry))
    {
        return nullptr;
    }
    return entry;
}

Core::u32 Sprite2DBindingRegistry::resolveSingleTextureDependency(
    AssetHandle asset,
    AssetFormat::AssetKind expectedKind) const noexcept
{
    const Entry* entry = resolveSingleTextureDependencyEntry(asset, expectedKind);
    return entry == nullptr ? 0U : entry->bindingKey;
}

Core::Result<Render::FrameResourceRef> Sprite2DBindingRegistry::internSingleTextureDependency(
    AssetHandle asset,
    AssetFormat::AssetKind expectedKind,
    Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry frame resource intern must run on its owner thread");
    }

    const Entry* resolvedEntry = resolveSingleTextureDependencyEntry(asset, expectedKind);
    if (resolvedEntry == nullptr)
    {
        return Render::FrameResourceRef{};
    }
    Entry* entry = findExact(resolvedEntry->textureAsset);
    if (entry == nullptr || entry->frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Sprite2D binding cannot acquire another frame resource borrow");
    }

    ++entry->frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, entry->bindingKey, entry,
                         &Sprite2DBindingRegistry::releaseFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Sprite2DTexture,
            .deviceBindingKey = entry->bindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(
            std::move(resource.error()).withContext("Sprite2DBindingRegistry::internFrameResource", "sink"));
    }
    return *resource;
}

void Sprite2DBindingRegistry::releaseFrameBorrow(void* userData) noexcept
{
    auto* entry = static_cast<Entry*>(userData);
    if (entry == nullptr || entry->frameBorrowCount == 0)
    {
        std::terminate();
    }
    --entry->frameBorrowCount;
}

bool Sprite2DBindingRegistry::isOwnerThread() const noexcept
{
    return *this && std::this_thread::get_id() == m_ownerThread;
}

Sprite2DBindingRegistry::Entry* Sprite2DBindingRegistry::findExact(AssetHandle textureAsset) noexcept
{
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.textureAsset == textureAsset)
        {
            return &entry;
        }
    }
    return nullptr;
}

const Sprite2DBindingRegistry::Entry* Sprite2DBindingRegistry::findExact(AssetHandle textureAsset) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.textureAsset == textureAsset)
        {
            return &entry;
        }
    }
    return nullptr;
}

Sprite2DBindingRegistry::Entry* Sprite2DBindingRegistry::findByAssetId(Core::AssetId textureAssetId) noexcept
{
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.textureAssetId == textureAssetId)
        {
            return &entry;
        }
    }
    return nullptr;
}

const Sprite2DBindingRegistry::Entry*
Sprite2DBindingRegistry::findByAssetId(Core::AssetId textureAssetId) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.textureAssetId == textureAssetId)
        {
            return &entry;
        }
    }
    return nullptr;
}

const Sprite2DBindingRegistry::Entry*
Sprite2DBindingRegistry::findByGpuTexture(Render::GpuTextureId gpuTexture) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.gpuTexture == gpuTexture)
        {
            return &entry;
        }
    }
    return nullptr;
}

Sprite2DBindingRegistry::Entry* Sprite2DBindingRegistry::findFree() noexcept
{
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey == 0)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool Sprite2DBindingRegistry::isLiveTextureEntry(const Entry& entry) const noexcept
{
    return entry.bindingKey != 0 && entry.lease && entry.gpuTexture &&
           m_store->assetKind(entry.textureAsset) == AssetFormat::AssetKind::Texture2D &&
           hasRenderableCpuPayload(*m_store, entry.textureAsset) && m_store->tryGet(entry.textureAsset) != nullptr &&
           m_store->assetId(entry.textureAsset) == entry.textureAssetId;
}

} // namespace Tina::Asset
