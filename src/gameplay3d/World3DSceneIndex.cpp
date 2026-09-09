#include <tina/gameplay3d/World3DSceneIndex.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <new>

namespace Tina::Gameplay3D {

Core::Status World3DSceneIndex::build(const Scene::World& world,
    const AssetFormat::PrefabPayloadView& prefab, std::span<const Scene::EntityId> entities)
try
{
    if (prefab.schemaVersion != AssetFormat::PrefabWire::SchemaVersion || prefab.nodes.empty() ||
        prefab.nodes.size() != entities.size() || entities.size() > AssetFormat::PrefabWire::MaxNodes)
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D index requires a current complete prefab instance");
    std::vector<World3DSceneEntry> candidate;
    candidate.reserve(entities.size());
    for (Core::usize index = 0; index < entities.size(); ++index)
    {
        const auto& node = prefab.nodes[index];
        if (!node.stableNodeId || !world.contains(entities[index]) || node.parentIndex < -1 ||
            node.parentIndex >= static_cast<Core::i32>(index) ||
            world.parent(entities[index]) != (node.parentIndex < 0 ? Scene::EntityId{} : entities[node.parentIndex]))
            return Core::failure(Scene::SceneErrorCode::CorruptHierarchy, "Scene3D index instance hierarchy differs from authored nodes");
        if (std::any_of(candidate.begin(), candidate.end(), [&](const auto& entry) { return entry.entity == entities[index]; }))
            return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D index contains a duplicate runtime entity");
        candidate.push_back({node.stableNodeId, entities[index]});
    }
    std::sort(candidate.begin(), candidate.end(), [](const auto& first, const auto& second) {
        return first.stableNodeId < second.stableNodeId;
    });
    if (std::adjacent_find(candidate.begin(), candidate.end(), [](const auto& first, const auto& second) {
            return first.stableNodeId == second.stableNodeId;
        }) != candidate.end())
        return Core::failure(Scene::SceneErrorCode::InvalidComponent, "Scene3D index contains a duplicate stable node ID");
    m_entries.swap(candidate);
    return Core::success();
} catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Scene3D index allocation failed");
}

Scene::EntityId World3DSceneIndex::find(Core::u32 stableNodeId, const Scene::World& world) const noexcept
{
    const auto entry = std::lower_bound(m_entries.begin(), m_entries.end(), stableNodeId,
        [](const auto& candidate, Core::u32 key) { return candidate.stableNodeId < key; });
    return entry != m_entries.end() && entry->stableNodeId == stableNodeId && world.contains(entry->entity)
        ? entry->entity : Scene::EntityId{};
}

Core::u32 World3DSceneIndex::stableId(Scene::EntityId entity) const noexcept
{
    for (const auto& entry : m_entries) if (entry.entity == entity) return entry.stableNodeId;
    return 0;
}

} // namespace Tina::Gameplay3D
