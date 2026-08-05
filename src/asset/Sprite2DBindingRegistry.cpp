#include <tina/asset/Sprite2DBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>

#include <algorithm>
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
    if (m_bindingCount != 0 || m_preparedCount != 0 || m_pendingRetirementCount != 0)
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
                                                 std::pmr::vector<Entry> entries,
                                                 std::pmr::vector<PreparedEntry> preparedEntries,
                                                 std::pmr::vector<PendingRetirement> pendingRetirements,
                                                 Core::usize capacity) noexcept
    : m_assets(&assets), m_store(&assets.store()), m_device(&device), m_entries(std::move(entries)),
      m_preparedEntries(std::move(preparedEntries)), m_pendingRetirements(std::move(pendingRetirements)),
      m_capacity(capacity), m_ownerThread(std::this_thread::get_id())
{
}

Sprite2DBindingRegistry::Sprite2DBindingRegistry(Sprite2DBindingRegistry&& other) noexcept
    : m_assets(std::exchange(other.m_assets, nullptr)), m_store(std::exchange(other.m_store, nullptr)),
      m_device(std::exchange(other.m_device, nullptr)),
      m_entries(std::move(other.m_entries)), m_preparedEntries(std::move(other.m_preparedEntries)),
      m_pendingRetirements(std::move(other.m_pendingRetirements)), m_capacity(std::exchange(other.m_capacity, 0)),
      m_bindingCount(std::exchange(other.m_bindingCount, 0)),
      m_preparedCount(std::exchange(other.m_preparedCount, 0)),
      m_pendingRetirementCount(std::exchange(other.m_pendingRetirementCount, 0)),
      m_ownerThread(std::exchange(other.m_ownerThread, {}))
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
        std::pmr::vector<PreparedEntry> preparedEntries{memoryResource};
        std::pmr::vector<PendingRetirement> pendingRetirements{memoryResource};
        entries.resize(config.textureCapacity);
        preparedEntries.resize(config.textureCapacity);
        pendingRetirements.resize(config.textureCapacity);
        return Sprite2DBindingRegistry{assets, device, std::move(entries), std::move(preparedEntries),
                                       std::move(pendingRetirements), config.textureCapacity};
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

Core::usize Sprite2DBindingRegistry::pendingRetirementCount() const noexcept
{
    return m_pendingRetirementCount;
}

