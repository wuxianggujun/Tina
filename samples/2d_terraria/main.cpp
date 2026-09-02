// Terraria-like 2D sample: procedurally generated, diggable/buildable tile world.
//
// Shape of the thing:
//   - WorldGen.hpp generates a 256x128 cell world and emits it as recipe text.
//   - That text is appended to catalog/world_assets.recipe (atlas + tileset + tiles),
//     parsed in memory, and cooked into a temp Catalog package.
//   - TileMapStream keeps EVERY chunk resident, because an evicted chunk is restored
//     from its cooked payload and would silently refill every hole the player dug.
//   - CharacterController2D moves the player by AABB sweeps against tile material
//     flags; there is no rigid body, which is what a block game wants.
//   - Left click digs, right click places, both through the locked worldPointerSample
//     that Runtime attaches to an unconsumed Simulation pointer edge.
//
// Air is a real tile (localId 1, flags 0) rather than tile id 0. The recipe cooker
// drops all-empty chunks, and a dropped chunk has no mutable resident asset, so
// setTile on it fails forever -- the sky would be permanently unbuildable.

#include "DeviceCapture.hpp"
#include "WorldGen.hpp"

#include "../common/SampleSpriteFrameResource.hpp"
#include "../common/SampleTempDirectory.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileChunkView.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset/TileMapStream.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/desktop/UiFontFile.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PhaseContexts.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
namespace Terraria = Tina::SampleTerraria;

constexpr Tina::InputActionId MoveLeftAction{1};
constexpr Tina::InputActionId MoveRightAction{2};
constexpr Tina::InputActionId JumpAction{3};
constexpr Tina::InputActionId DigAction{4};
constexpr Tina::InputActionId PlaceAction{5};

// One atlas, one binding. Tiles and the player quad both sample it.
constexpr u64 AtlasBindingKey = 1;

// Tiles occupy the low stable-entity keys; the player sits above the whole world.
constexpr u64 TileStableEntityKeyBase = 1;
constexpr u64 PlayerStableEntityKey = 4'000'001;
constexpr Tina::Core::i16 TileSortingLayer = 0;
constexpr Tina::Core::i16 PlayerSortingLayer = 1;

// Visible world height in meters. Width follows the surface aspect ratio. Kept close
// to EditReachMeters so most of what is on screen is actually reachable.
constexpr float CameraHeightMeters = 18.0F;
constexpr float PlayerHalfWidth = 0.35F;
constexpr float PlayerHalfHeight = 0.9F;
constexpr float PlayerMoveSpeed = 7.0F;
constexpr float PlayerJumpSpeed = 13.0F;
// Reach margin past the visible half-diagonal. The limit itself is derived from the
// live camera extents, so anything on screen is editable and the two cannot drift
// apart when CameraHeightMeters or the window aspect changes.
constexpr float EditReachMarginMeters = 1.5F;

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(Tina::Core::u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

// Must match the hex ids written into catalog/world_assets.recipe.
constexpr std::string_view AtlasTextureIdHex = "010000000000000000000000000000a4";
constexpr std::string_view TilesetIdHex = "020000000000000000000000000000a7";
constexpr std::string_view TileMapIdHex = "030000000000000000000000000000a6";

void writeError(const Tina::Core::Error& error)
{
    std::fprintf(stderr, "tina_sample_terraria error: %s\n", error.message.c_str());
}

struct SampleOptions final {
    u32 windowLogicalWidth = 1280;
    u32 windowLogicalHeight = 720;
    // 0 runs until the window closes; the gate path passes a fixed budget.
    u32 maxFrames = 0;
    u32 worldSeed = 1337;
    // Exercises dig/place through the same applyEdit path a pointer edge takes, so
    // the core mechanic has evidence without a human clicking.
    bool selfTestEdits = false;
};

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argc, char** argv)
{
    SampleOptions options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        // Rejects trailing garbage and anything above u32, which the digit loop this replaces
        // wrapped instead: --frames=4294967296 became 0, and 0 means "run until the window closes".
        const auto readU32 = [&](std::string_view prefix, u32& target) -> Tina::Core::Result<bool> {
            if (!argument.starts_with(prefix))
            {
                return false;
            }
            if (!Tina::Core::parseArgUnsigned(argument.substr(prefix.size()), target))
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                        "Option value must be an unsigned 32-bit integer"};
                error.addContext("parseOptions", argument);
                return Tina::Core::failure(std::move(error));
            }
            return true;
        };
        if (argument == "--selftest-edits")
        {
            options.selfTestEdits = true;
            continue;
        }
        bool matched = false;
        for (const auto& [prefix, target] :
             std::initializer_list<std::pair<std::string_view, u32*>>{{"--frames=", &options.maxFrames},
                                                                     {"--seed=", &options.worldSeed},
                                                                     {"--width=", &options.windowLogicalWidth},
                                                                     {"--height=", &options.windowLogicalHeight}})
        {
            auto claimed = readU32(prefix, *target);
            if (!claimed)
            {
                return Tina::Core::failure(claimed.error());
            }
            if (*claimed)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Unsupported command-line argument"};
            error.addContext("parseOptions", argument);
            return Tina::Core::failure(std::move(error));
        }
    }
    return options;
}

