#include <tina/asset/Mesh3DBindingRegistry.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>

#include <algorithm>
#include <exception>
#include <memory>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

// Render restates the frozen SkinnedMesh v1 joint bound because it cannot see
// AssetFormat; this is where both sides meet, so drift fails the build here.
static_assert(Render::MaxSkinnedMesh3DPaletteJointCount == AssetFormat::SkinnedMeshWire::MaxJointCount);
static_assert(Render::SkinnedMesh3DPaletteFloatsPerJoint ==
              AssetFormat::SkinnedMeshWire::FloatsPerInverseBindMatrix);

[[nodiscard]] bool hasRenderableCpuPayload(const AssetStore& store, AssetHandle handle) noexcept
{
    const AssetLogicalState state = store.state(handle);
    return state == AssetLogicalState::ReadyCpu || state == AssetLogicalState::UploadQueued ||
           state == AssetLogicalState::ReadyGpu;
}

} // namespace

Mesh3DBindingRegistry::~Mesh3DBindingRegistry() noexcept
{
    if (m_meshBindingCount != 0 || m_materialBindingCount != 0 || m_textureOwnerCount != 0 ||
        m_preparedMeshCount != 0 || m_preparedMaterialCount != 0 || m_preparedTextureCount != 0 ||
        m_pendingMeshCount != 0 || m_pendingMaterialCount != 0 || m_pendingTextureCount != 0)
    {
        // Releasing CPU leases here would silently leak their GPU owners.
        std::terminate();
    }
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.frameBorrowCount != 0)
        {
            std::terminate();
        }
    }
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.frameBorrowCount != 0)
        {
            std::terminate();
        }
    }
}

Mesh3DBindingRegistry::Mesh3DBindingRegistry(
    AssetSystem& assets, Render::IRenderDevice& device,
    std::pmr::vector<MeshEntry> meshEntries,
    std::pmr::vector<MaterialEntry> materialEntries,
    std::pmr::vector<TextureEntry> textureEntries,
    std::pmr::vector<PreparedMeshEntry> preparedMeshes,
    std::pmr::vector<PreparedMaterialEntry> preparedMaterials,
    std::pmr::vector<PreparedTextureEntry> preparedTextures,
    std::pmr::vector<PendingMeshRetirement> pendingMeshes,
    std::pmr::vector<PendingMaterialRetirement> pendingMaterials,
    std::pmr::vector<PendingTextureRetirement> pendingTextures,
    Core::usize meshCapacity, Core::usize materialCapacity,
    Core::usize textureCapacity) noexcept
    : m_assets(&assets), m_store(&assets.store()), m_device(&device),
      m_meshEntries(std::move(meshEntries)), m_materialEntries(std::move(materialEntries)),
      m_textureEntries(std::move(textureEntries)), m_preparedMeshes(std::move(preparedMeshes)),
      m_preparedMaterials(std::move(preparedMaterials)), m_preparedTextures(std::move(preparedTextures)),
      m_pendingMeshes(std::move(pendingMeshes)), m_pendingMaterials(std::move(pendingMaterials)),
      m_pendingTextures(std::move(pendingTextures)), m_meshCapacity(meshCapacity),
      m_materialCapacity(materialCapacity), m_textureCapacity(textureCapacity),
      m_ownerThread(std::this_thread::get_id())
{
}

Mesh3DBindingRegistry::Mesh3DBindingRegistry(Mesh3DBindingRegistry&& other) noexcept
    : m_assets(std::exchange(other.m_assets, nullptr)),
      m_store(std::exchange(other.m_store, nullptr)),
      m_device(std::exchange(other.m_device, nullptr)),
      m_meshEntries(std::move(other.m_meshEntries)),
      m_materialEntries(std::move(other.m_materialEntries)),
      m_textureEntries(std::move(other.m_textureEntries)),
      m_preparedMeshes(std::move(other.m_preparedMeshes)),
      m_preparedMaterials(std::move(other.m_preparedMaterials)),
      m_preparedTextures(std::move(other.m_preparedTextures)),
      m_pendingMeshes(std::move(other.m_pendingMeshes)),
      m_pendingMaterials(std::move(other.m_pendingMaterials)),
      m_pendingTextures(std::move(other.m_pendingTextures)),
      m_meshCapacity(std::exchange(other.m_meshCapacity, 0)),
      m_materialCapacity(std::exchange(other.m_materialCapacity, 0)),
      m_textureCapacity(std::exchange(other.m_textureCapacity, 0)),
      m_meshBindingCount(std::exchange(other.m_meshBindingCount, 0)),
      m_materialBindingCount(std::exchange(other.m_materialBindingCount, 0)),
      m_textureOwnerCount(std::exchange(other.m_textureOwnerCount, 0)),
      m_preparedMeshCount(std::exchange(other.m_preparedMeshCount, 0)),
      m_preparedMaterialCount(std::exchange(other.m_preparedMaterialCount, 0)),
      m_preparedTextureCount(std::exchange(other.m_preparedTextureCount, 0)),
      m_pendingMeshCount(std::exchange(other.m_pendingMeshCount, 0)),
      m_pendingMaterialCount(std::exchange(other.m_pendingMaterialCount, 0)),
      m_pendingTextureCount(std::exchange(other.m_pendingTextureCount, 0)),
      m_ownerThread(std::exchange(other.m_ownerThread, {}))
{
}

Core::Result<Mesh3DBindingRegistry> Mesh3DBindingRegistry::Create(
    AssetSystem& assets, Render::IRenderDevice& device, Mesh3DBindingRegistryConfig config)
{
    if (config.meshCapacity == 0 || config.meshCapacity > MaximumMesh3DBindingCapacity ||
        config.materialCapacity == 0 || config.materialCapacity > MaximumMesh3DBindingCapacity ||
        config.textureCapacity == 0 || config.textureCapacity > MaximumMesh3DTextureCapacity)
    {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "Mesh3DBindingRegistry capacities are outside their supported ranges");
    }
    std::pmr::memory_resource* memoryResource =
        config.memoryResource != nullptr ? config.memoryResource : std::pmr::get_default_resource();
    try
    {
        std::pmr::vector<MeshEntry> meshEntries{memoryResource};
        std::pmr::vector<MaterialEntry> materialEntries{memoryResource};
        std::pmr::vector<TextureEntry> textureEntries{memoryResource};
        std::pmr::vector<PreparedMeshEntry> preparedMeshes{memoryResource};
        std::pmr::vector<PreparedMaterialEntry> preparedMaterials{memoryResource};
        std::pmr::vector<PreparedTextureEntry> preparedTextures{memoryResource};
        std::pmr::vector<PendingMeshRetirement> pendingMeshes{memoryResource};
        std::pmr::vector<PendingMaterialRetirement> pendingMaterials{memoryResource};
        std::pmr::vector<PendingTextureRetirement> pendingTextures{memoryResource};
        meshEntries.resize(config.meshCapacity);
        materialEntries.resize(config.materialCapacity);
        textureEntries.resize(config.textureCapacity);
        preparedMeshes.resize(config.meshCapacity);
        preparedMaterials.resize(config.materialCapacity);
        preparedTextures.resize(config.textureCapacity);
        pendingMeshes.resize(config.meshCapacity);
        pendingMaterials.resize(config.materialCapacity);
        pendingTextures.resize(config.textureCapacity);
        return Mesh3DBindingRegistry{
            assets,
            device,
            std::move(meshEntries),
            std::move(materialEntries),
            std::move(textureEntries),
            std::move(preparedMeshes),
            std::move(preparedMaterials),
            std::move(preparedTextures),
            std::move(pendingMeshes),
            std::move(pendingMaterials),
            std::move(pendingTextures),
            config.meshCapacity,
            config.materialCapacity,
            config.textureCapacity,
        };
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "Mesh3DBindingRegistry storage allocation failed");
    }
}

Mesh3DBindingRegistry::operator bool() const noexcept
{
    return m_assets != nullptr && m_store != nullptr && m_device != nullptr &&
           m_meshCapacity != 0 && m_materialCapacity != 0 && m_textureCapacity != 0;
}

