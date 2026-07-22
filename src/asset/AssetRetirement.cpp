#include <tina/asset/AssetRetirement.hpp>

namespace Tina::Asset {

Core::u32 AssetRetirementLedger::liveCount() const noexcept
{
    Core::u32 live = 0;
    for (const auto& record : m_records)
    {
        if (record.state == AssetRetirementState::DestroyQueued || record.state == AssetRetirementState::Retiring)
        {
            ++live;
        }
    }
    return live;
}

AssetRetirementStats AssetRetirementLedger::stats() const noexcept
{
    AssetRetirementStats stats{};
    for (const auto& record : m_records)
    {
        switch (record.state)
        {
        case AssetRetirementState::DestroyQueued:
            ++stats.destroyQueued;
            break;
        case AssetRetirementState::Retiring:
            ++stats.retiring;
            break;
        case AssetRetirementState::Released:
            ++stats.released;
            break;
        }
    }
    stats.live = stats.destroyQueued + stats.retiring;
    return stats;
}

void AssetRetirementLedger::enqueueDestroy(AssetHandle handle, Core::AssetId assetId, Render::UploadTicketId ticket)
{
    if (auto* existing = find(handle))
    {
        existing->assetId = assetId;
        existing->ticket = ticket;
        existing->state = AssetRetirementState::DestroyQueued;
        return;
    }
    m_records.push_back(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .ticket = ticket,
        .state = AssetRetirementState::DestroyQueued,
    });
}

void AssetRetirementLedger::markRetiring(AssetHandle handle) noexcept
{
    if (auto* existing = find(handle))
    {
        if (existing->state != AssetRetirementState::Released)
        {
            existing->state = AssetRetirementState::Retiring;
        }
    }
}

void AssetRetirementLedger::markReleased(AssetHandle handle) noexcept
{
    if (auto* existing = find(handle))
    {
        existing->state = AssetRetirementState::Released;
        existing->ticket = {};
    }
}

bool AssetRetirementLedger::contains(AssetHandle handle) const noexcept
{
    return find(handle) != nullptr;
}

AssetRetirementRecord* AssetRetirementLedger::find(AssetHandle handle) noexcept
{
    for (auto& record : m_records)
    {
        if (record.handle == handle)
        {
            return &record;
        }
    }
    return nullptr;
}

const AssetRetirementRecord* AssetRetirementLedger::find(AssetHandle handle) const noexcept
{
    for (const auto& record : m_records)
    {
        if (record.handle == handle)
        {
            return &record;
        }
    }
    return nullptr;
}

} // namespace Tina::Asset
