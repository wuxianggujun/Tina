#include <tina/asset/AssetGpuUpload.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Asset {
AssetGpuUploadCoordinator::AssetGpuUploadCoordinator(AssetStore& store, Render::NullUploadLedger& ledger,
                                                     AssetGpuUploadConfig config,
                                                     AssetRetirementLedger* retirement)
    : m_store(&store), m_ledger(&ledger), m_retirement(retirement), m_config(config)
{
    // The store is the fixed-capacity owner.  Reserving both coordinator
    // queues up front makes ReadyCpu notification non-allocating for every
    // valid handle and closes the submit->record exception window.
    m_readyCpuQueue.reserve(store.capacity());
    m_pending.reserve(store.capacity());
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
        try
        {
            m_readyCpuQueue.push_back(handle);
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(AssetErrorCode::AllocationFailed,
                                 "GPU upload ready queue allocation failed");
        }
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

    stats.retried = std::exchange(m_retryCount, 0U);

    while (stats.submitted < submitBudget && !m_readyCpuQueue.empty())
    {
        const auto handle = m_readyCpuQueue.front();

        if (m_store->state(handle) != AssetLogicalState::ReadyCpu)
        {
            m_readyCpuQueue.erase(m_readyCpuQueue.begin());
            continue;
        }
        const auto* file = m_store->tryGet(handle);
        if (file == nullptr || !*file)
        {
            m_readyCpuQueue.erase(m_readyCpuQueue.begin());
            continue;
        }

        // Reserve before submitting.  Once the ledger accepts a ticket, the
        // coordinator must have a non-throwing place to record it.
        try
        {
            m_pending.reserve(m_pending.size() + 1U);
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(AssetErrorCode::AllocationFailed,
                                 "GPU upload pending queue allocation failed");
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
            if (ticket.error().code == Render::RenderErrorCode::UploadLedgerFull)
            {
                // Capacity is transient backpressure. Return the asset to the
                // CPU-ready state and preserve it for the next pump.
                auto rollback = m_store->rollbackGpuUpload(handle);
                if (!rollback)
                {
                    return Core::failure(std::move(rollback.error()).withContext(
                        "AssetGpuUpload", "rollbackGpuUpload"));
                }
                ++stats.backpressure;
                break;
            }
            if (auto failStatus = m_store->failGpu(handle); !failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext(
                    "AssetGpuUpload", "failGpu"));
            }
            m_readyCpuQueue.erase(m_readyCpuQueue.begin());
            ++stats.failed;
            continue;
        }

        m_readyCpuQueue.erase(m_readyCpuQueue.begin());
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
                if (auto retireStatus = m_ledger->retire(it->ticket); !retireStatus)
                {
                    return Core::failure(std::move(retireStatus.error()).withContext(
                        "AssetGpuUpload", "retireReadyTicket"));
                }
                if (m_retirement != nullptr)
                {
                    m_retirement->markReleased(it->handle, AssetRetirementKind::UploadStaging);
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
        //
        // Stale still has to be resolved. Advancing the iterator and leaving the
        // entry in place kept it in m_pending for the process lifetime: every later
        // pump would poll the same dead ticket, pendingUploadCount would never
        // return to zero, and the asset would sit in UploadQueued with nothing left
        // to complete it. Report the asset as failed -- retryUpload() is the
        // documented recovery -- and drop the entry.
        if (m_store->state(it->handle) == AssetLogicalState::UploadQueued)
        {
            if (auto failStatus = m_store->failGpu(it->handle); !failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext(
                    "AssetGpuUpload", "failGpuAfterStaleTicket"));
            }
        }
        it = m_pending.erase(it);
        ++stats.failed;
    }

    stats.pendingTickets = static_cast<Core::u32>(m_pending.size());
    stats.readyCpuRemaining = static_cast<Core::u32>(m_readyCpuQueue.size());
    return stats;
}

