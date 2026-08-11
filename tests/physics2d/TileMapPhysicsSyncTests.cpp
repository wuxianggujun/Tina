#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset/TileMapPhysicsSync.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <gtest/gtest.h>

#include "../asset/support/TileMapInstanceTestSupport.hpp"

#include <array>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace Tina {
namespace {

inline constexpr AssetFormat::TileMapLayerId CollisionLayerId = 20;
inline constexpr AssetFormat::TileMapLayerId ObjectLayerId = 30;
inline constexpr Core::u16 SolidTile = 1;
inline constexpr Core::u16 DecorTile = 2;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

// localId 1 is MaterialSolid; localId 2 is a visible-but-passable decoration.
[[nodiscard]] AssetFormat::TilesetPayloadView parseSharedTileset(
    std::vector<std::byte>& storage)
{
    const std::array tiles{
        AssetFormat::TilesetTileDesc{
            .localId = SolidTile,
            .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
            .u0 = 0.0F,
            .v0 = 0.0F,
            .u1 = 0.5F,
            .v1 = 1.0F},
        AssetFormat::TilesetTileDesc{
            .localId = DecorTile,
            .materialFlags = 0,
            .u0 = 0.5F,
            .v0 = 0.0F,
            .u1 = 1.0F,
            .v1 = 1.0F},
    };
    auto bytes = AssetFormat::writeTilesetPayloadBytes(AssetFormat::TilesetPayloadDesc{
        .tilePixelWidth = 16,
        .tilePixelHeight = 16,
        .tiles = tiles,
    });
    storage = std::move(*bytes);
    return *AssetFormat::parseTilesetPayload(storage);
}

[[nodiscard]] Physics2D::PhysicsWorld2DConfig worldConfig(
    Core::usize bodyCapacity = 32,
    Core::usize shapeCapacity = 64) noexcept
{
    Physics2D::PhysicsWorld2DConfig config;
    config.bodyCapacity = bodyCapacity;
    config.shapeCapacity = shapeCapacity;
    config.contactBeginCapacity = 16;
    config.contactEndCapacity = 16;
    config.contactHitCapacity = 4;
    config.commandCapacity = 8;
    config.solverSubStepCount = 1;
    config.gravityMetersPerSecondSquared = {0.0F, -9.8F};
    return config;
}

[[nodiscard]] Asset::TileMapPhysicsSync2DConfig syncConfig(
    std::pmr::memory_resource& memory,
    Core::usize chunkCapacity = 16) noexcept
{
    Asset::TileMapPhysicsSync2DConfig config;
    config.layerId = CollisionLayerId;
    config.chunkCapacity = chunkCapacity;
    config.material.enableContactEvents = true;
    config.memoryResource = &memory;
    return config;
}

// Counts allocations so the "no allocation after Create()" contract is provable.
class CountingResource final : public std::pmr::memory_resource {
  public:
    explicit CountingResource(std::pmr::memory_resource* upstream) noexcept
        : m_upstream(upstream)
    {
    }

