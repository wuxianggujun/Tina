#pragma once

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <memory_resource>
#include <utility>

namespace Tina::Asset {

struct AssetHandleTag final {};
using AssetHandleId = Core::GenerationId<AssetHandleTag>;

// Copyable weak handle. Does not keep payload alive.
struct AssetHandle final {
    AssetHandleId id{};

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return static_cast<bool>(id);
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return hasValue();
    }
    [[nodiscard]] friend constexpr bool operator==(const AssetHandle&, const AssetHandle&) = default;
};

enum class AssetLogicalState : Core::u8 {
    Queued = 1,
    Loading = 2,
    Ready = 3,
    Failed = 4,
    UnloadPending = 5,
    Unloaded = 6,
};

struct AssetStoreConfig final {
    Core::usize capacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
};

class AssetStore;

// Move-only strong reference. Keeps CPU cooked payload alive until all leases are released.
class AssetLease final {
  public:
    AssetLease() noexcept = default;
    ~AssetLease() noexcept;

    AssetLease(const AssetLease&) = delete;
    AssetLease& operator=(const AssetLease&) = delete;
    AssetLease(AssetLease&& other) noexcept;
    AssetLease& operator=(AssetLease&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_store != nullptr && static_cast<bool>(m_handle);
    }
    [[nodiscard]] AssetHandle handle() const noexcept
    {
        return m_handle;
    }
    [[nodiscard]] const CookedAssetFile* get() const noexcept;
    [[nodiscard]] Core::AssetId assetId() const noexcept;
    [[nodiscard]] AssetFormat::AssetKind assetKind() const noexcept;

  private:
    friend class AssetStore;

    AssetLease(AssetStore* store, AssetHandle handle) noexcept;

    void release() noexcept;

    AssetStore* m_store = nullptr;
    AssetHandle m_handle{};
};

// Owner-thread CPU asset registry (ADR 0016).
// Supports Queued/Loading/Ready/Failed plus weak Handle and strong Lease.
// No GPU UploadTicket, FramePin, or physical retirement ledger in this slice.
class AssetStore final {
  public:
    AssetStore() = delete;
    ~AssetStore() noexcept;

    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;
    AssetStore(AssetStore&&) noexcept;
    AssetStore& operator=(AssetStore&&) = delete;

    [[nodiscard]] static Core::Result<AssetStore> Create(AssetStoreConfig config);

    [[nodiscard]] Core::usize capacity() const noexcept;
    [[nodiscard]] Core::usize activeCount() const noexcept;
    [[nodiscard]] Core::usize availableCount() const noexcept;

    // Immediate Ready publish (sync path). Empty files are rejected.
    [[nodiscard]] Core::Result<AssetHandle> publish(CookedAssetFile asset);

    // Deferred path: create a Queued slot without payload.
    [[nodiscard]] Core::Result<AssetHandle> beginQueued(Core::AssetId assetId, AssetFormat::AssetKind assetKind);

    // Queued → Loading. Rejects other states.
    [[nodiscard]] Core::Status markLoading(AssetHandle handle) noexcept;

    // Loading/Queued → Ready with owning payload. assetId must match the slot.
    [[nodiscard]] Core::Status complete(AssetHandle handle, CookedAssetFile asset) noexcept;

    // Queued/Loading → Failed (payload empty). Handle remains until unload.
    [[nodiscard]] Core::Status fail(AssetHandle handle) noexcept;

    // Weak lookup. Payload only for Ready/UnloadPending.
    [[nodiscard]] const CookedAssetFile* tryGet(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetLogicalState state(AssetHandle handle) const noexcept;
    [[nodiscard]] Core::u32 leaseCount(AssetHandle handle) const noexcept;
    [[nodiscard]] Core::AssetId assetId(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetFormat::AssetKind assetKind(AssetHandle handle) const noexcept;

    // Strong acquire. Ready only.
    [[nodiscard]] Core::Result<AssetLease> acquire(AssetHandle handle);

    // Logical unload. Immediate erase when leaseCount==0; otherwise UnloadPending.
    [[nodiscard]] Core::Status unload(AssetHandle handle) noexcept;

  private:
    friend class AssetLease;

    struct Record final {
        Core::AssetId assetId{};
        AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
        AssetLogicalState state = AssetLogicalState::Unloaded;
        Core::u32 leaseCount = 0;
        CookedAssetFile payload{};
    };

    using Pool = Core::GenerationPool<Record, AssetHandleTag>;

    explicit AssetStore(Pool pool) noexcept;

    void releaseLease(AssetHandle handle) noexcept;
    [[nodiscard]] Record* findRecord(AssetHandle handle) noexcept;
    [[nodiscard]] const Record* findRecord(AssetHandle handle) const noexcept;

    Pool m_pool;
};

} // namespace Tina::Asset
