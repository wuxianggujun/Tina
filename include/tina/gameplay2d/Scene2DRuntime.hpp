#pragma once

#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/TileMapStream.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/gameplay2d/Scene2DPhysicsBridge.hpp>
#include <tina/math/Vec.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/Fx2DFactory.hpp>
#include <tina/scene/World.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace Tina::Gameplay2D {

struct Scene2DRuntimeConfig final {
    // Fixed at build. Exceeding any of these is CapacityExceeded rather than a
    // reallocation, matching the bounded contract of every subsystem below.
    Core::usize tileMapCapacity = 8;
    Core::usize fxCapacity = 64;
    Core::usize navigationCapacity = 4;
    Core::usize audioCapacity = 64;
    // Voices started by playAudio() that are still tracked. The runtime has to
    // remember them because the clip view it handed the engine borrows the lease
    // payload, so shutdown() must stop them before that payload is released.
    Core::usize audioVoiceCapacity = 32;
    // Tile layers instantiated per TileMap node. A map may author up to 256; a
    // scene node that uses more than this is CapacityExceeded.
    Core::usize tileLayersPerMapCapacity = 8;
    // Per TileMap node, forwarded to TileMapStream. residentCapacity has to cover
    // every tile layer at once, because all of them stream from the same stream.
    Asset::TileMapStreamConfig tileMapStream{};
    // Reserved once for the per-layer emission scratch buffer. Emission still grows
    // it if a layer produces more, so this is a no-reallocation hint rather than a
    // hard bound.
    Core::usize tileSpriteCapacity = 4096;
    // Forwarded to the physics bridge when build() is given a PhysicsWorld2D.
    Scene2DPhysicsBridgeConfig physics{};
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct Scene2DRuntimeStats final {
    Core::usize tileMapCount = 0;
    Core::usize fxCount = 0;
    Core::usize navigationCount = 0;
    Core::usize audioCount = 0;
    // Nodes whose AssetId did not resolve to the kind their node declares. They
    // are counted rather than failing the build, because one bad reference in a
    // large scene should not make the scene unloadable.
    Core::usize unresolvedCount = 0;
    // Tile layers driven across every TileMap node. Reported because a map whose
    // layers were all invisible looks identical to one that failed to instantiate.
    Core::usize tileLayerCount = 0;
    Core::usize residentTileChunks = 0;
    Core::u64 tileSpritesEmitted = 0;
    Core::u64 particleSpritesEmitted = 0;
    // Simulation steps driven through fixedUpdatePhysics(). Zero when the runtime
    // was built without a physics world.
    Core::u64 physicsSteps = 0;
};

// One tile layer of an instantiated TileMap node.
struct Scene2DTileLayer final {
    AssetFormat::TileMapLayerId layerId = 0;
    // Authored layer visibility. Every tile layer streams, because an invisible
    // layer is still the source a game queries for collision and navigation, but
    // only visible ones emit sprites -- which is what the flag already means in the
    // asset and in the Editor.
    bool visible = false;
};

// Owner that turns an authored 2D scene into a running one.
//
// It exists because the per-frame ordering and lease lifetime of the authored
// resource kinds are identical for every game and every mistake in them is
// silent: a TileMap extracted before commitReady() just draws less, and an audio
// clip whose lease is dropped early is a use-after-free. Before this, each game
// hand-wrote all of it (see the 2d_tilemap sample).
//
// It orchestrates rather than reimplements: TileMapStream still owns chunk
// residency, ParticleSystem2D/Trail2D still own simulation, NavigationGrid2D
// still owns the grid, AudioEngine still owns playback. This type owns the four
// things those cannot do for themselves -- getting from an AssetId to a usable
// object, holding the leases, fixing the call order, and unwinding in reverse.
//
// Runtime state lives in side tables keyed by EntityId, never in the component,
// so Scene::ResourceBinding2D stays pure data and tina_scene keeps linking
// neither Asset nor Audio (ADR 0031).
class Scene2DRuntime final {
  public:
    Scene2DRuntime() noexcept = default;
    ~Scene2DRuntime() noexcept = default;

    // Immovable. tileMap() hands out a reference that TileMapGridCollision and
    // TileMapPhysicsSync2D borrow for their whole life, and moving the runtime
    // would relocate the instance behind them. Making that a compile error is
    // better than documenting it, because the failure is a dangling read.
    Scene2DRuntime(const Scene2DRuntime&) = delete;
    Scene2DRuntime& operator=(const Scene2DRuntime&) = delete;
    Scene2DRuntime(Scene2DRuntime&&) = delete;
    Scene2DRuntime& operator=(Scene2DRuntime&&) = delete;

