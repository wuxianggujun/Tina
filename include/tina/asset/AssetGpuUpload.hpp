#pragma once

#include <tina/asset/AssetRetirement.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/UploadTicket.hpp>

#include <optional>
#include <vector>

namespace Tina::Asset {

struct AssetGpuUploadConfig final {
    // Max assets to advance from ReadyCpu → UploadQueued (and submit) per pumpUploads call.
    // 0 means process all currently ReadyCpu assets tracked by this coordinator.
    Core::u32 submitBudget = 8;
    // Max Pending tickets to poll toward Ready per pumpUploads call. 0 means all pending.
    Core::u32 pollBudget = 8;
    // When true, completeGpu() on poll Ready and retire the ticket immediately (Null ledger path).
    // Real bgfx path will keep ticket until fence; set false when wiring real backends.
    bool retireOnGpuReady = true;
};

struct AssetGpuUploadStats final {
    Core::u32 submitted = 0;
    Core::u32 becameGpuReady = 0;
    Core::u32 failed = 0;
    Core::u32 pendingTickets = 0;
    Core::u32 readyCpuRemaining = 0;
    Core::u32 retiredOnCancel = 0;
};

// Coordinates ReadyCpu → UploadQueued → ReadyGpu using an external NullUploadLedger.
// Owner-thread only. Does not own the ledger or store; both must outlive this coordinator.
// Optional retirement ledger records DestroyQueued→Released when unload races with upload.
class AssetGpuUploadCoordinator final {
  public:
    AssetGpuUploadCoordinator(AssetStore& store, Render::NullUploadLedger& ledger,
                              AssetGpuUploadConfig config = {},
                              AssetRetirementLedger* retirement = nullptr) noexcept;

    // Enqueue a ReadyCpu handle for GPU upload tracking (idempotent if already tracked).
    [[nodiscard]] Core::Status track(AssetHandle handle);

    // 1) submit up to submitBudget ReadyCpu tracked assets (copy payload bytes to ledger)
    // 2) poll up to pollBudget pending tickets → completeGpu / failGpu
    [[nodiscard]] Core::Result<AssetGpuUploadStats> pumpUploads();

    // Cancel outstanding upload for handle: retire ticket if any, update retirement ledger.
    // Safe if handle was never tracked.
    [[nodiscard]] Core::Status cancelUpload(AssetHandle handle) noexcept;

    [[nodiscard]] std::optional<Render::UploadTicketId> pendingTicket(AssetHandle handle) const noexcept;

    [[nodiscard]] Core::u32 trackedCount() const noexcept;
    [[nodiscard]] Core::u32 pendingUploadCount() const noexcept;

  private:
    struct PendingUpload final {
        AssetHandle handle{};
        Render::UploadTicketId ticket{};
    };

    AssetStore* m_store = nullptr;
    Render::NullUploadLedger* m_ledger = nullptr;
    AssetRetirementLedger* m_retirement = nullptr;
    AssetGpuUploadConfig m_config{};
    std::vector<AssetHandle> m_readyCpuQueue{};
    std::vector<PendingUpload> m_pending{};
};

} // namespace Tina::Asset