Core::Status AssetGpuUploadCoordinator::cancelUpload(AssetHandle handle) noexcept
{
    if (!handle)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "cannot cancel an invalid asset handle");
    }

    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
    {
        if (it->handle != handle)
        {
            continue;
        }
        const auto assetId = m_store->assetId(handle);
        if (!assetId)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "cannot cancel upload for a stale asset handle");
        }

        // A previous invocation may have retired the ticket but failed while
        // rolling the Store back. The pending record is intentionally retained so
        // this retry can finish that transaction, and ticketRetired says which
        // half already happened. Without it the retry could only look at the
        // ledger, which reports a retired id as Invalid — the same answer it gives
        // for an id that was never live.
        if (!it->ticketRetired)
        {
            auto ticketState = m_ledger->state(it->ticket);
            if (ticketState == Render::UploadTicketState::Pending)
            {
                if (auto status = m_ledger->poll(it->ticket); !status)
                {
                    return status;
                }
                ticketState = m_ledger->state(it->ticket);
            }
            if (ticketState != Render::UploadTicketState::Ready)
            {
                // The staging bytes are gone but this coordinator never retired
                // them, so the pending entry describes a ticket that no longer
                // exists. Resolve it as a definite failure and drop the entry
                // rather than leaving it to occupy pendingUploadCount forever.
                if (m_store->state(handle) == AssetLogicalState::UploadQueued)
                {
                    if (auto status = m_store->failGpu(handle); !status)
                    {
                        return Core::failure(std::move(status.error()).withContext(
                            "AssetGpuUpload", "failGpuAfterStaleCancel"));
                    }
                }
                m_pending.erase(it);
                return Core::failure(Render::RenderErrorCode::UploadTicketInvalid,
                                     "upload ticket cannot be cancelled in its current state");
            }

            if (m_retirement != nullptr)
            {
                if (auto status = m_retirement->enqueueUploadStaging(handle, assetId, it->ticket); !status)
                {
                    return status;
                }
                m_retirement->markRetiring(handle, AssetRetirementKind::UploadStaging);
            }
            if (auto status = m_ledger->retire(it->ticket); !status)
            {
                return status;
            }
            // Record the retirement before attempting the Store transition: if
            // that half fails, the next call must skip straight to the rollback.
            it->ticketRetired = true;
        }

        // Do not mark the retirement record Released until the logical Store
        // transition also succeeds. If rollback fails, the record and pending
        // entry remain retryable (the ticket is already retired, as intended).
        if (m_store->state(handle) == AssetLogicalState::UploadQueued)
        {
            if (auto status = m_store->rollbackGpuUpload(handle); !status)
            {
                return Core::failure(std::move(status.error()).withContext(
                    "AssetGpuUpload", "rollbackAfterCancel"));
            }
        }
        if (m_retirement != nullptr)
        {
            m_retirement->markReleased(handle, AssetRetirementKind::UploadStaging);
        }
        m_pending.erase(it);
        return Core::success();
    }

    // A ReadyCpu item has no external ticket yet; cancellation only removes
    // the coordinator's queue entry.  Do this after the pending search so a
    // failed ticket retirement never loses the retryable entry.
    if (m_store->state(handle) == AssetLogicalState::UploadQueued)
    {
        return Core::failure(AssetErrorCode::AssetUploadFailed,
                             "UploadQueued asset has no coordinator ticket to cancel");
    }
    m_readyCpuQueue.erase(std::remove(m_readyCpuQueue.begin(), m_readyCpuQueue.end(), handle),
                          m_readyCpuQueue.end());
    return Core::success();
}

Core::Status AssetGpuUploadCoordinator::retryUpload(AssetHandle handle)
{
    if (!handle)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "cannot retry an invalid asset handle");
    }
    const bool alreadyTracked =
        std::find(m_readyCpuQueue.begin(), m_readyCpuQueue.end(), handle) != m_readyCpuQueue.end() ||
        std::find_if(m_pending.begin(), m_pending.end(),
                     [&](const PendingUpload& pending) { return pending.handle == handle; }) != m_pending.end();
    if (!alreadyTracked)
    {
        try
        {
            m_readyCpuQueue.reserve(m_readyCpuQueue.size() + 1U);
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(AssetErrorCode::AllocationFailed,
                                 "GPU upload retry queue allocation failed");
        }
    }
    if (!alreadyTracked && m_retryCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetUploadFailed, "GPU upload retry counter overflow");
    }
    auto status = m_store->retryGpuUpload(handle);
    if (!status)
    {
        return status;
    }
    status = track(handle);
    if (!status)
    {
        return status;
    }
    if (!alreadyTracked)
    {
        ++m_retryCount;
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
