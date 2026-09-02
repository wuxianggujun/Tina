#pragma once

#include <tina/editor/TileMapAuthoringDocument.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Editor {

struct TileMapGameplayArchetypeBinding final {
    std::string_view archetype{};
    Core::u32 gameArchetypeId = 0;
};

struct TileMapGameplaySpawnRecord final {
    AssetFormat::TileMapObjectId stableObjectId = 0;
    Core::u32 gameArchetypeId = 0;
    AssetFormat::TileMapObjectKind kind = AssetFormat::TileMapObjectKind::Point;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    friend bool operator==(const TileMapGameplaySpawnRecord&,
                           const TileMapGameplaySpawnRecord&) = default;
};

struct TileMapGameplaySpawnPlanConfig final {
    AssetFormat::TileMapLayerId objectLayerId = 0;
    std::string_view archetypePropertyKey = "archetype";
    Core::usize recordCapacity = AssetFormat::TileMapWire::MaxObjectsPerMap;
};

struct TileMapGameplayPublicationConfig final {
    Core::u32 gameplaySchema = 0;
    Core::u32 gameplayVersion = 0;
};

class TileMapGameplaySpawnPlan final {
public:
    [[nodiscard]] static Core::Result<TileMapGameplaySpawnPlan>
    Build(const TileMapAuthoringDocument& tileMap,
          std::span<const TileMapGameplayArchetypeBinding> archetypes,
          TileMapGameplaySpawnPlanConfig config);

    [[nodiscard]] Core::u64 sourceDocumentRevision() const noexcept
    {
        return m_sourceDocumentRevision;
    }
    [[nodiscard]] AssetFormat::TileMapLayerId objectLayerId() const noexcept
    {
        return m_objectLayerId;
    }
    [[nodiscard]] std::span<const TileMapGameplaySpawnRecord> records() const noexcept
    {
        return m_records;
    }

private:
    TileMapGameplaySpawnPlan(Core::u64 sourceDocumentRevision,
                             AssetFormat::TileMapLayerId objectLayerId,
                             std::vector<TileMapGameplaySpawnRecord> records) noexcept;

    Core::u64 m_sourceDocumentRevision = 0;
    AssetFormat::TileMapLayerId m_objectLayerId = 0;
    std::vector<TileMapGameplaySpawnRecord> m_records{};
};

using TileMapGameplaySpawnEncoder =
    std::function<Core::Result<std::vector<std::byte>>(
        std::span<const TileMapGameplaySpawnRecord>)>;

// Builds all game-owned spawn records before invoking the caller-owned encoder.
// Tina does not mutate either document until encoding succeeds, then publishes one
// complete World2D revision transactionally. The encoder must not mutate either
// document; revision checks reject a reentrant change before Tina publishes.
[[nodiscard]] Core::Result<TileMapGameplaySpawnPlan>
generateTileMapGameplay(const TileMapAuthoringDocument& tileMap,
                        World2DAuthoringDocument& world,
                        std::span<const TileMapGameplayArchetypeBinding> archetypes,
                        TileMapGameplaySpawnPlanConfig planConfig,
                        TileMapGameplayPublicationConfig publication,
                        const TileMapGameplaySpawnEncoder& encoder);

} // namespace Tina::Editor
