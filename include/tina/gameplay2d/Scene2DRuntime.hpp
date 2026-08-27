#pragma once

#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/TileMapStream.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/Fx2DFactory.hpp>
#include <tina/scene/World.hpp>

#include <memory_resource>
#include <optional>
#include <vector>

namespace Tina::Gameplay2D {

struct Scene2DRuntimeConfig final {
    // Fixed at build. Exceeding any of these is CapacityExceeded rather than a
    // reallocation, matching the bounded contract of every subsystem below.
    Core::usize tileMapCapacity = 8;
    Core::usize fxCapacity = 64;
    Core::usize navigationCapacity = 4;
    Core::usize audioCapacity = 64;
    // Per TileMap node, forwarded to TileMapStream.
    Asset::TileMapStreamConfig tileMapStream{};
    // Sprites emitted by one TileMap layer in one frame.
    Core::usize tileSpriteCapacity = 4096;
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
    Core::usize residentTileChunks = 0;
    Core::u64 tileSpritesEmitted = 0;
    Core::u64 particleSpritesEmitted = 0;
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

    Scene2DRuntime(const Scene2DRuntime&) = delete;
    Scene2DRuntime& operator=(const Scene2DRuntime&) = delete;
    Scene2DRuntime(Scene2DRuntime&&) noexcept = default;
    Scene2DRuntime& operator=(Scene2DRuntime&&) = delete;

    // Instantiates every ResourceBinding2D in the world. Each kind acquires the
    // leases its subsystem requires and is validated against the kind the node
    // declares; a mismatch increments unresolvedCount instead of failing, so one
    // stale reference cannot make a whole scene unloadable.
    //
    // audioEngine may be null when a product composes no Audio module; audio
    // nodes then count as unresolved rather than failing the build.
    //
    // On failure everything this call created is released, so a partial runtime
    // is never left behind.
    [[nodiscard]] Core::Status build(const Scene::World& world, Asset::AssetSystem& assets,
                                     Audio::AudioEngine* audioEngine,
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

    // Step 4. Writes tile and particle sprites for active nodes into the frame.
    // Inactive nodes keep their leases but emit nothing.
    [[nodiscard]] Core::Status extract(const Scene::World& world,
                                       Render::RenderSceneWriter& writer,
                                       Render::FrameResourceSink& frameResources,
                                       const Asset::AssetFrameResourceResolver& resolver);

    // Plays the clip bound to one AudioPlayer2D node. Playback is explicit rather
    // than automatic on build, because when a sound starts is gameplay's decision.
    [[nodiscard]] Core::Result<Audio::AudioVoiceId> playAudio(Scene::EntityId entity);

    // Releases every lease, voice and grid in reverse order. Must run before the
    // AssetSystem or AudioEngine it was built against is destroyed, matching the
    // TileMapPhysicsSync2D and Scene2DPhysicsBridge contract. Safe to call twice.
    [[nodiscard]] Core::Status shutdown() noexcept;

    [[nodiscard]] const Scene2DRuntimeStats& stats() const noexcept { return m_stats; }
    // Null when the entity is not an instantiated navigation node. Exposed so a
    // game can path against an authored grid without rebuilding it.
    [[nodiscard]] Navigation2D::NavigationGrid2D* navigationGrid(Scene::EntityId entity) noexcept;

  private:
    struct TileMapEntry final {
        Scene::EntityId entity{};
        // TileMapStream is move-constructible but not move-assignable, so it is
        // constructed in place rather than assigned into an existing entry.
        std::optional<Asset::TileMapStream> stream{};
        AssetFormat::TileMapLayerId layerId = 0;
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

    Scene2DRuntimeConfig m_config{};
    Asset::AssetSystem* m_assets = nullptr;
    Audio::AudioEngine* m_audio = nullptr;
    std::vector<TileMapEntry> m_tileMaps{};
    std::vector<FxEntry> m_fx{};
    std::vector<NavigationEntry> m_navigation{};
    std::vector<AudioEntry> m_audio_nodes{};
    // Reused across frames so extraction allocates nothing per frame.
    std::pmr::vector<Render::RenderSprite2DInput> m_tileSprites{
        std::pmr::polymorphic_allocator<Render::RenderSprite2DInput>{
            std::pmr::get_default_resource()}};
    Scene2DRuntimeStats m_stats{};
    bool m_committedThisFrame = false;
};

} // namespace Tina::Gameplay2D
