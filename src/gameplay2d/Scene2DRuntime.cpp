#include <tina/gameplay2d/Scene2DRuntime.hpp>

#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/audio/AudioClipView.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Gameplay2D {
namespace {

[[nodiscard]] AssetFormat::AssetKind expectedKindFor(Scene::ResourceBindingKind2D kind) noexcept
{
    switch (kind)
    {
    case Scene::ResourceBindingKind2D::FxEmitter:
        return AssetFormat::AssetKind::Fx2D;
    case Scene::ResourceBindingKind2D::NavigationRegion:
        return AssetFormat::AssetKind::NavigationGrid2D;
    case Scene::ResourceBindingKind2D::AudioPlayer:
        return AssetFormat::AssetKind::AudioClip;
    case Scene::ResourceBindingKind2D::TileMap:
    default:
        return AssetFormat::AssetKind::TileMap;
    }
}

// A TileMap declares exactly one required Tileset dependency (enforced by the
// cooker), so the runtime discovers it instead of making the caller supply it.
[[nodiscard]] Core::Result<Core::AssetId> requiredTilesetId(const Asset::CookedAssetFile& file)
{
    for (Core::u32 index = 0; index < file.header().dependencyCount; ++index)
    {
        const auto dependency = file.dependency(index);
        if (!dependency.has_value())
        {
            break;
        }
        if (dependency->expectedKind == AssetFormat::AssetKind::Tileset &&
            dependency->flags == AssetFormat::DependencyFlags::Required)
        {
            return dependency->assetId;
        }
    }
    return Core::failure(Asset::AssetErrorCode::CatalogEntryMismatch,
                         "TileMap asset has no required Tileset dependency");
}

// A TileMap node binds a whole map, and the wire format carries no field to select
// a layer, so the runtime drives every tile layer the map authored. Object layers
// are skipped: they hold gameplay spawn data, not cells, and the stream rejects
// demand for them.
[[nodiscard]] Core::Status collectTileLayers(const Asset::CookedAssetFile& file,
                                            Core::usize capacity,
                                            std::vector<Scene2DTileLayer>& out)
{
    out.clear();
    auto payload = Asset::parseTileMapFromCooked(file);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    for (Core::u16 index = 0; index < payload->layerCount; ++index)
    {
        const auto layer = payload->layerAt(index);
        if (!layer.has_value() || layer->kind != AssetFormat::TileMapLayerKind::Tile)
        {
            continue;
        }
        if (out.size() == capacity)
        {
            return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                 "TileMap authors more tile layers than tileLayersPerMapCapacity");
        }
        out.push_back(
            Scene2DTileLayer{.layerId = layer->stableLayerId, .visible = layer->visible});
    }
    if (out.empty())
    {
        return Core::failure(Asset::AssetErrorCode::CatalogEntryMismatch,
                             "TileMap asset authors no tile layer");
    }
    return Core::success();
}

// Where an authored node sits in the world. WorldTransform is used rather than
// LocalTransform so a node parented under a moved root lands correctly; the caller
// must have run updateWorldTransforms(). A missing transform means the entity was
// never published, which is treated as the origin rather than an error because the
// resource binding itself is still valid.
[[nodiscard]] Math::Vec2 worldOrigin(const Scene::World& world, Scene::EntityId entity) noexcept
{
    const Scene::WorldTransform* transform = world.worldTransform(entity);
    if (transform == nullptr)
    {
        return {};
    }
    return {transform->position.x, transform->position.y};
}

// Separates one layer's stable tile keys from the next. A layer holds at most
// MaxDimension^2 cells, so this stride cannot overlap, and layer ids are bounded by
// MaxLayers -- the product stays far inside u64.
constexpr Core::u64 TileLayerStableKeyStride =
    static_cast<Core::u64>(AssetFormat::TileMapWire::MaxDimension) *
    AssetFormat::TileMapWire::MaxDimension;

} // namespace

std::pmr::memory_resource& Scene2DRuntime::memory() const noexcept
{
    return m_config.memoryResource != nullptr ? *m_config.memoryResource
                                              : *std::pmr::get_default_resource();
}

