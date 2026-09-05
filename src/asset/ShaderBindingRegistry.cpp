#include <tina/asset/ShaderBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuShader.hpp>
#include <tina/asset/AssetSystem.hpp>
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

ShaderBindingRegistry::~ShaderBindingRegistry() noexcept
{
    if (m_bindingCount != 0 || m_preparedCount != 0 || m_pendingRetirementCount != 0)
    {
        std::terminate();
    }
    for (const Entry& entry : m_entries)
    {
        if (entry.frameBorrowCount != 0)
        {
            std::terminate();
        }
    }
}

ShaderBindingRegistry::ShaderBindingRegistry(AssetSystem& assets, Render::IRenderDevice& device,
                                             std::pmr::vector<Entry> entries,
                                             std::pmr::vector<PreparedEntry> preparedEntries,
                                             std::pmr::vector<PendingRetirement> pendingRetirements,
                                             Core::usize capacity) noexcept
    : m_assets(&assets), m_store(&assets.mutableStoreForOwner()), m_device(&device), m_entries(std::move(entries)),
      m_preparedEntries(std::move(preparedEntries)), m_pendingRetirements(std::move(pendingRetirements)),
      m_capacity(capacity), m_ownerThread(std::this_thread::get_id())
{
}

ShaderBindingRegistry::ShaderBindingRegistry(ShaderBindingRegistry&& other) noexcept
    : m_assets(std::exchange(other.m_assets, nullptr)), m_store(std::exchange(other.m_store, nullptr)),
      m_device(std::exchange(other.m_device, nullptr)), m_entries(std::move(other.m_entries)),
      m_preparedEntries(std::move(other.m_preparedEntries)),
      m_pendingRetirements(std::move(other.m_pendingRetirements)), m_capacity(std::exchange(other.m_capacity, 0)),
      m_bindingCount(std::exchange(other.m_bindingCount, 0)),
      m_preparedCount(std::exchange(other.m_preparedCount, 0)),
      m_pendingRetirementCount(std::exchange(other.m_pendingRetirementCount, 0)),
      m_ownerThread(std::exchange(other.m_ownerThread, {}))
{
}

Core::Result<ShaderBindingRegistry>
ShaderBindingRegistry::Create(AssetSystem& assets, Render::IRenderDevice& device, ShaderBindingRegistryConfig config)
{
    if (config.shaderCapacity == 0 || config.shaderCapacity > MaximumShaderBindingCapacity)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "ShaderBindingRegistry shaderCapacity must be in [1, 512]");
    }
    std::pmr::memory_resource* memoryResource =
        config.memoryResource != nullptr ? config.memoryResource : std::pmr::get_default_resource();
    try
    {
        std::pmr::vector<Entry> entries{memoryResource};
        std::pmr::vector<PreparedEntry> preparedEntries{memoryResource};
        std::pmr::vector<PendingRetirement> pendingRetirements{memoryResource};
        entries.resize(config.shaderCapacity);
        preparedEntries.resize(config.shaderCapacity);
        pendingRetirements.resize(config.shaderCapacity);
        return ShaderBindingRegistry{assets, device, std::move(entries), std::move(preparedEntries),
                                     std::move(pendingRetirements), config.shaderCapacity};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "ShaderBindingRegistry storage allocation failed");
    }
}

ShaderBindingRegistry::operator bool() const noexcept
{
    return m_assets != nullptr && m_store != nullptr && m_device != nullptr && m_capacity != 0;
}

Core::usize ShaderBindingRegistry::capacity() const noexcept
{
    return m_capacity;
}

Core::usize ShaderBindingRegistry::bindingCount() const noexcept
{
    return m_bindingCount;
}

bool ShaderBindingRegistry::hasActiveFrameBorrows() const noexcept
{
    return std::any_of(m_entries.begin(), m_entries.end(), [](const Entry& entry) {
        return entry.frameBorrowCount != 0;
    });
}

Core::usize ShaderBindingRegistry::pendingRetirementCount() const noexcept
{
    return m_pendingRetirementCount;
}

