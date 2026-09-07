#include <tina/asset/AssetStore.hpp>

#include <limits>
#include <exception>
#include <thread>
#include <utility>

namespace Tina::Asset {

struct AssetStoreLifetime final {
    explicit AssetStoreLifetime(AssetStore::Pool source) noexcept : pool(std::move(source)) {}
    [[nodiscard]] bool onOwnerThread() const noexcept
    {
        return ownerThread == std::this_thread::get_id();
    }
    void erase(AssetHandle handle, const AssetStore::Record& record) noexcept
    {
        const auto bytes = static_cast<Core::u64>(record.payload.bytes().size());
        residentCookedFileBytes -= bytes;
        (void)pool.erase(handle.id);
    }
    AssetStore::Pool pool;
    Core::u64 residentCookedFileBytes = 0;
    const std::thread::id ownerThread = std::this_thread::get_id();
};

bool AssetStore::stateHasCpuPayload(AssetLogicalState state) noexcept
{
    return state == AssetLogicalState::ReadyCpu || state == AssetLogicalState::UploadQueued ||
           state == AssetLogicalState::ReadyGpu || state == AssetLogicalState::Failed ||
           state == AssetLogicalState::UnloadPending;
}

AssetLease::AssetLease(AssetHandle handle, std::shared_ptr<AssetStoreLifetime> lifetime) noexcept
    : m_lifetime(std::move(lifetime)), m_handle(handle)
{
}

AssetLease::~AssetLease() noexcept
{
    release();
}

AssetLease::AssetLease(AssetLease&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_handle(std::exchange(other.m_handle, {}))
{
}

AssetLease& AssetLease::operator=(AssetLease&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    release();
    m_lifetime = std::move(other.m_lifetime);
    m_handle = std::exchange(other.m_handle, {});
    return *this;
}

const CookedAssetFile* AssetLease::get() const noexcept
{
    if (m_lifetime == nullptr || !m_lifetime->onOwnerThread())
    {
        return nullptr;
    }
    const auto* record = m_lifetime->pool.tryGet(m_handle.id);
    return record != nullptr && record->payload ? &record->payload : nullptr;
}

Core::AssetId AssetLease::assetId() const noexcept
{
    if (m_lifetime == nullptr || !m_lifetime->onOwnerThread())
    {
        return {};
    }
    const auto* record = m_lifetime->pool.tryGet(m_handle.id);
    return record != nullptr ? record->assetId : Core::AssetId{};
}

AssetFormat::AssetKind AssetLease::assetKind() const noexcept
{
    if (m_lifetime == nullptr || !m_lifetime->onOwnerThread())
    {
        return AssetFormat::AssetKind::Invalid;
    }
    const auto* record = m_lifetime->pool.tryGet(m_handle.id);
    return record != nullptr ? record->assetKind : AssetFormat::AssetKind::Invalid;
}

void AssetLease::release() noexcept
{
    if (m_lifetime == nullptr)
    {
        m_handle = {};
        return;
    }
    if (!m_lifetime->onOwnerThread())
    {
        std::terminate();
    }
    auto* record = m_lifetime->pool.tryGet(m_handle.id);
    if (record != nullptr && record->leaseCount != 0)
    {
        --record->leaseCount;
        if (record->state == AssetLogicalState::UnloadPending && record->leaseCount == 0)
        {
            m_lifetime->erase(m_handle, *record);
        }
    }
    m_lifetime.reset();
    m_handle = {};
}

AssetStore::AssetStore(std::shared_ptr<AssetStoreLifetime> lifetime) noexcept
    : m_lifetime(std::move(lifetime))
{
}

AssetStore::~AssetStore() noexcept
{
    if (m_lifetime != nullptr && !m_lifetime->onOwnerThread())
    {
        std::terminate();
    }
}

AssetStore::AssetStore(AssetStore&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime))
{
    if (m_lifetime != nullptr && !m_lifetime->onOwnerThread())
    {
        std::terminate();
    }
}

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
    try
    {
        auto lifetime = std::allocate_shared<AssetStoreLifetime>(
            std::pmr::polymorphic_allocator<AssetStoreLifetime>{config.memoryResource}, std::move(*pool));
        return AssetStore(std::move(lifetime));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "asset store lifetime allocation failed");
    }
}

Core::usize AssetStore::capacity() const noexcept
{
    return onOwnerThread() ? m_lifetime->pool.capacity() : 0;
}