Core::Status Scene2DRuntime::build(const Scene::World& world, Asset::AssetSystem& assets,
                                   Audio::AudioEngine* audioEngine,
                                   Physics2D::PhysicsWorld2D* physicsWorld,
                                   Scene2DRuntimeConfig config)
try
{
    if (m_assets != nullptr)
    {
        return Core::failure(Core::CoreErrorCode::AlreadyExists,
                             "Scene2DRuntime is already built; shutdown first");
    }
    if (config.tileSpriteCapacity == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Scene2DRuntime tile sprite capacity must be non-zero");
    }
    if (config.tileLayersPerMapCapacity == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Scene2DRuntime tile layer capacity must be non-zero");
    }
    m_config = config;
    m_assets = &assets;
    m_audio = audioEngine;
    m_physics = physicsWorld;
    m_stats = {};

    // Anything acquired before a failure is released, so a failed build never
    // leaves a partially wired scene holding leases.
    const auto rollback = [this]() noexcept { static_cast<void>(shutdown()); };

    for (const Scene::EntityId entity : world.liveEntities())
    {
        const Scene::ResourceBinding2D* binding = world.resourceBinding2D(entity);
        if (binding == nullptr)
        {
            continue;
        }
        auto handle = assets.find(binding->assetId);
        if (!handle.has_value() || assets.tryGet(*handle) == nullptr ||
            assets.store().assetKind(*handle) != expectedKindFor(binding->kind))
        {
            // One stale or wrong-kind reference must not make the whole scene
            // unloadable, so it is counted and skipped.
            ++m_stats.unresolvedCount;
            continue;
        }

        switch (binding->kind)
        {
        case Scene::ResourceBindingKind2D::TileMap: {
            if (m_tileMaps.size() == m_config.tileMapCapacity)
            {
                rollback();
                return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                     "Scene2DRuntime TileMap capacity is exhausted");
            }
            const Asset::CookedAssetFile* file = assets.tryGet(*handle);
            auto tilesetId = requiredTilesetId(*file);
            if (!tilesetId)
            {
                ++m_stats.unresolvedCount;
                break;
            }
            auto tilesetHandle = assets.find(*tilesetId);
            if (!tilesetHandle.has_value() || assets.tryGet(*tilesetHandle) == nullptr)
            {
                ++m_stats.unresolvedCount;
                break;
            }
            // TileMapStream requires both leases already acquired and kind-checked.
            auto rootLease = assets.acquire(*handle);
            if (!rootLease)
            {
                rollback();
                return Core::failure(std::move(rootLease.error()));
            }
            auto tilesetLease = assets.acquire(*tilesetHandle);
            if (!tilesetLease)
            {
                rollback();
                return Core::failure(std::move(tilesetLease.error()));
            }
            Asset::TileMapStreamConfig streamConfig = m_config.tileMapStream;
            if (streamConfig.memoryResource == nullptr)
            {
                streamConfig.memoryResource = &memory();
            }
            auto stream = Asset::TileMapStream::Create(assets, std::move(*rootLease),
                                                       std::move(*tilesetLease), streamConfig);
            if (!stream)
            {
                rollback();
                return Core::failure(std::move(stream.error()));
            }
            TileMapEntry entry{.entity = entity, .active = binding->active};
            // Discovered before the stream is stored so a malformed map fails the
            // build instead of returning an error from the first frame's demand.
            if (auto status = collectTileLayers(*file, m_config.tileLayersPerMapCapacity,
                                                entry.layers);
                !status)
            {
                rollback();
                return status;
            }
            entry.stream.emplace(std::move(*stream));
            entry.tileset = *tilesetHandle;
            // Map-local origin comes from the node's published world transform, so
            // moving the node in the Editor moves the tiles.
            const Math::Vec2 origin = worldOrigin(world, entity);
            entry.originX = origin.x;
            entry.originY = origin.y;
            m_tileMaps.push_back(std::move(entry));
            break;
        }
        case Scene::ResourceBindingKind2D::FxEmitter: {
            if (m_fx.size() == m_config.fxCapacity)
            {
                rollback();
                return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                     "Scene2DRuntime Fx capacity is exhausted");
            }
            auto desc = Asset::parseFx2DFromCooked(*assets.tryGet(*handle));
            if (!desc)
            {
                ++m_stats.unresolvedCount;
                break;
            }
            // The factory needs an already-resolved sprite; the payload names it as
            // a dependency and will not load it itself.
            auto spriteHandle = assets.find(desc->spriteAssetId);
            if (!spriteHandle.has_value() || assets.tryGet(*spriteHandle) == nullptr)
            {
                ++m_stats.unresolvedCount;
                break;
            }
            // Particle and trail sprites are weak handles, so the lease lives here.
            auto spriteLease = assets.acquire(*spriteHandle);
            if (!spriteLease)
            {
                rollback();
                return Core::failure(std::move(spriteLease.error()));
            }
            auto instance = Scene::createFx2DFromAsset(*desc, *spriteHandle, memory());
            if (!instance)
            {
                rollback();
                return Core::failure(std::move(instance.error()));
            }
            // The authored node transform places the emitter; the payload origin is
            // an offset within it. Ignoring the transform would make dragging an
            // FxEmitter2D in the Editor have no runtime effect.
            const Math::Vec2 origin = worldOrigin(world, entity);
            instance->initialBurst.origin.x += origin.x;
            instance->initialBurst.origin.y += origin.y;
            // The factory returns the initial burst but does not emit it.
            if (auto status = instance->particles.emitBurst(instance->initialBurst); !status)
            {
                rollback();
                return status;
            }
            FxEntry entry{.entity = entity, .active = binding->active};
            entry.instance.emplace(std::move(*instance));
            entry.spriteLease = std::move(*spriteLease);
            entry.origin = origin;
            m_fx.push_back(std::move(entry));
            break;
        }
        case Scene::ResourceBindingKind2D::NavigationRegion: {
            if (m_navigation.size() == m_config.navigationCapacity)
            {
                rollback();
                return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                     "Scene2DRuntime navigation capacity is exhausted");
            }
            auto data = Asset::loadNavigationGrid2DDataFromCooked(*assets.tryGet(*handle), memory());
            if (!data)
            {
                ++m_stats.unresolvedCount;
                break;
            }
            // The grid copies what it needs, so no lease is retained: navigation is
            // one-shot rather than streamed.
            auto grid = Navigation2D::NavigationGrid2D::Create(std::move(*data), {}, memory());
            if (!grid)
            {
                rollback();
                return Core::failure(std::move(grid.error()));
            }
            NavigationEntry entry{.entity = entity, .active = binding->active};
            entry.grid.emplace(std::move(*grid));
            m_navigation.push_back(std::move(entry));
            break;
        }
        case Scene::ResourceBindingKind2D::AudioPlayer: {
            if (audioEngine == nullptr)
            {
                // A product may compose no Audio module; that is not a scene error.
                ++m_stats.unresolvedCount;
                break;
            }
            if (m_audio_nodes.size() == m_config.audioCapacity)
            {
                rollback();
                return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                     "Scene2DRuntime audio capacity is exhausted");
            }
            // AudioPcmClipView is non-owning, so the cooked payload must stay
            // resident for the whole playback, not just until enqueuePlay returns.
            auto clipLease = assets.acquire(*handle);
            if (!clipLease)
            {
                rollback();
                return Core::failure(std::move(clipLease.error()));
            }
            AudioEntry entry{.entity = entity, .active = binding->active};
            entry.clipLease = std::move(*clipLease);
            m_audio_nodes.push_back(std::move(entry));
            break;
        }
        }
    }

    try
    {
        m_tileSprites = std::pmr::vector<Render::RenderSprite2DInput>{
            std::pmr::polymorphic_allocator<Render::RenderSprite2DInput>{&memory()}};
        m_tileSprites.reserve(m_config.tileSpriteCapacity);
        // Reserved up front so per-frame demand publication never allocates.
        m_demands.reserve(m_config.tileLayersPerMapCapacity);
        m_voices.reserve(m_config.audioVoiceCapacity);
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Scene2DRuntime tile sprite storage allocation failed");
    }

    // Physics last, so a bridge failure unwinds the resource leases above through
    // the same rollback rather than needing its own path.
    if (m_physics != nullptr)
    {
        if (auto status = m_bridge.build(world, *m_physics, m_config.physics); !status)
        {
            rollback();
            return status;
        }
    }

    m_stats.tileMapCount = m_tileMaps.size();
    for (const TileMapEntry& entry : m_tileMaps)
    {
        m_stats.tileLayerCount += entry.layers.size();
    }
    m_stats.fxCount = m_fx.size();
    m_stats.navigationCount = m_navigation.size();
    m_stats.audioCount = m_audio_nodes.size();
    return Core::success();
}
catch (const std::bad_alloc&)
{
    static_cast<void>(shutdown());
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Scene2DRuntime build allocation failed");
}