    [[nodiscard]] Core::usize allocationCount() const noexcept
    {
        return m_allocationCount;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++m_allocationCount;
        return m_upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_upstream->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::pmr::memory_resource* m_upstream = nullptr;
    Core::usize m_allocationCount = 0;
};

// Rebuilds the chunk payload for one coordinate so it can be re-attached after
// a detach, mirroring what TileMapStream does on unload/reload.
[[nodiscard]] std::vector<std::byte> makeChunkPayload(
    Core::AssetId mapId,
    Core::u32 widthCells,
    Core::u16 chunkSizeCells,
    std::span<const Core::u16> cells,
    Core::u32 chunkX,
    Core::u32 chunkY,
    Core::u16 chunkWidth,
    Core::u16 chunkHeight)
{
    std::vector<Core::u16> chunkCells;
    chunkCells.reserve(static_cast<std::size_t>(chunkWidth) * chunkHeight);
    Core::u32 nonEmpty = 0;
    const Core::u32 originX = chunkX * chunkSizeCells;
    const Core::u32 originY = chunkY * chunkSizeCells;
    for (Core::u16 y = 0; y < chunkHeight; ++y) {
        for (Core::u16 x = 0; x < chunkWidth; ++x) {
            const Core::u16 cell = cells[(originY + y) * widthCells + originX + x];
            chunkCells.push_back(cell);
            nonEmpty += cell != AssetFormat::TileMapWire::EmptyTileId ? 1U : 0U;
        }
    }
    auto payload = AssetFormat::writeTileMapChunkPayloadBytes(
        AssetFormat::TileMapChunkPayloadDesc{
            .parentTileMapId = mapId,
            .layerId = CollisionLayerId,
            .chunkX = chunkX,
            .chunkY = chunkY,
            .widthCells = chunkWidth,
            .heightCells = chunkHeight,
            .cells = chunkCells,
        });
    return std::move(*payload);
}

} // namespace

// Create() must reject every out-of-contract configuration before publishing.
TEST(TileMapPhysicsSync2DTest, CreateRejectsInvalidConfiguration)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(31U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(32U));

    const std::array<Core::u16, 4> cells{SolidTile, SolidTile, 0, 0};
    const std::array objects{AssetFormat::TileMapObjectDesc{
        .stableObjectId = 101,
        .kind = AssetFormat::TileMapObjectKind::Point,
        .visible = true,
        .name = "spawn",
        .x = 0.5F,
        .y = 0.5F,
    }};
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = ObjectLayerId,
            .kind = AssetFormat::TileMapLayerKind::Object,
            .visible = true,
            .name = "gameplay",
            .objects = objects,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        2U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    // layerId 0 has no meaning: there is no implicit default layer.
    auto zeroLayer = syncConfig(memory);
    zeroLayer.layerId = 0;
    EXPECT_FALSE(Asset::TileMapPhysicsSync2D::Create(*instance, zeroLayer));

    auto zeroChunks = syncConfig(memory);
    zeroChunks.chunkCapacity = 0;
    EXPECT_FALSE(Asset::TileMapPhysicsSync2D::Create(*instance, zeroChunks));

    auto hugeChunks = syncConfig(memory);
    hugeChunks.chunkCapacity = 4097;
    EXPECT_FALSE(Asset::TileMapPhysicsSync2D::Create(*instance, hugeChunks));

    // An object layer cannot supply solid cells.
    auto objectLayer = syncConfig(memory);
    objectLayer.layerId = ObjectLayerId;
    auto objectResult = Asset::TileMapPhysicsSync2D::Create(*instance, objectLayer);
    ASSERT_FALSE(objectResult);
    EXPECT_EQ(objectResult.error().code, Asset::AssetErrorCode::TileMapLayerTypeMismatch);

    auto missingLayer = syncConfig(memory);
    missingLayer.layerId = 99;
    EXPECT_FALSE(Asset::TileMapPhysicsSync2D::Create(*instance, missingLayer));

    // categoryBits=0 would produce colliders that collide with nothing.
    auto unfiltered = syncConfig(memory);
    unfiltered.material.filter.categoryBits = 0;
    EXPECT_FALSE(Asset::TileMapPhysicsSync2D::Create(*instance, unfiltered));

    // A per-chunk rectangle budget larger than one source chunk is nonsense.
    auto oversizedRectangles = syncConfig(memory);
    oversizedRectangles.rectangleCapacityPerChunk = 5; // chunk holds 2x2 = 4 cells
    EXPECT_FALSE(
        Asset::TileMapPhysicsSync2D::Create(*instance, oversizedRectangles));

    auto valid = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(valid) << valid.error().message;
    EXPECT_TRUE(static_cast<bool>(*valid));
}