Core::usize AssetStore::activeCount() const noexcept
{
    return onOwnerThread() ? m_lifetime->pool.activeCount() : 0;
}

Core::usize AssetStore::availableCount() const noexcept
{
    return onOwnerThread() ? m_lifetime->pool.availableCount() : 0;
}

Core::u64 AssetStore::residentCookedFileBytes() const noexcept
{
    return onOwnerThread() ? m_lifetime->residentCookedFileBytes : 0;
}

bool AssetStore::onOwnerThread() const noexcept
{
    return m_lifetime != nullptr && m_lifetime->onOwnerThread();
}

Core::Result<AssetHandle> AssetStore::publish(CookedAssetFile asset)
{
    if (!onOwnerThread())
    {
        return Core::failure(AssetErrorCode::WrongOwnerThread, "asset store publication requires its live owner thread");
    }
    if (!asset)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cannot publish empty cooked asset");
    }
    const auto cookedFileBytes = static_cast<Core::u64>(asset.bytes().size());
    if (cookedFileBytes > (std::numeric_limits<Core::u64>::max)() - m_lifetime->residentCookedFileBytes)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "asset store resident cooked-file byte count overflowed");
    }
    Record record{
        .assetId = asset.header().assetId,
        .assetKind = asset.header().assetKind,
        .state = AssetLogicalState::ReadyCpu,
        .leaseCount = 0,
        .payload = std::move(asset),
    };
    auto id = m_lifetime->pool.tryEmplace(std::move(record));
    if (!id)
    {
        if (id.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(AssetErrorCode::CatalogCapacityExceeded, "asset store capacity exceeded");
        }
        return Core::failure(std::move(id.error()).withContext("AssetStore::publish", "emplace"));
    }
    m_lifetime->residentCookedFileBytes += cookedFileBytes;
    return AssetHandle{.id = *id};
}

Core::Result<AssetHandle> AssetStore::beginQueued(Core::AssetId assetId, AssetFormat::AssetKind assetKind)
{
    if (!onOwnerThread())
    {
        return Core::failure(AssetErrorCode::WrongOwnerThread, "asset store queueing requires its live owner thread");
    }
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
    auto id = m_lifetime->pool.tryEmplace(std::move(record));
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
    const auto cookedFileBytes = static_cast<Core::u64>(asset.bytes().size());
    if (cookedFileBytes > (std::numeric_limits<Core::u64>::max)() - m_lifetime->residentCookedFileBytes)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "asset store resident cooked-file byte count overflowed");
    }
    record->assetKind = asset.header().assetKind;
    record->payload = std::move(asset);
    m_lifetime->residentCookedFileBytes += cookedFileBytes;
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

Core::Status AssetStore::rollbackGpuUpload(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::UploadQueued || !record->payload)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "only UploadQueued assets with CPU payload can roll back GPU upload");
    }
    record->state = AssetLogicalState::ReadyCpu;
    return Core::success();
}

Core::Status AssetStore::retryGpuUpload(AssetHandle handle) noexcept
{
    auto* record = findRecord(handle);
    if (record == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "asset handle is invalid or stale");
    }
    if (record->state != AssetLogicalState::Failed || !record->payload)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "only failed GPU assets with retained CPU payload can be retried");
    }
    record->state = AssetLogicalState::ReadyCpu;
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
    return AssetLease(handle, m_lifetime);
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
        eraseRecord(handle, *record);
        return Core::success();
    }
    if (record->state == AssetLogicalState::UploadQueued)
    {
        // The upload coordinator must retire staging and roll this state back
        // before logical unload.  Erasing here would invalidate a live ticket
        // and let a later poll address a recycled generation.
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "cancel GPU upload before unloading an UploadQueued asset");
    }
    if (record->leaseCount == 0U)
    {
        eraseRecord(handle, *record);
        return Core::success();
    }
    record->state = AssetLogicalState::UnloadPending;
    return Core::success();
}

void AssetStore::eraseRecord(AssetHandle handle, const Record& record) noexcept
{
    m_lifetime->erase(handle, record);
}

AssetStore::Record* AssetStore::findRecord(AssetHandle handle) noexcept
{
    return onOwnerThread() ? m_lifetime->pool.tryGet(handle.id) : nullptr;
}

const AssetStore::Record* AssetStore::findRecord(AssetHandle handle) const noexcept
{
    return onOwnerThread() ? m_lifetime->pool.tryGet(handle.id) : nullptr;
}

} // namespace Tina::Asset
