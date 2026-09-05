#pragma once

#include <tina/asset/AssetStore.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/UploadTicket.hpp>

#include <vector>

namespace Tina::Asset {

// Logical unload is represented by the AssetStore itself.  This ledger only records
// resources whose ownership survives that logical transition: upload staging or a
// backend GPU resource pinned by an AssetLease.  Keeping those domains separate is
// what prevents a staging ticket from overwriting a GPU retirement record for the
// same weak handle.
enum class AssetRetirementState : Core::u8 {
    DestroyQueued = 1,
    Retiring = 2,
    Released = 3,
};

enum class AssetRetirementKind : Core::u8 {
    UploadStaging = 1,
    GpuTexture2D = 2,
    GpuMesh = 3,
    GpuShader = 4,
};

struct AssetRetirementRecord final {
    Core::AssetId assetId{};
    AssetHandle handle{};
    Render::UploadTicketId ticket{};
    Render::GpuTextureId texture{};
    Render::GpuMeshId mesh{};
    Render::GpuShaderId shader{};
    // Every record is created through one of the typed enqueue methods.  The
    // default is only to keep aggregate construction well-formed; enqueue()
    // still validates the kind/resource invariant before publication.
    AssetRetirementKind kind = AssetRetirementKind::UploadStaging;
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

    // Begin tracking upload staging that still owns an outstanding upload ticket.
    // A logical unload without a ticket has no retirement record and must be
    // handled by AssetStore::unload().
    [[nodiscard]] Core::Status enqueueUploadStaging(AssetHandle handle, Core::AssetId assetId,
                                                    Render::UploadTicketId ticket) noexcept;

    [[nodiscard]] Core::Status enqueueTexture2D(AssetHandle handle, Core::AssetId assetId,
                                                Render::GpuTextureId texture) noexcept;

    [[nodiscard]] Core::Status enqueueGpuMesh(AssetHandle handle, Core::AssetId assetId,
                                              Render::GpuMeshId mesh) noexcept;

    [[nodiscard]] Core::Status enqueueGpuShader(AssetHandle handle, Core::AssetId assetId,
                                                Render::GpuShaderId shader) noexcept;

    // Mark one resource kind as drain in progress (optional; Null may skip
    // straight to Released). A handle may have independent staging and GPU
    // records, so the kind is part of the identity.
    void markRetiring(AssetHandle handle, AssetRetirementKind kind) noexcept;

    // Ticket/resource retired / no ownership remains for this kind.
    void markReleased(AssetHandle handle, AssetRetirementKind kind) noexcept;

    // Removes a request that the render device rejected before consuming its pin.
    void cancel(AssetHandle handle, AssetRetirementKind kind) noexcept;

    [[nodiscard]] bool contains(AssetHandle handle, AssetRetirementKind kind) const noexcept;

  private:
    [[nodiscard]] AssetRetirementRecord* find(AssetHandle handle, AssetRetirementKind kind) noexcept;
    [[nodiscard]] const AssetRetirementRecord* find(AssetHandle handle,
                                                    AssetRetirementKind kind) const noexcept;
    [[nodiscard]] Core::Status enqueue(AssetRetirementRecord record) noexcept;

    std::vector<AssetRetirementRecord> m_records{};
};

} // namespace Tina::Asset
