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

} // namespace

std::pmr::memory_resource& Scene2DRuntime::memory() const noexcept
{
    return m_config.memoryResource != nullptr ? *m_config.memoryResource
                                              : *std::pmr::get_default_resource();
}

Core::Status Scene2DRuntime::build(const Scene::World& world, Asset::AssetSystem& assets,
                                   Audio::AudioEngine* audioEngine, Scene2DRuntimeConfig config)
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
    m_config = config;
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
            entry.stream.emplace(std::move(*stream));
            entry.tileset = *tilesetHandle;
            // Map-local origin comes from the node's published world transform, so
            // moving the node in the Editor moves the tiles.
            if (const Scene::WorldTransform* transform = world.worldTransform(entity);
                transform != nullptr)
            {
                entry.originX = transform->position.x;
                entry.originY = transform->position.y;
            }
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
            // The factory returns the initial burst but does not emit it.
            if (auto status = instance->particles.emitBurst(instance->initialBurst); !status)
            {
                rollback();
                return status;
            }
            FxEntry entry{.entity = entity, .active = binding->active};
            entry.instance.emplace(std::move(*instance));
            entry.spriteLease = std::move(*spriteLease);
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
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Scene2DRuntime tile sprite storage allocation failed");
    }

    m_stats.tileMapCount = m_tileMaps.size();
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
        const std::array<Asset::TileMapChunkDemand, 1> demands{
            Asset::TileMapChunkDemand{.layerId = entry.layerId, .priority = 0, .camera = local},
        };
        if (auto status = entry.stream->updateDemand(demands); !status)
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
        Asset::TileChunkSpriteEmitParams params{};
        params.tileset = entry.tileset;
        params.bindingResolver = resolver;
        params.originX = entry.originX;
        params.originY = entry.originY;
        Asset::TileChunkCameraQuery local{};
        local.centerX = 0.0F;
        local.centerY = 0.0F;
        // Residency already bounded what is loaded, so emission covers everything
        // resident rather than re-culling against a camera it was not given.
        local.halfWidth = 1.0e6F;
        local.halfHeight = 1.0e6F;
        auto emitted = Asset::emitVisibleTileMapSprites(map, entry.layerId, local, params,
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
    return m_audio->playOneShotPcm(*clip);
}

Navigation2D::NavigationGrid2D* Scene2DRuntime::navigationGrid(Scene::EntityId entity) noexcept
{
    const auto found = std::ranges::find(m_navigation, entity, &NavigationEntry::entity);
    if (found == m_navigation.end() || !found->grid.has_value())
    {
        return nullptr;
    }
    return &*found->grid;
}

Core::Status Scene2DRuntime::shutdown() noexcept
{
    Core::Status result = Core::success();
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
    m_assets = nullptr;
    m_audio = nullptr;
    m_stats = {};
    m_committedThisFrame = false;
    return result;
}

} // namespace Tina::Gameplay2D
