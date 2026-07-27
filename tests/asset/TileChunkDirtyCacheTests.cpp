#include <tina/asset/AssetStore.hpp>
#include <tina/asset/TileChunkDirtyCache.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>

#include "support/TileMapInstanceTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr AssetFormat::TileMapLayerId VisualLayerId = 10;
inline constexpr AssetFormat::TileMapLayerId AlternateLayerId = 20;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x3CU);
    return bytes;
}

// 8x8 map, chunk 2 → 4x4 chunks; fill a checker of non-empty tiles so several
// chunks are visible under a wide camera.
[[nodiscard]] TileMapInstance makeLargeMap(std::pmr::memory_resource& memory)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(11U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(12U));
    const std::array tiles{
        AssetFormat::TilesetTileDesc{.localId = 1,
                                     .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
                                     .u0 = 0,
                                     .v0 = 0,
                                     .u1 = 0.5f,
                                     .v1 = 1},
        AssetFormat::TilesetTileDesc{.localId = 2, .materialFlags = 0, .u0 = 0.5f, .v0 = 0, .u1 = 1, .v1 = 1},
    };
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    std::array<Core::u16, 64> cells{};
    for (Core::u32 y = 0; y < 8; ++y)
    {
        for (Core::u32 x = 0; x < 8; ++x)
        {
            // Sparse non-empty so multiple chunks exist but not all full.
            cells[y * 8 + x] = ((x + y) % 3 == 0) ? 1 : 0;
        }
    }
    const std::array layers{
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = VisualLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .cells = cells,
        },
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = AlternateLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "alternate",
            .cells = cells,
        },
    };
    auto instance = TestSupport::makeResidentTileMapInstance(8, 8, 2, mapId, tilesetId, *tileset, layers, memory);
    EXPECT_TRUE(instance.has_value()) << (instance ? "" : instance.error().message);
    return std::move(*instance);
}

[[nodiscard]] TileChunkCameraQuery fullMapCamera()
{
    return TileChunkCameraQuery{.centerX = 4.0f, .centerY = 4.0f, .halfWidth = 5.0f, .halfHeight = 5.0f};
}

struct TestTilesetBinding final {
    [[nodiscard]] static Core::Result<Render::FrameResourceRef>
    resolve(void* userData, AssetHandle tileset, Render::FrameResourceSink& sink) noexcept
    {
        const auto* expected = static_cast<const AssetHandle*>(userData);
        if (expected == nullptr || tileset != *expected)
        {
            return Render::FrameResourceRef{};
        }
        return sink.intern(
            Render::FrameResourceDescriptor{
                .kind = Render::FrameResourceKind::Sprite2DTexture,
                .deviceBindingKey = 1U,
            },
            Render::FramePin{Render::FramePinKind::Custom, 1U, nullptr, &release});
    }

    static void release(void*) noexcept
    {
    }
};

TEST(TileChunkDirtyCacheTests, FirstSyncRebuildsThenHitsWithoutEdits)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeLargeMap(memory);
    auto cache = TileChunkDirtyCache::Create({.capacity = 64, .memoryResource = &memory});
    ASSERT_TRUE(cache.has_value()) << cache.error().message;

    std::pmr::vector<TileChunkView> rebuilt{&memory};
    auto first = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_GT(*first, 0U);
    EXPECT_EQ(rebuilt.size(), *first);
    const auto firstRebuilds = *first;

    rebuilt.clear();
    auto second = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(*second, 0U);
    EXPECT_TRUE(rebuilt.empty());

    const auto st = cache->stats();
    EXPECT_EQ(st.framesSynced, 2U);
    EXPECT_EQ(st.rebuilds, firstRebuilds);
    EXPECT_EQ(st.cacheHits, firstRebuilds); // second frame hits each previous visible
    EXPECT_EQ(st.visibleChunkObservations, firstRebuilds * 2U);
}

