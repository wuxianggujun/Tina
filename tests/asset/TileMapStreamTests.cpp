#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/TileMapStream.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>

#include "support/CatalogPackageTestSupport.hpp"
#include "support/Utf8Path.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory_resource>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace Tina::Asset {
namespace {

using TestSupport::TrackingMemoryResource;
using TestSupport::assetId;
using TestSupport::toUtf8;
using TestSupport::writeBytes;

inline constexpr AssetFormat::TileMapLayerId VisualLayerId = 11U;

struct TileMapStreamPackage final {
    std::filesystem::path root;
    Core::AssetId textureId{};
    Core::AssetId tilesetId{};
    Core::AssetId tileMapId{};
    Core::AssetId chunkAId{};
    Core::AssetId chunkBId{};
    Core::AssetId chunkCId{};
};

[[nodiscard]] AssetFormat::CookedManifestWriteEntry manifestEntry(
    std::span<const std::byte> bytes,
    std::span<const AssetFormat::CookedAssetWriteDependency> dependencies = {})
{
    auto view = AssetFormat::parseCookedAssetView(bytes);
    EXPECT_TRUE(view.has_value()) << view.error().message;
    return AssetFormat::CookedManifestWriteEntry{
        .assetId = view->header().assetId,
        .contentHash = view->header().contentHash,
        .assetKind = view->header().assetKind,
        .assetTypeVersion = view->header().assetTypeVersion,
        .cookedFileBytes = view->header().fileBytes,
        .dependencies = dependencies,
    };
}

void writeCookedObject(const std::filesystem::path& root, AssetFormat::AssetKind kind, Core::AssetId id,
                       const std::vector<std::byte>& bytes)
{
    auto relative = AssetFormat::makeCookedArtifactPath(kind, id);
    ASSERT_TRUE(relative.has_value()) << relative.error().message;
    writeBytes(root / Tina::TestSupport::pathFromUtf8Bytes(relative->view()), bytes);
}

[[nodiscard]] TileMapStreamPackage writeTileMapStreamPackage(std::string_view name)
{
    TileMapStreamPackage package{
        .root = std::filesystem::temp_directory_path() / name,
        .textureId = assetId(1U),
        .tilesetId = assetId(2U),
        .tileMapId = assetId(3U),
        .chunkAId = assetId(4U),
        .chunkBId = assetId(5U),
        .chunkCId = assetId(6U),
    };
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);

