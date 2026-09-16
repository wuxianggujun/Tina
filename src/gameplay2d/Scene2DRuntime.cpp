#include <tina/gameplay2d/Scene2DRuntime.hpp>

#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/audio/AudioClipView.hpp>
#include <tina/audio/EncodedPcmStreamer.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <exception>
#include <new>
#include <stdexcept>
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
    case Scene::ResourceBindingKind2D::PrefabInstance:
        return AssetFormat::AssetKind::Prefab2D;
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
[[nodiscard]] Math::Vec3 worldOrigin(const Scene::World& world, Scene::EntityId entity) noexcept
{
    const Scene::WorldTransform* transform = world.worldTransform(entity);
    if (transform == nullptr)
    {
        return {};
    }
    return transform->position;
}

// Separates one layer's stable tile keys from the next. A layer holds at most
// MaxDimension^2 cells, so this stride cannot overlap, and layer ids are bounded by
// MaxLayers -- the product stays far inside u64.
constexpr Core::u64 TileLayerStableKeyStride =
    static_cast<Core::u64>(AssetFormat::TileMapWire::MaxDimension) *
    AssetFormat::TileMapWire::MaxDimension;

} // namespace

Scene2DRuntime::~Scene2DRuntime() noexcept
{
    if (!shutdown()) {
        // A destructor cannot return the retryable owner. Never release leases
        // while an AudioEngine callback can still read their PCM payload.
        std::terminate();
    }
}

Core::Status Scene2DRuntime::requireReady() const
{
    if (m_state != Scene2DRuntimeState::Ready) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Scene2DRuntime must be Ready for playback or frame updates");
    }
    return Core::success();
}

std::pmr::memory_resource& Scene2DRuntime::memory() const noexcept
{
    return m_config.memoryResource != nullptr ? *m_config.memoryResource
                                              : *std::pmr::get_default_resource();
}

#if defined(TINA_HAS_PHYSICS2D)
Core::Status Scene2DRuntime::build(const Scene::World& world, Asset::AssetSystem& assets,
                                   Audio::AudioEngine* audioEngine,
                                   Physics2D::PhysicsWorld2D* physicsWorld,
                                   Scene2DRuntimeConfig config)
{
    if (m_assets != nullptr)
    {
        return Core::failure(Core::CoreErrorCode::AlreadyExists,
                             "Scene2DRuntime is already built; shutdown first");
    }
    // Validated first so a rejected config cannot leave physicsBridge() non-null
    // on an unbuilt runtime.
    m_physics = physicsWorld;
    auto status = build(world, assets, audioEngine, config);
    if (!status)
    {
        m_physics = nullptr;
    }
    return status;
}
#endif

Core::Status Scene2DRuntime::build(const Scene::World& world, Asset::AssetSystem& assets,
                                   Audio::AudioEngine* audioEngine, Scene2DRuntimeConfig config)