// A fully solid chunk must merge into a single rectangle, and an L shape must
// merge into a deterministic row-major rectangle sequence.
TEST(TileMapPhysicsSync2DTest, MergesSolidCellsIntoDeterministicRectangles)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(33U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(34U));

    // One 4x4 chunk. Rows are listed bottom-up (y=0 first):
    //   y=3: . . . .
    //   y=2: # . . .
    //   y=1: # . . .
    //   y=0: # # # #
    // Greedy row-major merge yields exactly two rectangles:
    //   {0,0,4x1} (the full bottom run) then {0,1,1x2} (the remaining column).
    const std::array<Core::u16, 16> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        SolidTile, 0,         0,         0,
        SolidTile, 0,         0,         0,
        0,         0,         0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 4U, 4U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;

    auto stats = sync->synchronize(*instance, world);
    ASSERT_TRUE(stats) << stats.error().message;
    EXPECT_EQ(stats->residentChunkCount, 1U);
    EXPECT_EQ(stats->colliderBodyCount, 1U);
    // 6 solid cells collapse into 2 boxes rather than 6.
    EXPECT_EQ(stats->colliderShapeCount, 2U);
    EXPECT_EQ(stats->lastBakedRectangleCount, 2U);

    // Verify the merged geometry actually covers the intended cells. The bottom
    // run spans x in [0,4) at y in [0,1); the column spans x in [0,1), y [1,3).
    const auto overlapCount = [&world](float minX, float minY, float maxX, float maxY) {
        std::array<Physics2D::PhysicsOverlapHit2D, 8> hits{};
        auto result = world.overlapAabb(
            Physics2D::PhysicsAabb2D{.lowerMeters = {minX, minY}, .upperMeters = {maxX, maxY}},
            Physics2D::PhysicsQueryFilter2D{},
            hits);
        return result ? result->totalFound : Core::usize{0};
    };
    // Far end of the bottom run is covered (proves the box spans, not just cell 0).
    EXPECT_GT(overlapCount(3.4F, 0.4F, 3.6F, 0.6F), 0U);
    // Top of the column is covered.
    EXPECT_GT(overlapCount(0.4F, 2.4F, 0.6F, 2.6F), 0U);
    // The empty interior is not covered.
    EXPECT_EQ(overlapCount(2.4F, 2.4F, 2.6F, 2.6F), 0U);
}

// Re-synchronizing an unchanged map must not touch any collider.
TEST(TileMapPhysicsSync2DTest, UnchangedResynchronizeKeepsBodiesAndAllocatesNothing)
{
    std::pmr::unsynchronized_pool_resource upstream;
    CountingResource memory{&upstream};
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(35U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(36U));

    // 4x2 map with chunkSize 2 => 2 chunks, both containing solid cells.
    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         0,         DecorTile, 0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, upstream);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    const Core::usize allocationsAfterCreate = memory.allocationCount();
    ASSERT_GT(allocationsAfterCreate, 0U);

    auto first = sync->synchronize(*instance, world);
    ASSERT_TRUE(first) << first.error().message;
    EXPECT_EQ(first->residentChunkCount, 2U);
    EXPECT_EQ(first->colliderBodyCount, 2U);
    EXPECT_EQ(first->lastAddedChunkCount, 2U);
    EXPECT_EQ(first->lastUnchangedChunkCount, 0U);
    const Core::usize bodiesAfterFirst = world.stats().bodyCount;
    const Core::usize shapesAfterFirst = world.stats().shapeCount;
    EXPECT_EQ(bodiesAfterFirst, 2U);

    for (int pass = 0; pass < 3; ++pass) {
        auto again = sync->synchronize(*instance, world);
        ASSERT_TRUE(again) << again.error().message;
        EXPECT_EQ(again->lastAddedChunkCount, 0U);
        EXPECT_EQ(again->lastRebuiltChunkCount, 0U);
        EXPECT_EQ(again->lastRemovedChunkCount, 0U);
        EXPECT_EQ(again->lastUnchangedChunkCount, 2U);
        // Nothing was destroyed and recreated.
        EXPECT_EQ(world.stats().bodyCount, bodiesAfterFirst);
        EXPECT_EQ(world.stats().shapeCount, shapesAfterFirst);
    }

    // Steady state is allocation free: Create() is the only allocation point.
    EXPECT_EQ(memory.allocationCount(), allocationsAfterCreate);
}