    const std::array tiles{
        AssetFormat::TilesetTileDesc{.localId = 1U, .materialFlags = AssetFormat::TilesetWire::MaterialSolid},
        AssetFormat::TilesetTileDesc{.localId = 2U},
    };
    const std::array<std::byte, 4> texturePixels{
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    auto texture = AssetFormat::writeCookedTexture2DAsset(
        package.textureId,
        AssetFormat::Texture2DPayloadDesc{.width = 1U, .height = 1U, .pixels = texturePixels});
    EXPECT_TRUE(texture.has_value()) << texture.error().message;
    auto tileset = AssetFormat::writeCookedTilesetAsset(
        package.tilesetId, AssetFormat::TilesetPayloadDesc{.tiles = tiles, .textureId = package.textureId});
    EXPECT_TRUE(tileset.has_value()) << tileset.error().message;

    const std::array<Core::u16, 16> chunkACells{1U, 0U, 0U, 0U,
                                                0U, 2U, 0U, 0U,
                                                0U, 0U, 0U, 0U,
                                                0U, 0U, 0U, 0U};
    const std::array<Core::u16, 16> chunkBCells{0U, 0U, 0U, 2U,
                                                0U, 0U, 0U, 0U,
                                                0U, 0U, 1U, 0U,
                                                0U, 0U, 0U, 0U};
    const std::array<Core::u16, 16> chunkCCells{0U, 1U, 0U, 0U,
                                                0U, 0U, 0U, 0U,
                                                0U, 0U, 0U, 0U,
                                                0U, 0U, 0U, 2U};
    auto chunkA = AssetFormat::writeCookedTileMapChunkAsset(
        package.chunkAId,
        AssetFormat::TileMapChunkPayloadDesc{.parentTileMapId = package.tileMapId,
                                             .layerId = VisualLayerId,
                                             .chunkX = 0U,
                                             .chunkY = 0U,
                                             .widthCells = 4U,
                                             .heightCells = 4U,
                                             .cells = chunkACells});
    auto chunkB = AssetFormat::writeCookedTileMapChunkAsset(
        package.chunkBId,
        AssetFormat::TileMapChunkPayloadDesc{.parentTileMapId = package.tileMapId,
                                             .layerId = VisualLayerId,
                                             .chunkX = 1U,
                                             .chunkY = 0U,
                                             .widthCells = 4U,
                                             .heightCells = 4U,
                                             .cells = chunkBCells});
    auto chunkC = AssetFormat::writeCookedTileMapChunkAsset(
        package.chunkCId,
        AssetFormat::TileMapChunkPayloadDesc{.parentTileMapId = package.tileMapId,
                                             .layerId = VisualLayerId,
                                             .chunkX = 2U,
                                             .chunkY = 0U,
                                             .widthCells = 4U,
                                             .heightCells = 4U,
                                             .cells = chunkCCells});
    EXPECT_TRUE(chunkA.has_value()) << chunkA.error().message;
    EXPECT_TRUE(chunkB.has_value()) << chunkB.error().message;
    EXPECT_TRUE(chunkC.has_value()) << chunkC.error().message;

    const std::array refs{
        AssetFormat::TileMapChunkRefDesc{.chunkX = 0U,
                                         .chunkY = 0U,
                                         .widthCells = 4U,
                                         .heightCells = 4U,
                                         .nonEmptyCount = 2U,
                                         .chunkAssetId = package.chunkAId},
        AssetFormat::TileMapChunkRefDesc{.chunkX = 1U,
                                         .chunkY = 0U,
                                         .widthCells = 4U,
                                         .heightCells = 4U,
                                         .nonEmptyCount = 2U,
                                         .chunkAssetId = package.chunkBId},
        AssetFormat::TileMapChunkRefDesc{.chunkX = 2U,
                                         .chunkY = 0U,
                                         .widthCells = 4U,
                                         .heightCells = 4U,
                                         .nonEmptyCount = 2U,
                                         .chunkAssetId = package.chunkCId},
    };
    const std::array layers{
        AssetFormat::TileMapLayerDesc{.stableLayerId = VisualLayerId,
                                      .kind = AssetFormat::TileMapLayerKind::Tile,
                                      .name = "visual",
                                      .chunkRefs = refs},
    };
    auto tileMap = AssetFormat::writeCookedTileMapAsset(
        package.tileMapId, AssetFormat::TileMapPayloadDesc{.widthCells = 12U,
                                                           .heightCells = 4U,
                                                           .chunkSizeCells = 4U,
                                                           .layers = layers,
                                                           .tilesetId = package.tilesetId});
    EXPECT_TRUE(tileMap.has_value()) << tileMap.error().message;

    const std::array tilesetDeps{
        AssetFormat::CookedAssetWriteDependency{.assetId = package.textureId,
                                                .expectedKind = AssetFormat::AssetKind::Texture2D},
    };
    const std::array tileMapDeps{
        AssetFormat::CookedAssetWriteDependency{.assetId = package.tilesetId,
                                                .expectedKind = AssetFormat::AssetKind::Tileset},
        AssetFormat::CookedAssetWriteDependency{.assetId = package.chunkAId,
                                                .expectedKind = AssetFormat::AssetKind::TileMapChunk,
                                                .flags = AssetFormat::DependencyFlags::Required |
                                                         AssetFormat::DependencyFlags::Deferred},
        AssetFormat::CookedAssetWriteDependency{.assetId = package.chunkBId,
                                                .expectedKind = AssetFormat::AssetKind::TileMapChunk,
                                                .flags = AssetFormat::DependencyFlags::Required |
                                                         AssetFormat::DependencyFlags::Deferred},
        AssetFormat::CookedAssetWriteDependency{.assetId = package.chunkCId,
                                                .expectedKind = AssetFormat::AssetKind::TileMapChunk,
                                                .flags = AssetFormat::DependencyFlags::Required |
                                                         AssetFormat::DependencyFlags::Deferred},
    };
    const std::array entries{
        manifestEntry(*texture),
        manifestEntry(*tileset, tilesetDeps),
        manifestEntry(*tileMap, tileMapDeps),
        manifestEntry(*chunkA),
        manifestEntry(*chunkB),
        manifestEntry(*chunkC),
    };
    auto manifest = AssetFormat::writeCookedManifestBytes(AssetFormat::CookedManifestWriteDesc{.entries = entries});
    EXPECT_TRUE(manifest.has_value()) << manifest.error().message;

    writeBytes(package.root / "manifest.tmnft", *manifest);
    writeCookedObject(package.root, AssetFormat::AssetKind::Texture2D, package.textureId, *texture);
    writeCookedObject(package.root, AssetFormat::AssetKind::Tileset, package.tilesetId, *tileset);
    writeCookedObject(package.root, AssetFormat::AssetKind::TileMap, package.tileMapId, *tileMap);
    writeCookedObject(package.root, AssetFormat::AssetKind::TileMapChunk, package.chunkAId, *chunkA);
    writeCookedObject(package.root, AssetFormat::AssetKind::TileMapChunk, package.chunkBId, *chunkB);
    writeCookedObject(package.root, AssetFormat::AssetKind::TileMapChunk, package.chunkCId, *chunkC);
    return package;
}

[[nodiscard]] Core::Status bindPackage(AssetSystem& system, const TileMapStreamPackage& package,
                                       std::pmr::memory_resource& resource)
{
    return system.openAndBindCatalog(
        toUtf8(package.root),
        CatalogPackageOpenConfig{
            .manifest = CatalogFileLoadConfig{.catalog = CatalogConfig{.maxEntries = 16,
                                                                       .maxDependencies = 16,
                                                                       .maxDependenciesPerAsset = 8,
                                                                       .memoryResource = &resource}},
            .validateOnOpen = true,
            .validation = CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
                .verifyTypedPayload = true,
            },
        });
}

