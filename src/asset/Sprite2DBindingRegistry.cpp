#include <tina/asset/Sprite2DBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/render/RenderErrors.hpp>

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

Sprite2DBindingRegistry::Sprite2DBindingRegistry(AssetStore& store, Render::IRenderDevice& device,
                                                 std::pmr::vector<Entry> entries, Core::usize capacity) noexcept
    : m_store(&store), m_device(&device), m_entries(std::move(entries)), m_capacity(capacity),
      m_ownerThread(std::this_thread::get_id())
{
}

Sprite2DBindingRegistry::Sprite2DBindingRegistry(Sprite2DBindingRegistry&& other) noexcept
    : m_store(std::exchange(other.m_store, nullptr)), m_device(std::exchange(other.m_device, nullptr)),
      m_entries(std::move(other.m_entries)), m_capacity(std::exchange(other.m_capacity, 0)),
      m_bindingCount(std::exchange(other.m_bindingCount, 0)), m_ownerThread(std::exchange(other.m_ownerThread, {}))
{
}

Core::Result<Sprite2DBindingRegistry> Sprite2DBindingRegistry::Create(AssetStore& store, Render::IRenderDevice& device,
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
        return Sprite2DBindingRegistry{store, device, std::move(entries), config.textureCapacity};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "Sprite2DBindingRegistry storage allocation failed");
    }
}

Sprite2DBindingRegistry::operator bool() const noexcept
{
    return m_store != nullptr && m_device != nullptr && m_capacity != 0;
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
                                                                        Render::GpuTextureId gpuTexture) noexcept
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

    if (Entry* exact = findExact(textureAsset); exact != nullptr)
    {
        if (exact->gpuTexture == gpuTexture)
        {
            return exact->bindingKey;
        }
        return Core::failure(AssetErrorCode::SpriteBindingConflict,
                             "Texture2D handle is already registered with another GPU texture");
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
        .gpuTexture = gpuTexture,
        .bindingKey = candidateKey,
    };
    ++m_bindingCount;
    return candidateKey;
}

Core::Status Sprite2DBindingRegistry::unbindTextureBinding(AssetHandle textureAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry unbind must run on its owner thread");
    }
    Entry* entry = findExact(textureAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::SpriteBindingNotFound, "Texture2D handle has no Sprite2D binding");
    }
    if (auto status = m_device->setSprite2DTextureBinding(entry->bindingKey, {}); !status)
    {
        return Core::failure(
            std::move(status.error()).withContext("Sprite2DBindingRegistry::unbindTextureBinding", "device"));
    }
    *entry = Entry{};
    --m_bindingCount;
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
    if (!isOwnerThread() || !spriteAsset || m_store->assetKind(spriteAsset) != AssetFormat::AssetKind::Sprite ||
        !hasRenderableCpuPayload(*m_store, spriteAsset))
    {
        return 0;
    }
    const CookedAssetFile* spriteFile = m_store->tryGet(spriteAsset);
    if (spriteFile == nullptr || spriteFile->header().dependencyCount != 1U)
    {
        return 0;
    }
    const auto textureDependency = spriteFile->dependency(0);
    if (!textureDependency || textureDependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
        textureDependency->flags != AssetFormat::DependencyFlags::Required)
    {
        return 0;
    }
    const Entry* entry = findByAssetId(textureDependency->assetId);
    if (entry == nullptr || !isLiveTextureEntry(*entry))
    {
        return 0;
    }
    return entry->bindingKey;
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
    return entry.bindingKey != 0 && entry.gpuTexture &&
           m_store->assetKind(entry.textureAsset) == AssetFormat::AssetKind::Texture2D &&
           hasRenderableCpuPayload(*m_store, entry.textureAsset) && m_store->tryGet(entry.textureAsset) != nullptr &&
           m_store->assetId(entry.textureAsset) == entry.textureAssetId;
}

} // namespace Tina::Asset