Core::Status Scene2DRuntime::updateDemand(const Asset::TileChunkCameraQuery& camera)
{
    m_committedThisFrame = false;
    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.active || !entry.stream.has_value())
        {
            continue;
        }
        // Camera bounds arrive in world meters; chunk demand is map-local.
        Asset::TileChunkCameraQuery local = camera;
        local.centerX -= entry.originX;
        local.centerY -= entry.originY;
        // One demand per tile layer in a single call: updateDemand is transactional
        // over the whole span, so splitting it per layer would let the second layer
        // hit capacity after the first already replaced the active set.
        m_demands.clear();
        for (const Scene2DTileLayer& layer : entry.layers)
        {
            // Visible layers first so they win the request budget when residency
            // cannot cover every layer this frame.
            m_demands.push_back(Asset::TileMapChunkDemand{
                .layerId = layer.layerId,
                .priority = layer.visible ? 1U : 0U,
                .camera = local,
            });
        }
        if (auto status = entry.stream->updateDemand(m_demands); !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status Scene2DRuntime::commitReady()
{
    Core::usize resident = 0;
    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.active || !entry.stream.has_value())
        {
            continue;
        }
        auto stats = entry.stream->commitReady();
        if (!stats)
        {
            return Core::failure(std::move(stats.error()));
        }
        resident += stats->residentSlots;
    }
    m_stats.residentTileChunks = resident;
    m_committedThisFrame = true;
    return Core::success();
}