void pumpUntilReady(AssetSystem& system, AssetHandle handle)
{
    for (Core::u32 frame = 0; frame < 16 && system.state(handle) != AssetLogicalState::ReadyCpu; ++frame)
    {
        auto stats = system.pump(8);
        ASSERT_TRUE(stats.has_value()) << stats.error().message;
    }
    ASSERT_EQ(system.state(handle), AssetLogicalState::ReadyCpu);
}

} // namespace

TEST(TileMapStreamTests, DemandLoadsOnlyVisibleChunksAndCommitsResidentCells)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_visible_chunks");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value()) << rootLease.error().message;
    ASSERT_TRUE(tilesetLease.has_value()) << tilesetLease.error().message;

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 2,
                                                            .requestBudgetPerUpdate = 1,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const std::array demand{TileMapChunkDemand{.layerId = VisualLayerId,
                                               .camera = TileChunkCameraQuery{.centerX = 1.5f,
                                                                              .centerY = 1.5f,
                                                                              .halfWidth = 1.5f,
                                                                              .halfHeight = 1.5f}}};
    ASSERT_TRUE(stream->updateDemand(demand).has_value());
    EXPECT_EQ(stream->stats().requestedSlots, 1U);
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));

    pumpUntilReady(*system, *system->find(package.chunkAId));
    auto committed = stream->commitReady();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_EQ(committed->residentSlots, 1U);
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));

    auto tile = stream->map().tileIdAt(VisualLayerId, 0U, 0U);
    ASSERT_TRUE(tile.has_value()) << tile.error().message;
    EXPECT_EQ(*tile, 1U);

    std::pmr::vector<TileChunkView> visible{&resource};
    auto count = extractVisibleTileChunks(stream->map(), VisualLayerId, demand[0].camera, visible);
    ASSERT_TRUE(count.has_value()) << count.error().message;
    ASSERT_EQ(*count, 1U);
    EXPECT_EQ(visible[0].residencyGeneration, stream->map().chunkState(VisualLayerId, 0U, 0U)->residencyGeneration);

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