try
{
    if (m_assets != nullptr)
    {
        return Core::failure(Core::CoreErrorCode::AlreadyExists,
                             "Scene2DRuntime is already built; shutdown first");
    }
    // Size side tables from the authored input before constructing their nested
    // owners. There is no node ceiling, and growing a table cannot relocate a
    // live TileMapStream / Fx instance midway through the build transaction.
    Core::usize tileMapNodes = 0;
    Core::usize fxNodes = 0;
    Core::usize navigationNodes = 0;
    Core::usize audioNodes = 0;
    for (const auto entity : world.liveEntities()) {
        const auto* binding = world.resourceBinding2D(entity);
        if (binding == nullptr) { continue; }
        switch (binding->kind) {
        case Scene::ResourceBindingKind2D::TileMap: ++tileMapNodes; break;
        case Scene::ResourceBindingKind2D::FxEmitter: ++fxNodes; break;
        case Scene::ResourceBindingKind2D::NavigationRegion: ++navigationNodes; break;
        case Scene::ResourceBindingKind2D::AudioPlayer: ++audioNodes; break;
        case Scene::ResourceBindingKind2D::PrefabInstance: break;
        }
    }
    m_tileMaps.reserve(tileMapNodes);
    m_fx.reserve(fxNodes);
    m_navigation.reserve(navigationNodes);
    m_audio_nodes.reserve(audioNodes);
    auto assetSystemBorrow = assets.acquireStableBorrow();
    if (!assetSystemBorrow)
    {
        return Core::failure(std::move(assetSystemBorrow.error()));
    }
    m_config = config;
    m_assetSystemBorrow = std::move(*assetSystemBorrow);
    m_assets = &assets;
    m_audio = audioEngine;
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
        if (binding->kind == Scene::ResourceBindingKind2D::PrefabInstance)
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
        case Scene::ResourceBindingKind2D::PrefabInstance:
            break;
        case Scene::ResourceBindingKind2D::TileMap: {
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
            if (auto status = collectTileLayers(*file, entry.layers);
                !status)
            {
                rollback();
                return status;
            }
            entry.stream.emplace(std::move(*stream));
            entry.tileset = *tilesetHandle;
            // Map-local origin comes from the node's published world transform, so
            // moving the node in the Editor moves the tiles.
            const Math::Vec3 origin = worldOrigin(world, entity);
            entry.originX = origin.x;
            entry.originY = origin.y;
            entry.elevation = origin.z;
            m_tileMaps.push_back(std::move(entry));
            break;
        }
        case Scene::ResourceBindingKind2D::FxEmitter: {
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
            const Math::Vec3 origin = worldOrigin(world, entity);
            auto instance = Scene::createFx2DFromAsset(*desc, *spriteHandle, origin, memory());
            if (!instance)
            {
                rollback();
                return Core::failure(std::move(instance.error()));
            }
            // The factory returns the initial burst but does not emit it.
            if (auto status = instance->particles.emitBurst(instance->initialBurst); !status)
            {
                rollback();
                return status;
            }
            FxEntry entry{.entity = entity, .active = binding->active};
            entry.instance.emplace(std::move(*instance));
            entry.spriteLease = std::move(*spriteLease);
            entry.origin = {origin.x, origin.y};
            m_fx.push_back(std::move(entry));
            break;
        }
        case Scene::ResourceBindingKind2D::NavigationRegion: {
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
            entry.loopMode = binding->audioLoopMode == 1U ? Audio::AudioLoopMode::Loop
                                                          : Audio::AudioLoopMode::Once;
            m_audio_nodes.push_back(std::move(entry));
            break;
        }
        }
    }

    try
    {
        m_tileScratch.emplace(memory());
        m_tileScratch->sprites.reserve(m_config.initialTileSpriteReserve);
        m_tileScratch->chunks.reserve(m_config.tileMapStream.residentCapacity);
        // Reserved up front so per-frame demand publication never allocates.
        Core::usize maximumLayers = 0;
        for (const auto& entry : m_tileMaps) {
            maximumLayers = (std::max)(maximumLayers, entry.layers.size());
        }
        m_demands.reserve(maximumLayers);
        m_voices.emplace(Core::usize{0}, &memory());
        m_voices->reserve(m_audio_nodes.size());
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Scene2DRuntime scratch or voice tracking allocation failed");
    }

#if defined(TINA_HAS_PHYSICS2D)
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
#endif

    m_stats.tileMapCount = m_tileMaps.size();
    for (const TileMapEntry& entry : m_tileMaps)
    {
        m_stats.tileLayerCount += entry.layers.size();
    }
    m_stats.fxCount = m_fx.size();
    m_stats.navigationCount = m_navigation.size();
    m_stats.audioCount = m_audio_nodes.size();
    auto actions = Gameplay::ActionRunner::Create(
        {.memoryResource = m_config.memoryResource});
    if (!actions)
    {
        rollback();
        return Core::failure(std::move(actions.error()));
    }
    m_actions = std::move(*actions);
    m_state = Scene2DRuntimeState::Ready;
    return Core::success();
}
catch (const std::bad_alloc&)
{
    static_cast<void>(shutdown());
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Scene2DRuntime build allocation failed");
}
catch (const std::length_error&)
{
    static_cast<void>(shutdown());
    return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Scene2DRuntime storage exceeds addressable size");
}

Core::Status Scene2DRuntime::updateDemand(const Render::RenderCamera2D& camera)
{
    if (auto status = requireReady(); !status) { return status; }
    m_committedThisFrame = false;
    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.active || !entry.stream.has_value())
        {
            continue;
        }
        auto local = Asset::makeTileChunkCameraQuery(camera, {entry.originX, entry.originY, entry.elevation});
        if (!local) {
            return Core::failure(std::move(local.error()));
        }
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
                .camera = *local,
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
    if (auto status = requireReady(); !status) { return status; }
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
    if (auto status = requireReady(); !status) { return status; }
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
    if (auto status = requireReady(); !status) { return status; }