Core::Status ShaderBindingRegistry::prepareCatalogReload(
    AssetSystem& owner, std::span<const CatalogResidentMigration> migrations) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry migration must run on its owner thread");
    }
    if (m_assets != &owner)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "ShaderBindingRegistry belongs to another AssetSystem");
    }
    if (m_preparedCount != 0)
    {
        return Core::failure(AssetErrorCode::CatalogReloadBusy,
                             "ShaderBindingRegistry already has a prepared catalog migration");
    }
    if (auto status = drainPendingRetirements(); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "ShaderBindingRegistry::prepareCatalogReload", "pendingRetirement"));
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
                return candidate.assetId == entry.shaderAssetId;
            });
        if (migration == migrations.end())
        {
            continue;
        }
        if (entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Shader binding is still borrowed by an active frame resource");
        }
        if (migration->kind == CatalogResidentMigrationKind::LoadedDependency ||
            migration->previous != entry.shaderAsset)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "ShaderBindingRegistry migration does not match its active handle");
        }
        ++affectedCount;
    }
    if (m_pendingRetirementCount > m_pendingRetirements.size() ||
        affectedCount > m_pendingRetirements.size() - m_pendingRetirementCount)
    {
        return Core::failure(AssetErrorCode::CatalogCapacityExceeded,
                             "ShaderBindingRegistry has no pending retirement headroom");
    }

    const auto rollback = [&]() noexcept { abortPreparedCatalogReload(); };
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
                    return candidate.assetId == entry.shaderAssetId;
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
                                         "removed Shader migration unexpectedly has a current handle");
                }
                continue;
            }
            if (migrationIt->kind != CatalogResidentMigrationKind::Replaced || !migrationIt->current ||
                migrationIt->current == entry.shaderAsset ||
                m_store->assetKind(migrationIt->current) != AssetFormat::AssetKind::Shader ||
                !hasRenderableCpuPayload(*m_store, migrationIt->current) ||
                m_store->assetId(migrationIt->current) != entry.shaderAssetId ||
                m_store->tryGet(migrationIt->current) == nullptr)
            {
                rollback();
                return Core::failure(AssetErrorCode::InvalidHandle,
                                     "replacement Shader migration has an invalid generation");
            }

            auto lease = m_assets->acquire(migrationIt->current);
            if (!lease)
            {
                rollback();
                return Core::failure(std::move(lease.error()).withContext(
                    "ShaderBindingRegistry::prepareCatalogReload", "lease"));
            }
            auto gpuShader = uploadShaderFromCooked(*m_device, *m_store->tryGet(migrationIt->current));
            if (!gpuShader)
            {
                rollback();
                return Core::failure(std::move(gpuShader.error()).withContext(
                    "ShaderBindingRegistry::prepareCatalogReload", "upload"));
            }
            prepared.replacement = Entry{
                .shaderAsset = migrationIt->current,
                .shaderAssetId = entry.shaderAssetId,
                .lease = std::move(*lease),
                .gpuShader = *gpuShader,
            };
            auto bindingKey = m_device->createShaderBinding(prepared.replacement.gpuShader);
            if (!bindingKey)
            {
                rollback();
                return Core::failure(std::move(bindingKey.error()).withContext(
                    "ShaderBindingRegistry::prepareCatalogReload", "binding"));
            }
            prepared.replacement.bindingKey = *bindingKey;
            auto uniformKey = m_device->createShaderUniformBinding({});
            if (!uniformKey)
            {
                rollback();
                return Core::failure(std::move(uniformKey.error()).withContext(
                    "ShaderBindingRegistry::prepareCatalogReload", "uniforms"));
            }
            prepared.replacement.uniformBindingKey = *uniformKey;
        }
    }
    catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "ShaderBindingRegistry catalog migration allocation failed");
    }
    return Core::success();
}

void ShaderBindingRegistry::commitPreparedCatalogReload() noexcept
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
        pending.gpuShader = active.gpuShader;
        pending.uniformBindingKey = active.uniformBindingKey;
        active.gpuShader = {};
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