struct RunCounters final {
    u64 frames = 0;
    u64 fixedSteps = 0;
    u32 generatedSolidCells = 0;
    u32 generatedAirCells = 0;
    u32 cookedRecipeAssets = 0;
    u64 residentChunks = 0;
    u64 lastTileSprites = 0;
    u64 digRequests = 0;
    u64 digApplied = 0;
    u64 placeRequests = 0;
    u64 placeApplied = 0;
    u64 editRejectedOutOfReach = 0;
    u64 editRejectedOccupied = 0;
    u64 selfTestAttempts = 0;
    u64 selfTestSkippedNotGrounded = 0;
    // Raw edge accounting, before any world-sample or reach filtering, so a missing
    // right click can be told apart from a right click that arrived without a
    // projected world position.
    u64 rawDigEdges = 0;
    u64 rawPlaceEdges = 0;
    u64 edgesMissingWorldSample = 0;
    u64 edgesWorldSampleMissed = 0;
    // Every Started edge the Simulation domain delivers, regardless of action, plus a
    // bitmask of the raw action ids seen. Separates "the binding never fires" from
    // "this sample filters the edge out".
    u64 simStartedEdges = 0;
    u32 simActionIdMask = 0;
    bool playerEverGrounded = false;
    std::optional<Tina::Core::Error> shutdownError{};
};

// Everything the state borrows. Held by main so its address is stable: the stream
// hands out a TileMapInstance reference that the collision adapter borrows.
struct WorldResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::unique_ptr<Tina::Asset::AssetSystem> system{};
    Tina::Asset::AssetHandle tileMapHandle{};
    Tina::Asset::AssetHandle tilesetHandle{};
    Tina::Asset::AssetHandle atlasTextureHandle{};
    std::optional<Tina::Asset::TileMapStream> stream{};
    std::optional<Tina::Asset::TileMapGridCollision> collision{};
    std::optional<Tina::Asset::CharacterController2D> player{};
    std::pmr::vector<Tina::Asset::TileMapSolidHit> solidScratch{&memory};
    Tina::Render::GpuTextureId gpuAtlas{};
    std::filesystem::path catalogRoot{};
    float spawnX = 0.0F;
    float spawnY = 0.0F;
    float cameraCenterX = 0.0F;
    float cameraCenterY = 0.0F;
};

// Builds the full recipe text: shipped asset lines + generated tilemap block.
[[nodiscard]] Tina::Core::Result<std::string> buildRecipeText(const Terraria::GeneratedWorld& world)
{
    auto assetsPath = Tina::Core::applicationFilePath("catalog/world_assets.recipe");
    if (!assetsPath)
    {
        return Tina::Core::failure(std::move(assetsPath.error()));
    }
    std::pmr::unsynchronized_pool_resource readMemory;
    auto bytes = Tina::Core::readFile(
        *assetsPath, Tina::Core::ReadFileConfig{.maxBytes = 1ULL << 20U, .memoryResource = &readMemory});
    if (!bytes)
    {
        return Tina::Core::failure(std::move(bytes.error()).withContext("buildRecipeText", "readAssetRecipe"));
    }

    std::string text;
    // Rough upper bound: asset lines plus up to 4 chars per cell.
    text.reserve(bytes->size() + static_cast<std::size_t>(world.widthCells) * world.heightCells * 4U + 4096U);
    for (const std::byte value : *bytes)
    {
        text.push_back(static_cast<char>(value));
    }
    if (!text.empty() && text.back() != '\n')
    {
        text.push_back('\n');
    }
    Terraria::appendTileMapRecipe(text, TileMapIdHex, TilesetIdHex, world);
    return text;
}