Core::Status Scene2DRuntime::fixedUpdate(Core::Duration delta)
{
    for (FxEntry& entry : m_fx)
    {
        if (!entry.active || !entry.instance.has_value())
        {
            continue;
        }
        auto particles = entry.instance->particles.update(delta);
        if (!particles)
        {
            return Core::failure(std::move(particles.error()));
        }
        if (auto status = entry.instance->trail.update(delta); !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status Scene2DRuntime::fixedUpdatePhysics(Scene::World& world)
{
    if (m_physics == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "Scene2DRuntime was built without a PhysicsWorld2D");
    }
    if (auto status = m_physics->step(); !status)
    {
        return status;
    }
    // Immediately after the step, before anything reads a transform: applyTo copies
    // simulated position/angle into LocalTransform.
    if (auto status = m_bridge.applyTo(world, *m_physics); !status)
    {
        return status;
    }
    // And republish, or children still sit at the world position their parent body
    // occupied before the step.
    if (auto status = world.updateWorldTransforms(); !status)
    {
        return status;
    }
    ++m_stats.physicsSteps;
    return Core::success();
}

Core::Status Scene2DRuntime::extract(const Scene::World& world, Render::RenderSceneWriter& writer,
                                     Render::FrameResourceSink& frameResources,
                                     const Asset::AssetFrameResourceResolver& resolver)
{
    // Extracting before commitReady would draw a stale or partial map, and that
    // failure is invisible on screen, so it is reported instead.
    if (!m_tileMaps.empty() && !m_committedThisFrame)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Scene2DRuntime::extract requires commitReady() earlier in the frame");
    }
    static_cast<void>(world);

    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.active || !entry.stream.has_value())
        {
            continue;
        }
        const Asset::TileMapInstance& map = entry.stream->map();
        // Every resident chunk of the map spans the same world rectangle, so the
        // emission window is computed once per node from the map extent rather than
        // from an unbounded constant that would depend on float range.
        const float extentX =
            static_cast<float>(map.widthCells()) * map.cellSizeMeters() * 0.5F;
        const float extentY =
            static_cast<float>(map.heightCells()) * map.cellSizeMeters() * 0.5F;
        Asset::TileChunkCameraQuery local{};
        // Map-local: the whole map, because residency already bounded what is
        // loaded. Re-culling here against a camera extract was not given would drop
        // chunks the caller paid to stream in.
        local.centerX = extentX;
        local.centerY = extentY;
        local.halfWidth = extentX;
        local.halfHeight = extentY;

        Asset::TileChunkSpriteEmitParams params{};
        params.tileset = entry.tileset;
        params.bindingResolver = resolver;
        params.originX = entry.originX;
        params.originY = entry.originY;
        Core::i16 sortingLayer = 0;
        for (const Scene2DTileLayer& layer : entry.layers)
        {
            // An invisible layer still streams -- games query it for collision -- but
            // emitting it would draw the collision mask over the visual one.
            if (!layer.visible)
            {
                continue;
            }
            // Authored layer order decides draw order. Without this every layer
            // would share sortingLayer 0 and overlap resolution would fall back to
            // the tile stable key, which is layer-independent.
            params.sortingLayer = sortingLayer++;
            // Distinct per layer, or two layers' tiles at the same cell would claim
            // the same stable key and the sprite sort would treat them as one item.
            params.stableEntityKeyBase =
                static_cast<Core::u64>(layer.layerId) * TileLayerStableKeyStride;
            auto emitted = Asset::emitVisibleTileMapSprites(map, layer.layerId, local, params,
                                                            frameResources, m_tileSprites);
            if (!emitted)
            {
                return Core::failure(std::move(emitted.error()));
            }
            for (const Render::RenderSprite2DInput& sprite : m_tileSprites)
            {
                if (auto status = writer.addSprite2D(sprite); !status)
                {
                    return status;
                }
            }
            m_stats.tileSpritesEmitted += *emitted;
        }
    }

    for (FxEntry& entry : m_fx)
    {
        if (!entry.active || !entry.instance.has_value())
        {
            continue;
        }
        auto particles = entry.instance->particles.extract(writer, frameResources, resolver);
        if (!particles)
        {
            return Core::failure(std::move(particles.error()));
        }
        m_stats.particleSpritesEmitted += particles->submitted;
        if (auto status = entry.instance->trail.extract(writer, frameResources, resolver); !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Result<Audio::AudioVoiceId> Scene2DRuntime::playAudio(Scene::EntityId entity)
{
    if (m_audio == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "Scene2DRuntime was built without an AudioEngine");
    }
    const auto found = std::ranges::find(m_audio_nodes, entity, &AudioEntry::entity);
    if (found == m_audio_nodes.end() || !found->clipLease)
    {
        return Core::failure(Core::CoreErrorCode::NotFound,
                             "Scene entity is not an instantiated AudioPlayer2D node");
    }
    if (!found->active)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "AudioPlayer2D node is authored inactive");
    }
    // A tracked voice keeps this node's lease reachable for shutdown, so refusing
    // here is better than starting playback the runtime cannot later stop safely.
    if (m_voices.size() == m_config.audioVoiceCapacity)
    {
        if (auto status = releaseFinishedVoices(); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (m_voices.size() == m_config.audioVoiceCapacity)
        {
            return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                 "Scene2DRuntime tracked audio voice capacity is exhausted");
        }
    }
    const Asset::CookedAssetFile* file = found->clipLease.get();
    if (file == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Scene2DRuntime audio lease no longer resolves a cooked payload");
    }
    auto payload = Asset::parseAudioClipFromCooked(*file);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    auto clip = Audio::pcmClipViewFromAudioClipPayload(*payload);
    if (!clip)
    {
        return Core::failure(std::move(clip.error()));
    }
    auto voice = m_audio->playOneShotPcm(*clip);
    if (!voice)
    {
        return voice;
    }
    try
    {
        m_voices.push_back(*voice);
    } catch (const std::bad_alloc&)
    {
        // Losing track of the voice would leave it reading the clip payload after
        // shutdown released the lease, so the voice is stopped instead of kept.
        static_cast<void>(m_audio->enqueueStop(*voice));
        static_cast<void>(m_audio->pumpCompletions());
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Scene2DRuntime could not track the started audio voice");
    }
    return voice;
}