// Editing one tile must rebuild only the affected chunk's collider.
TEST(TileMapPhysicsSync2DTest, ContentRevisionRebuildsOnlyTheChangedChunk)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(37U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(38U));

    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         0,         0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    ASSERT_TRUE(sync->synchronize(*instance, world));
    EXPECT_EQ(world.stats().bodyCount, 2U);
    EXPECT_EQ(world.stats().shapeCount, 2U);

    // Add a solid tile above the floor inside chunk (0,0) only.
    ASSERT_TRUE(instance->setTile(CollisionLayerId, 0, 1, SolidTile));

    auto rebuilt = sync->synchronize(*instance, world);
    ASSERT_TRUE(rebuilt) << rebuilt.error().message;
    EXPECT_EQ(rebuilt->lastRebuiltChunkCount, 1U);
    EXPECT_EQ(rebuilt->lastUnchangedChunkCount, 1U);
    EXPECT_EQ(rebuilt->lastAddedChunkCount, 0U);
    EXPECT_EQ(rebuilt->lastRemovedChunkCount, 0U);
    // Still one body per resident chunk: the old collider was retired, not leaked.
    EXPECT_EQ(rebuilt->colliderBodyCount, 2U);
    EXPECT_EQ(world.stats().bodyCount, 2U);
    // Chunk (0,0) now needs two boxes (floor run + the new cell above).
    EXPECT_EQ(rebuilt->colliderShapeCount, 3U);
    EXPECT_EQ(world.stats().shapeCount, 3U);

    // Removing the tile again collapses back to the original shape count.
    ASSERT_TRUE(instance->setTile(CollisionLayerId, 0, 1, 0));
    auto reverted = sync->synchronize(*instance, world);
    ASSERT_TRUE(reverted) << reverted.error().message;
    EXPECT_EQ(reverted->lastRebuiltChunkCount, 1U);
    EXPECT_EQ(reverted->colliderShapeCount, 2U);
    EXPECT_EQ(world.stats().shapeCount, 2U);
}

// Streaming unload/reload must drive collider destroy/create.
TEST(TileMapPhysicsSync2DTest, ResidencyChangesAddAndRemoveChunkColliders)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(39U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(40U));

    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         0,         0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    ASSERT_TRUE(sync->synchronize(*instance, world));
    EXPECT_EQ(world.stats().bodyCount, 2U);

    // Stream unload of chunk (1,0).
    const Asset::TileMapChunkCoord unloaded{.chunkX = 1, .chunkY = 0};
    ASSERT_TRUE(instance->detachChunk(CollisionLayerId, unloaded));

    auto removed = sync->synchronize(*instance, world);
    ASSERT_TRUE(removed) << removed.error().message;
    EXPECT_EQ(removed->residentChunkCount, 1U);
    EXPECT_EQ(removed->lastRemovedChunkCount, 1U);
    EXPECT_EQ(removed->colliderBodyCount, 1U);
    // The unloaded chunk's collider is gone from the world, not orphaned.
    EXPECT_EQ(world.stats().bodyCount, 1U);
    EXPECT_EQ(world.stats().shapeCount, 1U);

    // Stream reload with a fresh residency generation.
    auto payload = makeChunkPayload(mapId, 4U, 2U, cells, 1U, 0U, 2U, 2U);
    auto chunkView = AssetFormat::parseTileMapChunkPayload(payload);
    ASSERT_TRUE(chunkView) << chunkView.error().message;
    auto chunkId = Asset::TestSupport::makeDerivedChunkId(mapId, CollisionLayerId, 1U, 0U);
    ASSERT_TRUE(chunkId) << chunkId.error().message;
    ASSERT_TRUE(instance->attachChunk(*chunkId, *chunkView, 99U));

    auto readded = sync->synchronize(*instance, world);
    ASSERT_TRUE(readded) << readded.error().message;
    EXPECT_EQ(readded->residentChunkCount, 2U);
    EXPECT_EQ(readded->lastAddedChunkCount, 1U);
    EXPECT_EQ(readded->lastUnchangedChunkCount, 1U);
    EXPECT_EQ(readded->colliderBodyCount, 2U);
    EXPECT_EQ(world.stats().bodyCount, 2U);
}