Core::usize Mesh3DBindingRegistry::meshCapacity() const noexcept { return m_meshCapacity; }
Core::usize Mesh3DBindingRegistry::materialCapacity() const noexcept { return m_materialCapacity; }
Core::usize Mesh3DBindingRegistry::textureCapacity() const noexcept { return m_textureCapacity; }
Core::usize Mesh3DBindingRegistry::meshBindingCount() const noexcept { return m_meshBindingCount; }
Core::usize Mesh3DBindingRegistry::materialBindingCount() const noexcept { return m_materialBindingCount; }
Core::usize Mesh3DBindingRegistry::textureOwnerCount() const noexcept { return m_textureOwnerCount; }

bool Mesh3DBindingRegistry::hasActiveFrameBorrows() const noexcept
{
    return std::any_of(m_meshEntries.begin(), m_meshEntries.end(),
                       [](const MeshEntry& entry) {
                           return entry.frameBorrowCount != 0;
                       }) ||
           std::any_of(m_materialEntries.begin(), m_materialEntries.end(),
                       [](const MaterialEntry& entry) {
                           return entry.frameBorrowCount != 0;
                       });
}

Core::usize Mesh3DBindingRegistry::pendingRetirementCount() const noexcept
{
    return m_pendingMeshCount + m_pendingMaterialCount + m_pendingTextureCount;
}

const CatalogResidentMigration* Mesh3DBindingRegistry::findMigration(
    std::span<const CatalogResidentMigration> migrations, Core::AssetId assetId) const noexcept
{
    const auto it = std::find_if(
        migrations.begin(), migrations.end(),
        [assetId](const CatalogResidentMigration& migration) { return migration.assetId == assetId; });
    return it == migrations.end() ? nullptr : std::addressof(*it);
}

