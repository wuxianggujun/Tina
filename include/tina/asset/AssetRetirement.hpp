#pragma once

#include <tina/asset/AssetStore.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/UploadTicket.hpp>

#include <vector>

namespace Tina::Asset {

// Logical unload may finish before GPU staging is free. Retirement tracks physical release of
// UploadTicket-owned staging after backend poll/retire (Null path is immediate after Ready).
enum class AssetRetirementState : Core::u8 {
    DestroyQueued = 1,
    Retiring = 2,
    Released = 3,
};

struct AssetRetirementRecord final {
    Core::AssetId assetId{};
    AssetHandle handle{};
    Render::UploadTicketId ticket{};
    AssetRetirementState state = AssetRetirementState::DestroyQueued;
};

struct AssetRetirementStats final {
    Core::u32 destroyQueued = 0;
    Core::u32 retiring = 0;
    Core::u32 released = 0;
    Core::u32 live = 0; // destroyQueued + retiring
};

// Owner-thread diagnostic ledger. Does not free GPU resources itself; coordinator drives retire.
class AssetRetirementLedger final {
  public:
    [[nodiscard]] Core::u32 liveCount() const noexcept;
    [[nodiscard]] AssetRetirementStats stats() const noexcept;
    [[nodiscard]] const std::vector<AssetRetirementRecord>& records() const noexcept
    {
        return m_records;
    }

    // Begin tracking unload that still owns an outstanding upload ticket.
    void enqueueDestroy(AssetHandle handle, Core::AssetId assetId, Render::UploadTicketId ticket);

    // Mark ticket drain in progress (optional; Null may skip straight to Released).
    void markRetiring(AssetHandle handle) noexcept;

    // Ticket retired / no GPU ownership remains.
    void markReleased(AssetHandle handle) noexcept;

    [[nodiscard]] bool contains(AssetHandle handle) const noexcept;

  private:
    [[nodiscard]] AssetRetirementRecord* find(AssetHandle handle) noexcept;
    [[nodiscard]] const AssetRetirementRecord* find(AssetHandle handle) const noexcept;

    std::vector<AssetRetirementRecord> m_records{};
};

} // namespace Tina::Asset
