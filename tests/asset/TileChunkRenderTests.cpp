#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include "support/TileMapInstanceTestSupport.hpp"
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <optional>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr AssetFormat::TileMapLayerId VisualLayerId = 10;
inline constexpr AssetFormat::TileMapLayerId HiddenLayerId = 20;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] Core::AssetId tilesetAssetId()
{
    return *Core::AssetId::fromBytes(idBytes(5U));
}

[[nodiscard]] TileMapInstance makeMap(std::pmr::memory_resource& memory)
{
    const auto tilesetId = tilesetAssetId();
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const std::array tiles{
        AssetFormat::TilesetTileDesc{.localId = 1,
                                     .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
                                     .u0 = 0.0f,
                                     .v0 = 0.0f,
                                     .u1 = 0.5f,
                                     .v1 = 0.5f},
        AssetFormat::TilesetTileDesc{.localId = 2,
                                     .materialFlags = 0,
                                     .u0 = 0.5f,
                                     .v0 = 0.0f,
                                     .u1 = 1.0f,
                                     .v1 = 0.5f},
    };
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    // 4x2 all filled across two 2x2 chunks.
    const std::array<Core::u16, 8> cells{1, 2, 2, 1, 2, 1, 1, 2};
    const std::array layers{
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = VisualLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .cells = cells,
        },
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = HiddenLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
    };
    auto instance = TestSupport::makeResidentTileMapInstance(4, 2, 2, mapId, tilesetId, *tileset, layers, memory);
    EXPECT_TRUE(instance.has_value()) << instance.error().message;
    return std::move(*instance);
}

struct TestTilesetBinding final {
    [[nodiscard]] AssetFrameResourceResolver resolver() noexcept
    {
        return AssetFrameResourceResolver{.userData = this, .resolve = &resolve};
    }

    [[nodiscard]] static Core::Result<Render::FrameResourceRef>
    resolve(void* userData, AssetHandle tileset, Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<TestTilesetBinding*>(userData);
        ++self.resolveCalls;
        self.lastResolved = tileset;
        if (self.store == nullptr || tileset != self.boundTileset ||
            self.store->assetKind(tileset) != AssetFormat::AssetKind::Tileset ||
            self.store->state(tileset) == AssetLogicalState::Unloaded || self.bindingKey == 0)
        {
            return Render::FrameResourceRef{};
        }
        ++self.activePins;
        Render::FramePin pin{Render::FramePinKind::Custom, self.bindingKey, &self, &releasePin};
        auto resource = sink.intern(
            Render::FrameResourceDescriptor{
                .kind = Render::FrameResourceKind::Sprite2DTexture,
                .deviceBindingKey = self.bindingKey,
            },
            std::move(pin));
        if (!resource)
        {
            return Core::failure(std::move(resource.error()));
        }
        self.lastResource = *resource;
        return *resource;
    }

    static void releasePin(void* userData) noexcept
    {
        auto& self = *static_cast<TestTilesetBinding*>(userData);
        --self.activePins;
        ++self.releasedPins;
    }

    AssetStore* store = nullptr;
    AssetHandle boundTileset{};
    Core::u32 bindingKey = 41U;
    Core::usize resolveCalls = 0;
    AssetHandle lastResolved{};
    Render::FrameResourceRef lastResource{};
    Core::usize activePins = 0;
    Core::usize releasedPins = 0;
};

class TileChunkRenderTests : public testing::Test {
  protected:
    void SetUp() override
    {
        auto store = AssetStore::Create({.capacity = 2, .memoryResource = &memory_});
        ASSERT_TRUE(store.has_value()) << store.error().message;
        store_.emplace(std::move(*store));
        auto tileset = store_->beginQueued(tilesetAssetId(), AssetFormat::AssetKind::Tileset);
        ASSERT_TRUE(tileset.has_value()) << tileset.error().message;
        tileset_ = *tileset;
        binding_ = TestTilesetBinding{.store = &*store_, .boundTileset = tileset_};
        ASSERT_TRUE(frame_.beginFrame(1).has_value());
    }