    // Instantiates every ResourceBinding2D in the world. Each kind acquires the
    // leases its subsystem requires and is validated against the kind the node
    // declares; a mismatch increments unresolvedCount instead of failing, so one
    // stale reference cannot make a whole scene unloadable.
    //
    // audioEngine may be null when a product composes no Audio module; audio
    // nodes then count as unresolved rather than failing the build.
    //
    // physicsWorld may be null when a scene has no authored physics or a product
    // drives its own. When given, the authored PhysicsBody2D/PhysicsShape2D
    // components are bridged here and fixedUpdatePhysics() owns the step order.
    //
    // On failure everything this call created is released, so a partial runtime
    // is never left behind.
    [[nodiscard]] Core::Status build(const Scene::World& world, Asset::AssetSystem& assets,
                                     Audio::AudioEngine* audioEngine,
                                     Physics2D::PhysicsWorld2D* physicsWorld,
                                     Scene2DRuntimeConfig config = {});

    // Step 1 of the frame. Publishes chunk demand for every active TileMap node
    // from the camera query. The caller then pumps its AssetSystem -- that stays
    // outside because AssetSystem serves the whole game, and a scene object must
    // not implicitly drive global resource loading.
    [[nodiscard]] Core::Status updateDemand(const Asset::TileChunkCameraQuery& camera);

    // Step 2. Commits chunks that finished loading. extract() before this returns
    // an error rather than quietly drawing a stale or partial map.
    [[nodiscard]] Core::Status commitReady();

    // Step 3. Advances particle and trail simulation for active Fx nodes.
    [[nodiscard]] Core::Status fixedUpdate(Core::Duration delta);

    // Step 3b, when build() was given a PhysicsWorld2D. Runs one simulation step
    // and republishes the hierarchy in the only order that is correct:
    //
    //   step() -> applyTo(world) -> updateWorldTransforms()
    //
    // Getting this wrong is silent. Reading transforms before applyTo renders a
    // frame behind; skipping updateWorldTransforms leaves children at their old
    // world position while the parent body has already moved. Physics stays
    // authoritative in one direction -- to move a body, use
    // PhysicsWorld2D::enqueueSetTransform rather than writing a transform.
    //
    // One step per call: the runtime does not own a fixed-step accumulator, because
    // catch-up policy belongs to whoever owns the frame loop (ADR 0015). Call it
    // once per substep. Returns Unsupported when no physics world was given.
    [[nodiscard]] Core::Status fixedUpdatePhysics(Scene::World& world);

    // Step 4. Writes tile and particle sprites for active nodes into the frame.
    // Inactive nodes keep their leases but emit nothing.
    [[nodiscard]] Core::Status extract(const Scene::World& world,
                                       Render::RenderSceneWriter& writer,
                                       Render::FrameResourceSink& frameResources,
                                       const Asset::AssetFrameResourceResolver& resolver);

    // Plays the clip bound to one AudioPlayer2D node. Playback is explicit rather
    // than automatic on build, because when a sound starts is gameplay's decision.
    //
    // An inactive node refuses to play, matching the rule that active is what
    // decides whether an authored node participates.
    //
    // The returned voice is tracked until it reaches a terminal completion or
    // shutdown() stops it, because the clip view the engine holds borrows this
    // node's lease payload.
    [[nodiscard]] Core::Result<Audio::AudioVoiceId> playAudio(Scene::EntityId entity);

    // Drops voices the engine has already retired, so long-running scenes do not
    // fill audioVoiceCapacity with finished one-shots. The caller still owns
    // AudioEngine::pumpCompletions; this only reconciles against what it observed.
    [[nodiscard]] Core::Status releaseFinishedVoices();

    // Releases every lease, voice and grid in reverse order. Must run before the
    // AssetSystem or AudioEngine it was built against is destroyed, matching the
    // TileMapPhysicsSync2D and Scene2DPhysicsBridge contract. Safe to call twice.
    //
    // Voices are stopped and pumped to their terminal completion *before* the clip
    // leases backing them are released: AudioPcmClipView is non-owning, and
    // releasing the last lease erases the cooked payload the engine is still
    // reading. Ordering it the other way is a use-after-free, not a leak.
    [[nodiscard]] Core::Status shutdown() noexcept;

    [[nodiscard]] const Scene2DRuntimeStats& stats() const noexcept { return m_stats; }

    // The bridge this runtime owns, so a game can look up the body for an authored
    // entity and enqueue forces or teleports against it. Null when build() was
    // given no physics world.
    [[nodiscard]] Scene2DPhysicsBridge* physicsBridge() noexcept
    {
        return m_physics != nullptr ? &m_bridge : nullptr;
    }
    // Null when the entity is not an instantiated navigation node, or when that
    // node is authored inactive. Exposed so a game can path against an authored
    // grid without rebuilding it.
    [[nodiscard]] Navigation2D::NavigationGrid2D* navigationGrid(Scene::EntityId entity) noexcept;