[[nodiscard]] Tina::Core::Status prepareWorld(const SampleOptions& options, WorldResources& resources,
                                              RunCounters& counters)
{
    const Terraria::WorldGenConfig genConfig{.seed = options.worldSeed};
    const Terraria::GeneratedWorld world = Terraria::generateWorld(genConfig);
    counters.generatedSolidCells = world.solidCells;
    counters.generatedAirCells = world.airCells;

    auto recipeText = buildRecipeText(world);
    if (!recipeText)
    {
        return Tina::Core::failure(std::move(recipeText.error()));
    }

    auto request = Tina::Asset::parseCatalogCookRecipe(*recipeText, std::string_view{});
    if (!request)
    {
        return Tina::Core::failure(std::move(request.error()).withContext("prepareWorld", "parseRecipe"));
    }
    counters.cookedRecipeAssets = static_cast<u32>(request->assets.size());

    auto catalogRoot = Tina::Sample::createUniqueTempDirectory("tina_sample_terraria_pkg");
    if (!catalogRoot)
    {
        return Tina::Core::failure(std::move(catalogRoot.error()));
    }
    resources.catalogRoot = std::move(*catalogRoot);
    const std::string rootUtf8 = [&] {
        const auto encoded = resources.catalogRoot.u8string();
        return std::string(encoded.begin(), encoded.end());
    }();
    if (auto status = Tina::Asset::cookAndPublishCatalogPackage(rootUtf8, *request); !status)
    {
        return status;
    }

    const auto tileMapId = *Tina::Core::AssetId::fromBytes(idBytes(3U));
    const auto tilesetId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const auto atlasId = *Tina::Core::AssetId::fromBytes(idBytes(1U));

    // Chunk assets are one per 16x16 block per layer, plus atlas/tileset/tilemap.
    auto system = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
        .storeCapacity = Terraria::WorldChunkCount + 16U,
        .memoryResource = &resources.memory,
        .batch =
            Tina::Asset::CookedAssetBatchLoadConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory},
                .memoryResource = &resources.memory,
            },
        .requireTyped2dPayloads = true,
    });
    if (!system)
    {
        return Tina::Core::failure(std::move(system.error()));
    }
    // The tilemap root depends on one chunk asset per 16x16 block, and the snapshot
    // default caps a single asset at 64 dependencies -- far below a world this size.
    Tina::Asset::CatalogPackageOpenConfig openConfig{};
    openConfig.manifest.catalog.maxEntries = Terraria::WorldChunkCount + 64U;
    openConfig.manifest.catalog.maxDependencies = Terraria::WorldChunkCount * 2U + 64U;
    openConfig.manifest.catalog.maxDependenciesPerAsset = Terraria::WorldChunkCount + 8U;
    openConfig.manifest.catalog.memoryResource = &resources.memory;
    if (auto status = system->openAndBindCatalog(rootUtf8, openConfig); !status)
    {
        return status;
    }
    if (auto loaded = system->load(std::array{tileMapId}); !loaded)
    {
        return Tina::Core::failure(std::move(loaded.error()));
    }

    auto tileMapHandle = system->find(tileMapId);
    auto tilesetHandle = system->find(tilesetId);
    auto atlasHandle = system->find(atlasId);
    if (!tileMapHandle || !tilesetHandle || !atlasHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle,
                                   "tilemap, tileset, or atlas texture is not loaded");
    }
    resources.tileMapHandle = *tileMapHandle;
    resources.tilesetHandle = *tilesetHandle;
    resources.atlasTextureHandle = *atlasHandle;

    // AssetLease stores an AssetStore pointer, so the system must reach its final
    // address before any lease is acquired.
    resources.system = std::make_unique<Tina::Asset::AssetSystem>(std::move(*system));
    auto rootLease = resources.system->acquire(resources.tileMapHandle);
    if (!rootLease)
    {
        return Tina::Core::failure(std::move(rootLease.error()));
    }
    auto tilesetLease = resources.system->acquire(resources.tilesetHandle);
    if (!tilesetLease)
    {
        return Tina::Core::failure(std::move(tilesetLease.error()));
    }

    // residentCapacity covers the whole world: a chunk that gets unloaded comes back
    // from its cooked payload, which would undo every edit the player made in it.
    auto stream = Tina::Asset::TileMapStream::Create(
        *resources.system, std::move(*rootLease), std::move(*tilesetLease),
        Tina::Asset::TileMapStreamConfig{
            .residentCapacity = Terraria::WorldChunkCount,
            .requestBudgetPerUpdate = 64,
            .loadMarginChunks = 1,
            .retainMarginChunks = Terraria::WorldChunksX > Terraria::WorldChunksY
                                      ? static_cast<u16>(Terraria::WorldChunksX)
                                      : static_cast<u16>(Terraria::WorldChunksY),
            .memoryResource = &resources.memory,
        });
    if (!stream)
    {
        return Tina::Core::failure(std::move(stream.error()));
    }
    // Emplace at the final address before constructing anything that borrows map().
    resources.stream.emplace(std::move(*stream));
    resources.collision.emplace(resources.stream->map(), Terraria::WorldTileLayerId);

    resources.player.emplace(Tina::Asset::CharacterController2DConfig{
        .halfWidth = PlayerHalfWidth,
        .halfHeight = PlayerHalfHeight,
        .gravity = 32.0F,
        .maxFallSpeed = 45.0F,
    });
    resources.spawnX = (static_cast<float>(world.spawnCellX) + 0.5F) * Terraria::WorldCellSizeMeters;
    resources.spawnY = (static_cast<float>(world.spawnCellY) + PlayerHalfHeight) * Terraria::WorldCellSizeMeters;
    resources.player->teleport(resources.spawnX, resources.spawnY, true);
    resources.cameraCenterX = resources.spawnX;
    resources.cameraCenterY = resources.spawnY;
    return Tina::Core::success();
}