    [[nodiscard]] TileChunkSpriteEmitParams emitParams() noexcept
    {
        return TileChunkSpriteEmitParams{
            .tileset = tileset_,
            .bindingResolver = binding_.resolver(),
        };
    }

    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<AssetStore> store_{};
    AssetHandle tileset_{};
    TestTilesetBinding binding_{};
    Render::RenderFramePacket frame_{};
};

TEST_F(TileChunkRenderTests, EmitSpritesWithUvAndCenter)
{
    auto map = makeMap(memory_);
    std::pmr::vector<TileChunkView> chunks{&memory_};
    ASSERT_TRUE(extractVisibleTileChunks(
                    map, VisualLayerId, TileChunkCameraQuery{.centerX = 2.0f, .centerY = 1.0f,
                                                            .halfWidth = 3.0f, .halfHeight = 2.0f}, chunks)
                    .has_value());
    ASSERT_EQ(chunks.size(), 2U);

    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory_};
    auto n = emitTileChunkSprites(map, chunks[0], emitParams(), frame_.resourceSink(), sprites);
    ASSERT_TRUE(n.has_value()) << n.error().message;
    EXPECT_EQ(*n, 4U);
    EXPECT_EQ(sprites.size(), 4U);
    EXPECT_EQ(binding_.resolveCalls, 1U);
    EXPECT_EQ(binding_.lastResolved, tileset_);

    // Cell (0,0) tile 1 UV 0..0.5, center (0.5, 0.5)
    EXPECT_FLOAT_EQ(sprites[0].centerX, 0.5f);
    EXPECT_FLOAT_EQ(sprites[0].centerY, 0.5f);
    EXPECT_FLOAT_EQ(sprites[0].widthMeters, 1.0f);
    EXPECT_FLOAT_EQ(sprites[0].u0, 0.0f);
    EXPECT_FLOAT_EQ(sprites[0].u1, 0.5f);
    EXPECT_EQ(sprites[0].texture, binding_.lastResource);
    EXPECT_EQ(frame_.resourceCount(), 1U);

    // Commit into RenderScene
    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 16}, memory_);
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame({}).has_value());
    auto writer = builder->writer();
    ASSERT_TRUE(writer.setCamera2D(Render::RenderCamera2DInput{
                                       .stableCameraKey = 1,
                                       .centerX = 1.0f,
                                       .centerY = 1.0f,
                                       .worldWidth = 4.0f,
                                       .worldHeight = 4.0f,
                                       .actualPixelsPerMeter = 16.0f,
                                   })
                    .has_value());
    for (const auto& sprite : sprites)
    {
        ASSERT_TRUE(writer.addSprite2D(sprite).has_value());
    }
    auto view = builder->commit();
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->sprites2D().size(), 4U);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u1, 0.5f);
}

TEST_F(TileChunkRenderTests, EmitVisibleResolvesTilesetOnceAcrossChunks)
{
    auto map = makeMap(memory_);
    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory_};
    auto n = emitVisibleTileMapSprites(
        map, VisualLayerId,
        TileChunkCameraQuery{.centerX = 2.0f, .centerY = 1.0f, .halfWidth = 3.0f, .halfHeight = 2.0f},
        emitParams(), frame_.resourceSink(), sprites);
    ASSERT_TRUE(n.has_value()) << n.error().message;
    EXPECT_EQ(*n, 8U);
    EXPECT_EQ(sprites.size(), 8U);
    EXPECT_EQ(binding_.resolveCalls, 1U);
    EXPECT_EQ(frame_.resourceCount(), 1U);
    for (const auto& sprite : sprites)
    {
        EXPECT_EQ(sprite.texture, binding_.lastResource);
    }
}

TEST_F(TileChunkRenderTests, EmitVisibleSkipsOffCameraWithoutResolving)
{
    auto map = makeMap(memory_);
    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory_};
    // Camera far away → 0 sprites
    auto n = emitVisibleTileMapSprites(
        map, VisualLayerId,
        TileChunkCameraQuery{.centerX = 100.0f, .centerY = 100.0f, .halfWidth = 0.5f, .halfHeight = 0.5f},
        emitParams(), frame_.resourceSink(), sprites);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 0U);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 0U);

    sprites.push_back({.stableEntityKey = 99U});
    const auto emptyChunk = emitTileChunkSprites(
        map, TileChunkView{.layerId = VisualLayerId, .empty = true}, emitParams(), frame_.resourceSink(), sprites);
    ASSERT_TRUE(emptyChunk.has_value()) << emptyChunk.error().message;
    EXPECT_EQ(*emptyChunk, 0U);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 0U);
}