TEST(TileMapStreamTests, AggregatesHighestPriorityAndOrdersOnlyNewRequests)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_priority_dispatch");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value()) << rootLease.error().message;
    ASSERT_TRUE(tilesetLease.has_value()) << tilesetLease.error().message;

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 3,
                                                            .requestBudgetPerUpdate = 1,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const auto demandChunk = [](float centerX, Core::u32 priority) {
        return TileMapChunkDemand{.layerId = VisualLayerId,
                                  .priority = priority,
                                  .camera = TileChunkCameraQuery{.centerX = centerX,
                                                                 .centerY = 1.0f,
                                                                 .halfWidth = 1.0f,
                                                                 .halfHeight = 1.0f}};
    };
    const std::array initial{
        demandChunk(1.0f, 1U),
        demandChunk(10.0f, 5U),
        demandChunk(1.0f, 10U),
    };
    ASSERT_TRUE(stream->updateDemand(initial).has_value());
    EXPECT_TRUE(system->find(package.chunkAId).has_value());
    EXPECT_FALSE(system->find(package.chunkBId).has_value());
    EXPECT_FALSE(system->find(package.chunkCId).has_value());

    const std::array reordered{
        demandChunk(10.0f, 7U),
        demandChunk(6.0f, 7U),
        demandChunk(1.0f, 0U),
    };
    ASSERT_TRUE(stream->updateDemand(reordered).has_value());
    EXPECT_TRUE(system->find(package.chunkAId).has_value());
    EXPECT_TRUE(system->find(package.chunkBId).has_value());
    EXPECT_FALSE(system->find(package.chunkCId).has_value());
    EXPECT_EQ(stream->stats().totalCancelled, 0U);

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

TEST(TileMapStreamTests, DemandShiftCancelsAndUnloadsOutsideRetainWindow)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_shift_unload");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value());
    ASSERT_TRUE(tilesetLease.has_value());

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 2,
                                                            .requestBudgetPerUpdate = 1,
                                                            .retainMarginChunks = 0,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const std::array left{TileMapChunkDemand{.layerId = VisualLayerId,
                                             .camera = TileChunkCameraQuery{.centerX = 1.0f,
                                                                            .centerY = 1.0f,
                                                                            .halfWidth = 1.0f,
                                                                            .halfHeight = 1.0f}}};
    ASSERT_TRUE(stream->updateDemand(left).has_value());
    EXPECT_EQ(stream->stats().requestedSlots, 1U);

    const std::array right{TileMapChunkDemand{.layerId = VisualLayerId,
                                              .camera = TileChunkCameraQuery{.centerX = 6.0f,
                                                                             .centerY = 1.0f,
                                                                             .halfWidth = 1.0f,
                                                                             .halfHeight = 1.0f}}};
    ASSERT_TRUE(stream->updateDemand(right).has_value());
    EXPECT_EQ(stream->stats().totalCancelled, 1U);
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));

    pumpUntilReady(*system, *system->find(package.chunkBId));
    auto committed = stream->commitReady();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));
    EXPECT_EQ(committed->residentSlots, 1U);

    ASSERT_TRUE(stream->updateDemand(left).has_value());
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));
    EXPECT_EQ(stream->stats().totalUnloaded, 1U);
    pumpUntilReady(*system, *system->find(package.chunkAId));
    committed = stream->commitReady();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

