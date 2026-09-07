#include <tina/asset/AssetRetirement.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <new>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool sameResource(const AssetRetirementRecord& left,
                                const AssetRetirementRecord& right) noexcept
{
    return left.handle == right.handle && left.kind == right.kind &&
           left.ticket == right.ticket && left.texture == right.texture &&
           left.mesh == right.mesh && left.shader == right.shader;
}

} // namespace

Core::Status AssetRetirementLedger::reserveAdditional(Core::usize count) noexcept
{
    if (count > m_records.max_size() - m_records.size())
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "asset retirement ledger capacity exhausted");
    }
    try
    {
        m_records.reserve(m_records.size() + count);
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "asset retirement ledger allocation failed");
    }
}

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
    if (auto* existing = find(record))
    {
        if (existing->assetId != record.assetId)
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

void AssetRetirementLedger::markRetiring(const AssetRetirementRecord& resource) noexcept
{
    if (auto* existing = find(resource))
    {
        if (existing->state != AssetRetirementState::Released)
        {
            existing->state = AssetRetirementState::Retiring;
        }
    }
}

void AssetRetirementLedger::markReleased(const AssetRetirementRecord& resource) noexcept
{
    if (auto* existing = find(resource))
    {
        existing->state = AssetRetirementState::Released;
    }
}

void AssetRetirementLedger::cancel(const AssetRetirementRecord& resource) noexcept
{
    m_records.erase(std::remove_if(m_records.begin(), m_records.end(),
                                   [&resource](const AssetRetirementRecord& record) {
                                       return sameResource(record, resource) &&
                                              record.state != AssetRetirementState::Released;
                                   }),
                    m_records.end());
}

bool AssetRetirementLedger::contains(const AssetRetirementRecord& resource) const noexcept
{
    return find(resource) != nullptr;
}

AssetRetirementRecord* AssetRetirementLedger::find(const AssetRetirementRecord& resource) noexcept
{
    for (auto& record : m_records)
    {
        if (sameResource(record, resource))
        {
            return &record;
        }
    }
    return nullptr;
}

const AssetRetirementRecord* AssetRetirementLedger::find(const AssetRetirementRecord& resource) const noexcept
{
    for (const auto& record : m_records)
    {
        if (sameResource(record, resource))
        {
            return &record;
        }
    }
    return nullptr;
}

} // namespace Tina::Asset
