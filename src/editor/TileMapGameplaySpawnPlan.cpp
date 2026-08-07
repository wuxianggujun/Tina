#include <tina/editor/TileMapGameplaySpawnPlan.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <exception>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

struct SortedArchetype final {
    std::string_view name{};
    Core::u32 id = 0;
};

[[nodiscard]] Core::Status validatePlanConfig(
    const TileMapGameplaySpawnPlanConfig& config,
    std::span<const TileMapGameplayArchetypeBinding> archetypes) noexcept
{
    if (config.objectLayerId == 0U)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap gameplay generation requires a non-zero object layer id");
    }
    if (config.archetypePropertyKey.empty() ||
        config.archetypePropertyKey.size() > AssetFormat::TileMapWire::MaxStringBytes ||
        !Core::isStrictUtf8WithoutNul(config.archetypePropertyKey))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap gameplay archetype property key must be strict UTF-8");
    }
    if (config.recordCapacity == 0U ||
        config.recordCapacity > AssetFormat::TileMapWire::MaxObjectsPerMap)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap gameplay record capacity exceeds the current TileMap limit");
    }
    if (archetypes.empty() || archetypes.size() > AssetFormat::TileMapWire::MaxObjectsPerMap)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap gameplay generation requires a bounded archetype table");
    }
    for (const TileMapGameplayArchetypeBinding& binding : archetypes)
    {
        if (binding.gameArchetypeId == 0U || binding.archetype.empty() ||
            binding.archetype.size() > AssetFormat::TileMapWire::MaxStringBytes ||
            !Core::isStrictUtf8WithoutNul(binding.archetype))
        {
            return Core::failure(EditorErrorCode::InvalidConfiguration,
                                 "TileMap gameplay archetype bindings require a non-zero id and strict UTF-8 name");
        }
    }
    return Core::success();
}

[[nodiscard]] const TileMapAuthoringLayer*
findLayer(const TileMapAuthoringDesc& map, AssetFormat::TileMapLayerId layerId) noexcept
{
    const auto found = std::find_if(
        map.layers.begin(), map.layers.end(),
        [layerId](const TileMapAuthoringLayer& layer) { return layer.stableLayerId == layerId; });
    return found != map.layers.end() ? &*found : nullptr;
}

[[nodiscard]] const TileMapAuthoringProperty*
findProperty(const TileMapAuthoringObject& object, std::string_view key) noexcept
{
    const auto found = std::find_if(
        object.properties.begin(), object.properties.end(),
        [key](const TileMapAuthoringProperty& property) { return property.key == key; });
    return found != object.properties.end() ? &*found : nullptr;
}

} // namespace

TileMapGameplaySpawnPlan::TileMapGameplaySpawnPlan(
    Core::u64 sourceDocumentRevision, AssetFormat::TileMapLayerId objectLayerId,
    std::vector<TileMapGameplaySpawnRecord> records) noexcept
    : m_sourceDocumentRevision(sourceDocumentRevision), m_objectLayerId(objectLayerId),
      m_records(std::move(records))
{
}