TEST(TileMapStreamTests, RetainOverflowEvictsOptionalResidentInsteadOfFailing)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_retain_overflow");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value());
    ASSERT_TRUE(tilesetLease.has_value());

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 1,
                                                            .requestBudgetPerUpdate = 1,
                                                            .retainMarginChunks = 1,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const std::array left{TileMapChunkDemand{.layerId = VisualLayerId,
                                             .camera = TileChunkCameraQuery{.centerX = 1.0f,
                                                                            .centerY = 1.0f,
                                                                            .halfWidth = 1.0f,
                                                                            .halfHeight = 1.0f}}};
    ASSERT_TRUE(stream->updateDemand(left).has_value());
    pumpUntilReady(*system, *system->find(package.chunkAId));
    ASSERT_TRUE(stream->commitReady().has_value());

    const std::array right{TileMapChunkDemand{.layerId = VisualLayerId,
                                              .camera = TileChunkCameraQuery{.centerX = 6.0f,
                                                                             .centerY = 1.0f,
                                                                             .halfWidth = 1.0f,
                                                                             .halfHeight = 1.0f}}};
    auto shifted = stream->updateDemand(right);
    ASSERT_TRUE(shifted.has_value()) << shifted.error().message;
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId,
                                                TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));
    EXPECT_EQ(stream->stats().totalUnloaded, 1U);
    EXPECT_EQ(stream->stats().requestedSlots, 1U);

    pumpUntilReady(*system, *system->find(package.chunkBId));
    auto committed = stream->commitReady();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId,
                                               TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));
    EXPECT_EQ(committed->residentSlots, 1U);

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

TEST(TileMapStreamTests, RetainOverflowKeepsMostRecentlyDemandedResident)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_lru_recency");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value());
    ASSERT_TRUE(tilesetLease.has_value());

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 2,
                                                            .requestBudgetPerUpdate = 1,
                                                            .retainMarginChunks = 2,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const auto demandChunk = [&](float centerX) {
        return std::array{TileMapChunkDemand{.layerId = VisualLayerId,
                                             .camera = TileChunkCameraQuery{.centerX = centerX,
                                                                            .centerY = 1.0f,
                                                                            .halfWidth = 1.0f,
                                                                            .halfHeight = 1.0f}}};
    };

    ASSERT_TRUE(stream->updateDemand(demandChunk(1.0f)).has_value());
    pumpUntilReady(*system, *system->find(package.chunkAId));
    ASSERT_TRUE(stream->commitReady().has_value());

    ASSERT_TRUE(stream->updateDemand(demandChunk(6.0f)).has_value());
    pumpUntilReady(*system, *system->find(package.chunkBId));
    ASSERT_TRUE(stream->commitReady().has_value());
    ASSERT_TRUE(stream->map().isChunkResident(VisualLayerId,
                                               TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));
    ASSERT_TRUE(stream->map().isChunkResident(VisualLayerId,
                                               TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));

    auto advanced = stream->updateDemand(demandChunk(10.0f));
    ASSERT_TRUE(advanced.has_value()) << advanced.error().message;
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId,
                                                TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId,
                                               TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));
    EXPECT_EQ(stream->stats().totalUnloaded, 1U);

    pumpUntilReady(*system, *system->find(package.chunkCId));
    auto committed = stream->commitReady();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId,
                                               TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId,
                                               TileMapChunkCoord{.chunkX = 2U, .chunkY = 0U}));
    EXPECT_EQ(committed->residentSlots, 2U);

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

