#include <tina/editor/World2DAuthoringDocument.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

using AssetFormat::World2DEntityDesc;
using AssetFormat::World2DSnapshotDesc;

[[nodiscard]] Core::Status allocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "World2D authoring document allocation failed");
}

} // namespace

Core::Status validateWorld2DAuthoringDocumentConfig(const World2DAuthoringDocumentConfig& config) noexcept
{
    if (config.entityCapacity == 0U ||
        config.entityCapacity > AssetFormat::World2DSnapshotWire::MaximumEntities)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World2D authoring entity capacity is outside the current schema limit");
    }
    if (config.gameplayByteCapacity > AssetFormat::World2DSnapshotWire::MaximumGameplayBytes)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World2D authoring gameplay capacity exceeds the current schema limit");
    }
    if (config.historyEntryCapacity < World2DAuthoringLimits::MinimumHistoryEntries ||
        config.historyEntryCapacity > World2DAuthoringLimits::MaximumHistoryEntries)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World2D authoring history must contain between 2 and 256 entries");
    }
    if (config.historyByteCapacity < AssetFormat::World2DSnapshotWire::HeaderBytes * 2U ||
        config.historyByteCapacity > World2DAuthoringLimits::MaximumHistoryBytes)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "World2D authoring history byte capacity is outside the supported range");
    }
    return Core::success();
}

Core::Result<World2DAuthoringDocument>
World2DAuthoringDocument::Create(World2DAuthoringDocumentConfig config)
{
    if (const Core::Status status = validateWorld2DAuthoringDocumentConfig(config); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    auto emptyBytes = AssetFormat::writeWorld2DSnapshotBytes({});
    if (!emptyBytes)
    {
        return Core::failure(std::move(emptyBytes.error()));
    }

    try
    {
        std::vector<Revision> history;
        history.reserve(config.historyEntryCapacity);
        history.push_back(Revision{.bytes = std::move(*emptyBytes)});
        return World2DAuthoringDocument{config, std::move(history)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "World2D authoring history allocation failed");
    }
}

World2DAuthoringDocument::World2DAuthoringDocument(World2DAuthoringDocumentConfig config,
                                                   std::vector<Revision> history) noexcept
    : m_config(config), m_history(std::move(history)), m_historyBytes(m_history.front().bytes.size())
{
}

Core::usize World2DAuthoringDocument::entityCount() const noexcept
{
    return current().entityCount;
}

Core::usize World2DAuthoringDocument::gameplayByteCount() const noexcept
{
    return current().gameplayByteCount;
}

Core::u32 World2DAuthoringDocument::gameplaySchema() const noexcept
{
    return current().gameplaySchema;
}

Core::u32 World2DAuthoringDocument::gameplayVersion() const noexcept
{
    return current().gameplayVersion;
}

std::span<const std::byte> World2DAuthoringDocument::snapshotBytes() const noexcept
{
    return current().bytes;
}

Core::Result<AssetFormat::World2DSnapshotView>
World2DAuthoringDocument::parseCurrentSnapshot(std::vector<World2DEntityDesc>& entityStorage) const
{
    return AssetFormat::parseWorld2DSnapshot(current().bytes, entityStorage);
}

Core::Status World2DAuthoringDocument::replace(const World2DSnapshotDesc& desc)
{
    if (desc.entities.size() > m_config.entityCapacity ||
        desc.gameplayBytes.size() > m_config.gameplayByteCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "World2D authoring edit exceeds the configured document capacity");
    }

    auto bytes = AssetFormat::writeWorld2DSnapshotBytes(desc);
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()));
    }
    return commit(Revision{
        .bytes = std::move(*bytes),
        .entityCount = static_cast<Core::u32>(desc.entities.size()),
        .gameplaySchema = desc.gameplaySchema,
        .gameplayVersion = desc.gameplayVersion,
        .gameplayByteCount = static_cast<Core::u32>(desc.gameplayBytes.size()),
    });
}

Core::Status World2DAuthoringDocument::loadSnapshot(std::span<const std::byte> snapshotBytes)
{
    std::vector<World2DEntityDesc> entities;
    auto parsed = AssetFormat::parseWorld2DSnapshot(snapshotBytes, entities);
    if (!parsed)
    {
        return Core::failure(std::move(parsed.error()));
    }
    if (parsed->entities.size() > m_config.entityCapacity ||
        parsed->gameplayBytes.size() > m_config.gameplayByteCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "World2D authoring snapshot exceeds the configured document capacity");
    }
    auto canonicalBytes = AssetFormat::writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .entities = parsed->entities,
        .gameplaySchema = parsed->gameplaySchema,
        .gameplayVersion = parsed->gameplayVersion,
        .gameplayBytes = parsed->gameplayBytes,
    });
    if (!canonicalBytes)
    {
        return Core::failure(std::move(canonicalBytes.error()));
    }
    return resetBaseline(Revision{
        .bytes = std::move(*canonicalBytes),
        .entityCount = static_cast<Core::u32>(parsed->entities.size()),
        .gameplaySchema = parsed->gameplaySchema,
        .gameplayVersion = parsed->gameplayVersion,
        .gameplayByteCount = static_cast<Core::u32>(parsed->gameplayBytes.size()),
    });
}

