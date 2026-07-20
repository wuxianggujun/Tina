#include <tina/asset/AssetStore.hpp>

#include <limits>
#include <utility>

namespace Tina::Asset {

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
    const auto* file = get();
    if (file == nullptr || !*file)
    {
        return AssetFormat::AssetKind::Invalid;
    }
    return file->header().assetKind;
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
        .state = AssetLogicalState::Ready,
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

const CookedAssetFile* AssetStore::tryGet(AssetHandle handle) const noexcept
{
    const auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return nullptr;
    }
    if (record->state == AssetLogicalState::Unloaded || !record->payload)
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

Core::Result<AssetLease> AssetStore::acquire(AssetHandle handle)
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state == AssetLogicalState::Unloaded || !record->payload)
    {
        return Core::failure(AssetErrorCode::AssetUnloaded, "asset payload is unloaded");
    }
    if (record->state == AssetLogicalState::UnloadPending)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "asset is unloading; new leases are rejected");
    }
    if (record->state != AssetLogicalState::Ready)
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
    if (record->leaseCount == 0U)
    {
        // Immediate logical+physical release for CPU payload; handle becomes stale.
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
