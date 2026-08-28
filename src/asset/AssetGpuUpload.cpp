#include <tina/asset/AssetGpuUpload.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool hasGpuRetirementRecord(const AssetRetirementLedger& retirement,
                                          AssetHandle handle) noexcept
{
    for (const auto& record : retirement.records())
    {
        if (record.handle == handle &&
            (record.kind == AssetRetirementKind::GpuTexture2D ||
             record.kind == AssetRetirementKind::GpuMesh))
        {
            return true;
        }
    }
    return false;
}

} // namespace

AssetGpuUploadCoordinator::AssetGpuUploadCoordinator(AssetStore& store, Render::NullUploadLedger& ledger,
                                                     AssetGpuUploadConfig config,
                                                     AssetRetirementLedger* retirement) noexcept
    : m_store(&store), m_ledger(&ledger), m_retirement(retirement), m_config(config)
{
}

Core::Status AssetGpuUploadCoordinator::track(AssetHandle handle)
{
    if (!handle)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "cannot track invalid asset handle");
    }
    const auto st = m_store->state(handle);
    if (st != AssetLogicalState::ReadyCpu && st != AssetLogicalState::UploadQueued &&
        st != AssetLogicalState::ReadyGpu)
    {
        return Core::failure(AssetErrorCode::AssetNotReady, "only ReadyCpu/UploadQueued/ReadyGpu assets can be tracked");
    }
    if (std::find(m_readyCpuQueue.begin(), m_readyCpuQueue.end(), handle) != m_readyCpuQueue.end())
    {
        return Core::success();
    }
    if (std::find_if(m_pending.begin(), m_pending.end(),
                     [&](const PendingUpload& p) { return p.handle == handle; }) != m_pending.end())
    {
        return Core::success();
    }
    if (st == AssetLogicalState::ReadyCpu)
    {
        m_readyCpuQueue.push_back(handle);
    }
    return Core::success();
}

Core::Result<AssetGpuUploadStats> AssetGpuUploadCoordinator::pumpUploads()
{
    AssetGpuUploadStats stats{};

    Core::u32 submitBudget = m_config.submitBudget;
    if (submitBudget == 0U)
    {
        submitBudget = static_cast<Core::u32>(m_readyCpuQueue.size());
    }

    while (stats.submitted < submitBudget && !m_readyCpuQueue.empty())
    {
        const auto handle = m_readyCpuQueue.front();
        m_readyCpuQueue.erase(m_readyCpuQueue.begin());

        if (m_store->state(handle) != AssetLogicalState::ReadyCpu)
        {
            continue;
        }
        const auto* file = m_store->tryGet(handle);
        if (file == nullptr || !*file)
        {
            continue;
        }

        auto beginStatus = m_store->beginUpload(handle);
        if (!beginStatus)
        {
            return Core::failure(std::move(beginStatus.error()).withContext("AssetGpuUpload", "beginUpload"));
        }

        auto ticket = m_ledger->submit(Render::UploadSubmitParams{
            .bytes = file->bytes(),
            .userTag = handle.id.index(),
        });
        if (!ticket)
        {
            (void)m_store->failGpu(handle);
            ++stats.failed;
            if (ticket.error().code == Render::RenderErrorCode::UploadLedgerFull)
            {
                break;
            }
            continue;
        }

        m_pending.push_back(PendingUpload{.handle = handle, .ticket = *ticket});
        ++stats.submitted;
    }

    Core::u32 pollBudget = m_config.pollBudget;
    if (pollBudget == 0U)
    {
        pollBudget = static_cast<Core::u32>(m_pending.size());
    }

    Core::u32 polled = 0;
    for (auto it = m_pending.begin(); it != m_pending.end() && polled < pollBudget;)
    {
        ++polled;
        auto pollStatus = m_ledger->poll(it->ticket);
        if (!pollStatus)
        {
            return Core::failure(std::move(pollStatus.error()).withContext("AssetGpuUpload", "poll"));
        }
        const auto ticketState = m_ledger->state(it->ticket);
        if (ticketState == Render::UploadTicketState::Pending)
        {
            ++it;
            continue;
        }
        if (ticketState == Render::UploadTicketState::Ready)
        {
            // completeGpu is idempotent-friendly: only transition UploadQueued → ReadyGpu once.
            if (m_store->state(it->handle) == AssetLogicalState::UploadQueued)
            {
                auto gpuStatus = m_store->completeGpu(it->handle);
                if (!gpuStatus)
                {
                    return Core::failure(std::move(gpuStatus.error()).withContext("AssetGpuUpload", "completeGpu"));
                }
                ++stats.becameGpuReady;
            }
            if (m_config.retireOnGpuReady)
            {
                (void)m_ledger->retire(it->ticket);
                if (m_retirement != nullptr)
                {
                    m_retirement->markReleased(it->handle);
                }
                it = m_pending.erase(it);
            } else
            {
                // Keep ticket until cancelUpload/explicit retire.
                ++it;
            }
            continue;
        }
        // No failure branch: a ticket cannot fail after submit() returned it. The
        // ledger only owns the staging copy, which is already made by then, so the
        // states left here are Retired and Invalid -- both meaning this entry is
        // stale rather than failed. AssetLogicalState::Failed is still reachable via
        // failGpu() above when submit() itself fails.
        ++it;
    }

    stats.pendingTickets = static_cast<Core::u32>(m_pending.size());
    stats.readyCpuRemaining = static_cast<Core::u32>(m_readyCpuQueue.size());
    return stats;
}