Core::Status Mesh3DBindingRegistry::prepareCatalogReload(
    AssetSystem& owner, std::span<const CatalogResidentMigration> migrations) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry migration must run on its owner thread");
    }
    if (m_assets != &owner)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry belongs to another AssetSystem");
    }
    if (m_preparedMeshCount != 0 || m_preparedMaterialCount != 0 || m_preparedTextureCount != 0)
    {
        return Core::failure(AssetErrorCode::CatalogReloadBusy,
                             "Mesh3DBindingRegistry already has a prepared catalog migration");
    }
    if (auto status = drainPendingRetirements(); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::prepareCatalogReload", "pendingRetirement"));
    }

    const auto resetPrepared = [&]() noexcept {
        abortPreparedCatalogReload();
    };
    const auto validateMigrationSet = [&]() -> Core::Status {
        for (Core::usize left = 0; left < migrations.size(); ++left)
        {
            if (!migrations[left].assetId)
            {
                return Core::failure(AssetErrorCode::InvalidHandle,
                                     "catalog GPU migration contains an invalid AssetId");
            }
            for (Core::usize right = left + 1U; right < migrations.size(); ++right)
            {
                if (migrations[left].assetId == migrations[right].assetId)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "catalog GPU migration contains duplicate AssetIds");
                }
            }
        }
        return Core::success();
    };
    if (auto status = validateMigrationSet(); !status)
    {
        return status;
    }

    Core::usize oldMeshCount = 0;
    Core::usize oldMaterialCount = 0;
    Core::usize oldTextureCount = 0;
    for (Core::u32 index = 0; index < static_cast<Core::u32>(m_meshEntries.size()); ++index)
    {
        const MeshEntry& entry = m_meshEntries[index];
        if (entry.bindingKey == 0)
        {
            continue;
        }
        const auto* migration = findMigration(migrations, entry.assetId);
        if (migration == nullptr)
        {
            continue;
        }
        if (entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Mesh3D geometry binding is still borrowed by an active frame resource");
        }
        if (migration->kind == CatalogResidentMigrationKind::LoadedDependency ||
            migration->previous != entry.asset)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "Mesh3DBindingRegistry mesh migration does not match its active handle");
        }
        if (migration->kind == CatalogResidentMigrationKind::Removed)
        {
            if (migration->current)
            {
                return Core::failure(AssetErrorCode::InvalidHandle,
                                     "removed StaticMesh migration unexpectedly has a current handle");
            }
        }
        else if (migration->kind != CatalogResidentMigrationKind::Replaced || !migration->current ||
                 migration->current == entry.asset ||
                 m_store->assetKind(migration->current) !=
                     (entry.skinned ? AssetFormat::AssetKind::SkinnedMesh
                                    : AssetFormat::AssetKind::StaticMesh) ||
                 m_store->assetId(migration->current) != entry.assetId ||
                 !hasRenderableCpuPayload(*m_store, migration->current) ||
                 m_store->tryGet(migration->current) == nullptr)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "replacement StaticMesh migration has an invalid generation");
        }
        ++oldMeshCount;
    }
    for (Core::u32 index = 0; index < static_cast<Core::u32>(m_materialEntries.size()); ++index)
    {
        const MaterialEntry& entry = m_materialEntries[index];
        if (entry.bindingKey == 0)
        {
            continue;
        }
        const auto* migration = findMigration(migrations, entry.assetId);
        if (migration == nullptr)
        {
            continue;
        }
        if (entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Mesh3D material binding is still borrowed by an active frame resource");
        }
        if (migration->kind == CatalogResidentMigrationKind::LoadedDependency ||
            migration->previous != entry.asset)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "Mesh3DBindingRegistry material migration does not match its active handle");
        }
        if (migration->kind == CatalogResidentMigrationKind::Removed)
        {
            if (migration->current)
            {
                return Core::failure(AssetErrorCode::InvalidHandle,
                                     "removed Material migration unexpectedly has a current handle");
            }
        }
        else if (migration->kind != CatalogResidentMigrationKind::Replaced || !migration->current ||
                 migration->current == entry.asset ||
                 m_store->assetKind(migration->current) != AssetFormat::AssetKind::Material ||
                 m_store->assetId(migration->current) != entry.assetId ||
                 !hasRenderableCpuPayload(*m_store, migration->current) ||
                 m_store->tryGet(migration->current) == nullptr)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "replacement Material migration has an invalid generation");
        }
        ++oldMaterialCount;
    }
    for (Core::u32 index = 0; index < static_cast<Core::u32>(m_textureEntries.size()); ++index)
    {
        const TextureEntry& entry = m_textureEntries[index];
        if (!entry.gpuTexture)
        {
            continue;
        }
        const auto* migration = findMigration(migrations, entry.assetId);
        if (migration == nullptr)
        {
            continue;
        }
        if (migration->kind == CatalogResidentMigrationKind::LoadedDependency ||
            migration->previous != entry.asset)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "Mesh3DBindingRegistry texture migration does not match its active handle");
        }
        if (migration->kind == CatalogResidentMigrationKind::Removed)
        {
            if (migration->current)
            {
                return Core::failure(AssetErrorCode::InvalidHandle,
                                     "removed Texture2D migration unexpectedly has a current handle");
            }
        }
        else if (migration->kind != CatalogResidentMigrationKind::Replaced || !migration->current ||
                 migration->current == entry.asset ||
                 m_store->assetKind(migration->current) != AssetFormat::AssetKind::Texture2D ||
                 m_store->assetId(migration->current) != entry.assetId ||
                 !hasRenderableCpuPayload(*m_store, migration->current) ||
                 m_store->tryGet(migration->current) == nullptr)
        {
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "replacement Texture2D migration has an invalid generation");
        }
        ++oldTextureCount;
    }

    try
    {
        // Reserve fixed action slots before touching any GPU owner. Removed
        // texture slots can be reused by a newly required material dependency.
        for (Core::u32 index = 0; index < static_cast<Core::u32>(m_meshEntries.size()); ++index)
        {
            const MeshEntry& entry = m_meshEntries[index];
            const auto* migration = entry.bindingKey == 0 ? nullptr : findMigration(migrations, entry.assetId);
            if (migration == nullptr)
            {
                continue;
            }
            if (m_preparedMeshCount >= m_preparedMeshes.size())
            {
                resetPrepared();
                return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                                     "Mesh3DBindingRegistry has no prepared StaticMesh slot");
            }
            m_preparedMeshes[m_preparedMeshCount++] = PreparedMeshEntry{
                .entryIndex = index,
                .replacement = migration->kind == CatalogResidentMigrationKind::Replaced
                                   ? MeshEntry{.asset = migration->current,
                                               .assetId = entry.assetId,
                                               .skinned = entry.skinned}
                                   : MeshEntry{},
                .remove = migration->kind == CatalogResidentMigrationKind::Removed,
            };
        }
        for (Core::u32 index = 0; index < static_cast<Core::u32>(m_materialEntries.size()); ++index)
        {
            const MaterialEntry& entry = m_materialEntries[index];
            const auto* migration = entry.bindingKey == 0 ? nullptr : findMigration(migrations, entry.assetId);
            if (migration == nullptr)
            {
                continue;
            }
            if (m_preparedMaterialCount >= m_preparedMaterials.size())
            {
                resetPrepared();
                return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                                     "Mesh3DBindingRegistry has no prepared Material slot");
            }
            m_preparedMaterials[m_preparedMaterialCount++] = PreparedMaterialEntry{
                .entryIndex = index,
                .replacement = migration->kind == CatalogResidentMigrationKind::Replaced
                                   ? MaterialEntry{.asset = migration->current, .assetId = entry.assetId}
                                   : MaterialEntry{},
                .remove = migration->kind == CatalogResidentMigrationKind::Removed,
            };
        }
        for (Core::u32 index = 0; index < static_cast<Core::u32>(m_textureEntries.size()); ++index)
        {
            const TextureEntry& entry = m_textureEntries[index];
            const auto* migration = entry.gpuTexture ? findMigration(migrations, entry.assetId) : nullptr;
            if (migration == nullptr)
            {
                continue;
            }
            if (m_preparedTextureCount >= m_preparedTextures.size())
            {
                resetPrepared();
                return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                                     "Mesh3DBindingRegistry has no prepared Texture2D slot");
            }
            m_preparedTextures[m_preparedTextureCount++] = PreparedTextureEntry{
                .entryIndex = index,
                .replacement = migration->kind == CatalogResidentMigrationKind::Replaced
                                   ? TextureEntry{.asset = migration->current, .assetId = entry.assetId}
                                   : TextureEntry{},
                .remove = migration->kind == CatalogResidentMigrationKind::Removed,
            };
        }

        // A changed resident material may acquire a newly loaded Texture2D
        // dependency that had no previous registry owner. Stage one owner in a
        // removed slot first, then consume an actually free slot.
        for (Core::usize materialIndex = 0; materialIndex < m_preparedMaterialCount; ++materialIndex)
        {
            const PreparedMaterialEntry& preparedMaterial = m_preparedMaterials[materialIndex];
            if (preparedMaterial.remove)
            {
                continue;
            }
            const CookedAssetFile* file = m_store->tryGet(preparedMaterial.replacement.asset);
            if (file == nullptr)
            {
                resetPrepared();
                return Core::failure(AssetErrorCode::AssetNotReady,
                                     "replacement Material payload is unavailable");
            }
            for (Core::u32 dependencyIndex = 0; dependencyIndex < file->header().dependencyCount;
                 ++dependencyIndex)
            {
                const auto dependency = file->dependency(dependencyIndex);
                if (!dependency || dependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
                    dependency->flags != AssetFormat::DependencyFlags::Required)
                {
                    resetPrepared();
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "replacement Material has an invalid required Texture2D dependency");
                }
                if (findCandidateTextureByAssetId(dependency->assetId).entry != nullptr)
                {
                    continue;
                }

                AssetHandle candidateHandle{};
                if (const auto* dependencyMigration = findMigration(migrations, dependency->assetId);
                    dependencyMigration != nullptr)
                {
                    if (dependencyMigration->kind == CatalogResidentMigrationKind::Removed ||
                        !dependencyMigration->current)
                    {
                        resetPrepared();
                        return Core::failure(
                            AssetErrorCode::InvalidCatalogConfig,
                            "replacement Material references a removed Texture2D dependency");
                    }
                    candidateHandle = dependencyMigration->current;
                }
                else if (const auto current = m_assets->find(dependency->assetId))
                {
                    candidateHandle = *current;
                }
                if (!candidateHandle ||
                    m_store->assetKind(candidateHandle) != AssetFormat::AssetKind::Texture2D ||
                    m_store->assetId(candidateHandle) != dependency->assetId ||
                    !hasRenderableCpuPayload(*m_store, candidateHandle) ||
                    m_store->tryGet(candidateHandle) == nullptr)
                {
                    resetPrepared();
                    return Core::failure(AssetErrorCode::AssetNotReady,
                                         "replacement Material dependency has no resident Texture2D generation");
                }

                Core::u32 targetIndex = findFreePreparedTextureSlot();
                if (targetIndex == InvalidTextureIndex)
                {
                    resetPrepared();
                    return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                                         "Mesh3DBindingRegistry has no Texture2D slot for replacement dependency");
                }
                bool reusedPreparedRemoval = false;
                for (Core::usize preparedIndex = 0; preparedIndex < m_preparedTextureCount; ++preparedIndex)
                {
                    auto& candidate = m_preparedTextures[preparedIndex];
                    if (candidate.entryIndex == targetIndex && candidate.remove)
                    {
                        candidate.remove = false;
                        candidate.replacement = TextureEntry{
                            .asset = candidateHandle,
                            .assetId = dependency->assetId,
                        };
                        reusedPreparedRemoval = true;
                        break;
                    }
                }
                if (!reusedPreparedRemoval)
                {
                    if (m_preparedTextureCount >= m_preparedTextures.size())
                    {
                        resetPrepared();
                        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                                             "Mesh3DBindingRegistry has no prepared Texture2D slot");
                    }
                    m_preparedTextures[m_preparedTextureCount++] = PreparedTextureEntry{
                        .entryIndex = targetIndex,
                        .replacement = TextureEntry{
                            .asset = candidateHandle,
                            .assetId = dependency->assetId,
                        },
                    };
                }
            }
        }

        // A material binding embeds the concrete GPU texture ids. Any texture
        // slot replacement therefore requires the referencing material to be
        // prepared in the same transaction.
        for (Core::u32 materialIndex = 0;
             materialIndex < static_cast<Core::u32>(m_materialEntries.size()); ++materialIndex)
        {
            const MaterialEntry& material = m_materialEntries[materialIndex];
            if (material.bindingKey == 0)
            {
                continue;
            }
            const bool materialPrepared = std::any_of(
                m_preparedMaterials.begin(),
                m_preparedMaterials.begin() + static_cast<std::ptrdiff_t>(m_preparedMaterialCount),
                [materialIndex](const PreparedMaterialEntry& candidate) {
                    return candidate.entryIndex == materialIndex;
                });
            if (materialPrepared)
            {
                continue;
            }
            for (Core::u32 role = 0; role < material.textureCount; ++role)
            {
                const bool texturePrepared = std::any_of(
                    m_preparedTextures.begin(),
                    m_preparedTextures.begin() + static_cast<std::ptrdiff_t>(m_preparedTextureCount),
                    [&material, role](const PreparedTextureEntry& candidate) {
                        return candidate.entryIndex == material.textureIndices[role];
                    });
                if (texturePrepared)
                {
                    resetPrepared();
                    return Core::failure(
                        AssetErrorCode::InvalidCatalogConfig,
                        "Texture2D migration is missing its dependent Material migration");
                }
            }
        }

        const Core::usize newMeshCount = std::count_if(
            m_preparedMeshes.begin(), m_preparedMeshes.begin() + static_cast<std::ptrdiff_t>(m_preparedMeshCount),
            [](const PreparedMeshEntry& entry) { return !entry.remove; });
        const Core::usize newMaterialCount = std::count_if(
            m_preparedMaterials.begin(),
            m_preparedMaterials.begin() + static_cast<std::ptrdiff_t>(m_preparedMaterialCount),
            [](const PreparedMaterialEntry& entry) { return !entry.remove; });
        const Core::usize newTextureCount = std::count_if(
            m_preparedTextures.begin(),
            m_preparedTextures.begin() + static_cast<std::ptrdiff_t>(m_preparedTextureCount),
            [](const PreparedTextureEntry& entry) { return !entry.remove; });
        const auto pendingHeadroom = [](Core::usize storageSize, Core::usize pending,
                                        Core::usize oldCount, Core::usize newCount) noexcept {
            return pending <= storageSize &&
                   std::max(oldCount, newCount) <= storageSize - pending;
        };
        if (oldMeshCount > m_meshBindingCount || oldMaterialCount > m_materialBindingCount ||
            oldTextureCount > m_textureOwnerCount ||
            !pendingHeadroom(m_pendingMeshes.size(), m_pendingMeshCount, oldMeshCount, newMeshCount) ||
            !pendingHeadroom(m_pendingMaterials.size(), m_pendingMaterialCount, oldMaterialCount,
                             newMaterialCount) ||
            !pendingHeadroom(m_pendingTextures.size(), m_pendingTextureCount, oldTextureCount,
                             newTextureCount) ||
            m_meshBindingCount - oldMeshCount + newMeshCount > m_meshCapacity ||
            m_materialBindingCount - oldMaterialCount + newMaterialCount > m_materialCapacity ||
            m_textureOwnerCount - oldTextureCount + newTextureCount > m_textureCapacity)
        {
            resetPrepared();
            return Core::failure(AssetErrorCode::CatalogCapacityExceeded,
                                 "Mesh3DBindingRegistry lacks catalog migration headroom");
        }

        // Stage Texture owners first because material preparation resolves them.
        for (Core::usize index = 0; index < m_preparedTextureCount; ++index)
        {
            auto& prepared = m_preparedTextures[index];
            if (prepared.remove)
            {
                continue;
            }
            const CookedAssetFile* file = m_store->tryGet(prepared.replacement.asset);
            if (file == nullptr || m_store->assetKind(prepared.replacement.asset) != AssetFormat::AssetKind::Texture2D ||
                !hasRenderableCpuPayload(*m_store, prepared.replacement.asset))
            {
                resetPrepared();
                return Core::failure(AssetErrorCode::AssetNotReady,
                                     "replacement Texture2D payload is unavailable");
            }
            auto lease = m_assets->acquire(prepared.replacement.asset);
            if (!lease)
            {
                resetPrepared();
                return Core::failure(std::move(lease.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "textureLease"));
            }
            auto gpu = uploadTexture2DFromCooked(*m_device, *file);
            if (!gpu)
            {
                resetPrepared();
                return Core::failure(std::move(gpu.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "textureUpload"));
            }
            prepared.replacement.lease = std::move(*lease);
            prepared.replacement.gpuTexture = *gpu;
        }

        for (Core::usize index = 0; index < m_preparedMeshCount; ++index)
        {
            auto& prepared = m_preparedMeshes[index];
            if (prepared.remove)
            {
                continue;
            }
            const auto* file = m_store->tryGet(prepared.replacement.asset);
            if (!prepared.replacement.asset || file == nullptr ||
                m_store->assetKind(prepared.replacement.asset) !=
                    (prepared.replacement.skinned ? AssetFormat::AssetKind::SkinnedMesh
                                                  : AssetFormat::AssetKind::StaticMesh) ||
                !hasRenderableCpuPayload(*m_store, prepared.replacement.asset))
            {
                resetPrepared();
                return Core::failure(AssetErrorCode::AssetNotReady,
                                     "replacement StaticMesh payload is unavailable");
            }
            auto lease = m_assets->acquire(prepared.replacement.asset);
            if (!lease)
            {
                resetPrepared();
                return Core::failure(std::move(lease.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "meshLease"));
            }
            auto gpu = prepared.replacement.skinned ? uploadSkinnedMeshFromCooked(*m_device, *file)
                                                    : uploadStaticMeshFromCooked(*m_device, *file);
            if (!gpu)
            {
                resetPrepared();
                return Core::failure(std::move(gpu.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "meshUpload"));
            }
            auto key = m_device->createMesh3DBinding(*gpu);
            if (!key)
            {
                // Keep the uploaded owner in the prepared slot so the common
                // abort path can hand it to retryable retirement.
                prepared.replacement.lease = std::move(*lease);
                prepared.replacement.gpuMesh = *gpu;
                resetPrepared();
                return Core::failure(std::move(key.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "meshBinding"));
            }
            prepared.replacement.lease = std::move(*lease);
            prepared.replacement.gpuMesh = *gpu;
            prepared.replacement.bindingKey = *key;
        }

        for (Core::usize index = 0; index < m_preparedMaterialCount; ++index)
        {
            auto& prepared = m_preparedMaterials[index];
            if (prepared.remove)
            {
                continue;
            }
            auto validated = validatePreparedMaterialBinding(prepared.replacement.asset);
            if (!validated)
            {
                resetPrepared();
                return Core::failure(std::move(validated.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "materialValidate"));
            }
            auto lease = m_assets->acquire(prepared.replacement.asset);
            if (!lease)
            {
                resetPrepared();
                return Core::failure(std::move(lease.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "materialLease"));
            }
            auto key = m_device->createMesh3DMaterialBinding(validated->renderBinding);
            if (!key)
            {
                prepared.replacement.lease = std::move(*lease);
                resetPrepared();
                return Core::failure(std::move(key.error()).withContext(
                    "Mesh3DBindingRegistry::prepareCatalogReload", "materialBinding"));
            }
            prepared.replacement.lease = std::move(*lease);
            prepared.replacement.textureIndices = validated->textureIndices;
            prepared.replacement.textureCount = validated->textureCount;
            prepared.replacement.bindingKey = *key;
        }
    }
    catch (const std::bad_alloc&)
    {
        resetPrepared();
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "Mesh3DBindingRegistry catalog migration allocation failed");
    }
    return Core::success();
}