// Resolved edit request from one pointer edge.
struct EditRequest final {
    u32 cellX = 0;
    u32 cellY = 0;
    bool place = false;
};

// Turns unconsumed Simulation pointer edges into cell coordinates. The world sample
// is locked by Runtime at Action Mapping time, so this must not re-project it.
void collectEditRequests(std::span<const Tina::SimulationActionTransition> transitions,
                         std::pmr::vector<EditRequest>& out, RunCounters& counters)
{
    out.clear();
    for (const Tina::SimulationActionTransition& transition : transitions)
    {
        const auto* edge = std::get_if<Tina::InputActionTransition>(&transition);
        if (edge == nullptr || edge->kind != Tina::InputActionTransitionKind::Started)
        {
            continue;
        }
        ++counters.simStartedEdges;
        if (const u32 id = edge->action.value(); id < 32U)
        {
            counters.simActionIdMask |= 1U << id;
        }
        const bool place = edge->action == PlaceAction;
        if (!place && edge->action != DigAction)
        {
            continue;
        }
        if (place)
        {
            ++counters.rawPlaceEdges;
        }
        else
        {
            ++counters.rawDigEdges;
        }
        if (!edge->worldPointerSample.has_value())
        {
            ++counters.edgesMissingWorldSample;
            continue;
        }
        if (!edge->worldPointerSample->hit)
        {
            ++counters.edgesWorldSampleMissed;
            continue;
        }
        const Tina::Render::WorldPointerSample& sample = *edge->worldPointerSample;
        if (!std::isfinite(sample.worldX) || !std::isfinite(sample.worldY))
        {
            continue;
        }
        const float cellSize = Terraria::WorldCellSizeMeters;
        const double cellX = static_cast<double>(sample.worldX) / static_cast<double>(cellSize);
        const double cellY = static_cast<double>(sample.worldY) / static_cast<double>(cellSize);
        if (cellX < 0.0 || cellY < 0.0 || cellX >= static_cast<double>(Terraria::WorldWidthCells) ||
            cellY >= static_cast<double>(Terraria::WorldHeightCells))
        {
            continue;
        }
        out.push_back(EditRequest{
            .cellX = static_cast<u32>(std::floor(cellX)),
            .cellY = static_cast<u32>(std::floor(cellY)),
            .place = place,
        });
    }
}