Core::Status Sprite2DBindingRegistry::prepareCatalogReload(
    AssetSystem& owner, std::span<const CatalogResidentMigration> migrations) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry migration must run on its owner thread");
    }
    if (m_assets != &owner)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Sprite2DBindingRegistry belongs to another AssetSystem");
    }
    if (m_preparedCount != 0)
    {
        return Core::failure(AssetErrorCode::CatalogReloadBusy,
                             "Sprite2DBindingRegistry already has a prepared catalog migration");
    }
    if (auto status = drainPendingRetirements(); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Sprite2DBindingRegistry::prepareCatalogReload", "pendingRetirement"));
    }

    Core::usize affectedCount = 0;
    for (Core::u32 entryIndex = 0; entryIndex < static_cast<Core::u32>(m_entries.size()); ++entryIndex)
    {
        const Entry& entry = m_entries[entryIndex];
        if (entry.bindingKey == 0)
        {
            continue;
        }
        const auto migration = std::find_if(
            migrations.begin(), migrations.end(),
            [&entry](const CatalogResidentMigration& candidate) {
                return candidate.assetId == entry.textureAssetId;
            });
        if (migration == migrations.end())
        {
            continue;
        }
        if (entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Sprite2D binding is still borrowed by an active frame resource");
        }
        if (migration->kind == CatalogResidentMigrationKind::LoadedDependency ||
            migration->previous != entry.textureAsset)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "Sprite2DBindingRegistry migration does not match its active handle");
        }
        ++affectedCount;
    }
    if (m_pendingRetirementCount > m_pendingRetirements.size() ||
        affectedCount > m_pendingRetirements.size() - m_pendingRetirementCount)
    {
        return Core::failure(AssetErrorCode::CatalogCapacityExceeded,
                             "Sprite2DBindingRegistry has no pending retirement headroom");
    }

    const auto rollback = [&]() noexcept {
        abortPreparedCatalogReload();
    };
    try
    {
        for (Core::u32 entryIndex = 0; entryIndex < static_cast<Core::u32>(m_entries.size()); ++entryIndex)
        {
            const Entry& entry = m_entries[entryIndex];
            if (entry.bindingKey == 0)
            {
                continue;
            }
            const auto migrationIt = std::find_if(
                migrations.begin(), migrations.end(),
                [&entry](const CatalogResidentMigration& candidate) {
                    return candidate.assetId == entry.textureAssetId;
                });
            if (migrationIt == migrations.end())
            {
                continue;
            }

            PreparedEntry& prepared = m_preparedEntries[m_preparedCount++];
            prepared = PreparedEntry{
                .entryIndex = entryIndex,
                .remove = migrationIt->kind == CatalogResidentMigrationKind::Removed,
            };
            if (prepared.remove)
            {
                if (migrationIt->current)
                {
                    rollback();
                    return Core::failure(AssetErrorCode::InvalidHandle,
                                         "removed Sprite2D migration unexpectedly has a current handle");
                }
                continue;
            }
            if (migrationIt->kind != CatalogResidentMigrationKind::Replaced || !migrationIt->current ||
                migrationIt->current == entry.textureAsset ||
                m_store->assetKind(migrationIt->current) != AssetFormat::AssetKind::Texture2D ||
                !hasRenderableCpuPayload(*m_store, migrationIt->current) ||
                m_store->assetId(migrationIt->current) != entry.textureAssetId ||
                m_store->tryGet(migrationIt->current) == nullptr)
            {
                rollback();
                return Core::failure(AssetErrorCode::InvalidHandle,
                                     "replacement Sprite2D migration has an invalid Texture2D generation");
            }

            auto lease = m_assets->acquire(migrationIt->current);
            if (!lease)
            {
                rollback();
                return Core::failure(std::move(lease.error()).withContext(
                    "Sprite2DBindingRegistry::prepareCatalogReload", "lease"));
            }
            auto gpuTexture = uploadTexture2DFromCooked(*m_device, *m_store->tryGet(migrationIt->current));
            if (!gpuTexture)
            {
                rollback();
                return Core::failure(std::move(gpuTexture.error()).withContext(
                    "Sprite2DBindingRegistry::prepareCatalogReload", "upload"));
            }
            prepared.replacement = Entry{
                .textureAsset = migrationIt->current,
                .textureAssetId = entry.textureAssetId,
                .lease = std::move(*lease),
                .gpuTexture = *gpuTexture,
            };
            auto bindingKey = m_device->createTexture2DBinding(prepared.replacement.gpuTexture);
            if (!bindingKey)
            {
                rollback();
                return Core::failure(std::move(bindingKey.error()).withContext(
                    "Sprite2DBindingRegistry::prepareCatalogReload", "binding"));
            }
            prepared.replacement.bindingKey = *bindingKey;
        }
    }
    catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "Sprite2DBindingRegistry catalog migration allocation failed");
    }
    return Core::success();
}

void Sprite2DBindingRegistry::commitPreparedCatalogReload() noexcept
{
    for (Core::usize preparedIndex = 0; preparedIndex < m_preparedCount; ++preparedIndex)
    {
        PreparedEntry& prepared = m_preparedEntries[preparedIndex];
        if (prepared.entryIndex >= m_entries.size())
        {
            std::terminate();
        }
        Entry& active = m_entries[prepared.entryIndex];
        if (active.bindingKey == 0 || active.frameBorrowCount != 0 ||
            m_pendingRetirementCount >= m_pendingRetirements.size())
        {
            std::terminate();
        }
        PendingRetirement& pending = m_pendingRetirements[m_pendingRetirementCount++];
        pending.lease = std::move(active.lease);
        pending.gpuTexture = active.gpuTexture;
        active.gpuTexture = {};
        if (prepared.remove)
        {
            active = Entry{};
            --m_bindingCount;
        }
        else
        {
            active = std::move(prepared.replacement);
        }
        prepared = PreparedEntry{};
    }
    m_preparedCount = 0;
}