void Mesh3DBindingRegistry::commitPreparedCatalogReload() noexcept
{
    for (Core::usize index = 0; index < m_preparedMaterialCount; ++index)
    {
        auto& prepared = m_preparedMaterials[index];
        if (prepared.entryIndex >= m_materialEntries.size())
        {
            std::terminate();
        }
        auto& active = m_materialEntries[prepared.entryIndex];
        if (active.bindingKey == 0 || active.frameBorrowCount != 0 ||
            m_pendingMaterialCount >= m_pendingMaterials.size() ||
            (!prepared.remove &&
             (prepared.replacement.bindingKey == 0 || !prepared.replacement.lease)))
        {
            std::terminate();
        }
        auto& pending = m_pendingMaterials[m_pendingMaterialCount++];
        pending.lease = std::move(active.lease);
        pending.bindingKey = active.bindingKey;
        if (prepared.remove)
        {
            active = MaterialEntry{};
            --m_materialBindingCount;
        }
        else
        {
            active = std::move(prepared.replacement);
        }
        prepared = PreparedMaterialEntry{};
    }
    for (Core::usize index = 0; index < m_preparedTextureCount; ++index)
    {
        auto& prepared = m_preparedTextures[index];
        if (prepared.entryIndex >= m_textureEntries.size() ||
            (prepared.remove && !m_textureEntries[prepared.entryIndex].gpuTexture) ||
            (!prepared.remove &&
             (!prepared.replacement.gpuTexture || !prepared.replacement.lease)))
        {
            std::terminate();
        }
        auto& active = m_textureEntries[prepared.entryIndex];
        if (active.gpuTexture && m_pendingTextureCount >= m_pendingTextures.size())
        {
            std::terminate();
        }
        if (active.gpuTexture)
        {
            auto& pending = m_pendingTextures[m_pendingTextureCount++];
            pending.lease = std::move(active.lease);
            pending.gpuTexture = active.gpuTexture;
            active.gpuTexture = {};
            --m_textureOwnerCount;
        }
        if (prepared.remove)
        {
            active = TextureEntry{};
        }
        else
        {
            active = std::move(prepared.replacement);
            ++m_textureOwnerCount;
        }
        prepared = PreparedTextureEntry{};
    }
    for (Core::usize index = 0; index < m_preparedMeshCount; ++index)
    {
        auto& prepared = m_preparedMeshes[index];
        if (prepared.entryIndex >= m_meshEntries.size())
        {
            std::terminate();
        }
        auto& active = m_meshEntries[prepared.entryIndex];
        if (active.bindingKey == 0 || active.frameBorrowCount != 0 ||
            m_pendingMeshCount >= m_pendingMeshes.size() ||
            (!prepared.remove &&
             (prepared.replacement.bindingKey == 0 || !prepared.replacement.gpuMesh ||
              !prepared.replacement.lease)))
        {
            std::terminate();
        }
        auto& pending = m_pendingMeshes[m_pendingMeshCount++];
        pending.lease = std::move(active.lease);
        pending.gpuMesh = active.gpuMesh;
        if (prepared.remove)
        {
            active = MeshEntry{};
            --m_meshBindingCount;
        }
        else
        {
            active = std::move(prepared.replacement);
        }
        prepared = PreparedMeshEntry{};
    }

    for (auto& texture : m_textureEntries)
    {
        if (texture.gpuTexture)
        {
            texture.materialReferenceCount = 0;
        }
    }
    for (const auto& material : m_materialEntries)
    {
        if (!material.bindingKey)
        {
            continue;
        }
        for (Core::u32 role = 0; role < material.textureCount; ++role)
        {
            if (material.textureIndices[role] >= m_textureEntries.size() ||
                !m_textureEntries[material.textureIndices[role]].gpuTexture)
            {
                std::terminate();
            }
            auto& texture = m_textureEntries[material.textureIndices[role]];
            if (texture.materialReferenceCount == (std::numeric_limits<Core::u32>::max)())
            {
                std::terminate();
            }
            ++texture.materialReferenceCount;
        }
    }
    m_preparedMeshCount = 0;
    m_preparedMaterialCount = 0;
    m_preparedTextureCount = 0;
}