Core::Status Scene2DRuntime::releaseFinishedVoices()
{
    if (m_audio == nullptr)
    {
        return Core::success();
    }
    // isVoiceLive is false once a terminal completion retired the transient voice,
    // which is exactly when its clip payload is no longer being read.
    for (auto it = m_voices.begin(); it != m_voices.end();)
    {
        auto live = m_audio->isVoiceLive(*it);
        if (!live)
        {
            return Core::failure(std::move(live.error()));
        }
        it = *live ? std::next(it) : m_voices.erase(it);
    }
    return Core::success();
}

void Scene2DRuntime::stopTrackedVoices() noexcept
{
    if (m_audio == nullptr)
    {
        m_voices.clear();
        return;
    }
    for (const Audio::AudioVoiceId voice : m_voices)
    {
        // A voice the engine already retired reports StaleVoice; that is the
        // desired end state, so the result is deliberately not propagated.
        static_cast<void>(m_audio->enqueueStop(voice));
    }
    // Stop is a queued command: without pumping it, the mix slot stays active and
    // the callback keeps reading the payload the lease is about to release.
    static_cast<void>(m_audio->pumpCompletions());
    m_voices.clear();
}

const Asset::TileMapInstance* Scene2DRuntime::tileMap(Scene::EntityId entity) const noexcept
{
    const auto found = std::ranges::find(m_tileMaps, entity, &TileMapEntry::entity);
    if (found == m_tileMaps.end() || !found->stream.has_value())
    {
        return nullptr;
    }
    return &found->stream->map();
}

