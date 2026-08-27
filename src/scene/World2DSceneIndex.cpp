#include <tina/scene/World2DSceneIndex.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Scene {

World2DSceneIndex::World2DSceneIndex(std::vector<Entry> entries, std::string names) noexcept
    : m_entries(std::move(entries)), m_names(std::move(names))
{
}

Core::Result<World2DSceneIndex> World2DSceneIndex::Create(const AssetFormat::World2DSnapshotView& snapshot,
                                                         std::span<const World2DEntityBinding> bindings)
try
{
    std::vector<Entry> entries;
    entries.reserve(bindings.size());
    std::string names;
    for (const World2DEntityBinding& binding : bindings)
    {
        const auto source = std::ranges::find(snapshot.entities, binding.stableEntityId,
                                              &AssetFormat::World2DEntityDesc::stableEntityId);
        if (source == snapshot.entities.end())
        {
            return Core::failure(SceneErrorCode::CorruptHierarchy,
                                 "World2D scene index binding is absent from the snapshot");
        }
        const std::string_view name = source->name;
        entries.push_back(Entry{
            .stableEntityId = binding.stableEntityId,
            .entity = binding.entity,
            .nameOffset = static_cast<Core::u32>(names.size()),
            .nameLength = static_cast<Core::u32>(name.size()),
        });
        names.append(name);
    }
    std::ranges::sort(entries, {}, &Entry::stableEntityId);
    // Duplicate stable IDs would make entityForStableId ambiguous, and the wire
    // format already forbids them, so a repeat here means the inputs disagree.
    const auto duplicate = std::ranges::adjacent_find(entries, {}, &Entry::stableEntityId);
    if (duplicate != entries.end())
    {
        return Core::failure(SceneErrorCode::CorruptHierarchy,
                             "World2D scene index received duplicate stable entity IDs");
    }
    return World2DSceneIndex{std::move(entries), std::move(names)};
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D scene index allocation failed");
}

std::string_view World2DSceneIndex::nameOf(const Entry& entry) const noexcept
{
    return std::string_view{m_names}.substr(entry.nameOffset, entry.nameLength);
}

EntityId World2DSceneIndex::entityForStableId(Core::u32 stableEntityId) const noexcept
{
    const auto found = std::ranges::lower_bound(m_entries, stableEntityId, {}, &Entry::stableEntityId);
    if (found == m_entries.end() || found->stableEntityId != stableEntityId)
    {
        return EntityId{};
    }
    return found->entity;
}

EntityId World2DSceneIndex::entityForName(std::string_view name) const noexcept
{
    if (name.empty())
    {
        return EntityId{};
    }
    // Entries are stable-ID ordered, so the first match is the lowest stable ID.
    for (const Entry& entry : m_entries)
    {
        if (nameOf(entry) == name)
        {
            return entry.entity;
        }
    }
    return EntityId{};
}

Core::usize World2DSceneIndex::entityCountForName(std::string_view name) const noexcept
{
    if (name.empty())
    {
        return 0;
    }
    Core::usize count = 0;
    for (const Entry& entry : m_entries)
    {
        if (nameOf(entry) == name)
        {
            ++count;
        }
    }
    return count;
}

Core::u32 World2DSceneIndex::stableIdForEntity(EntityId entity) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.entity == entity)
        {
            return entry.stableEntityId;
        }
    }
    return 0;
}

std::string_view World2DSceneIndex::nameForEntity(EntityId entity) const noexcept
{
    for (const Entry& entry : m_entries)
    {
        if (entry.entity == entity)
        {
            return nameOf(entry);
        }
    }
    return {};
}

} // namespace Tina::Scene
