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

Core::Status AssetRetirementLedger::enqueueUploadStaging(AssetHandle handle, Core::AssetId assetId,
                                                          Render::UploadTicketId ticket) noexcept
{
    if (!handle || !assetId || !ticket)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "upload staging retirement requires valid asset and ticket handles");
    }
    return enqueue(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .ticket = ticket,
        .kind = AssetRetirementKind::UploadStaging,
        .state = AssetRetirementState::DestroyQueued,
    });
}

Core::Status AssetRetirementLedger::enqueueTexture2D(AssetHandle handle, Core::AssetId assetId,
                                                      Render::GpuTextureId texture) noexcept
{
    if (!handle || !assetId || !texture)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "texture retirement requires valid asset and texture handles");
    }
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
    if (!handle || !assetId || !mesh)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "mesh retirement requires valid asset and mesh handles");
    }
    return enqueue(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .mesh = mesh,
        .kind = AssetRetirementKind::GpuMesh,
        .state = AssetRetirementState::DestroyQueued,
    });
}

Core::Status AssetRetirementLedger::enqueueGpuShader(AssetHandle handle, Core::AssetId assetId,
                                                      Render::GpuShaderId shader) noexcept
{
    if (!handle || !assetId || !shader)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "shader retirement requires valid asset and shader handles");
    }
    return enqueue(AssetRetirementRecord{
        .assetId = assetId,
        .handle = handle,
        .shader = shader,
        .kind = AssetRetirementKind::GpuShader,
        .state = AssetRetirementState::DestroyQueued,
    });
}

Core::Status AssetRetirementLedger::enqueue(AssetRetirementRecord record) noexcept
{
    if (!record.handle || !record.assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "retirement record requires valid asset identity");
    }
    const bool hasTicket = static_cast<bool>(record.ticket);
    const bool hasTexture = static_cast<bool>(record.texture);
    const bool hasMesh = static_cast<bool>(record.mesh);
    const bool hasShader = static_cast<bool>(record.shader);
    const bool resourceMatchesKind =
        (record.kind == AssetRetirementKind::UploadStaging && hasTicket && !hasTexture && !hasMesh && !hasShader) ||
        (record.kind == AssetRetirementKind::GpuTexture2D && !hasTicket && hasTexture && !hasMesh && !hasShader) ||
        (record.kind == AssetRetirementKind::GpuMesh && !hasTicket && !hasTexture && hasMesh && !hasShader) ||
        (record.kind == AssetRetirementKind::GpuShader && !hasTicket && !hasTexture && !hasMesh && hasShader);
    if (!resourceMatchesKind)
    {
        return Core::failure(AssetErrorCode::AssetRetirementConflict,
                             "retirement kind does not match its resource identity");
    }
    if (auto* existing = find(record.handle, record.kind))
    {
        const bool sameResource = existing->assetId == record.assetId && existing->ticket == record.ticket &&
                                  existing->texture == record.texture && existing->mesh == record.mesh &&
                                  existing->shader == record.shader;
        if (!sameResource)
        {
            return Core::failure(AssetErrorCode::AssetRetirementConflict,
                                 "retirement record conflicts with an active asset resource");
        }
        // Repeated enqueue of the same resource is an idempotent operation. Keep
        // the current state so a retry cannot resurrect a Released record.
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

void AssetRetirementLedger::markRetiring(AssetHandle handle, AssetRetirementKind kind) noexcept
{
    if (auto* existing = find(handle, kind))
    {
        if (existing->state != AssetRetirementState::Released)
        {
            existing->state = AssetRetirementState::Retiring;
        }
    }
}

void AssetRetirementLedger::markReleased(AssetHandle handle, AssetRetirementKind kind) noexcept
{
    if (auto* existing = find(handle, kind))
    {
        existing->state = AssetRetirementState::Released;
        existing->ticket = {};
        existing->texture = {};
        existing->mesh = {};
        existing->shader = {};
    }
}

void AssetRetirementLedger::cancel(AssetHandle handle, AssetRetirementKind kind) noexcept
{
    m_records.erase(std::remove_if(m_records.begin(), m_records.end(),
                                   [handle, kind](const AssetRetirementRecord& record) {
                                       return record.handle == handle && record.kind == kind;
                                   }),
                    m_records.end());
}

bool AssetRetirementLedger::contains(AssetHandle handle, AssetRetirementKind kind) const noexcept
{
    return find(handle, kind) != nullptr;
}

AssetRetirementRecord* AssetRetirementLedger::find(AssetHandle handle, AssetRetirementKind kind) noexcept
{
    for (auto& record : m_records)
    {
        if (record.handle == handle && record.kind == kind)
        {
            return &record;
        }
    }
    return nullptr;
}

const AssetRetirementRecord* AssetRetirementLedger::find(AssetHandle handle,
                                                          AssetRetirementKind kind) const noexcept
{
    for (const auto& record : m_records)
    {
        if (record.handle == handle && record.kind == kind)
        {
            return &record;
        }
    }
    return nullptr;
}

} // namespace Tina::Asset
