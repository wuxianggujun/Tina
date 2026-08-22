#include <tina/asset/AssetRetirement.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <new>

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

Core::Status AssetRetirementLedger::enqueueDestroy(AssetHandle handle, Core::AssetId assetId,
                                                    Render::UploadTicketId ticket) noexcept
{
    return enqueue(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .ticket = ticket,
        .kind = ticket ? AssetRetirementKind::UploadStaging : AssetRetirementKind::Logical,
        .state = AssetRetirementState::DestroyQueued,
    });
}

Core::Status AssetRetirementLedger::enqueueTexture2D(AssetHandle handle, Core::AssetId assetId,
                                                      Render::GpuTextureId texture) noexcept
{
    return enqueue(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .texture = texture,
        .kind = AssetRetirementKind::GpuTexture2D,
        .state = AssetRetirementState::DestroyQueued,
    });
}

Core::Status AssetRetirementLedger::enqueueGpuMesh(AssetHandle handle, Core::AssetId assetId,
                                                    Render::GpuMeshId mesh) noexcept
{
    return enqueue(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .mesh = mesh,
        .kind = AssetRetirementKind::GpuMesh,
        .state = AssetRetirementState::DestroyQueued,
    });
}

Core::Status AssetRetirementLedger::enqueue(AssetRetirementRecord record) noexcept
{
    if (auto* existing = find(record.handle))
    {
        *existing = record;
        return Core::success();
    }
    try
    {
        m_records.push_back(record);
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "asset retirement ledger allocation failed");
    }
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
        existing->texture = {};
        existing->mesh = {};
    }
}

void AssetRetirementLedger::cancel(AssetHandle handle) noexcept
{
    m_records.erase(std::remove_if(m_records.begin(), m_records.end(),
                                   [handle](const AssetRetirementRecord& record) {
                                       return record.handle == handle;
                                   }),
                    m_records.end());
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
