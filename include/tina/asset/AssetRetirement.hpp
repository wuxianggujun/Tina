#pragma once

#include <tina/asset/AssetStore.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/UploadTicket.hpp>

#include <array>
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
    Core::usize destroyQueued = 0;
    Core::usize retiring = 0;
    Core::usize live = 0; // destroyQueued + retiring
    Core::usize recordCapacity = 0; // Reusable storage, driven by peak live records.
    Core::u64 releasedTotal = 0; // Saturating lifetime counter, not retained history.
};

// Owner-thread diagnostic ledger. Does not free GPU resources itself; coordinator drives retire.
class AssetRetirementLedger final {
  public:
    [[nodiscard]] Core::usize liveCount() const noexcept;
    [[nodiscard]] AssetRetirementStats stats() const noexcept;
    [[nodiscard]] Core::u64 releasedCount(AssetRetirementKind kind) const noexcept;
    // Active records only. Any mutation can invalidate references and ordering.
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

    // Identity includes the exact ticket/GPU generation, not only handle/kind:
    // independent registries may own different GPU resources for one CPU asset.
    void markRetiring(const AssetRetirementRecord& resource) noexcept;

    // Completion immediately removes the record without allocating. Repeated
    // completion/cancel is harmless; enqueue idempotence covers active requests
    // only. A consumed resource must not be enqueued again by its former owner.
    void markReleased(const AssetRetirementRecord& resource) noexcept;

    // Removes a request that the render device rejected before consuming its pin.
    void cancel(const AssetRetirementRecord& resource) noexcept;

    [[nodiscard]] bool contains(const AssetRetirementRecord& resource) const noexcept;

  private:
    friend class AssetSystem;
    [[nodiscard]] Core::Status reserveAdditional(Core::usize count) noexcept;
    [[nodiscard]] AssetRetirementRecord* find(const AssetRetirementRecord& resource) noexcept;
    [[nodiscard]] const AssetRetirementRecord* find(const AssetRetirementRecord& resource) const noexcept;
    [[nodiscard]] Core::Status enqueue(AssetRetirementRecord record) noexcept;

    std::vector<AssetRetirementRecord> m_records{};
    Core::u64 m_releasedTotal = 0;
    // One diagnostic counter per resource kind, never one entry per release.
    std::array<Core::u64, 4> m_releasedByKind{};
};

} // namespace Tina::Asset