void Mesh3DBindingRegistry::abortPreparedCatalogReload() noexcept
{
    for (Core::usize index = 0; index < m_preparedMaterialCount; ++index)
    {
        auto& prepared = m_preparedMaterials[index];
        if (prepared.replacement.bindingKey != 0)
        {
            if (m_pendingMaterialCount >= m_pendingMaterials.size())
            {
                std::terminate();
            }
            auto& pending = m_pendingMaterials[m_pendingMaterialCount++];
            pending.lease = std::move(prepared.replacement.lease);
            pending.bindingKey = prepared.replacement.bindingKey;
            prepared.replacement.bindingKey = 0;
        }
        prepared = PreparedMaterialEntry{};
    }
    for (Core::usize index = 0; index < m_preparedTextureCount; ++index)
    {
        auto& prepared = m_preparedTextures[index];
        if (prepared.replacement.gpuTexture)
        {
            if (m_pendingTextureCount >= m_pendingTextures.size())
            {
                std::terminate();
            }
            auto& pending = m_pendingTextures[m_pendingTextureCount++];
            pending.lease = std::move(prepared.replacement.lease);
            pending.gpuTexture = prepared.replacement.gpuTexture;
            prepared.replacement.gpuTexture = {};
        }
        prepared = PreparedTextureEntry{};
    }
    for (Core::usize index = 0; index < m_preparedMeshCount; ++index)
    {
        auto& prepared = m_preparedMeshes[index];
        if (prepared.replacement.gpuMesh)
        {
            if (m_pendingMeshCount >= m_pendingMeshes.size())
            {
                std::terminate();
            }
            auto& pending = m_pendingMeshes[m_pendingMeshCount++];
            pending.lease = std::move(prepared.replacement.lease);
            pending.gpuMesh = prepared.replacement.gpuMesh;
            prepared.replacement.gpuMesh = {};
        }
        prepared = PreparedMeshEntry{};
    }
    m_preparedMeshCount = 0;
    m_preparedMaterialCount = 0;
    m_preparedTextureCount = 0;
    (void)drainPendingRetirements();
}

Core::Status Mesh3DBindingRegistry::drainPendingRetirements() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry retirement must run on its owner thread");
    }
    while (m_pendingMaterialCount != 0)
    {
        auto& pending = m_pendingMaterials[m_pendingMaterialCount - 1U];
        if (auto status = m_device->clearMesh3DMaterialBinding(pending.bindingKey); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::drainPendingRetirements", "material"));
        }
        if (pending.lease)
        {
            const auto state = m_store->state(pending.lease.handle());
            if (state != AssetLogicalState::UnloadPending && state != AssetLogicalState::Unloaded)
            {
                if (auto status = m_assets->unload(pending.lease.handle()); !status)
                {
                    return Core::failure(std::move(status.error()).withContext(
                        "Mesh3DBindingRegistry::drainPendingRetirements", "materialAsset"));
                }
            }
        }
        pending = PendingMaterialRetirement{};
        --m_pendingMaterialCount;
    }
    while (m_pendingTextureCount != 0)
    {
        auto& pending = m_pendingTextures[m_pendingTextureCount - 1U];
        if (auto status = m_assets->retireTexture2D(*m_device, pending.lease, pending.gpuTexture); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::drainPendingRetirements", "texture"));
        }
        pending = PendingTextureRetirement{};
        --m_pendingTextureCount;
    }
    while (m_pendingMeshCount != 0)
    {
        auto& pending = m_pendingMeshes[m_pendingMeshCount - 1U];
        if (auto status = m_assets->retireGpuMesh(*m_device, pending.lease, pending.gpuMesh); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::drainPendingRetirements", "mesh"));
        }
        pending = PendingMeshRetirement{};
        --m_pendingMeshCount;
    }
    return Core::success();
}

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMeshBinding(
    AssetHandle meshAsset, Render::GpuMeshId& gpuMesh) noexcept
{
    return registerMeshBindingImpl(meshAsset, gpuMesh, false);
}

Core::Result<Core::u32> Mesh3DBindingRegistry::registerSkinnedMeshBinding(
    AssetHandle meshAsset, Render::GpuMeshId& gpuMesh) noexcept
{
    return registerMeshBindingImpl(meshAsset, gpuMesh, true);
}

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMeshBindingImpl(
    AssetHandle meshAsset, Render::GpuMeshId& gpuMesh, bool skinned) noexcept
{
    const AssetFormat::AssetKind expectedKind =
        skinned ? AssetFormat::AssetKind::SkinnedMesh : AssetFormat::AssetKind::StaticMesh;
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh register must run on its owner thread");
    }
    if (!meshAsset || m_store->assetKind(meshAsset) != expectedKind)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             skinned
                                 ? "Mesh3DBindingRegistry requires a SkinnedMesh handle from its AssetSystem"
                                 : "Mesh3DBindingRegistry requires a StaticMesh handle from its AssetSystem");
    }
    if (!hasRenderableCpuPayload(*m_store, meshAsset) || m_store->tryGet(meshAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry StaticMesh CPU payload is not ready");
    }
    if (!gpuMesh)
    {
        return Core::failure(Render::RenderErrorCode::InvalidMeshUpload,
                             "Mesh3DBindingRegistry requires a live GPU mesh owner");
    }
    if (findMeshExact(meshAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "StaticMesh handle already owns a Mesh3D binding");
    }

    const Core::AssetId assetId = m_store->assetId(meshAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "StaticMesh handle has no AssetId");
    }
    if (findMeshByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "StaticMesh AssetId already has another owned binding");
    }
    if (findMeshByGpu(gpuMesh) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "GPU mesh already belongs to another Mesh3D binding");
    }
    if (m_meshBindingCount >= m_meshCapacity)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free mesh slot");
    }
    MeshEntry* freeEntry = findFreeMesh();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free mesh slot");
    }

    auto lease = m_assets->acquire(meshAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext(
            "Mesh3DBindingRegistry::registerMeshBinding", "lease"));
    }
    auto bindingKey = m_device->createMesh3DBinding(gpuMesh);
    if (!bindingKey)
    {
        return Core::failure(std::move(bindingKey.error()).withContext(
            "Mesh3DBindingRegistry::registerMeshBinding", "device"));
    }

    const Core::u32 candidateKey = *bindingKey;
    *freeEntry = MeshEntry{
        .asset = meshAsset,
        .assetId = assetId,
        .lease = std::move(*lease),
        .gpuMesh = gpuMesh,
        .bindingKey = candidateKey,
        .skinned = skinned,
    };
    gpuMesh = {};
    ++m_meshBindingCount;
    return candidateKey;
}