void ShaderBindingRegistry::abortPreparedCatalogReload() noexcept
{
    for (Core::usize preparedIndex = 0; preparedIndex < m_preparedCount; ++preparedIndex)
    {
        PreparedEntry& prepared = m_preparedEntries[preparedIndex];
        if (prepared.replacement.gpuShader)
        {
            if (m_pendingRetirementCount >= m_pendingRetirements.size())
            {
                std::terminate();
            }
            PendingRetirement& pending = m_pendingRetirements[m_pendingRetirementCount++];
            pending.lease = std::move(prepared.replacement.lease);
            pending.gpuShader = prepared.replacement.gpuShader;
            pending.uniformBindingKey = prepared.replacement.uniformBindingKey;
            prepared.replacement.gpuShader = {};
        }
        prepared = PreparedEntry{};
    }
    m_preparedCount = 0;
    (void)drainPendingRetirements();
}

Core::Status ShaderBindingRegistry::drainPendingRetirements() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry retirement must run on its owner thread");
    }
    while (m_pendingRetirementCount != 0)
    {
        PendingRetirement& pending = m_pendingRetirements[m_pendingRetirementCount - 1U];
        if (auto status = m_assets->retireGpuShader(*m_device, pending.lease, pending.gpuShader); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "ShaderBindingRegistry::drainPendingRetirements", "assets"));
        }
        if (auto status = clearUniformBinding(pending.uniformBindingKey); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "ShaderBindingRegistry::drainPendingRetirements", "uniforms"));
        }
        pending = PendingRetirement{};
        --m_pendingRetirementCount;
    }
    return Core::success();
}

Core::Result<Core::u32>
ShaderBindingRegistry::registerShaderBinding(AssetHandle shaderAsset, Render::GpuShaderId& gpuShader) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry register must run on its owner thread");
    }
    if (!shaderAsset || m_store->assetKind(shaderAsset) != AssetFormat::AssetKind::Shader)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "ShaderBindingRegistry requires a Shader handle from its AssetStore");
    }
    if (!hasRenderableCpuPayload(*m_store, shaderAsset) || m_store->tryGet(shaderAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "ShaderBindingRegistry Shader CPU payload is not ready");
    }
    if (!gpuShader)
    {
        return Core::failure(Render::RenderErrorCode::InvalidShaderUpload,
                             "ShaderBindingRegistry requires a live GPU shader");
    }
    if (findExact(shaderAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::ShaderBindingConflict,
                             "Shader handle already owns a shader binding");
    }

    const Core::AssetId shaderAssetId = m_store->assetId(shaderAsset);
    if (!shaderAssetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Shader handle has no AssetId");
    }
    if (findByAssetId(shaderAssetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::ShaderBindingConflict,
                             "Shader AssetId already has another registered handle");
    }
    if (findByGpuShader(gpuShader) != nullptr)
    {
        return Core::failure(AssetErrorCode::ShaderBindingConflict,
                             "GPU shader already belongs to another shader binding");
    }
    if (m_bindingCount >= m_capacity || findFree() == nullptr)
    {
        return Core::failure(AssetErrorCode::ShaderBindingCapacityExceeded,
                             "ShaderBindingRegistry has no free shader slot");
    }

    Entry* freeEntry = findFree();
    auto lease = m_assets->acquire(shaderAsset);
    if (!lease)
    {
        return Core::failure(
            std::move(lease.error()).withContext("ShaderBindingRegistry::registerShaderBinding", "lease"));
    }
    auto bindingKey = m_device->createShaderBinding(gpuShader);
    if (!bindingKey)
    {
        if (bindingKey.error().code == Render::RenderErrorCode::ShaderBindingKeyExhausted)
        {
            return Core::failure(AssetErrorCode::ShaderBindingKeyExhausted,
                                 "ShaderBindingRegistry exhausted device binding keys");
        }
        return Core::failure(
            std::move(bindingKey.error()).withContext("ShaderBindingRegistry::registerShaderBinding", "device"));
    }

    auto uniformKey = m_device->createShaderUniformBinding({});
    if (!uniformKey)
    {
        (void)m_device->setShaderBinding(*bindingKey, Render::GpuShaderId{});
        if (uniformKey.error().code == Render::RenderErrorCode::ShaderUniformBindingKeyExhausted)
        {
            return Core::failure(AssetErrorCode::ShaderUniformBindingKeyExhausted,
                                 "ShaderBindingRegistry exhausted device uniform binding keys");
        }
        return Core::failure(
            std::move(uniformKey.error()).withContext("ShaderBindingRegistry::registerShaderBinding", "uniforms"));
    }

    const Core::u32 candidateKey = *bindingKey;
    *freeEntry = Entry{
        .shaderAsset = shaderAsset,
        .shaderAssetId = shaderAssetId,
        .lease = std::move(*lease),
        .gpuShader = gpuShader,
        .bindingKey = candidateKey,
        .uniformBindingKey = *uniformKey,
    };
    gpuShader = {};
    ++m_bindingCount;
    return candidateKey;
}