class TerrariaState final : public Tina::IGameState {
  public:
    TerrariaState(const SampleOptions& options, WorldResources& resources, RunCounters& counters,
                  Terraria::DeviceCapture& capture) noexcept
        : options_(&options), resources_(&resources), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        auto* device = capture_->get();
        if (device == nullptr || resources_->system == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or catalog missing");
        }
        const auto* atlasFile = resources_->system->tryGet(resources_->atlasTextureHandle);
        if (atlasFile == nullptr)
        {
            return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "atlas CPU payload missing");
        }
        auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, *atlasFile);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        if (auto status = device->setTexture2DBinding(static_cast<u32>(AtlasBindingKey), *texture); !status)
        {
            (void)device->destroyTexture2D(*texture);
            return status;
        }
        resources_->gpuAtlas = *texture;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        auto* device = capture_->get();
        if (device == nullptr || !resources_->gpuAtlas)
        {
            return;
        }
        (void)device->setTexture2DBinding(static_cast<u32>(AtlasBindingKey), {});
        auto retirement =
            resources_->system->retireTexture2D(*device, resources_->atlasTextureHandle, resources_->gpuAtlas);
        if (!retirement && !counters_->shutdownError.has_value())
        {
            counters_->shutdownError.emplace(std::move(retirement.error()));
        }
        resources_->gpuAtlas = {};
    }

    Tina::Core::Status fixedUpdate(Tina::FixedUpdateContext& context) override
    {
        ++counters_->fixedSteps;
        const auto& actions = context.simulationActions();
        const float step = static_cast<float>(context.fixedUpdateTiming().fixedDelta.count());

        // Residency comes first: both querySolidAabb and tileIdAt fail outright on a
        // non-resident chunk, so nothing may touch the map until the world is in.
        if (auto status = updateStreamResidency(); !status)
        {
            return status;
        }
        if (counters_->residentChunks < Terraria::WorldChunkCount)
        {
            return Tina::Core::success();
        }

        collectEditRequests(actions.transitions, editScratch_, *counters_);
        for (const EditRequest& request : editScratch_)
        {
            if (auto status = applyEdit(request); !status)
            {
                return status;
            }
        }
        if (auto status = runSelfTestEdits(); !status)
        {
            return status;
        }

        return advancePlayer(step);
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frames;
        const auto& actions = context.frameActions();
        wishVelocityX_ = 0.0F;
        if (actions.isActive(MoveLeftAction))
        {
            wishVelocityX_ -= PlayerMoveSpeed;
        }
        if (actions.isActive(MoveRightAction))
        {
            wishVelocityX_ += PlayerMoveSpeed;
        }
        jumpHeld_ = actions.isActive(JumpAction);

        if (options_->maxFrames != 0 && counters_->frames >= options_->maxFrames)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        auto& writer = context.renderSceneWriter();
        auto& frameResources = context.frameResourceSink();

        const float halfHeight = CameraHeightMeters * 0.5F;
        const float aspect = static_cast<float>(options_->windowLogicalWidth) /
                             static_cast<float>(options_->windowLogicalHeight == 0 ? 1U
                                                                                  : options_->windowLogicalHeight);
        const float halfWidth = halfHeight * aspect;

        if (auto status = writer.setCamera2D(Tina::Render::RenderCamera2DInput{
                .stableCameraKey = 1,
                .centerX = resources_->cameraCenterX,
                .centerY = resources_->cameraCenterY,
                .rotationRadians = 0.0F,
                .worldWidth = halfWidth * 2.0F,
                .worldHeight = CameraHeightMeters,
                .actualPixelsPerMeter =
                    static_cast<float>(options_->windowLogicalHeight) / CameraHeightMeters,
            });
            !status)
        {
            return status;
        }

        const Tina::Asset::TileChunkCameraQuery query{
            .centerX = resources_->cameraCenterX,
            .centerY = resources_->cameraCenterY,
            .halfWidth = halfWidth,
            .halfHeight = halfHeight,
        };

        tileSprites_.clear();
        auto emitted = Tina::Asset::emitVisibleTileMapSprites(
            resources_->stream->map(), Terraria::WorldTileLayerId, query,
            Tina::Asset::TileChunkSpriteEmitParams{
                .tileset = resources_->tilesetHandle,
                .bindingResolver =
                    Tina::Asset::AssetFrameResourceResolver{
                        .userData = const_cast<TerrariaState*>(this),
                        .resolve = &TerrariaState::resolveAtlasBinding,
                    },
                .stableEntityKeyBase = TileStableEntityKeyBase,
                .sortingLayer = TileSortingLayer,
            },
            frameResources, tileSprites_);
        if (!emitted)
        {
            return Tina::Core::failure(std::move(emitted.error()));
        }
        counters_->lastTileSprites = *emitted;
        for (const Tina::Render::RenderSprite2DInput& sprite : tileSprites_)
        {
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }

        auto atlas = atlasFrameResource_.intern(frameResources, AtlasBindingKey);
        if (!atlas)
        {
            return Tina::Core::failure(std::move(atlas.error()));
        }
        const Tina::Asset::CharacterController2DState& player = resources_->player->state();
        return writer.addSprite2D(Tina::Render::RenderSprite2DInput{
            .texture = *atlas,
            .stableEntityKey = PlayerStableEntityKey,
            .centerX = player.positionX,
            .centerY = player.positionY,
            .rotationRadians = 0.0F,
            .widthMeters = PlayerHalfWidth * 2.0F,
            .heightMeters = PlayerHalfHeight * 2.0F,
            .scaleX = 1.0F,
            .scaleY = 1.0F,
            .u0 = Terraria::PlayerAtlasU0,
            .v0 = 0.0F,
            .u1 = Terraria::PlayerAtlasU1,
            .v1 = 1.0F,
            .sortingLayer = PlayerSortingLayer,
            .orderInLayer = 0,
            .red = 255,
            .green = 255,
            .blue = 255,
            .alpha = 255,
            .flipX = false,
            .flipY = false,
            .visible = true,
        });
    }

  private:
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolveAtlasBinding(void* userData, Tina::Asset::AssetHandle, Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto* self = static_cast<TerrariaState*>(userData);
        if (self == nullptr)
        {
            return Tina::Render::FrameResourceRef{};
        }
        return self->atlasFrameResource_.intern(sink, AtlasBindingKey);
    }

    // Drives one dig then one place through applyEdit, on a cell beside the player so
    // it passes the reach test and does not overlap the body. Runs once the world is
    // fully resident and the player has settled on the ground.
    [[nodiscard]] Tina::Core::Status runSelfTestEdits()
    {
        if (!options_->selfTestEdits || selfTestDone_)
        {
            return Tina::Core::success();
        }
        ++counters_->selfTestAttempts;
        const Tina::Asset::CharacterController2DState& player = resources_->player->state();
        if (!player.grounded)
        {
            ++counters_->selfTestSkippedNotGrounded;
            return Tina::Core::success();
        }

        const float cell = Terraria::WorldCellSizeMeters;
        const u32 playerCellX = static_cast<u32>(player.positionX / cell);
        const u32 footCellY = static_cast<u32>((player.positionY - PlayerHalfHeight) / cell);
        if (playerCellX + 2U >= Terraria::WorldWidthCells || footCellY == 0U)
        {
            selfTestDone_ = true;
            return Tina::Core::success();
        }
        // Two cells to the right and one BELOW foot level. footCellY is the air cell the
        // feet occupy, so digging there would be a no-op against air; the ground the
        // player stands on is one row down.
        const u32 targetX = playerCellX + 2U;
        const u32 targetY = footCellY - 1U;

        if (auto status = applyEdit(EditRequest{.cellX = targetX, .cellY = targetY, .place = false}); !status)
        {
            return status;
        }
        if (auto status = applyEdit(EditRequest{.cellX = targetX, .cellY = targetY, .place = true}); !status)
        {
            return status;
        }
        selfTestDone_ = true;
        return Tina::Core::success();
    }

    [[nodiscard]] float editReachMeters() const noexcept
    {
        const Tina::Asset::CharacterController2DState& player = resources_->player->state();
        const float halfHeight = CameraHeightMeters * 0.5F;
        const float aspect = static_cast<float>(options_->windowLogicalWidth) /
                             static_cast<float>(options_->windowLogicalHeight == 0U
                                                     ? 1U
                                                     : options_->windowLogicalHeight);
        const float halfWidth = halfHeight * aspect;
        return std::hypot(std::abs(resources_->cameraCenterX - player.positionX) + halfWidth,
                          std::abs(resources_->cameraCenterY - player.positionY) + halfHeight) +
               EditReachMarginMeters;
    }

    // Digging clears to air rather than tile id 0 so the cell stays part of a
    // non-empty chunk and can be built on again.
    [[nodiscard]] Tina::Core::Status applyEdit(const EditRequest& request)
    {
        if (request.place)
        {
            ++counters_->placeRequests;
        }
        else
        {
            ++counters_->digRequests;
        }

        const Tina::Asset::CharacterController2DState& player = resources_->player->state();
        const float targetX = (static_cast<float>(request.cellX) + 0.5F) * Terraria::WorldCellSizeMeters;
        const float targetY = (static_cast<float>(request.cellY) + 0.5F) * Terraria::WorldCellSizeMeters;
        const float dx = targetX - player.positionX;
        const float dy = targetY - player.positionY;
        const float reach = editReachMeters();
        if ((dx * dx + dy * dy) > (reach * reach))
        {
            ++counters_->editRejectedOutOfReach;
            return Tina::Core::success();
        }

        Tina::Asset::TileMapInstance& map = resources_->stream->map();
        auto current = map.tileIdAt(Terraria::WorldTileLayerId, request.cellX, request.cellY);
        if (!current)
        {
            return Tina::Core::failure(std::move(current.error()));
        }

        if (request.place)
        {
            // Only air is replaceable, and never the cell the player stands in.
            if (*current != Terraria::TileAir)
            {
                ++counters_->editRejectedOccupied;
                return Tina::Core::success();
            }
            if (overlapsPlayer(request.cellX, request.cellY))
            {
                ++counters_->editRejectedOccupied;
                return Tina::Core::success();
            }
            if (auto status = map.setTile(Terraria::WorldTileLayerId, request.cellX, request.cellY,
                                          Terraria::TileDirt);
                !status)
            {
                return status;
            }
            ++counters_->placeApplied;
            return Tina::Core::success();
        }

        if (*current == Terraria::TileAir)
        {
            ++counters_->editRejectedOccupied;
            return Tina::Core::success();
        }
        if (auto status =
                map.setTile(Terraria::WorldTileLayerId, request.cellX, request.cellY, Terraria::TileAir);
            !status)
        {
            return status;
        }
        ++counters_->digApplied;
        return Tina::Core::success();
    }

    [[nodiscard]] bool overlapsPlayer(u32 cellX, u32 cellY) const noexcept
    {
        const Tina::Asset::CharacterController2DState& player = resources_->player->state();
        const float cell = Terraria::WorldCellSizeMeters;
        const float minX = static_cast<float>(cellX) * cell;
        const float minY = static_cast<float>(cellY) * cell;
        return minX < player.positionX + PlayerHalfWidth && minX + cell > player.positionX - PlayerHalfWidth &&
               minY < player.positionY + PlayerHalfHeight && minY + cell > player.positionY - PlayerHalfHeight;
    }

    [[nodiscard]] Tina::Core::Status advancePlayer(float step)
    {
        const Tina::Asset::CharacterController2DMoveInput input{
            .wishVelocityX = wishVelocityX_,
            .jump = jumpHeld_,
            .jumpSpeed = PlayerJumpSpeed,
        };
        if (auto status =
                resources_->player->move(*resources_->collision, step, input, resources_->solidScratch);
            !status)
        {
            return status;
        }
        const Tina::Asset::CharacterController2DState& player = resources_->player->state();
        if (player.grounded)
        {
            counters_->playerEverGrounded = true;
        }

        // Camera trails the player, clamped so the view never leaves the world.
        const float halfHeight = CameraHeightMeters * 0.5F;
        const float aspect = static_cast<float>(options_->windowLogicalWidth) /
                             static_cast<float>(options_->windowLogicalHeight == 0 ? 1U
                                                                                  : options_->windowLogicalHeight);
        const float halfWidth = halfHeight * aspect;
        const float worldWidth = static_cast<float>(Terraria::WorldWidthCells) * Terraria::WorldCellSizeMeters;
        const float worldHeight = static_cast<float>(Terraria::WorldHeightCells) * Terraria::WorldCellSizeMeters;
        resources_->cameraCenterX =
            halfWidth * 2.0F >= worldWidth
                ? worldWidth * 0.5F
                : std::clamp(player.positionX, halfWidth, worldWidth - halfWidth);
        resources_->cameraCenterY =
            halfHeight * 2.0F >= worldHeight
                ? worldHeight * 0.5F
                : std::clamp(player.positionY, halfHeight, worldHeight - halfHeight);
        return Tina::Core::success();
    }

    // Demand covers the whole world, so nothing is ever evicted and tile edits
    // survive for the entire run.
    [[nodiscard]] Tina::Core::Status updateStreamResidency()
    {
        const float worldWidth = static_cast<float>(Terraria::WorldWidthCells) * Terraria::WorldCellSizeMeters;
        const float worldHeight = static_cast<float>(Terraria::WorldHeightCells) * Terraria::WorldCellSizeMeters;
        const std::array demands{Tina::Asset::TileMapChunkDemand{
            .layerId = Terraria::WorldTileLayerId,
            .priority = 0,
            .camera =
                Tina::Asset::TileChunkCameraQuery{
                    .centerX = worldWidth * 0.5F,
                    .centerY = worldHeight * 0.5F,
                    .halfWidth = worldWidth * 0.5F,
                    .halfHeight = worldHeight * 0.5F,
                },
        }};
        if (auto status = resources_->stream->updateDemand(demands); !status)
        {
            return status;
        }
        if (auto pumped = resources_->system->pump(); !pumped)
        {
            return Tina::Core::failure(std::move(pumped.error()));
        }
        auto stats = resources_->stream->commitReady();
        if (!stats)
        {
            return Tina::Core::failure(std::move(stats.error()));
        }
        counters_->residentChunks = stats->residentSlots;
        return Tina::Core::success();
    }

    const SampleOptions* options_ = nullptr;
    WorldResources* resources_ = nullptr;
    RunCounters* counters_ = nullptr;
    Terraria::DeviceCapture* capture_ = nullptr;
    float wishVelocityX_ = 0.0F;
    bool jumpHeld_ = false;
    bool selfTestDone_ = false;
    std::pmr::vector<EditRequest> editScratch_{&resources_->memory};
    mutable std::pmr::vector<Tina::Render::RenderSprite2DInput> tileSprites_{&resources_->memory};
    mutable Tina::Samples::SampleSpriteFrameResource atlasFrameResource_{};
};