// A failed bake/create must leave the previously published colliders intact.
TEST(TileMapPhysicsSync2DTest, FailedSynchronizeKeepsPreviousCollidersPublished)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(41U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(42U));

    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         0,         0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    // Exactly enough room for the initial 2 bodies / 2 shapes and nothing more,
    // so any rebuild that must stage a new collider before retiring the old one
    // is forced to fail.
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig(2, 2));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    auto published = sync->synchronize(*instance, world);
    ASSERT_TRUE(published) << published.error().message;
    ASSERT_EQ(world.stats().bodyCount, 2U);
    const Asset::TileMapPhysicsSync2DStats before = sync->stats();

    // Force a rebuild of chunk (0,0); staging a new body exceeds capacity.
    ASSERT_TRUE(instance->setTile(CollisionLayerId, 0, 1, SolidTile));
    auto failed = sync->synchronize(*instance, world);
    ASSERT_FALSE(failed);

    // The old colliders are still live and the stats were not advanced: no
    // half-published state is visible to the caller.
    EXPECT_EQ(world.stats().bodyCount, 2U);
    EXPECT_EQ(world.stats().shapeCount, 2U);
    EXPECT_EQ(sync->stats().synchronizeCount, before.synchronizeCount);
    EXPECT_EQ(sync->stats().colliderBodyCount, before.colliderBodyCount);
    EXPECT_EQ(sync->stats().colliderShapeCount, before.colliderShapeCount);

    // Retiring everything still works after the failure.
    ASSERT_TRUE(sync->shutdown(world));
    EXPECT_EQ(world.stats().bodyCount, 0U);
}

// Resident chunk count beyond the configured capacity fails without mutating.
TEST(TileMapPhysicsSync2DTest, ExceedingChunkCapacityFailsAndPreservesState)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(43U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(44U));

    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         0,         0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    // Two chunks are resident but only one slot was budgeted.
    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory, 1));
    ASSERT_TRUE(sync) << sync.error().message;

    auto overflow = sync->synchronize(*instance, world);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, Asset::AssetErrorCode::TileMapPhysicsCapacityExceeded);
    // The staged collider for the first chunk was rolled back.
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(sync->stats().synchronizeCount, 0U);
}

// synchronize() only accepts the map instance the sync was bound to.
TEST(TileMapPhysicsSync2DTest, RejectsMapOutsideTheBoundContract)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(45U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(46U));
    const auto otherMapId = *Core::AssetId::fromBytes(idBytes(47U));

    const std::array<Core::u16, 4> cells{SolidTile, SolidTile, 0, 0};
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        2U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;
    auto other = Asset::TestSupport::makeResidentTileMapInstance(
        2U, 2U, 2U, otherMapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(other) << other.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    EXPECT_FALSE(sync->synchronize(*other, world));
    EXPECT_EQ(world.stats().bodyCount, 0U);

    // A closed world is rejected rather than silently skipped.
    ASSERT_TRUE(sync->synchronize(*instance, world));
    ASSERT_TRUE(world.shutdown());
    auto closed = sync->synchronize(*instance, world);
    ASSERT_FALSE(closed);
    EXPECT_EQ(closed.error().code, Physics2D::Physics2DErrorCode::WorldClosed);
}