Core::Status World2DAuthoringDocument::upsertEntity(const World2DEntityDesc& entity)
{
    try
    {
        std::vector<World2DEntityDesc> entities;
        auto parsed = parseCurrentSnapshot(entities);
        if (!parsed)
        {
            return Core::failure(std::move(parsed.error()));
        }

        const auto existing = std::find_if(entities.begin(), entities.end(), [&entity](const auto& candidate) {
            return candidate.stableEntityId == entity.stableEntityId;
        });
        if (existing == entities.end())
        {
            if (entities.size() >= m_config.entityCapacity)
            {
                return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                     "World2D authoring entity capacity is exhausted");
            }
            entities.push_back(entity);
        }
        else
        {
            *existing = entity;
        }

        return replace(World2DSnapshotDesc{
            .entities = entities,
            .gameplaySchema = parsed->gameplaySchema,
            .gameplayVersion = parsed->gameplayVersion,
            .gameplayBytes = parsed->gameplayBytes,
        });
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status World2DAuthoringDocument::eraseEntitySubtree(Core::u32 stableEntityId)
{
    try
    {
        std::vector<World2DEntityDesc> entities;
        auto parsed = parseCurrentSnapshot(entities);
        if (!parsed)
        {
            return Core::failure(std::move(parsed.error()));
        }
        if (std::none_of(entities.begin(), entities.end(), [stableEntityId](const auto& entity) {
                return entity.stableEntityId == stableEntityId;
            }))
        {
            return Core::failure(EditorErrorCode::EntityNotFound,
                                 "World2D authoring entity does not exist");
        }

        std::vector<Core::u32> removedIds;
        std::vector<World2DEntityDesc> retained;
        removedIds.reserve(entities.size());
        retained.reserve(entities.size());
        for (const World2DEntityDesc& entity : entities)
        {
            const bool remove = entity.stableEntityId == stableEntityId ||
                                std::find(removedIds.begin(), removedIds.end(), entity.parentStableEntityId) !=
                                    removedIds.end();
            if (remove)
            {
                removedIds.push_back(entity.stableEntityId);
            }
            else
            {
                retained.push_back(entity);
            }
        }

        return replace(World2DSnapshotDesc{
            .entities = retained,
            .gameplaySchema = parsed->gameplaySchema,
            .gameplayVersion = parsed->gameplayVersion,
            .gameplayBytes = parsed->gameplayBytes,
        });
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure();
    }
}

Core::Status World2DAuthoringDocument::setGameplay(Core::u32 schema, Core::u32 version,
                                                   std::span<const std::byte> bytes)
{
    if (bytes.size() > m_config.gameplayByteCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "World2D authoring gameplay capacity is exhausted");
    }

    std::vector<World2DEntityDesc> entities;
    auto parsed = parseCurrentSnapshot(entities);
    if (!parsed)
    {
        return Core::failure(std::move(parsed.error()));
    }
    return replace(World2DSnapshotDesc{
        .entities = entities,
        .gameplaySchema = schema,
        .gameplayVersion = version,
        .gameplayBytes = bytes,
    });
}

Core::Status World2DAuthoringDocument::undo() noexcept
{
    if (!canUndo())
    {
        return Core::failure(EditorErrorCode::UndoUnavailable,
                             "World2D authoring document has no undo revision");
    }
    --m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status World2DAuthoringDocument::redo() noexcept
{
    if (!canRedo())
    {
        return Core::failure(EditorErrorCode::RedoUnavailable,
                             "World2D authoring document has no redo revision");
    }
    ++m_historyCursor;
    advanceRevision();
    return Core::success();
}

Core::Status World2DAuthoringDocument::commit(Revision candidate)
{
    if (candidate.bytes == current().bytes)
    {
        return Core::success();
    }
    if (current().bytes.size() + candidate.bytes.size() > m_config.historyByteCapacity)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "World2D authoring history cannot retain an undoable edit");
    }

    const Core::usize retainedEnd = m_historyCursor + 1U;
    for (Core::usize index = retainedEnd; index < m_history.size(); ++index)
    {
        m_historyBytes -= m_history[index].bytes.size();
    }
    m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(retainedEnd), m_history.end());

    // The vector was reserved to historyEntryCapacity during Create(). Evict
    // before push so a successful edit never performs an implicit expansion.
    while (m_history.size() > 1U &&
           (m_history.size() >= m_config.historyEntryCapacity ||
            m_historyBytes + candidate.bytes.size() > m_config.historyByteCapacity))
    {
        m_historyBytes -= m_history.front().bytes.size();
        m_history.erase(m_history.begin());
        --m_historyCursor;
    }

    m_historyBytes += candidate.bytes.size();
    m_history.push_back(std::move(candidate));
    m_historyCursor = m_history.size() - 1U;

    advanceRevision();
    return Core::success();
}

Core::Status World2DAuthoringDocument::resetBaseline(Revision candidate)
{
    if (candidate.bytes.size() > m_config.historyByteCapacity)
    {
        return Core::failure(EditorErrorCode::HistoryCapacityExceeded,
                             "World2D authoring baseline exceeds the configured history byte capacity");
    }
    if (m_history.size() == 1U && candidate.bytes == current().bytes)
    {
        return Core::success();
    }

    m_history.clear();
    m_history.push_back(std::move(candidate));
    m_historyCursor = 0;
    m_historyBytes = m_history.front().bytes.size();
    advanceRevision();
    return Core::success();
}

void World2DAuthoringDocument::advanceRevision() noexcept
{
    if (m_revision != (std::numeric_limits<Core::u64>::max)())
    {
        ++m_revision;
    }
}

} // namespace Tina::Editor