    // The resident map of an instantiated TileMap node, or null for any other
    // entity. Inactive nodes still return theirs: unlike a navigation grid, an
    // inactive map is not a queryable world feature, so nothing is falsified by
    // handing it out, and a game switching a map on needs it already reachable.
    //
    // Exposed because every real TileMap consumer needs the instance by reference
    // and none of them can be reimplemented here: TileMapGridCollision borrows it
    // for its whole life, TileMapPhysicsSync2D takes it at Create and every
    // synchronize, TileChunkDirtyCache::syncVisible takes it per frame, and cell
    // picking plus camera world bounds read its extent.
    //
    // Stable while this runtime lives and is not rebuilt. shutdown() invalidates
    // it, so anything borrowing it long-term must be torn down first -- the same
    // ordering TileMapPhysicsSync2D::shutdown() already requires.
    [[nodiscard]] const Asset::TileMapInstance* tileMap(Scene::EntityId entity) const noexcept;

    // The tile layers of an instantiated TileMap node, in authored order. Empty for
    // any other entity. A game needs these to know which layer id to query for
    // collision, because the ids are authored in the asset rather than fixed.
    [[nodiscard]] std::span<const Scene2DTileLayer> tileLayers(Scene::EntityId entity) const noexcept;

    // Null for a non-Fx or inactive node. Mutable because a trail only has
    // segments if something appends points to it, and that something is gameplay:
    // the runtime advances and emits the trail but cannot know where it should go.
    [[nodiscard]] Scene::Fx2DInstance* fxInstance(Scene::EntityId entity) noexcept;

    // World position the node was instantiated at, or {0,0} for an unknown entity.
    // Particle origins are already offset by it; a game appending trail points
    // needs it to place them in the same space.
    [[nodiscard]] Math::Vec2 fxOrigin(Scene::EntityId entity) const noexcept;

  private:
    struct TileMapEntry final {
        Scene::EntityId entity{};
        // TileMapStream is move-constructible but not move-assignable, so it is
        // constructed in place rather than assigned into an existing entry.
        std::optional<Asset::TileMapStream> stream{};
        // Discovered from the asset. A TileMap node binds a whole map, and the wire
        // format has no field to select one layer, so all tile layers are driven.
        std::vector<Scene2DTileLayer> layers{};
        float originX = 0.0F;
        float originY = 0.0F;
        bool active = true;
        // The Tileset handle the stream was built against, kept for emission.
        Asset::AssetHandle tileset{};
    };

    struct FxEntry final {
        Scene::EntityId entity{};
        std::optional<Scene::Fx2DInstance> instance{};
        // The particle sprite is a weak handle, so the lease has to live here.
        Asset::AssetLease spriteLease{};
        // Node world position at build. The Fx2D payload's own origin is authored
        // relative to the node, so both are summed rather than one replacing the
        // other -- otherwise moving the node in the Editor changes nothing.
        Math::Vec2 origin{};
        bool active = true;
    };

    struct NavigationEntry final {
        Scene::EntityId entity{};
        std::optional<Navigation2D::NavigationGrid2D> grid{};
        bool active = true;
    };

    struct AudioEntry final {
        Scene::EntityId entity{};
        // AudioPcmClipView is non-owning: the cooked payload must outlive the
        // terminal Stopped/Cancelled completion, so the lease is held until
        // shutdown rather than until enqueuePlay returns.
        Asset::AssetLease clipLease{};
        bool active = true;
    };

    [[nodiscard]] std::pmr::memory_resource& memory() const noexcept;
    // Stops every tracked voice and pumps until the engine reports them retired,
    // so no voice is still reading a clip payload when its lease goes away.
    void stopTrackedVoices() noexcept;

    Scene2DRuntimeConfig m_config{};
    Asset::AssetSystem* m_assets = nullptr;
    Audio::AudioEngine* m_audio = nullptr;
    Physics2D::PhysicsWorld2D* m_physics = nullptr;
    // Kept as a member rather than an optional: the bridge is empty until built,
    // and its own build()/shutdown() already carry the lifecycle.
    Scene2DPhysicsBridge m_bridge{};
    std::vector<TileMapEntry> m_tileMaps{};
    std::vector<FxEntry> m_fx{};
    std::vector<NavigationEntry> m_navigation{};
    std::vector<AudioEntry> m_audio_nodes{};
    // Voices whose bound clip borrows one of the leases above.
    std::vector<Audio::AudioVoiceId> m_voices{};
    // Reused across frames so extraction allocates nothing per frame.
    std::pmr::vector<Render::RenderSprite2DInput> m_tileSprites{
        std::pmr::polymorphic_allocator<Render::RenderSprite2DInput>{
            std::pmr::get_default_resource()}};
    // One entry per tile layer of the node being updated, reused for the same
    // reason: updateDemand takes the whole span at once.
    std::vector<Asset::TileMapChunkDemand> m_demands{};
    Scene2DRuntimeStats m_stats{};
    bool m_committedThisFrame = false;
};

} // namespace Tina::Gameplay2D