#if !defined(TINA_HAS_PHYSICS2D)
    (void)world;
    return Core::failure(Core::CoreErrorCode::Unsupported,
                         "Scene2DRuntime was built without a PhysicsWorld2D");
#else
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
#endif
}

Core::Status Scene2DRuntime::extract(const Scene::World& world, Render::RenderSceneWriter& writer,
                                     Render::FrameResourceSink& frameResources,
                                     const Asset::AssetFrameResourceResolver& resolver)
{
    if (auto status = requireReady(); !status) { return status; }
    // Extracting before commitReady would draw a stale or partial map, and that
    // failure is invisible on screen, so it is reported instead.
    if (!m_tileMaps.empty() && !m_committedThisFrame)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Scene2DRuntime::extract requires commitReady() earlier in the frame");
    }
    static_cast<void>(world);

    std::optional<Render::RenderCamera2D> camera;
    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.active || !entry.stream.has_value())
        {
            continue;
        }
        const Asset::TileMapInstance& map = entry.stream->map();
        Asset::TileChunkSpriteEmitParams params{};
        params.tileset = entry.tileset;
        params.bindingResolver = resolver;
        params.originX = entry.originX;
        params.originY = entry.originY;
        params.elevation = entry.elevation;
        Core::i16 sortingLayer = 0;
        for (const Scene2DTileLayer& layer : entry.layers)
        {
            // An invisible layer still streams -- games query it for collision -- but
            // emitting it would draw the collision mask over the visual one.
            if (!layer.visible)
            {
                continue;
            }
            if (!camera) {
                auto resolved = writer.camera2D();
                if (!resolved) {
                    return Core::failure(std::move(resolved.error()));
                }
                camera = *resolved;
            }
            // Authored layer order decides draw order. Without this every layer
            // would share sortingLayer 0 and overlap resolution would fall back to
            // the tile stable key, which is layer-independent.
            params.sortingLayer = sortingLayer++;
            // Distinct per layer, or two layers' tiles at the same cell would claim
            // the same stable key and the sprite sort would treat them as one item.
            params.stableEntityKeyBase =
                static_cast<Core::u64>(layer.layerId) * TileLayerStableKeyStride;
            auto emitted = Asset::emitVisibleTileMapSprites(map, layer.layerId, *camera, params,
                                                            frameResources, *m_tileScratch);
            if (!emitted)
            {
                return Core::failure(std::move(emitted.error()));
            }
            for (const Render::RenderSprite2DInput& sprite : m_tileScratch->sprites)
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

Core::Result<Audio::AudioVoiceId> Scene2DRuntime::playAudio(
    Scene::EntityId entity, std::optional<Audio::AudioPlayDesc> desc)
{
    if (auto status = requireReady(); !status) { return Core::failure(std::move(status.error())); }
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
    if (auto status = releaseFinishedVoices(); !status) {
        return Core::failure(std::move(status.error()));
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
    try
    {
        if (m_voices->size() == m_voices->capacity()) {
            const auto capacity = m_voices->capacity();
            if (capacity == m_voices->max_size()) {
                return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Scene2DRuntime voice storage is exhausted");
            }
            const auto grown = capacity > m_voices->max_size() / 2 ? m_voices->max_size() : capacity * 2;
            m_voices->reserve((std::max)(grown, capacity + 1));
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Scene2DRuntime could not reserve audio voice tracking");
    } catch (const std::length_error&) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Scene2DRuntime voice storage exceeds addressable size");
    }
    Audio::AudioPlayDesc play = desc.value_or(Audio::AudioPlayDesc{.loopMode = found->loopMode});
    if (payload->storage == AssetFormat::AudioClipStorage::EncodedStream)
    {
        auto streamer = Audio::EncodedPcmStreamer::Start(
            *m_audio, payload->encoded,
            Audio::EncodedPcmStreamDesc{
                .play = play,
                .bus = Audio::AudioBusId::Sfx,
                .sourceFrameCount = payload->frameCount,
            });
        if (!streamer) { return Core::failure(std::move(streamer.error())); }
        const auto voice = streamer->voice();
        m_voices->push_back(TrackedVoice{.voice = voice, .streamer = std::move(*streamer)});
        return voice;
    }
    auto clip = Audio::pcmClipViewFromAudioClipPayload(*payload);
    if (!clip)
    {
        return Core::failure(std::move(clip.error()));
    }
    auto voice = m_audio->playPcm(*clip, play);
    if (!voice) {
        return voice;
    }
    m_voices->push_back(TrackedVoice{.voice = *voice});
    return voice;
}

Core::Status Scene2DRuntime::pumpAudioStreams()
{
    if (!m_voices || m_audio == nullptr) { return Core::success(); }
    for (TrackedVoice& tracked : *m_voices)
    {
        if (!tracked.streamer.has_value() || tracked.streamer->finished()) { continue; }
        if (auto status = tracked.streamer->pump(); !status)
        {
            return status;
        }
    }
    return Core::success();
}

Core::Status Scene2DRuntime::releaseFinishedVoices()
{
    if (!m_voices || m_voices->empty())
    {
        return Core::success();
    }
    if (m_audio == nullptr) {
        return Core::failure(Core::CoreErrorCode::Internal, "tracked voices lost their AudioEngine owner");
    }
    if (auto status = pumpAudioStreams(); !status) { return status; }
    // isVoiceLive is false once a terminal completion retired the transient voice,
    // which is exactly when its clip payload is no longer being read.
    Core::Status result = Core::success();
    std::erase_if(*m_voices, [&](const TrackedVoice& tracked) {
        auto live = m_audio->isVoiceLive(tracked.voice);
        if (!live) {
            if (result) { result = Core::failure(std::move(live.error())); }
            return false;
        }
        return !*live;
    });
    return result;
}

Core::Status Scene2DRuntime::stopTrackedVoices() noexcept
{
    if (auto status = releaseFinishedVoices(); !status) { return status; }
    if (!m_voices || m_voices->empty()) { return Core::success(); }
    if (m_audio->state() == Audio::AudioEngineState::Stopping) {
        return Core::failure(Scene::SceneErrorCode::RetirementPending,
                             "shared AudioEngine is stopping; its owner must finish shutdown before releasing scene PCM");
    }
    Core::Status firstFailure = Core::success();
    for (TrackedVoice& tracked : *m_voices) {
        if (tracked.stopQueued) { continue; }
        if (auto status = m_audio->enqueueStop(tracked.voice); !status) {
            firstFailure = std::move(status);
            break;
        }
        tracked.stopQueued = true;
    }
    // Exactly one pump advances accepted commands and can free queue space for a
    // later retry. Neither an accepted Stop nor a pump alone proves reader exit.
    if (auto pumped = m_audio->pumpCompletions(); !pumped && firstFailure) {
        firstFailure = Core::failure(std::move(pumped.error()));
    }
    if (auto status = releaseFinishedVoices(); !status) { return status; }
    if (m_voices->empty()) { return Core::success(); }
    if (!firstFailure) { return firstFailure; }
    return Core::failure(Scene::SceneErrorCode::RetirementPending,
                         "scene audio retirement is pending; retain this owner and retry shutdown");
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
    if (m_assets == nullptr) { return Core::success(); }
    m_state = Scene2DRuntimeState::Stopping;
    if (m_actions)
    {
        m_actions->cancelAll();
    }
    // Before any lease is dropped: a live voice holds a non-owning view into a
    // clip lease payload, and releasing the last lease erases that payload.
    if (auto status = stopTrackedVoices(); !status) { return status; }
#if defined(TINA_HAS_PHYSICS2D)
    // Bodies and shapes go before the physics world they live in, which is the
    // contract the caller is honouring by calling us first.
    if (m_physics != nullptr)
    {
        if (Core::Status status = m_bridge.shutdown(*m_physics); !status)
        {
            return status;
        }
    }
#endif
    // Reverse acquisition order: streams release their chunk leases before the
    // root leases they were built from are dropped.
    for (TileMapEntry& entry : m_tileMaps)
    {
        if (!entry.stream.has_value())
        {
            continue;
        }
        if (Core::Status status = entry.stream->shutdown(); !status)
        {
            return status;
        }
    }
    m_tileMaps.clear();
    m_fx.clear();
    m_navigation.clear();
    m_audio_nodes.clear();
    m_voices.reset();
    m_actions.reset();
    m_tileScratch.reset();
    m_demands.clear();
    m_assets = nullptr;
    m_assetSystemBorrow = {};
    m_audio = nullptr;
#if defined(TINA_HAS_PHYSICS2D)
    m_physics = nullptr;
#endif
    m_stats = {};
    m_committedThisFrame = false;
    m_state = Scene2DRuntimeState::Empty;
    return Core::success();
}

} // namespace Tina::Gameplay2D
