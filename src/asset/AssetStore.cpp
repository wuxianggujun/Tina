#include <tina/asset/AssetStore.hpp>

#include <limits>
#include <utility>

namespace Tina::Asset {

bool AssetStore::stateHasCpuPayload(AssetLogicalState state) noexcept
{
    return state == AssetLogicalState::ReadyCpu || state == AssetLogicalState::UploadQueued ||
           state == AssetLogicalState::ReadyGpu || state == AssetLogicalState::UnloadPending;
}

AssetLease::AssetLease(AssetStore* store, AssetHandle handle) noexcept : m_store(store), m_handle(handle) {}

AssetLease::~AssetLease() noexcept
{
    release();
}

AssetLease::AssetLease(AssetLease&& other) noexcept
    : m_store(std::exchange(other.m_store, nullptr)), m_handle(std::exchange(other.m_handle, {}))
{
}

AssetLease& AssetLease::operator=(AssetLease&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    release();
    m_store = std::exchange(other.m_store, nullptr);
    m_handle = std::exchange(other.m_handle, {});
    return *this;
}

const CookedAssetFile* AssetLease::get() const noexcept
{
    if (m_store == nullptr)
    {
        return nullptr;
    }
    return m_store->tryGet(m_handle);
}

Core::AssetId AssetLease::assetId() const noexcept
{
    if (m_store == nullptr)
    {
        return {};
    }
    return m_store->assetId(m_handle);
}

AssetFormat::AssetKind AssetLease::assetKind() const noexcept
{
    if (m_store == nullptr)
    {
        return AssetFormat::AssetKind::Invalid;
    }
    return m_store->assetKind(m_handle);
}

void AssetLease::release() noexcept
{
    if (m_store == nullptr)
    {
        m_handle = {};
        return;
    }
    m_store->releaseLease(m_handle);
    m_store = nullptr;
    m_handle = {};
}

AssetStore::AssetStore(Pool pool) noexcept : m_pool(std::move(pool)) {}

AssetStore::~AssetStore() noexcept = default;

AssetStore::AssetStore(AssetStore&&) noexcept = default;

Core::Result<AssetStore> AssetStore::Create(AssetStoreConfig config)
{
    if (config.memoryResource == nullptr || config.capacity == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset store requires capacity and memory resource");
    }
    auto pool = Pool::Create(config.capacity, *config.memoryResource);
    if (!pool)
    {
        return Core::failure(std::move(pool.error()).withContext("AssetStore::Create", "pool"));
    }
    return AssetStore(std::move(*pool));
}

Core::usize AssetStore::capacity() const noexcept
{
    return m_pool.capacity();
}

Core::usize AssetStore::activeCount() const noexcept
{
    return m_pool.activeCount();
}

Core::usize AssetStore::availableCount() const noexcept
{
    return m_pool.availableCount();
}

Core::Result<AssetHandle> AssetStore::publish(CookedAssetFile asset)
{
    if (!asset)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cannot publish empty cooked asset");
    }
    Record record{
        .assetId = asset.header().assetId,
        .assetKind = asset.header().assetKind,
        .state = AssetLogicalState::ReadyCpu,
        .leaseCount = 0,
        .payload = std::move(asset),
    };
    auto id = m_pool.tryEmplace(std::move(record));
    if (!id)
    {
        if (id.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(AssetErrorCode::CatalogCapacityExceeded, "asset store capacity exceeded");
        }
        return Core::failure(std::move(id.error()).withContext("AssetStore::publish", "emplace"));
    }
    return AssetHandle{.id = *id};
}

Core::Result<AssetHandle> AssetStore::beginQueued(Core::AssetId assetId, AssetFormat::AssetKind assetKind)
{
    if (!assetId || assetKind == AssetFormat::AssetKind::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "queued asset requires valid id and kind");
    }
    Record record{
        .assetId = assetId,
        .assetKind = assetKind,
        .state = AssetLogicalState::Queued,
        .leaseCount = 0,
        .payload = {},
    };
    auto id = m_pool.tryEmplace(std::move(record));
    if (!id)
    {
        if (id.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(AssetErrorCode::CatalogCapacityExceeded, "asset store capacity exceeded");
        }
        return Core::failure(std::move(id.error()).withContext("AssetStore::beginQueued", "emplace"));
    }
    return AssetHandle{.id = *id};
}

Core::Status AssetStore::markLoading(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::Queued)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only Queued assets can enter Loading");
    }
    record->state = AssetLogicalState::Loading;
    return Core::success();
}

Core::Status AssetStore::complete(AssetHandle handle, CookedAssetFile asset) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::Queued && record->state != AssetLogicalState::Loading)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only Queued/Loading assets can complete");
    }
    if (!asset)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cannot complete with empty cooked asset");
    }
    if (asset.header().assetId != record->assetId)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "completed asset id does not match slot");
    }
    record->assetKind = asset.header().assetKind;
    record->payload = std::move(asset);
    record->state = AssetLogicalState::ReadyCpu;
    return Core::success();
}