TEST(TileMapStreamTests, CapacityFailureLeavesResidentSetUnchanged)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_capacity_transactional");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value());
    ASSERT_TRUE(tilesetLease.has_value());

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 1,
                                                            .requestBudgetPerUpdate = 1,
                                                            .retainMarginChunks = 0,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const std::array left{TileMapChunkDemand{.layerId = VisualLayerId,
                                             .camera = TileChunkCameraQuery{.centerX = 1.0f,
                                                                            .centerY = 1.0f,
                                                                            .halfWidth = 1.0f,
                                                                            .halfHeight = 1.0f}}};
    auto leftDemand = stream->updateDemand(left);
    ASSERT_TRUE(leftDemand.has_value()) << leftDemand.error().message;
    pumpUntilReady(*system, *system->find(package.chunkAId));
    ASSERT_TRUE(stream->commitReady().has_value());

    const std::array both{TileMapChunkDemand{.layerId = VisualLayerId,
                                             .camera = TileChunkCameraQuery{.centerX = 4.0f,
                                                                            .centerY = 1.0f,
                                                                            .halfWidth = 4.0f,
                                                                            .halfHeight = 1.0f}}};
    auto rejected = stream->updateDemand(both);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));
    EXPECT_EQ(stream->stats().residentSlots, 1U);

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

// A load failure used to be terminal: the slot stayed Failed, eviction kept it
// because it was still desired, the request loop skipped it because a slot existed,
// and commitReady only looks at Requested slots. One transient IO error made a chunk
// blank for as long as the camera kept it in view, and only stats() showed it.
TEST(TileMapStreamTests, FailedChunkIsRetriedOnTheNextDemandUpdate)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_failed_retry");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value());
    ASSERT_TRUE(tilesetLease.has_value());

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 2,
                                                            .requestBudgetPerUpdate = 1,
                                                            .retainMarginChunks = 0,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    const std::array left{TileMapChunkDemand{.layerId = VisualLayerId,
                                             .camera = TileChunkCameraQuery{.centerX = 1.0f,
                                                                            .centerY = 1.0f,
                                                                            .halfWidth = 1.0f,
                                                                            .halfHeight = 1.0f}}};

    // Simulate a transient read failure by removing the artifact before the pump.
    auto chunkPath = AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::TileMapChunk,
                                                         package.chunkAId);
    ASSERT_TRUE(chunkPath.has_value()) << chunkPath.error().message;
    const std::filesystem::path chunkFile =
        package.root / Tina::TestSupport::pathFromUtf8Bytes(chunkPath->view());
    std::vector<std::byte> saved;
    {
        std::ifstream input{chunkFile, std::ios::binary};
        ASSERT_TRUE(input.good());
        const std::string raw{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        saved.reserve(raw.size());
        for (const char byte : raw)
        {
            saved.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
        }
    }
    std::error_code removeError;
    std::filesystem::remove(chunkFile, removeError);
    ASSERT_FALSE(removeError);

    ASSERT_TRUE(stream->updateDemand(left).has_value());
    ASSERT_TRUE(system->pump(8).has_value());
    auto failedCommit = stream->commitReady();
    ASSERT_TRUE(failedCommit.has_value()) << failedCommit.error().message;
    EXPECT_EQ(failedCommit->failedSlots, 1U);
    EXPECT_EQ(failedCommit->totalFailed, 1U);
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));

    // The transient cause is gone; the same demand must re-request rather than leave
    // the slot stranded.
    writeBytes(chunkFile, saved);
    ASSERT_TRUE(stream->updateDemand(left).has_value());
    EXPECT_EQ(stream->stats().failedSlots, 0U);
    EXPECT_EQ(stream->stats().requestedSlots, 1U);
    EXPECT_EQ(stream->stats().totalRequests, 2U);

    pumpUntilReady(*system, *system->find(package.chunkAId));
    auto recovered = stream->commitReady();
    ASSERT_TRUE(recovered.has_value()) << recovered.error().message;
    EXPECT_EQ(recovered->residentSlots, 1U);
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 0U, .chunkY = 0U}));

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