std::span<const Scene2DTileLayer> Scene2DRuntime::tileLayers(Scene::EntityId entity) const noexcept
{
    const auto found = std::ranges::find(m_tileMaps, entity, &TileMapEntry::entity);
    return found == m_tileMaps.end() ? std::span<const Scene2DTileLayer>{} : found->layers;
}

Scene::Fx2DInstance* Scene2DRuntime::fxInstance(Scene::EntityId entity) noexcept
{
    const auto found = std::ranges::find(m_fx, entity, &FxEntry::entity);
    if (found == m_fx.end() || !found->active || !found->instance.has_value())
    {
        return nullptr;
    }
    return &*found->instance;
}

Math::Vec2 Scene2DRuntime::fxOrigin(Scene::EntityId entity) const noexcept
{
    const auto found = std::ranges::find(m_fx, entity, &FxEntry::entity);
    return found == m_fx.end() ? Math::Vec2{} : found->origin;
}

Navigation2D::NavigationGrid2D* Scene2DRuntime::navigationGrid(Scene::EntityId entity) noexcept
{
    const auto found = std::ranges::find(m_navigation, entity, &NavigationEntry::entity);
    // An inactive node keeps its grid built so re-activating is a bool flip, but it
    // must not be reachable: a game that pathed against it would still be blocked
    // by geometry the author switched off.
    if (found == m_navigation.end() || !found->active || !found->grid.has_value())
    {
        return nullptr;
    }
    return &*found->grid;
}

Core::Status Scene2DRuntime::shutdown() noexcept
{
    Core::Status result = Core::success();
    // Before any lease is dropped: a live voice holds a non-owning view into a
    // clip lease payload, and releasing the last lease erases that payload.
    stopTrackedVoices();
    // Bodies and shapes go before the physics world they live in, which is the
    // contract the caller is honouring by calling us first.
    if (m_physics != nullptr)
    {
        if (Core::Status status = m_bridge.shutdown(*m_physics); !status && result)
        {
            result = status;
        }
    }
    // Reverse acquisition order: streams release their chunk leases before the
    // root leases they were built from are dropped.
    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.stream.has_value())
        {
            continue;
        }
        if (Core::Status status = entry.stream->shutdown(); !status && result)
        {
            result = status;
        }
    }
    m_tileMaps.clear();
    m_fx.clear();
    m_navigation.clear();
    m_audio_nodes.clear();
    m_tileSprites.clear();
    m_demands.clear();
    m_assets = nullptr;
    m_audio = nullptr;
    m_physics = nullptr;
    m_stats = {};
    m_committedThisFrame = false;
    return result;
}

} // namespace Tina::Gameplay2D