Core::Status AssetGpuUploadCoordinator::cancelUpload(AssetHandle handle) noexcept
{
    m_readyCpuQueue.erase(std::remove(m_readyCpuQueue.begin(), m_readyCpuQueue.end(), handle), m_readyCpuQueue.end());

    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
    {
        if (it->handle != handle)
        {
            continue;
        }
        const auto assetId = m_store->assetId(handle);
        const bool gpuRetirementOwnsRecord =
            m_retirement != nullptr && hasGpuRetirementRecord(*m_retirement, handle);
        if (m_retirement != nullptr && !gpuRetirementOwnsRecord)
        {
            (void)m_retirement->enqueueDestroy(handle, assetId, it->ticket);
            m_retirement->markRetiring(handle);
        }
        // Free staging for Pending/Ready tickets.
        const auto ticketState = m_ledger->state(it->ticket);
        if (ticketState == Render::UploadTicketState::Pending)
        {
            (void)m_ledger->poll(it->ticket);
        }
        if (m_ledger->state(it->ticket) == Render::UploadTicketState::Ready)
        {
            (void)m_ledger->retire(it->ticket);
        }
        if (m_retirement != nullptr && !gpuRetirementOwnsRecord)
        {
            m_retirement->markReleased(handle);
        }
        m_pending.erase(it);
        return Core::success();
    }

    if (m_retirement != nullptr && !hasGpuRetirementRecord(*m_retirement, handle))
    {
        // No outstanding ticket: logical unload only.
        (void)m_retirement->enqueueDestroy(handle, m_store->assetId(handle), {});
        m_retirement->markReleased(handle);
    }
    return Core::success();
}

std::optional<Render::UploadTicketId> AssetGpuUploadCoordinator::pendingTicket(AssetHandle handle) const noexcept
{
    for (const auto& pending : m_pending)
    {
        if (pending.handle == handle)
        {
            return pending.ticket;
        }
    }
    return std::nullopt;
}

Core::u32 AssetGpuUploadCoordinator::trackedCount() const noexcept
{
    return static_cast<Core::u32>(m_readyCpuQueue.size() + m_pending.size());
}

Core::u32 AssetGpuUploadCoordinator::pendingUploadCount() const noexcept
{
    return static_cast<Core::u32>(m_pending.size());
}

} // namespace Tina::Asset