void Sprite2DBindingRegistry::abortPreparedCatalogReload() noexcept
{
    for (Core::usize preparedIndex = 0; preparedIndex < m_preparedCount; ++preparedIndex)
    {
        PreparedEntry& prepared = m_preparedEntries[preparedIndex];
        if (prepared.replacement.gpuTexture)
        {
            if (m_pendingRetirementCount >= m_pendingRetirements.size())
            {
                std::terminate();
            }
            PendingRetirement& pending = m_pendingRetirements[m_pendingRetirementCount++];
            pending.lease = std::move(prepared.replacement.lease);
            pending.gpuTexture = prepared.replacement.gpuTexture;
            prepared.replacement.gpuTexture = {};
        }
        prepared = PreparedEntry{};
    }
    m_preparedCount = 0;
    (void)drainPendingRetirements();
}

Core::Status Sprite2DBindingRegistry::drainPendingRetirements() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Sprite2DBindingRegistry retirement must run on its owner thread");
    }
    while (m_pendingRetirementCount != 0)
    {
        PendingRetirement& pending = m_pendingRetirements[m_pendingRetirementCount - 1U];
        if (auto status = m_assets->retireTexture2D(*m_device, pending.lease, pending.gpuTexture); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Sprite2DBindingRegistry::drainPendingRetirements", "assets"));
        }
        pending = PendingRetirement{};
        --m_pendingRetirementCount;
    }
    return Core::success();
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

    auto bindingKey = m_device->createTexture2DBinding(gpuTexture);
    if (!bindingKey)
    {
        if (bindingKey.error().code == Render::RenderErrorCode::TextureBindingKeyExhausted)
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
    if (auto status = drainPendingRetirements(); !status)
    {
        return status;
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
    return drainPendingRetirements();
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

Render::Texture2DFrameResourceResolver
Sprite2DBindingRegistry::texture2DFrameResourceResolver() noexcept
{
    return {
        .userData = this,
        .resolve = &Sprite2DBindingRegistry::resolveTexture2DFrameResourceCallback,
    };
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
    if (entry == nullptr)
    {
        return Render::FrameResourceRef{};
    }

    return internTextureEntry(*entry, sink);
}

Core::Result<Render::FrameResourceRef>
Sprite2DBindingRegistry::internTextureEntry(
    Entry& entry, Render::FrameResourceSink& sink) noexcept
{
    if (entry.frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Texture2D binding cannot acquire another frame resource borrow");
    }

    ++entry.frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, entry.bindingKey, &entry,
                         &Sprite2DBindingRegistry::releaseFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Texture2D,
            .deviceBindingKey = entry.bindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(
            std::move(resource.error()).withContext("Sprite2DBindingRegistry::internFrameResource", "sink"));
    }
    return *resource;
}

Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
Sprite2DBindingRegistry::resolveTexture2DFrameResource(
    Core::AssetId asset, Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Texture2D frame resource resolution must run on the registry owner thread");
    }
    if (!asset.hasValue())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Texture2D frame resource resolution requires a valid AssetId");
    }

    Entry* entry = findByAssetId(asset);
    if (entry == nullptr || !isLiveTextureEntry(*entry))
    {
        return std::optional<Render::Texture2DFrameResourceResolution>{};
    }
    const CookedAssetFile* file = entry->lease.get();
    if (file == nullptr)
    {
        return std::optional<Render::Texture2DFrameResourceResolution>{};
    }
    auto texture = parseTexture2DFromCooked(*file);
    if (!texture)
    {
        return Core::failure(
            std::move(texture.error()).withContext(
                "Sprite2DBindingRegistry::resolveTexture2DFrameResource", "Texture2D payload"));
    }
    auto resource = internTextureEntry(*entry, sink);
    if (!resource)
    {
        return Core::failure(std::move(resource.error()));
    }
    return std::optional<Render::Texture2DFrameResourceResolution>{
        Render::Texture2DFrameResourceResolution{
            .resource = *resource,
            .pixelWidth = texture->width,
            .pixelHeight = texture->height,
        }};
}

Core::Result<std::optional<Render::Texture2DFrameResourceResolution>>
Sprite2DBindingRegistry::resolveTexture2DFrameResourceCallback(
    void* userData, Core::AssetId asset, Render::FrameResourceSink& sink) noexcept
{
    if (userData == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Texture2D frame resource resolver has no registry");
    }
    return static_cast<Sprite2DBindingRegistry*>(userData)->resolveTexture2DFrameResource(asset, sink);
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