// shutdown() retires everything, zeroes the live stats, and is idempotent.
TEST(TileMapPhysicsSync2DTest, ShutdownRetiresAllCollidersAndIsIdempotent)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(48U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(49U));

    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         0,         0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    ASSERT_TRUE(sync->synchronize(*instance, world));
    ASSERT_EQ(world.stats().bodyCount, 2U);

    ASSERT_TRUE(sync->shutdown(world));
    EXPECT_EQ(world.stats().bodyCount, 0U);
    EXPECT_EQ(world.stats().shapeCount, 0U);
    EXPECT_EQ(sync->stats().residentChunkCount, 0U);
    EXPECT_EQ(sync->stats().colliderBodyCount, 0U);
    EXPECT_EQ(sync->stats().colliderShapeCount, 0U);
    EXPECT_EQ(sync->stats().lastRemovedChunkCount, 2U);

    // Idempotent: a second shutdown neither fails nor double-destroys.
    ASSERT_TRUE(sync->shutdown(world));
    EXPECT_EQ(world.stats().bodyCount, 0U);

    // The sync object is still usable and republishes from scratch.
    auto republished = sync->synchronize(*instance, world);
    ASSERT_TRUE(republished) << republished.error().message;
    EXPECT_EQ(republished->lastAddedChunkCount, 2U);
    EXPECT_EQ(world.stats().bodyCount, 2U);
}

// The synced terrain must actually stop a falling dynamic body.
TEST(TileMapPhysicsSync2DTest, ChunkColliderStopsFallingDynamicBody)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::vector<std::byte> tilesetStorage;
    const auto tileset = parseSharedTileset(tilesetStorage);
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(50U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(51U));

    // 4x2 floor on y=0 with a passable decoration above.
    const std::array<Core::u16, 8> cells{
        SolidTile, SolidTile, SolidTile, SolidTile,
        0,         DecorTile, 0,         0,
    };
    const std::array layers{
        Asset::TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = Asset::TestSupport::makeResidentTileMapInstance(
        4U, 2U, 2U, mapId, tilesetId, tileset, layers, memory);
    ASSERT_TRUE(instance) << instance.error().message;

    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig());
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    auto sync = Asset::TileMapPhysicsSync2D::Create(*instance, syncConfig(memory));
    ASSERT_TRUE(sync) << sync.error().message;
    auto stats = sync->synchronize(*instance, world);
    ASSERT_TRUE(stats) << stats.error().message;
    // The non-solid decoration contributed no collider.
    EXPECT_EQ(stats->colliderShapeCount, 2U);

    Physics2D::PhysicsBody2DDesc dynamicBody;
    dynamicBody.type = Physics2D::PhysicsBodyType2D::Dynamic;
    dynamicBody.positionMeters = {1.5F, 4.0F};
    dynamicBody.linearVelocityMetersPerSecond = {0.0F, -15.0F};
    auto dynamic = world.createBody(dynamicBody);
    ASSERT_TRUE(dynamic) << dynamic.error().message;

    Physics2D::PhysicsShape2DDesc box;
    box.kind = Physics2D::PhysicsShapeKind2D::Box;
    box.halfExtentsMeters = {0.4F, 0.4F};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamicShape = world.createShape(*dynamic, box);
    ASSERT_TRUE(dynamicShape) << dynamicShape.error().message;

    bool sawBegin = false;
    for (int step = 0; step < 240; ++step) {
        ASSERT_TRUE(world.step());
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts);
        if (!contacts->beginEvents.empty()) {
            sawBegin = true;
            break;
        }
    }
    EXPECT_TRUE(sawBegin);

    auto after = world.bodyState(*dynamic);
    ASSERT_TRUE(after) << after.error().message;
    EXPECT_LT(after->positionMeters.y, 4.0F);
    EXPECT_GT(after->positionMeters.y, 1.0F);
}

} // namespace Tina