Core::Status ShaderBindingRegistry::retireShaderBinding(AssetHandle shaderAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry retirement must run on its owner thread");
    }
    Entry* entry = findExact(shaderAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::ShaderBindingNotFound, "Shader handle has no shader binding");
    }
    if (entry->frameBorrowCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Shader binding is still borrowed by an active frame resource");
    }
    if (auto status = m_assets->retireGpuShader(*m_device, entry->lease, entry->gpuShader); !status)
    {
        return Core::failure(
            std::move(status.error()).withContext("ShaderBindingRegistry::retireShaderBinding", "assets"));
    }
    (void)clearUniformBinding(entry->uniformBindingKey);
    *entry = Entry{};
    --m_bindingCount;
    return Core::success();
}

Core::Status ShaderBindingRegistry::retireAllShaderBindings() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry retirement must run on its owner thread");
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
                                 "Shader binding is still borrowed by an active frame resource");
        }
    }
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey == 0)
        {
            continue;
        }
        if (auto status = m_assets->retireGpuShader(*m_device, entry.lease, entry.gpuShader); !status)
        {
            return Core::failure(
                std::move(status.error()).withContext("ShaderBindingRegistry::retireAllShaderBindings", "assets"));
        }
        (void)clearUniformBinding(entry.uniformBindingKey);
        entry = Entry{};
        --m_bindingCount;
    }
    return drainPendingRetirements();
}

Core::u32 ShaderBindingRegistry::bindingKey(AssetHandle shaderAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return 0;
    }
    const Entry* entry = findExact(shaderAsset);
    return entry == nullptr || !isLiveShaderEntry(*entry) ? 0U : entry->bindingKey;
}

Core::u32 ShaderBindingRegistry::uniformBindingKey(AssetHandle shaderAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return 0;
    }
    const Entry* entry = findExact(shaderAsset);
    return entry == nullptr || !isLiveShaderEntry(*entry) ? 0U : entry->uniformBindingKey;
}

Core::Status ShaderBindingRegistry::setShaderUniformValues(
    AssetHandle shaderAsset, const Render::GpuShaderUniformBindingDesc& desc) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry uniform publish must run on its owner thread");
    }
    Entry* entry = findExact(shaderAsset);
    if (entry == nullptr || !isLiveShaderEntry(*entry) || entry->uniformBindingKey == 0)
    {
        return Core::failure(AssetErrorCode::ShaderBindingNotFound,
                             "Shader handle has no live uniform binding");
    }
    if (auto status = m_device->setShaderUniformBinding(entry->uniformBindingKey, desc); !status)
    {
        return Core::failure(
            std::move(status.error()).withContext("ShaderBindingRegistry::setShaderUniformValues", "device"));
    }
    return Core::success();
}