TEST(TileChunkDirtyCacheTests, SetTileRebuildsOnlyAffectedChunk)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeLargeMap(memory);
    auto cache = TileChunkDirtyCache::Create({.capacity = 64, .memoryResource = &memory});
    ASSERT_TRUE(cache.has_value());

    std::pmr::vector<TileChunkView> rebuilt{&memory};
    ASSERT_TRUE(cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt).has_value());
    rebuilt.clear();
    ASSERT_TRUE(cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt).has_value());
    EXPECT_TRUE(rebuilt.empty());

    // Cell (0,0) is in chunk (0,0). Change tile id → only that chunk dirty.
    const auto revBefore = map.chunkRevision(VisualLayerId, 0, 0);
    ASSERT_TRUE(revBefore.has_value());
    ASSERT_TRUE(map.setTile(VisualLayerId, 0, 0, 2).has_value());
    const auto revisionAfterEdit = map.chunkRevision(VisualLayerId, 0, 0);
    ASSERT_TRUE(revisionAfterEdit.has_value());
    EXPECT_GT(*revisionAfterEdit, *revBefore);

    rebuilt.clear();
    auto afterEdit = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(afterEdit.has_value()) << afterEdit.error().message;
    EXPECT_EQ(*afterEdit, 1U);
    ASSERT_EQ(rebuilt.size(), 1U);
    EXPECT_EQ(rebuilt[0].coord.chunkX, 0U);
    EXPECT_EQ(rebuilt[0].coord.chunkY, 0U);
    EXPECT_EQ(rebuilt[0].revision, *revisionAfterEdit);
}

TEST(TileChunkDirtyCacheTests, TracksSameChunkCoordinatesPerLayer)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeLargeMap(memory);
    auto cache = TileChunkDirtyCache::Create({.capacity = 64, .memoryResource = &memory});
    ASSERT_TRUE(cache.has_value());

    std::pmr::vector<TileChunkView> rebuilt{&memory};
    auto visual = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(visual.has_value());
    ASSERT_GT(*visual, 0U);

    rebuilt.clear();
    auto alternate = cache->syncVisible(map, AlternateLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(alternate.has_value());
    EXPECT_EQ(*alternate, *visual);
    ASSERT_FALSE(rebuilt.empty());
    EXPECT_EQ(rebuilt.front().layerId, AlternateLayerId);
}

TEST(TileChunkDirtyCacheTests, IdenticalSetTileIsNoOpForRevisionAndCache)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeLargeMap(memory);
    auto cache = TileChunkDirtyCache::Create({.capacity = 32, .memoryResource = &memory});
    ASSERT_TRUE(cache.has_value());
    std::pmr::vector<TileChunkView> rebuilt{&memory};
    ASSERT_TRUE(cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt).has_value());

    const auto rev = map.chunkRevision(VisualLayerId, 1, 1);
    ASSERT_TRUE(rev.has_value());
    // set same value: TileMapInstance does not bump revision
    const auto id = map.tileIdAt(VisualLayerId, 2, 2);
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(map.setTile(VisualLayerId, 2, 2, *id).has_value());
    const auto revisionAfterNoOp = map.chunkRevision(VisualLayerId, 1, 1);
    ASSERT_TRUE(revisionAfterNoOp.has_value());
    EXPECT_EQ(*revisionAfterNoOp, *rev);

    rebuilt.clear();
    auto n = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 0U);
}

