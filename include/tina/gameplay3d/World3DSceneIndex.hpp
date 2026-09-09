#pragma once

#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/scene/World.hpp>

#include <span>
#include <vector>

namespace Tina::Gameplay3D {

struct World3DSceneEntry final {
    Core::u32 stableNodeId = 0;
    Scene::EntityId entity{};
};

// One instantiated prefab, never an index-only runtime identity. Rebuild after
// structural edits; dead entities resolve to an empty handle until then.
class World3DSceneIndex final {
  public:
    [[nodiscard]] Core::Status build(const Scene::World& world,
        const AssetFormat::PrefabPayloadView& prefab, std::span<const Scene::EntityId> entities);
    [[nodiscard]] Scene::EntityId find(Core::u32 stableNodeId, const Scene::World& world) const noexcept;
    [[nodiscard]] Core::u32 stableId(Scene::EntityId entity) const noexcept;
    [[nodiscard]] std::span<const World3DSceneEntry> entries() const noexcept { return m_entries; }
  private:
    std::vector<World3DSceneEntry> m_entries;
};

} // namespace Tina::Gameplay3D