Core::Result<TileMapGameplaySpawnPlan>
TileMapGameplaySpawnPlan::Build(
    const TileMapAuthoringDocument& tileMap,
    std::span<const TileMapGameplayArchetypeBinding> archetypes,
    TileMapGameplaySpawnPlanConfig config)
{
    if (const Core::Status status = validatePlanConfig(config, archetypes); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    try
    {
        auto authored = tileMap.snapshot();
        if (!authored)
        {
            return Core::failure(std::move(authored.error()));
        }
        const TileMapAuthoringLayer* layer = findLayer(*authored, config.objectLayerId);
        if (layer == nullptr)
        {
            return Core::failure(EditorErrorCode::LayerNotFound,
                                 "TileMap gameplay source object layer was not found");
        }
        if (layer->kind != AssetFormat::TileMapLayerKind::Object || !layer->visible)
        {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "TileMap gameplay source must be a visible object layer");
        }

        std::vector<SortedArchetype> sortedArchetypes;
        sortedArchetypes.reserve(archetypes.size());
        std::vector<Core::u32> sortedIds;
        sortedIds.reserve(archetypes.size());
        for (const TileMapGameplayArchetypeBinding& binding : archetypes)
        {
            sortedArchetypes.push_back({.name = binding.archetype, .id = binding.gameArchetypeId});
            sortedIds.push_back(binding.gameArchetypeId);
        }
        std::sort(sortedArchetypes.begin(), sortedArchetypes.end(),
                  [](const SortedArchetype& left, const SortedArchetype& right) {
                      return left.name < right.name;
                  });
        std::sort(sortedIds.begin(), sortedIds.end());
        if (std::adjacent_find(sortedArchetypes.begin(), sortedArchetypes.end(),
                               [](const SortedArchetype& left, const SortedArchetype& right) {
                                   return left.name == right.name;
                               }) != sortedArchetypes.end() ||
            std::adjacent_find(sortedIds.begin(), sortedIds.end()) != sortedIds.end())
        {
            return Core::failure(EditorErrorCode::DuplicateGameplayArchetype,
                                 "TileMap gameplay archetype names and ids must be unique");
        }

        std::vector<TileMapGameplaySpawnRecord> records;
        records.reserve((std::min)(config.recordCapacity, layer->objects.size()));
        for (const TileMapAuthoringObject& object : layer->objects)
        {
            if (!object.visible)
            {
                continue;
            }
            const TileMapAuthoringProperty* property =
                findProperty(object, config.archetypePropertyKey);
            if (property == nullptr)
            {
                return Core::failure(EditorErrorCode::UnknownGameplayArchetype,
                                     "TileMap gameplay object has no archetype property");
            }
            const auto binding = std::lower_bound(
                sortedArchetypes.begin(), sortedArchetypes.end(), property->value,
                [](const SortedArchetype& candidate, std::string_view name) {
                    return candidate.name < name;
                });
            if (binding == sortedArchetypes.end() || binding->name != property->value)
            {
                return Core::failure(EditorErrorCode::UnknownGameplayArchetype,
                                     "TileMap gameplay object references an unknown archetype");
            }
            if (records.size() == config.recordCapacity)
            {
                return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                     "TileMap gameplay spawn plan exceeds its record capacity");
            }
            records.push_back(TileMapGameplaySpawnRecord{
                .stableObjectId = object.stableObjectId,
                .gameArchetypeId = binding->id,
                .kind = object.kind,
                .x = object.x,
                .y = object.y,
                .width = object.width,
                .height = object.height,
            });
        }
        std::sort(records.begin(), records.end(),
                  [](const TileMapGameplaySpawnRecord& left,
                     const TileMapGameplaySpawnRecord& right) {
                      return left.stableObjectId < right.stableObjectId;
                  });
        return TileMapGameplaySpawnPlan{
            tileMap.revision(), config.objectLayerId, std::move(records)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "TileMap gameplay spawn plan allocation failed");
    }
}

Core::Result<TileMapGameplaySpawnPlan>
generateTileMapGameplay(const TileMapAuthoringDocument& tileMap,
                        World2DAuthoringDocument& world,
                        std::span<const TileMapGameplayArchetypeBinding> archetypes,
                        TileMapGameplaySpawnPlanConfig planConfig,
                        TileMapGameplayPublicationConfig publication,
                        const TileMapGameplaySpawnEncoder& encoder)
{
    if (publication.gameplaySchema == 0U || publication.gameplayVersion == 0U || !encoder)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "TileMap gameplay publication requires schema, version, and encoder");
    }

    auto plan = TileMapGameplaySpawnPlan::Build(tileMap, archetypes, planConfig);
    if (!plan)
    {
        return Core::failure(std::move(plan.error()));
    }
    const Core::u64 sourceRevision = plan->sourceDocumentRevision();
    const Core::u64 worldRevision = world.revision();

    try
    {
        auto encoded = encoder(plan->records());
        if (!encoded)
        {
            return Core::failure(std::move(encoded.error()));
        }
        if (tileMap.revision() != sourceRevision || world.revision() != worldRevision)
        {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "TileMap gameplay documents changed while encoding the spawn plan");
        }
        if (encoded->empty())
        {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "TileMap gameplay encoder returned an empty schema payload");
        }
        if (encoded->size() > world.config().gameplayByteCapacity)
        {
            return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                                 "Encoded TileMap gameplay exceeds the World2D document capacity");
        }

        std::vector<AssetFormat::World2DEntityDesc> entities;
        entities.reserve(world.entityCount());
        auto current = world.parseCurrentSnapshot(entities);
        if (!current)
        {
            return Core::failure(std::move(current.error()));
        }
        if (const Core::Status status = world.replace(AssetFormat::World2DSnapshotDesc{
                .entities = current->entities,
                .gameplaySchema = publication.gameplaySchema,
                .gameplayVersion = publication.gameplayVersion,
                .gameplayBytes = *encoded,
            });
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        return std::move(*plan);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "TileMap gameplay publication allocation failed");
    }
    catch (const std::exception& exception)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             exception.what());
    }
    catch (...)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "TileMap gameplay encoder threw an unknown exception");
    }
}

} // namespace Tina::Editor