TEST(TileChunkDirtyCacheTests, StressThreeHundredFramesRebuildsStaySparse)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeLargeMap(memory);
    auto store = AssetStore::Create({.capacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(store.has_value()) << store.error().message;
    auto tileset = store->beginQueued(map.tilesetAssetId(), AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tileset.has_value()) << tileset.error().message;
    const TileChunkSpriteEmitParams emitParams{
        .tileset = *tileset,
        .bindingResolver = AssetFrameResourceResolver{.userData = &*tileset, .resolve = &TestTilesetBinding::resolve},
    };
    auto cache = TileChunkDirtyCache::Create({.capacity = 128, .memoryResource = &memory});
    ASSERT_TRUE(cache.has_value());

    std::pmr::vector<TileChunkView> rebuilt{&memory};
    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory};
    Core::u64 totalRebuilds = 0;
    Core::u64 totalVisible = 0;
    Core::u64 totalSpritesFromRebuilds = 0;
    Render::RenderFramePacket packet;

    for (Core::u32 frame = 0; frame < 300; ++frame)
    {
        ASSERT_TRUE(packet.beginFrame(static_cast<Core::u64>(frame) + 1U).has_value());
        // Mild camera pan keeps most chunks visible; still conservative AABB.
        const float pan = static_cast<float>(frame % 40) * 0.05f;
        const TileChunkCameraQuery camera{
            .centerX = 4.0f + pan,
            .centerY = 4.0f,
            .halfWidth = 4.5f,
            .halfHeight = 4.5f,
        };

        // Every 17 frames: single-cell edit (one chunk dirty).
        if (frame > 0 && frame % 17U == 0U)
        {
            const Core::u32 x = (frame / 17U) % 8U;
            const Core::u32 y = (frame * 3U) % 8U;
            const auto current = map.tileIdAt(VisualLayerId, x, y);
            ASSERT_TRUE(current.has_value());
            const Core::u16 next = (*current == 1) ? 2 : 1;
            ASSERT_TRUE(map.setTile(VisualLayerId, x, y, next).has_value());
        }

        rebuilt.clear();
        auto n = cache->syncVisible(map, VisualLayerId, camera, rebuilt);
        ASSERT_TRUE(n.has_value()) << n.error().message;
        totalRebuilds += *n;

        // Only rebuild chunks re-emit sprites (dirty gate product path).
        for (const TileChunkView& view : rebuilt)
        {
            sprites.clear();
            auto emitted = emitTileChunkSprites(map, view, emitParams, packet.resourceSink(), sprites);
            ASSERT_TRUE(emitted.has_value()) << emitted.error().message;
            totalSpritesFromRebuilds += *emitted;
        }

        std::pmr::vector<TileChunkView> visible{&memory};
        auto vis = extractVisibleTileChunks(map, VisualLayerId, camera, visible);
        ASSERT_TRUE(vis.has_value());
        totalVisible += *vis;
        ASSERT_TRUE(packet.completeSkipped().has_value());
    }

    const auto st = cache->stats();
    EXPECT_EQ(st.framesSynced, 300U);
    EXPECT_EQ(st.visibleChunkObservations, totalVisible);
    EXPECT_EQ(st.rebuilds, totalRebuilds);
    // Without dirty cache, every visible chunk would rebuild every frame.
    EXPECT_LT(totalRebuilds, totalVisible);
    // Edits ≈ 300/17 ≈ 17; first frame rebuilds all visible (~O(10)); allow headroom.
    EXPECT_LT(totalRebuilds, 80U);
    EXPECT_GT(st.cacheHits, totalRebuilds);
    EXPECT_GT(totalSpritesFromRebuilds, 0U);
}

TEST(TileChunkDirtyCacheTests, ClearForcesFullRebuild)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeLargeMap(memory);
    auto cache = TileChunkDirtyCache::Create({.capacity = 64, .memoryResource = &memory});
    ASSERT_TRUE(cache.has_value());
    std::pmr::vector<TileChunkView> rebuilt{&memory};
    auto first = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(first.has_value());
    cache->clear();
    rebuilt.clear();
    auto again = cache->syncVisible(map, VisualLayerId, fullMapCamera(), rebuilt);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *first);
}

TEST(TileChunkDirtyCacheTests, RejectsZeroCapacity)
{
    auto bad = TileChunkDirtyCache::Create({.capacity = 0});
    EXPECT_FALSE(bad.has_value());
}

} // namespace
} // namespace Tina::Asset