Core::Result<Render::FrameResourceRef>
ShaderBindingRegistry::internShaderFrameResource(AssetHandle shaderAsset, Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry frame resource intern must run on its owner thread");
    }
    Entry* entry = findExact(shaderAsset);
    if (entry == nullptr || !isLiveShaderEntry(*entry))
    {
        return Render::FrameResourceRef{};
    }
    return internOwnedFrameResource(*entry, sink, Render::FrameResourceKind::Shader, entry->bindingKey);
}

Core::Result<Render::FrameResourceRef>
ShaderBindingRegistry::internShaderUniformFrameResource(AssetHandle shaderAsset,
                                                        Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "ShaderBindingRegistry uniform intern must run on its owner thread");
    }
    Entry* entry = findExact(shaderAsset);
    if (entry == nullptr || !isLiveShaderEntry(*entry) || entry->uniformBindingKey == 0)
    {
        return Render::FrameResourceRef{};
    }
    return internOwnedFrameResource(*entry, sink, Render::FrameResourceKind::ShaderUniforms,
                                    entry->uniformBindingKey);
}

Core::Result<Render::FrameResourceRef>
ShaderBindingRegistry::internOwnedFrameResource(Entry& entry, Render::FrameResourceSink& sink,
                                                Render::FrameResourceKind kind,
                                                Core::u32 deviceBindingKey) noexcept
{
    if (entry.frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Shader binding cannot acquire another frame resource borrow");
    }
    ++entry.frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, deviceBindingKey, &entry,
                         &ShaderBindingRegistry::releaseFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = kind,
            .deviceBindingKey = deviceBindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(
            std::move(resource.error()).withContext("ShaderBindingRegistry::internOwnedFrameResource", "sink"));
    }
    return *resource;
}

Core::Status ShaderBindingRegistry::clearUniformBinding(Core::u32 uniformBindingKey) noexcept
{
    if (uniformBindingKey == 0)
    {
        return Core::success();
    }
    return m_device->setShaderUniformBinding(uniformBindingKey, Render::GpuShaderUniformBindingDesc{});
}

void ShaderBindingRegistry::releaseFrameBorrow(void* userData) noexcept
{
    auto* entry = static_cast<Entry*>(userData);
    if (entry == nullptr || entry->frameBorrowCount == 0)
    {
        std::terminate();
    }
    --entry->frameBorrowCount;
}

bool ShaderBindingRegistry::isOwnerThread() const noexcept
{
    return *this && std::this_thread::get_id() == m_ownerThread;
}

ShaderBindingRegistry::Entry* ShaderBindingRegistry::findExact(AssetHandle shaderAsset) noexcept
{
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.shaderAsset == shaderAsset)
        {
            return &entry;
        }
    }
    return nullptr;
}

const ShaderBindingRegistry::Entry* ShaderBindingRegistry::findExact(AssetHandle shaderAsset) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.shaderAsset == shaderAsset)
        {
            return &entry;
        }
    }
    return nullptr;
}

ShaderBindingRegistry::Entry* ShaderBindingRegistry::findByAssetId(Core::AssetId shaderAssetId) noexcept
{
    for (Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.shaderAssetId == shaderAssetId)
        {
            return &entry;
        }
    }
    return nullptr;
}

const ShaderBindingRegistry::Entry* ShaderBindingRegistry::findByGpuShader(Render::GpuShaderId gpuShader) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.bindingKey != 0 && entry.gpuShader == gpuShader)
        {
            return &entry;
        }
    }
    return nullptr;
}

ShaderBindingRegistry::Entry* ShaderBindingRegistry::findFree() noexcept
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

bool ShaderBindingRegistry::isLiveShaderEntry(const Entry& entry) const noexcept
{
    return entry.bindingKey != 0 && entry.uniformBindingKey != 0 && entry.lease && entry.gpuShader &&
           m_store->assetKind(entry.shaderAsset) == AssetFormat::AssetKind::Shader &&
           hasRenderableCpuPayload(*m_store, entry.shaderAsset) && m_store->tryGet(entry.shaderAsset) != nullptr &&
           m_store->assetId(entry.shaderAsset) == entry.shaderAssetId;
}

} // namespace Tina::Asset
