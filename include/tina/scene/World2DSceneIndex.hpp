#pragma once

#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/scene/Entity.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Scene {

// Maps one authored stable ID to the entity instantiated for it. The runtime
// EntityId must not be persisted: it carries a generation that is only meaningful
// to the World that produced it.
//
// Declared here rather than beside instantiateWorld2DSnapshot() because both the
// snapshot API and the scene index need it, and the index must not depend on the
// snapshot header.
struct World2DEntityBinding final {
    Core::u32 stableEntityId = 0;
    EntityId entity{};

    friend bool operator==(const World2DEntityBinding&, const World2DEntityBinding&) = default;
};

// Relates authored identity to instantiated entities, which is the minimum a game
// needs to attach its own state to a scene it did not build in code.
//
// This is a snapshot of one instantiate call, not a live view: it holds no World
// reference and does not observe entity destruction. That is safe because
// EntityId is a generation handle, so a stale id is rejected by World rather than
// silently aliasing a recycled slot.
//
// Names are owned here rather than stored on the entity. Most entities are
// unnamed, and putting a 64-byte field on every EntityRecord would make all of
// them pay for it.
class World2DSceneIndex final {
  public:
    World2DSceneIndex() noexcept = default;

    // Builds from the snapshot that was instantiated and the bindings it returned.
    // Fails when a binding has no matching entity in the snapshot, since that
    // means the two inputs came from different calls and every later lookup would
    // be answering about the wrong scene.
    [[nodiscard]] static Core::Result<World2DSceneIndex>
    Create(const AssetFormat::World2DSnapshotView& snapshot,
           std::span<const World2DEntityBinding> bindings);

    [[nodiscard]] Core::usize entityCount() const noexcept { return m_entries.size(); }

    // Unset when the stable ID was not part of the instantiated snapshot.
    [[nodiscard]] EntityId entityForStableId(Core::u32 stableEntityId) const noexcept;

    // Authored names are matched exactly: no case folding and no trimming, because
    // the Editor treats them as opaque UTF-8 and folding would invent collisions.
    // Duplicate names are legal there, so this returns the entity with the lowest
    // stable ID and entityCountForName() reports the ambiguity.
    [[nodiscard]] EntityId entityForName(std::string_view name) const noexcept;
    [[nodiscard]] Core::usize entityCountForName(std::string_view name) const noexcept;

    [[nodiscard]] Core::u32 stableIdForEntity(EntityId entity) const noexcept;
    // Empty for an unnamed entity and for an entity this index does not know.
    [[nodiscard]] std::string_view nameForEntity(EntityId entity) const noexcept;

  private:
    struct Entry final {
        Core::u32 stableEntityId = 0;
        EntityId entity{};
        // Offsets into m_names, so one contiguous buffer owns every name.
        Core::u32 nameOffset = 0;
        Core::u32 nameLength = 0;
    };

    explicit World2DSceneIndex(std::vector<Entry> entries, std::string names) noexcept;

    [[nodiscard]] std::string_view nameOf(const Entry& entry) const noexcept;

    // Sorted by stable ID so lookup is a binary search and iteration order is
    // deterministic across runs.
    std::vector<Entry> m_entries{};
    std::string m_names{};
};

} // namespace Tina::Scene