Core::Status Mesh3DBindingRegistry::registerMaterialTexture(
    AssetHandle textureAsset, Render::GpuTextureId& gpuTexture) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry texture register must run on its owner thread");
    }
    if (!textureAsset || m_store->assetKind(textureAsset) != AssetFormat::AssetKind::Texture2D)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a Texture2D handle from its AssetSystem");
    }
    if (!hasRenderableCpuPayload(*m_store, textureAsset) || m_store->tryGet(textureAsset) == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry Texture2D CPU payload is not ready");
    }
    if (!gpuTexture)
    {
        return Core::failure(Render::RenderErrorCode::InvalidTextureUpload,
                             "Mesh3DBindingRegistry requires a live GPU texture owner");
    }
    if (findTextureExact(textureAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Texture2D handle already has an owned Material texture");
    }

    const Core::AssetId assetId = m_store->assetId(textureAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Texture2D handle has no AssetId");
    }
    if (findTextureByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Texture2D AssetId already has another owned Material texture");
    }
    if (findTextureByGpu(gpuTexture) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "GPU texture already belongs to another Material texture owner");
    }
    if (m_textureOwnerCount >= m_textureCapacity)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free Material texture slot");
    }
    TextureEntry* freeEntry = findFreeTexture();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free Material texture slot");
    }

    auto lease = m_assets->acquire(textureAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialTexture", "lease"));
    }
    if (auto status = m_device->validateTexture2D(gpuTexture); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialTexture", "device"));
    }
    *freeEntry = TextureEntry{
        .asset = textureAsset,
        .assetId = assetId,
        .lease = std::move(*lease),
        .gpuTexture = gpuTexture,
    };
    gpuTexture = {};
    ++m_textureOwnerCount;
    return Core::success();
}

Core::Result<Core::u32> Mesh3DBindingRegistry::registerMaterialBinding(
    AssetHandle materialAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material register must run on its owner thread");
    }
    if (findMaterialExact(materialAsset) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Material handle already owns a Mesh3D binding");
    }

    auto validated = validateMaterialBinding(materialAsset);
    if (!validated)
    {
        return Core::failure(std::move(validated.error()));
    }
    const Core::AssetId assetId = m_store->assetId(materialAsset);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "Material handle has no AssetId");
    }
    if (findMaterialByAssetId(assetId) != nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingConflict,
                             "Material AssetId already has another owned binding");
    }
    if (m_materialBindingCount >= m_materialCapacity)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free material slot");
    }
    MaterialEntry* freeEntry = findFreeMaterial();
    if (freeEntry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingCapacityExceeded,
                             "Mesh3DBindingRegistry has no free material slot");
    }

    auto lease = m_assets->acquire(materialAsset);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialBinding", "lease"));
    }
    auto bindingKey = m_device->createMesh3DMaterialBinding(validated->renderBinding);
    if (!bindingKey)
    {
        return Core::failure(std::move(bindingKey.error()).withContext(
            "Mesh3DBindingRegistry::registerMaterialBinding", "device"));
    }

    *freeEntry = MaterialEntry{
        .asset = materialAsset,
        .assetId = assetId,
        .lease = std::move(*lease),
        .textureIndices = validated->textureIndices,
        .textureCount = validated->textureCount,
        .bindingKey = *bindingKey,
    };
    for (Core::u32 roleIndex = 0; roleIndex < validated->textureCount; ++roleIndex)
    {
        TextureEntry& texture = m_textureEntries[validated->textureIndices[roleIndex]];
        if (texture.materialReferenceCount == (std::numeric_limits<Core::u32>::max)())
        {
            std::terminate();
        }
        ++texture.materialReferenceCount;
    }
    ++m_materialBindingCount;
    return *bindingKey;
}

bool Mesh3DBindingRegistry::hasMaterialTexture(AssetHandle textureAsset) const noexcept
{
    if (!isOwnerThread())
    {
        return false;
    }
    const TextureEntry* entry = findTextureExact(textureAsset);
    return entry != nullptr && isLiveTextureEntry(*entry);
}

Core::Status Mesh3DBindingRegistry::retireMeshBinding(AssetHandle meshAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh retirement must run on its owner thread");
    }
    MeshEntry* entry = findMeshExact(meshAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound,
                             "StaticMesh handle has no owned Mesh3D binding");
    }
    if (entry->frameBorrowCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D geometry binding is still borrowed by an active frame resource");
    }
    if (auto status = m_assets->retireGpuMesh(*m_device, entry->lease, entry->gpuMesh); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::retireMeshBinding", "assets"));
    }
    *entry = MeshEntry{};
    --m_meshBindingCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::retireMaterialBinding(AssetHandle materialAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material retirement must run on its owner thread");
    }
    MaterialEntry* entry = findMaterialExact(materialAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound,
                             "Material handle has no owned Mesh3D binding");
    }
    if (entry->frameBorrowCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D material binding is still borrowed by an active frame resource");
    }
    if (auto status = m_device->clearMesh3DMaterialBinding(entry->bindingKey); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::retireMaterialBinding", "device"));
    }
    if (auto status = m_assets->unload(entry->asset); !status)
    {
        // The retained exact lease makes unload infallible after the device
        // binding has been cleared. Continuing would strand a split owner.
        std::terminate();
    }
    for (Core::u32 roleIndex = 0; roleIndex < entry->textureCount; ++roleIndex)
    {
        TextureEntry& texture = m_textureEntries[entry->textureIndices[roleIndex]];
        if (texture.materialReferenceCount == 0)
        {
            std::terminate();
        }
        --texture.materialReferenceCount;
    }
    *entry = MaterialEntry{};
    --m_materialBindingCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::retireMaterialTexture(AssetHandle textureAsset) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry texture retirement must run on its owner thread");
    }
    TextureEntry* entry = findTextureExact(textureAsset);
    if (entry == nullptr)
    {
        return Core::failure(AssetErrorCode::Mesh3DBindingNotFound,
                             "Texture2D handle has no owned Material texture");
    }
    if (entry->materialReferenceCount != 0)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Material texture is still referenced by a live Material binding");
    }
    if (auto status = m_assets->retireTexture2D(*m_device, entry->lease, entry->gpuTexture); !status)
    {
        return Core::failure(std::move(status.error()).withContext(
            "Mesh3DBindingRegistry::retireMaterialTexture", "assets"));
    }
    *entry = TextureEntry{};
    --m_textureOwnerCount;
    return Core::success();
}

Core::Status Mesh3DBindingRegistry::retireAllBindings() noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry retirement must run on its owner thread");
    }
    if (auto status = drainPendingRetirements(); !status)
    {
        return status;
    }
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Mesh3D geometry binding is still borrowed by an active frame resource");
        }
    }
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.frameBorrowCount != 0)
        {
            return Core::failure(AssetErrorCode::AssetNotReady,
                                 "Mesh3D material binding is still borrowed by an active frame resource");
        }
    }

    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey == 0)
        {
            continue;
        }
        if (auto status = retireMaterialBinding(entry.asset); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::retireAllBindings", "material"));
        }
    }
    for (TextureEntry& entry : m_textureEntries)
    {
        if (!entry.gpuTexture)
        {
            continue;
        }
        if (auto status = retireMaterialTexture(entry.asset); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::retireAllBindings", "texture"));
        }
    }
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey == 0)
        {
            continue;
        }
        if (auto status = retireMeshBinding(entry.asset); !status)
        {
            return Core::failure(std::move(status.error()).withContext(
                "Mesh3DBindingRegistry::retireAllBindings", "mesh"));
        }
    }
    return drainPendingRetirements();
}