TEST_F(TileChunkRenderTests, HiddenLayerIsNotEmittedOrResolved)
{
    auto map = makeMap(memory_);
    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory_};
    auto n = emitVisibleTileMapSprites(
        map, HiddenLayerId,
        TileChunkCameraQuery{.centerX = 2.0f, .centerY = 1.0f, .halfWidth = 3.0f, .halfHeight = 2.0f},
        emitParams(), frame_.resourceSink(), sprites);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 0U);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 0U);
}

TEST_F(TileChunkRenderTests, MissingAndZeroBindingsFailClosedWithoutPublishing)
{
    auto map = makeMap(memory_);
    std::pmr::vector<TileChunkView> chunks{&memory_};
    ASSERT_TRUE(extractVisibleTileChunks(
                    map, VisualLayerId, TileChunkCameraQuery{.centerX = 2.0f, .centerY = 1.0f,
                                                            .halfWidth = 3.0f, .halfHeight = 2.0f}, chunks)
                    .has_value());
    ASSERT_FALSE(chunks.empty());
    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory_};
    sprites.push_back({.stableEntityKey = 99U});

    auto emptyHandleParams = emitParams();
    emptyHandleParams.tileset = {};
    const auto emptyHandle =
        emitTileChunkSprites(map, chunks.front(), emptyHandleParams, frame_.resourceSink(), sprites);
    ASSERT_FALSE(emptyHandle.has_value());
    EXPECT_EQ(emptyHandle.error().code, AssetErrorCode::InvalidHandle);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 0U);

    auto missingResolverParams = emitParams();
    missingResolverParams.bindingResolver = {};
    const auto missingResolver =
        emitTileChunkSprites(map, chunks.front(), missingResolverParams, frame_.resourceSink(), sprites);
    ASSERT_FALSE(missingResolver.has_value());
    EXPECT_EQ(missingResolver.error().code, AssetErrorCode::SpriteBindingNotFound);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 0U);

    binding_.bindingKey = 0U;
    const auto zeroBinding = emitVisibleTileMapSprites(
        map, VisualLayerId,
        TileChunkCameraQuery{.centerX = 2.0f, .centerY = 1.0f, .halfWidth = 3.0f, .halfHeight = 2.0f},
        emitParams(), frame_.resourceSink(), sprites);
    ASSERT_FALSE(zeroBinding.has_value());
    EXPECT_EQ(zeroBinding.error().code, AssetErrorCode::SpriteBindingNotFound);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 1U);
}

TEST_F(TileChunkRenderTests, SinkFailureReleasesResolverPinWithoutPublishing)
{
    auto map = makeMap(memory_);
    std::pmr::vector<TileChunkView> chunks{&memory_};
    ASSERT_TRUE(extractVisibleTileChunks(
                    map, VisualLayerId, TileChunkCameraQuery{.centerX = 2.0f, .centerY = 1.0f,
                                                            .halfWidth = 3.0f, .halfHeight = 2.0f}, chunks)
                    .has_value());
    ASSERT_FALSE(chunks.empty());
    ASSERT_TRUE(frame_.abandon().has_value());

    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory_};
    sprites.push_back({.stableEntityKey = 99U});
    const auto emitted = emitTileChunkSprites(map, chunks.front(), emitParams(), frame_.resourceSink(), sprites);

    ASSERT_FALSE(emitted.has_value());
    EXPECT_EQ(emitted.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_TRUE(sprites.empty());
    EXPECT_EQ(binding_.resolveCalls, 1U);
    EXPECT_EQ(binding_.activePins, 0U);
    EXPECT_EQ(binding_.releasedPins, 1U);
}

} // namespace
} // namespace Tina::Asset
