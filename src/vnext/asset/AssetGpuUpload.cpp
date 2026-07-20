#include <tina/asset/AssetGpuUpload.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::Asset {

AssetGpuUploadCoordinator::AssetGpuUploadCoordinator(AssetStore& store, Render::NullUploadLedger& ledger,
                                                     AssetGpuUploadConfig config) noexcept
    : m_store(&store), m_ledger(&ledger), m_config(config)
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

    // Submit ReadyCpu → UploadQueued + ledger ticket.
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

        // Stage cooked payload bytes (header+payload file). Null ledger owns the copy.
        auto ticket = m_ledger->submit(Render::UploadSubmitParams{
            .bytes = file->bytes(),
            .userTag = handle.id.index(),
        });
        if (!ticket)
        {
            // Roll back to ReadyCpu so caller can retry later.
            // beginUpload already moved to UploadQueued; failGpu marks Failed — prefer ReadyCpu retry:
            // Use failGpu then we lose ReadyCpu. Better: complete is not reversible without API.
            // For budget/full ledger: leave UploadQueued is wrong without ticket.
            // Mark Failed so state is consistent; caller can unload/retry.
            (void)m_store->failGpu(handle);
            ++stats.failed;
            if (ticket.error().code == Render::RenderErrorCode::UploadLedgerFull)
            {
                // Put remaining ReadyCpu back — this one failed.
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
            auto gpuStatus = m_store->completeGpu(it->handle);
            if (!gpuStatus)
            {
                return Core::failure(std::move(gpuStatus.error()).withContext("AssetGpuUpload", "completeGpu"));
            }
            ++stats.becameGpuReady;
            if (m_config.retireOnGpuReady)
            {
                (void)m_ledger->retire(it->ticket);
            }
            it = m_pending.erase(it);
            continue;
        }
        if (ticketState == Render::UploadTicketState::Failed)
        {
            (void)m_store->failGpu(it->handle);
            ++stats.failed;
            if (m_config.retireOnGpuReady)
            {
                (void)m_ledger->retire(it->ticket);
            }
            it = m_pending.erase(it);
            continue;
        }
        ++it;
    }

    stats.pendingTickets = static_cast<Core::u32>(m_pending.size());
    stats.readyCpuRemaining = static_cast<Core::u32>(m_readyCpuQueue.size());
    return stats;
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