class TerrariaApplication final : public Tina::IGameApplication {
  public:
    TerrariaApplication(const SampleOptions& options, WorldResources& resources, RunCounters& counters,
                        Terraria::DeviceCapture& capture) noexcept
        : options_(&options), resources_(&resources), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<TerrariaState>(*options_, *resources_, *counters_, *capture_)};
    }

  private:
    const SampleOptions* options_ = nullptr;
    WorldResources* resources_ = nullptr;
    RunCounters* counters_ = nullptr;
    Terraria::DeviceCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(const SampleOptions& options)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Terraria Sample";
    config.primaryWindow.title = "Tina 2D Terraria — dig with LMB, build with RMB";
    config.primaryWindow.initialLogicalExtent = {options.windowLogicalWidth, options.windowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    // Every visible cell is one sprite, air included, so the budget scales with the
    // viewport rather than the world.
    config.renderSceneCapacities.spriteCapacity = 32'768;

    const auto bindKey = [&config](Tina::Platform::Key key, Tina::InputActionId action) {
        config.inputActions.bindings.push_back(Tina::InputActionBinding{
            .input = Tina::PrimaryWindowKeyBinding{.key = key},
            .action = action,
            .domain = Tina::InputActionDomain::Frame,
        });
    };
    bindKey(Tina::Platform::Key::A, MoveLeftAction);
    bindKey(Tina::Platform::Key::Left, MoveLeftAction);
    bindKey(Tina::Platform::Key::D, MoveRightAction);
    bindKey(Tina::Platform::Key::Right, MoveRightAction);
    bindKey(Tina::Platform::Key::Space, JumpAction);
    bindKey(Tina::Platform::Key::W, JumpAction);

    // Pointer edits must be Simulation domain: that is the only domain Runtime
    // attaches a locked worldPointerSample to.
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PointerButtonBinding{.pointer = Tina::Platform::PrimaryPointerId,
                                            .button = Tina::Platform::PointerButton::Primary},
        .action = DigAction,
        .domain = Tina::InputActionDomain::Simulation,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PointerButtonBinding{.pointer = Tina::Platform::PrimaryPointerId,
                                            .button = Tina::Platform::PointerButton::Secondary},
        .action = PlaceAction,
        .domain = Tina::InputActionDomain::Simulation,
    });
    // Second placement button so a dead Secondary can be told apart from a dead
    // second pointer binding.
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PointerButtonBinding{.pointer = Tina::Platform::PrimaryPointerId,
                                            .button = Tina::Platform::PointerButton::Middle},
        .action = PlaceAction,
        .domain = Tina::InputActionDomain::Simulation,
    });
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    auto parsedOptions = parseOptions(argc, argv);
    if (!parsedOptions)
    {
        writeError(parsedOptions.error());
        return 1;
    }
    const SampleOptions options = *parsedOptions;
    RunCounters counters{};
    WorldResources resources{};

    if (auto status = prepareWorld(options, resources, counters); !status)
    {
        writeError(status.error());
        return 1;
    }
    auto catalogCleanup = Tina::Core::makeScopeExit([&resources]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(resources.catalogRoot, cleanupError);
    });

    Terraria::DeviceCapture capture{};
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    auto uiFont = Tina::Desktop::resolveUiFontBytes();
    if (!uiFont)
    {
        writeError(uiFont.error());
        return 1;
    }
    desktopOptions.uiFontBytes = std::move(uiFont->bytes);
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&capture](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
        return Terraria::wrapCapturingRenderDevice(std::move(device), capture);
    };

    auto host = Tina::Desktop::CreateEngine(createEngineConfig(options), std::move(desktopOptions));
    if (!host)
    {
        writeError(host.error());
        return 1;
    }

    TerrariaApplication application{options, resources, counters, capture};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }
    if (counters.shutdownError.has_value())
    {
        writeError(*counters.shutdownError);
        return 1;
    }

    std::printf("world_cells=%ux%u solid=%u air=%u recipe_assets=%u\n", Terraria::WorldWidthCells,
                Terraria::WorldHeightCells, counters.generatedSolidCells, counters.generatedAirCells,
                counters.cookedRecipeAssets);
    std::printf("frames=%llu fixed_steps=%llu resident_chunks=%llu last_tile_sprites=%llu\n",
                static_cast<unsigned long long>(counters.frames),
                static_cast<unsigned long long>(counters.fixedSteps),
                static_cast<unsigned long long>(counters.residentChunks),
                static_cast<unsigned long long>(counters.lastTileSprites));
    std::printf("dig=%llu/%llu place=%llu/%llu rejected_reach=%llu rejected_occupied=%llu grounded=%d\n",
                static_cast<unsigned long long>(counters.digApplied),
                static_cast<unsigned long long>(counters.digRequests),
                static_cast<unsigned long long>(counters.placeApplied),
                static_cast<unsigned long long>(counters.placeRequests),
                static_cast<unsigned long long>(counters.editRejectedOutOfReach),
                static_cast<unsigned long long>(counters.editRejectedOccupied),
                counters.playerEverGrounded ? 1 : 0);
    std::printf("selftest_attempts=%llu selftest_skipped_airborne=%llu\n",
                static_cast<unsigned long long>(counters.selfTestAttempts),
                static_cast<unsigned long long>(counters.selfTestSkippedNotGrounded));
    std::printf("raw_edges dig=%llu place=%llu no_world_sample=%llu sample_missed=%llu\n",
                static_cast<unsigned long long>(counters.rawDigEdges),
                static_cast<unsigned long long>(counters.rawPlaceEdges),
                static_cast<unsigned long long>(counters.edgesMissingWorldSample),
                static_cast<unsigned long long>(counters.edgesWorldSampleMissed));
    std::printf("sim_started_edges=%llu action_id_mask=0x%08X\n",
                static_cast<unsigned long long>(counters.simStartedEdges),
                static_cast<unsigned int>(counters.simActionIdMask));
    return 0;
}