Core::Result<Render::FrameResourceRef> Mesh3DBindingRegistry::internMeshFrameResource(
    AssetHandle meshAsset, Render::FrameResourceSink& sink) noexcept
{
    return internMeshFrameResourceImpl(meshAsset, sink, false);
}

Core::Result<Render::FrameResourceRef> Mesh3DBindingRegistry::internSkinnedMeshFrameResource(
    AssetHandle meshAsset, Render::FrameResourceSink& sink) noexcept
{
    return internMeshFrameResourceImpl(meshAsset, sink, true);
}

Core::Result<Render::FrameResourceRef> Mesh3DBindingRegistry::internMeshFrameResourceImpl(
    AssetHandle meshAsset, Render::FrameResourceSink& sink, bool skinned) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry mesh intern must run on its owner thread");
    }
    MeshEntry* entry = findMeshExact(meshAsset);
    if (entry == nullptr || entry->skinned != skinned || !isLiveMeshEntry(*entry))
    {
        return Render::FrameResourceRef{};
    }
    if (entry->frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D geometry binding cannot acquire another frame borrow");
    }
    ++entry->frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, entry->bindingKey, entry,
                         &Mesh3DBindingRegistry::releaseMeshFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = skinned ? Render::FrameResourceKind::SkinnedMesh3DGeometry
                            : Render::FrameResourceKind::Mesh3DGeometry,
            .deviceBindingKey = entry->bindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(std::move(resource.error()).withContext(
            "Mesh3DBindingRegistry::internMeshFrameResource", "sink"));
    }
    return *resource;
}

Core::Result<Render::FrameResourceRef> Mesh3DBindingRegistry::internMaterialFrameResource(
    AssetHandle materialAsset, Render::FrameResourceSink& sink) noexcept
{
    if (!isOwnerThread())
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "Mesh3DBindingRegistry material intern must run on its owner thread");
    }
    MaterialEntry* entry = findMaterialExact(materialAsset);
    if (entry == nullptr || !isLiveMaterialEntry(*entry))
    {
        return Render::FrameResourceRef{};
    }
    if (entry->frameBorrowCount == (std::numeric_limits<Core::u32>::max)())
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3D material binding cannot acquire another frame borrow");
    }
    ++entry->frameBorrowCount;
    Render::FramePin pin{Render::FramePinKind::Custom, entry->bindingKey, entry,
                         &Mesh3DBindingRegistry::releaseMaterialFrameBorrow};
    auto resource = sink.intern(
        Render::FrameResourceDescriptor{
            .kind = Render::FrameResourceKind::Mesh3DMaterial,
            .deviceBindingKey = entry->bindingKey,
        },
        std::move(pin));
    if (!resource)
    {
        return Core::failure(std::move(resource.error()).withContext(
            "Mesh3DBindingRegistry::internMaterialFrameResource", "sink"));
    }
    return *resource;
}

void Mesh3DBindingRegistry::releaseMeshFrameBorrow(void* userData) noexcept
{
    auto* entry = static_cast<MeshEntry*>(userData);
    if (entry == nullptr || entry->frameBorrowCount == 0)
    {
        std::terminate();
    }
    --entry->frameBorrowCount;
}

void Mesh3DBindingRegistry::releaseMaterialFrameBorrow(void* userData) noexcept
{
    auto* entry = static_cast<MaterialEntry*>(userData);
    if (entry == nullptr || entry->frameBorrowCount == 0)
    {
        std::terminate();
    }
    --entry->frameBorrowCount;
}

bool Mesh3DBindingRegistry::isOwnerThread() const noexcept
{
    return *this && std::this_thread::get_id() == m_ownerThread;
}

bool Mesh3DBindingRegistry::isLiveMeshEntry(const MeshEntry& entry) const noexcept
{
    const AssetFormat::AssetKind expectedKind =
        entry.skinned ? AssetFormat::AssetKind::SkinnedMesh : AssetFormat::AssetKind::StaticMesh;
    return entry.bindingKey != 0 && entry.lease && entry.gpuMesh &&
           m_store->assetKind(entry.asset) == expectedKind &&
           hasRenderableCpuPayload(*m_store, entry.asset) && m_store->tryGet(entry.asset) != nullptr &&
           m_store->assetId(entry.asset) == entry.assetId;
}

bool Mesh3DBindingRegistry::isLiveMaterialEntry(const MaterialEntry& entry) const noexcept
{
    if (entry.bindingKey == 0 || !entry.lease ||
        m_store->assetKind(entry.asset) != AssetFormat::AssetKind::Material ||
        !hasRenderableCpuPayload(*m_store, entry.asset) || m_store->tryGet(entry.asset) == nullptr ||
        m_store->assetId(entry.asset) != entry.assetId)
    {
        return false;
    }
    for (Core::u32 roleIndex = 0; roleIndex < entry.textureCount; ++roleIndex)
    {
        if (entry.textureIndices[roleIndex] >= m_textureEntries.size() ||
            !isLiveTextureEntry(m_textureEntries[entry.textureIndices[roleIndex]]))
        {
            return false;
        }
    }
    return true;
}