// The retain margin is a world-space window, so it has to widen the overlap test too.
// Testing the unwidened camera first made the retain set collapse to empty the moment
// the camera cleared the map rect, which unloaded every resident chunk in one frame
// and re-requested them all on the way back.
TEST(TileMapStreamTests, RetainMarginSurvivesSteppingJustPastTheMapEdge)
{
    TrackingMemoryResource resource;
    const auto package = writeTileMapStreamPackage("tina_tilemap_stream_edge_retain");

    auto system = AssetSystem::Create(AssetSystemConfig{.storeCapacity = 16,
                                                        .memoryResource = &resource,
                                                        .batch = CookedAssetBatchLoadConfig{
                                                            .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                                                            .memoryResource = &resource},
                                                        .queueCapacity = 16});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto bound = bindPackage(*system, package, resource);
    ASSERT_TRUE(bound.has_value()) << bound.error().message;

    auto rootHandle = system->loadOne(package.tileMapId);
    ASSERT_TRUE(rootHandle.has_value()) << rootHandle.error().message;
    auto tilesetHandle = system->findFirstLoadedOfKind(AssetFormat::AssetKind::Tileset);
    ASSERT_TRUE(tilesetHandle.has_value());
    auto rootLease = system->acquire(*rootHandle);
    auto tilesetLease = system->acquire(*tilesetHandle);
    ASSERT_TRUE(rootLease.has_value());
    ASSERT_TRUE(tilesetLease.has_value());

    auto stream = TileMapStream::Create(*system, std::move(*rootLease), std::move(*tilesetLease),
                                        TileMapStreamConfig{.residentCapacity = 4,
                                                            .requestBudgetPerUpdate = 4,
                                                            .retainMarginChunks = 1,
                                                            .memoryResource = &resource});
    ASSERT_TRUE(stream.has_value()) << stream.error().message;

    // Inside the map: chunk (1,0) becomes resident.
    const std::array inside{TileMapChunkDemand{.layerId = VisualLayerId,
                                               .camera = TileChunkCameraQuery{.centerX = 6.0f,
                                                                              .centerY = 1.0f,
                                                                              .halfWidth = 1.0f,
                                                                              .halfHeight = 1.0f}}};
    ASSERT_TRUE(stream->updateDemand(inside).has_value());
    pumpUntilReady(*system, *system->find(package.chunkBId));
    ASSERT_TRUE(stream->commitReady().has_value());
    ASSERT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));

    // One step past the right edge. The camera no longer overlaps the map, but the
    // retain window still does, so the resident chunk must stay.
    const float mapWidthMeters =
        static_cast<float>(stream->map().widthCells()) * stream->map().cellSizeMeters();
    const std::array justPast{
        TileMapChunkDemand{.layerId = VisualLayerId,
                           .camera = TileChunkCameraQuery{.centerX = mapWidthMeters + 1.5f,
                                                          .centerY = 1.0f,
                                                          .halfWidth = 1.0f,
                                                          .halfHeight = 1.0f}}};
    ASSERT_TRUE(stream->updateDemand(justPast).has_value());
    EXPECT_TRUE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}))
        << "the retain margin must survive the camera clearing the map rect";
    EXPECT_EQ(stream->stats().totalUnloaded, 0U);

    // Far away, beyond the retain window: now it is correct to evict.
    const std::array farAway{
        TileMapChunkDemand{.layerId = VisualLayerId,
                           .camera = TileChunkCameraQuery{.centerX = mapWidthMeters + 100.0f,
                                                          .centerY = 1.0f,
                                                          .halfWidth = 1.0f,
                                                          .halfHeight = 1.0f}}};
    ASSERT_TRUE(stream->updateDemand(farAway).has_value());
    EXPECT_FALSE(stream->map().isChunkResident(VisualLayerId, TileMapChunkCoord{.chunkX = 1U, .chunkY = 0U}));
    EXPECT_EQ(stream->stats().totalUnloaded, 1U);

    EXPECT_TRUE(stream->shutdown().has_value());
    std::error_code ec;
    std::filesystem::remove_all(package.root, ec);
}

} // namespace Tina::Asset