Core::Status AssetStore::fail(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::Queued && record->state != AssetLogicalState::Loading)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only Queued/Loading assets can fail");
    }
    if (record->leaseCount != 0U)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "cannot fail asset while leases are held");
    }
    record->payload = CookedAssetFile{};
    record->state = AssetLogicalState::Failed;
    return Core::success();
}

Core::Status AssetStore::beginUpload(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::ReadyCpu || !record->payload)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only ReadyCpu assets can begin upload");
    }
    record->state = AssetLogicalState::UploadQueued;
    return Core::success();
}

Core::Status AssetStore::completeGpu(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::UploadQueued || !record->payload)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only UploadQueued assets can complete GPU");
    }
    record->state = AssetLogicalState::ReadyGpu;
    return Core::success();
}

Core::Status AssetStore::failGpu(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::UploadQueued)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only UploadQueued assets can fail GPU");
    }
    // Keep CPU payload for diagnosis/retry; mark Failed.
    record->state = AssetLogicalState::Failed;
    return Core::success();
}

const CookedAssetFile* AssetStore::tryGet(AssetHandle handle) const noexcept
{
    const auto* record = findRecord(handle);
    if (record == nullptr || !stateHasCpuPayload(record->state) || !record->payload)
    {
        return nullptr;
    }
    return &record->payload;
}

AssetLogicalState AssetStore::state(AssetHandle handle) const noexcept
{
    const auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return AssetLogicalState::Unloaded;
    }
    return record->state;
}

Core::u32 AssetStore::leaseCount(AssetHandle handle) const noexcept
{
    const auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return 0;
    }
    return record->leaseCount;
}

Core::AssetId AssetStore::assetId(AssetHandle handle) const noexcept
{
    const auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return {};
    }
    return record->assetId;
}

AssetFormat::AssetKind AssetStore::assetKind(AssetHandle handle) const noexcept
{
    const auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return AssetFormat::AssetKind::Invalid;
    }
    return record->assetKind;
}

bool AssetStore::hasCpuPayload(AssetHandle handle) const noexcept
{
    return tryGet(handle) != nullptr;
}

bool AssetStore::isGpuReady(AssetHandle handle) const noexcept
{
    return state(handle) == AssetLogicalState::ReadyGpu;
}

Core::Result<AssetLease> AssetStore::acquire(AssetHandle handle)
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state == AssetLogicalState::Failed)
    {
        return Core::failure(AssetErrorCode::AssetFailed, "asset load failed");
    }
    if (record->state == AssetLogicalState::Queued || record->state == AssetLogicalState::Loading ||
        record->state == AssetLogicalState::UnloadPending)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "asset is not ready for lease acquire");
    }
    if (record->state == AssetLogicalState::Unloaded || !record->payload)
    {
        return Core::failure(AssetErrorCode::AssetUnloaded, "asset payload is unloaded");
    }
    // ReadyCpu / UploadQueued / ReadyGpu all expose CPU payload for leases.
    if (!stateHasCpuPayload(record->state))
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "asset is not ready");
    }
    if (record->leaseCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::LeaseCountOverflow, "asset lease count overflow");
    }
    ++record->leaseCount;
    return AssetLease(this, handle);
}

Core::Status AssetStore::unload(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state == AssetLogicalState::Unloaded)
    {
        return Core::success();
    }
    if (record->state == AssetLogicalState::Queued || record->state == AssetLogicalState::Loading ||
        record->state == AssetLogicalState::Failed)
    {
        if (record->leaseCount != 0U)
        {
            return Core::failure(AssetErrorCode::AssetNotReady, "cannot unload in-flight asset with leases");
        }
        (void)m_pool.erase(handle.id);
        return Core::success();
    }
    if (record->state == AssetLogicalState::UploadQueued)
    {
        // Logical unload while GPU ticket may still be outstanding; caller must retire ticket.
        if (record->leaseCount == 0U)
        {
            (void)m_pool.erase(handle.id);
            return Core::success();
        }
    }
    if (record->leaseCount == 0U)
    {
        (void)m_pool.erase(handle.id);
        return Core::success();
    }
    record->state = AssetLogicalState::UnloadPending;
    return Core::success();
}

void AssetStore::releaseLease(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr || record->leaseCount == 0U)
    {
        return;
    }
    --record->leaseCount;
    if (record->state == AssetLogicalState::UnloadPending && record->leaseCount == 0U)
    {
        (void)m_pool.erase(handle.id);
    }
}

AssetStore::Record* AssetStore::findRecord(AssetHandle handle) noexcept
{
    return m_pool.tryGet(handle.id);
}

const AssetStore::Record* AssetStore::findRecord(AssetHandle handle) const noexcept
{
    return m_pool.tryGet(handle.id);
}

} // namespace Tina::Asset