bool Mesh3DBindingRegistry::isLiveTextureEntry(const TextureEntry& entry) const noexcept
{
    return entry.lease && entry.gpuTexture &&
           m_store->assetKind(entry.asset) == AssetFormat::AssetKind::Texture2D &&
           hasRenderableCpuPayload(*m_store, entry.asset) && m_store->tryGet(entry.asset) != nullptr &&
           m_store->assetId(entry.asset) == entry.assetId;
}

Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshExact(AssetHandle asset) noexcept
{
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshExact(AssetHandle asset) const noexcept
{
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshByAssetId(Core::AssetId assetId) noexcept
{
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findMeshByGpu(
    Render::GpuMeshId gpuMesh) const noexcept
{
    for (const MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey != 0 && entry.gpuMesh == gpuMesh) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MeshEntry* Mesh3DBindingRegistry::findFreeMesh() noexcept
{
    for (MeshEntry& entry : m_meshEntries)
    {
        if (entry.bindingKey == 0) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialExact(AssetHandle asset) noexcept
{
    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialExact(
    AssetHandle asset) const noexcept
{
    for (const MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findMaterialByAssetId(
    Core::AssetId assetId) noexcept
{
    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey != 0 && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::MaterialEntry* Mesh3DBindingRegistry::findFreeMaterial() noexcept
{
    for (MaterialEntry& entry : m_materialEntries)
    {
        if (entry.bindingKey == 0) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureExact(AssetHandle asset) noexcept
{
    for (TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.asset == asset) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureExact(
    AssetHandle asset) const noexcept
{
    for (const TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.asset == asset) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureByAssetId(
    Core::AssetId assetId) noexcept
{
    for (TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureByAssetId(
    Core::AssetId assetId) const noexcept
{
    for (const TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture && entry.assetId == assetId) return &entry;
    }
    return nullptr;
}

const Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findTextureByGpu(
    Render::GpuTextureId gpuTexture) const noexcept
{
    for (const TextureEntry& entry : m_textureEntries)
    {
        if (entry.gpuTexture == gpuTexture) return &entry;
    }
    return nullptr;
}

Mesh3DBindingRegistry::TextureEntry* Mesh3DBindingRegistry::findFreeTexture() noexcept
{
    for (TextureEntry& entry : m_textureEntries)
    {
        if (!entry.gpuTexture) return &entry;
    }
    return nullptr;
}

Core::u32 Mesh3DBindingRegistry::textureIndex(const TextureEntry& entry) const noexcept
{
    return static_cast<Core::u32>(&entry - m_textureEntries.data());
}

Core::u32 Mesh3DBindingRegistry::findPreparedTextureByAssetId(Core::AssetId assetId) const noexcept
{
    for (Core::usize index = 0; index < m_preparedTextureCount; ++index)
    {
        const PreparedTextureEntry& prepared = m_preparedTextures[index];
        if (!prepared.remove && prepared.replacement.assetId == assetId)
        {
            return prepared.entryIndex;
        }
    }
    return InvalidTextureIndex;
}

Mesh3DBindingRegistry::CandidateTextureEntry
Mesh3DBindingRegistry::findCandidateTextureByAssetId(Core::AssetId assetId) const noexcept
{
    const Core::u32 preparedEntryIndex = findPreparedTextureByAssetId(assetId);
    if (preparedEntryIndex != InvalidTextureIndex)
    {
        for (Core::usize index = 0; index < m_preparedTextureCount; ++index)
        {
            const PreparedTextureEntry& prepared = m_preparedTextures[index];
            if (!prepared.remove && prepared.entryIndex == preparedEntryIndex &&
                prepared.replacement.assetId == assetId)
            {
                return CandidateTextureEntry{
                    .entry = std::addressof(prepared.replacement),
                    .entryIndex = preparedEntryIndex,
                };
            }
        }
    }

    for (Core::u32 entryIndex = 0;
         entryIndex < static_cast<Core::u32>(m_textureEntries.size()); ++entryIndex)
    {
        const bool hasPreparedAction = std::any_of(
            m_preparedTextures.begin(),
            m_preparedTextures.begin() + static_cast<std::ptrdiff_t>(m_preparedTextureCount),
            [entryIndex](const PreparedTextureEntry& prepared) {
                return prepared.entryIndex == entryIndex;
            });
        const TextureEntry& active = m_textureEntries[entryIndex];
        if (!hasPreparedAction && active.gpuTexture && active.assetId == assetId)
        {
            return CandidateTextureEntry{
                .entry = std::addressof(active),
                .entryIndex = entryIndex,
            };
        }
    }
    return {};
}

Core::u32 Mesh3DBindingRegistry::findFreePreparedTextureSlot() const noexcept
{
    for (Core::usize index = 0; index < m_preparedTextureCount; ++index)
    {
        const PreparedTextureEntry& prepared = m_preparedTextures[index];
        if (prepared.remove && prepared.entryIndex < m_textureEntries.size())
        {
            return prepared.entryIndex;
        }
    }

    for (Core::u32 entryIndex = 0;
         entryIndex < static_cast<Core::u32>(m_textureEntries.size()); ++entryIndex)
    {
        if (m_textureEntries[entryIndex].gpuTexture)
        {
            continue;
        }
        const bool alreadyPrepared = std::any_of(
            m_preparedTextures.begin(),
            m_preparedTextures.begin() + static_cast<std::ptrdiff_t>(m_preparedTextureCount),
            [entryIndex](const PreparedTextureEntry& prepared) {
                return prepared.entryIndex == entryIndex;
            });
        if (!alreadyPrepared)
        {
            return entryIndex;
        }
    }
    return InvalidTextureIndex;
}

Core::Result<Mesh3DBindingRegistry::ValidatedMaterialBinding>
Mesh3DBindingRegistry::validateMaterialBinding(AssetHandle materialAsset) const noexcept
{
    return validateMaterialBindingImpl(materialAsset, false);
}

Core::Result<Mesh3DBindingRegistry::ValidatedMaterialBinding>
Mesh3DBindingRegistry::validatePreparedMaterialBinding(AssetHandle materialAsset) const noexcept
{
    return validateMaterialBindingImpl(materialAsset, true);
}

Core::Result<Mesh3DBindingRegistry::ValidatedMaterialBinding>
Mesh3DBindingRegistry::validateMaterialBindingImpl(AssetHandle materialAsset,
                                                   bool usePreparedTextures) const noexcept
{
    const char* operation = usePreparedTextures
                                ? "Mesh3DBindingRegistry::prepareCatalogReload"
                                : "Mesh3DBindingRegistry::registerMaterialBinding";
    if (!materialAsset || m_store->assetKind(materialAsset) != AssetFormat::AssetKind::Material)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "Mesh3DBindingRegistry requires a Material handle from its AssetSystem");
    }
    if (!hasRenderableCpuPayload(*m_store, materialAsset))
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry Material CPU payload is not ready");
    }
    const CookedAssetFile* file = m_store->tryGet(materialAsset);
    if (file == nullptr)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "Mesh3DBindingRegistry Material CPU payload is unavailable");
    }
    auto material = parseMaterialFromCooked(*file);
    if (!material)
    {
        return Core::failure(std::move(material.error()).withContext(
            operation, "Material payload"));
    }

    std::array<Core::AssetId, 3> dependencyIds{};
    Core::u32 dependencyCount = 0;
    for (Core::u32 index = 0; index < file->header().dependencyCount; ++index)
    {
        const auto dependency = file->dependency(index);
        if (!dependency || dependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
            dependency->flags != AssetFormat::DependencyFlags::Required ||
            dependencyCount >= dependencyIds.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "Material dependencies must be at most three required Texture2D assets");
        }
        dependencyIds[dependencyCount++] = dependency->assetId;
    }

    const Core::u32 expectedDependencyCount =
        static_cast<Core::u32>(material->hasBaseColorTexture) +
        static_cast<Core::u32>(material->hasMetallicRoughnessTexture) +
        static_cast<Core::u32>(material->hasNormalTexture);
    if (dependencyCount != expectedDependencyCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Material texture flags and cooked dependencies do not match");
    }

    ValidatedMaterialBinding validated{
        .renderBinding = Render::Mesh3DMaterialBindingDesc{
            .metallicFactor = material->metallicFactor,
            .roughnessFactor = material->roughnessFactor,
            .alphaMode = material->alphaMode == AssetFormat::MaterialAlphaMode::Blend
                             ? Render::Mesh3DAlphaMode::Blend
                             : Render::Mesh3DAlphaMode::Opaque,
        },
    };
    Core::u32 dependencyIndex = 0;
    const auto resolveRole = [&](bool required, Render::GpuTextureId& gpuTexture,
                                 const char* role) -> Core::Status {
        if (!required)
        {
            return Core::success();
        }
        const Core::AssetId dependencyAssetId = dependencyIds[dependencyIndex++];
        CandidateTextureEntry candidate{};
        if (usePreparedTextures)
        {
            candidate = findCandidateTextureByAssetId(dependencyAssetId);
        }
        else if (const TextureEntry* texture = findTextureByAssetId(dependencyAssetId);
                 texture != nullptr)
        {
            candidate = CandidateTextureEntry{
                .entry = texture,
                .entryIndex = textureIndex(*texture),
            };
        }
        if (candidate.entry == nullptr || candidate.entryIndex >= m_textureEntries.size() ||
            !isLiveTextureEntry(*candidate.entry))
        {
            auto failure = Core::failure(
                AssetErrorCode::AssetNotReady,
                "Material texture dependency has no live shared GPU owner");
            return Core::failure(std::move(failure.error()).withContext(
                operation, role));
        }
        for (Core::u32 prior = 0; prior < validated.textureCount; ++prior)
        {
            if (validated.textureIndices[prior] == candidate.entryIndex)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "Material texture roles must use distinct cooked dependencies");
            }
        }
        validated.textureIndices[validated.textureCount++] = candidate.entryIndex;
        gpuTexture = candidate.entry->gpuTexture;
        return Core::success();
    };

    if (auto status = resolveRole(material->hasBaseColorTexture,
                                  validated.renderBinding.baseColorTexture, "baseColor");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = resolveRole(material->hasMetallicRoughnessTexture,
                                  validated.renderBinding.metallicRoughnessTexture,
                                  "metallicRoughness");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = resolveRole(material->hasNormalTexture,
                                  validated.renderBinding.normalTexture, "normal");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return validated;
}

} // namespace Tina::Asset
