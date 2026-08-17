#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/EnvironmentMapPayload.hpp>
#include <tina/asset_format/Fx2DPayload.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/NavigationGrid2DPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u8;
using Core::usize;

template <typename T>
[[nodiscard]] T requireValue(Core::Result<T> result)
{
    if (!result)
    {
        ADD_FAILURE() << result.error().message;
        return {};
    }
    return std::move(*result);
}

template <typename T>
void expectAssetError(const Core::Result<T>& result)
{
    ASSERT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().message.empty());
}

void putU16(std::vector<std::byte>& bytes, usize offset, u16 value)
{
    ASSERT_LE(offset + sizeof(value), bytes.size());
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void putU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    ASSERT_LE(offset + sizeof(value), bytes.size());
    for (usize index = 0; index < sizeof(value); ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void putF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    ASSERT_LE(offset + sizeof(value), bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

[[nodiscard]] Core::AssetId assetId(u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] std::vector<std::byte> makeTexturePayload()
{
    const std::array pixels{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xFF},
    };
    return requireValue(writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .width = 1,
        .height = 1,
        .pixels = pixels,
    }));
}

[[nodiscard]] std::vector<std::byte> makeSpritePayload()
{
    return requireValue(writeSpritePayloadBytes(SpritePayloadDesc{}));
}

[[nodiscard]] std::vector<std::byte> makeTilesetPayload()
{
    const std::array tiles{
        TilesetTileDesc{.localId = 1, .materialFlags = TilesetWire::MaterialSolid},
    };
    return requireValue(writeTilesetPayloadBytes(TilesetPayloadDesc{.tiles = tiles}));
}

[[nodiscard]] std::vector<std::byte> makeObjectTileMapPayload()
{
    const std::array layerProperties{
        TileMapPropertyDesc{.key = "key", .value = "value"},
    };
    const std::array objectProperties{
        TileMapPropertyDesc{.key = "kind", .value = "spawn"},
    };
    const std::array objects{
        TileMapObjectDesc{
            .stableObjectId = 7,
            .kind = TileMapObjectKind::Point,
            .name = "object",
            .properties = objectProperties,
        },
    };
    const std::array layers{
        TileMapLayerDesc{
            .stableLayerId = 3,
            .kind = TileMapLayerKind::Object,
            .name = "layer",
            .properties = layerProperties,
            .objects = objects,
        },
    };
    return requireValue(writeTileMapPayloadBytes(TileMapPayloadDesc{
        .widthCells = 1,
        .heightCells = 1,
        .chunkSizeCells = 1,
        .layers = layers,
    }));
}

[[nodiscard]] std::vector<std::byte> makeTileMapChunkPayload()
{
    const std::array<u16, 2> cells{1U, TileMapChunkWire::EmptyTileId};
    return requireValue(writeTileMapChunkPayloadBytes(TileMapChunkPayloadDesc{
        .parentTileMapId = assetId(1),
        .layerId = 3,
        .widthCells = 2,
        .heightCells = 1,
        .cells = cells,
    }));
}

[[nodiscard]] SpriteAnimationClipPayloadDesc makeAnimationDesc(
    std::span<const SpriteAnimationFrameDesc> frames) noexcept
{
    return SpriteAnimationClipPayloadDesc{
        .playbackMode = SpriteAnimationPlaybackMode::Loop,
        .frames = frames,
    };
}

[[nodiscard]] std::vector<std::byte> makeSpriteAnimationPayload()
{
    const std::array events{
        SpriteAnimationEventDesc{.eventTag = 1U, .normalizedOffset = 0.5F},
    };
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = assetId(2), .durationSeconds = 0.1F, .events = events},
    };
    return requireValue(writeSpriteAnimationClipPayloadBytes(makeAnimationDesc(frames)));
}

[[nodiscard]] std::vector<std::byte> makeStaticMeshPayload()
{
    const std::array submeshes{
        StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3},
    };
    const std::array<float, 36> vertices{
        0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
    };
    const std::array<u16, 3> indices{0U, 1U, 2U};
    return requireValue(writeStaticMeshPayloadBytes(StaticMeshPayloadDesc{
        .boundsRadius = 1.0F,
        .submeshes = submeshes,
        .vertices = vertices,
        .indices = indices,
    }));
}

[[nodiscard]] std::vector<std::byte> makeSkinnedMeshPayload()
{
    const std::array joints{
        SkinnedMeshJointDesc{.parentJoint = SkinnedMeshWire::JointIndexNone},
    };
    const std::array<float, 16> inverseBind{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    const std::array submeshes{
        StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3},
    };
    const std::array<float, 3 * SkinnedMeshWire::FloatsPerVertex> vertices{
        0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
    };
    const std::array<u16, 12> jointIndices{};
    const std::array<u16, 12> jointWeights{
        SkinnedMeshWire::WeightScale, 0U, 0U, 0U,
        SkinnedMeshWire::WeightScale, 0U, 0U, 0U,
        SkinnedMeshWire::WeightScale, 0U, 0U, 0U,
    };
    const std::array<u16, 3> indices{0U, 1U, 2U};
    return requireValue(writeSkinnedMeshPayloadBytes(SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F,
        .joints = joints,
        .inverseBindMatrices = inverseBind,
        .submeshes = submeshes,
        .vertices = vertices,
        .jointIndices = jointIndices,
        .jointWeights = jointWeights,
        .indices = indices,
    }));
}

[[nodiscard]] std::vector<std::byte> makeAnimationClip3DPayload()
{
    const std::array<float, 2> times{0.0F, 1.0F};
    const std::array<float, 6> values{0.0F, 0.0F, 0.0F, 1.0F, 2.0F, 3.0F};
    const std::array tracks{
        AnimationTrackDesc{
            .jointIndex = 0,
            .channel = AnimationChannel::Translation,
            .interpolation = AnimationInterpolation::Linear,
            .times = times,
            .values = values,
        },
    };
    return requireValue(writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .playbackMode = AnimationClip3DPlaybackMode::Loop,
        .jointCount = 1,
        .durationSeconds = 1.0F,
        .tracks = tracks,
    }));
}

[[nodiscard]] std::vector<std::byte> makeMaterialPayload()
{
    return requireValue(writeMaterialPayloadBytes(MaterialPayloadDesc{}));
}

[[nodiscard]] std::vector<std::byte> makePrefabPayload()
{
    const std::array nodes{
        PrefabNodeDesc{.stableNodeId = 1},
        PrefabNodeDesc{.stableNodeId = 2, .parentIndex = 0, .positionX = 1.0F},
    };
    return requireValue(writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = nodes}));
}

[[nodiscard]] std::vector<std::byte> makeEnvironmentMapPayload()
{
    const std::vector<std::byte> diffuse(EnvironmentMapWire::FaceCount *
                                         EnvironmentMapWire::Rgba16FloatBytesPerPixel);
    const std::vector<std::byte> specular(EnvironmentMapWire::FaceCount *
                                          EnvironmentMapWire::Rgba16FloatBytesPerPixel);
    const std::vector<std::byte> brdf(EnvironmentMapWire::Rg16FloatBytesPerPixel);
    return requireValue(writeEnvironmentMapPayloadBytes(EnvironmentMapPayloadDesc{
        .diffuseFaceSize = 1,
        .specularFaceSize = 1,
        .specularMipCount = 1,
        .brdfWidth = 1,
        .brdfHeight = 1,
        .diffusePixels = diffuse,
        .specularPixels = specular,
        .brdfPixels = brdf,
    }));
}

[[nodiscard]] std::vector<std::byte> makeAudioClipPayload()
{
    const std::array<float, 2> samples{0.0F, 0.5F};
    return requireValue(writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .channels = 1,
        .sampleRate = 48'000,
        .frameCount = 2,
        .interleavedPcm = samples,
    }));
}

[[nodiscard]] std::vector<std::byte> makeNavigationGrid2DPayload()
{
    const std::array<u8, 1> flags{0U};
    const std::array<u8, 1> costs{NavigationGrid2DWire::MinimumTraversalCost};
    return requireValue(writeNavigationGrid2DPayloadBytes(NavigationGrid2DPayloadDesc{
        .widthCells = 1,
        .heightCells = 1,
        .cellFlags = flags,
        .traversalCosts = costs,
    }));
}

[[nodiscard]] std::vector<std::byte> makeFx2DPayload()
{
    return requireValue(writeFx2DPayloadBytes(Fx2DPayloadDesc{
        .spriteAssetId = assetId(20),
    }));
}

[[nodiscard]] std::vector<std::byte> makeWorld2DSnapshot()
{
    const std::array entities{
        World2DEntityDesc{.stableEntityId = 1},
        World2DEntityDesc{.stableEntityId = 2, .parentStableEntityId = 1},
    };
    const std::array gameplay{std::byte{0x42}};
    return requireValue(writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .entities = entities,
        .gameplaySchema = 1,
        .gameplayVersion = 1,
        .gameplayBytes = gameplay,
    }));
}

TEST(TypedPayloadMalformedCorpusTests, Texture2DRejectsTruncationBombAndLengthMismatch)
{
    const auto canonical = makeTexturePayload();
    ASSERT_TRUE(parseTexture2DPayload(canonical));

    auto truncated = canonical;
    truncated.resize(Texture2DWire::HeaderBytes - 1U);
    expectAssetError(parseTexture2DPayload(truncated));

    auto bomb = canonical;
    putU16(bomb, 2U, static_cast<u16>(Texture2DWire::MaxDimension + 1U));
    expectAssetError(parseTexture2DPayload(bomb));

    auto byteCountMismatch = canonical;
    putU32(byteCountMismatch, 8U, 8U);
    expectAssetError(parseTexture2DPayload(byteCountMismatch));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseTexture2DPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, SpriteRejectsNonFiniteValuesAndFixedLengthDamage)
{
    const auto canonical = makeSpritePayload();
    ASSERT_TRUE(parseSpritePayload(canonical));

    for (const usize offset : {4U, 20U, 28U})
    {
        auto malformed = canonical;
        putF32(malformed, offset, offset == 4U ? std::numeric_limits<float>::quiet_NaN()
                                               : std::numeric_limits<float>::infinity());
        SCOPED_TRACE(offset);
        expectAssetError(parseSpritePayload(malformed));
    }

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseSpritePayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseSpritePayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, TilesetRejectsBombFlagsReservedUvAndLengthDamage)
{
    const auto canonical = makeTilesetPayload();
    ASSERT_TRUE(parseTilesetPayload(canonical));
    constexpr usize Entry = TilesetWire::HeaderBytes;

    auto bomb = canonical;
    putU32(bomb, 8U, TilesetWire::MaxTiles + 1U);
    expectAssetError(parseTilesetPayload(bomb));

    auto unknownFlags = canonical;
    putU16(unknownFlags, Entry + 2U, 0x8000U);
    expectAssetError(parseTilesetPayload(unknownFlags));

    auto nonFiniteUv = canonical;
    putF32(nonFiniteUv, Entry + 4U, std::numeric_limits<float>::infinity());
    expectAssetError(parseTilesetPayload(nonFiniteUv));

    auto reserved = canonical;
    putU32(reserved, Entry + 20U, 1U);
    expectAssetError(parseTilesetPayload(reserved));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseTilesetPayload(truncated));
}

TEST(TypedPayloadMalformedCorpusTests, TileMapRootRejectsUtf8CountsNonFiniteAndLengthDamage)
{
    const auto canonical = makeObjectTileMapPayload();
    ASSERT_TRUE(parseTileMapPayload(canonical));

    constexpr usize Layer = TileMapWire::HeaderBytes;
    constexpr usize LayerName = Layer + TileMapWire::LayerHeaderBytes;
    constexpr usize LayerPropertyKey = LayerName + 5U + 4U;
    constexpr usize Object = LayerPropertyKey + 3U + 5U;
    constexpr usize ObjectName = Object + TileMapWire::ObjectHeaderBytes;
    ASSERT_GT(canonical.size(), ObjectName);

    for (const usize offset : {LayerName, LayerPropertyKey, ObjectName})
    {
        auto invalidUtf8 = canonical;
        invalidUtf8[offset] = std::byte{0xFF};
        SCOPED_TRACE(offset);
        expectAssetError(parseTileMapPayload(invalidUtf8));
    }

    auto tooManyLayers = canonical;
    putU16(tooManyLayers, 18U, static_cast<u16>(TileMapWire::MaxLayers + 1U));
    expectAssetError(parseTileMapPayload(tooManyLayers));

    auto tooManyProperties = canonical;
    putU16(tooManyProperties, Layer + 8U, static_cast<u16>(TileMapWire::MaxPropertiesPerOwner + 1U));
    expectAssetError(parseTileMapPayload(tooManyProperties));

    auto tooManyObjects = canonical;
    putU32(tooManyObjects, Layer + 12U, TileMapWire::MaxObjectsPerLayer + 1U);
    expectAssetError(parseTileMapPayload(tooManyObjects));

    auto nonFiniteCellSize = canonical;
    putF32(nonFiniteCellSize, 12U, std::numeric_limits<float>::quiet_NaN());
    expectAssetError(parseTileMapPayload(nonFiniteCellSize));

    auto nonFiniteObjectGeometry = canonical;
    putF32(nonFiniteObjectGeometry, Object + 12U, std::numeric_limits<float>::infinity());
    expectAssetError(parseTileMapPayload(nonFiniteObjectGeometry));

    auto truncated = canonical;
    truncated.resize(TileMapWire::HeaderBytes - 1U);
    expectAssetError(parseTileMapPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseTileMapPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, TileMapRootCookedWriterRejectsSelfDependency)
{
    const auto mapId = assetId(10);
    const std::array chunkRefs{
        TileMapChunkRefDesc{
            .widthCells = 1,
            .heightCells = 1,
            .nonEmptyCount = 1,
            .chunkAssetId = mapId,
        },
    };
    const std::array layers{
        TileMapLayerDesc{.stableLayerId = 1, .kind = TileMapLayerKind::Tile, .chunkRefs = chunkRefs},
    };
    expectAssetError(writeCookedTileMapAsset(mapId, TileMapPayloadDesc{
                                                        .widthCells = 1,
                                                        .heightCells = 1,
                                                        .chunkSizeCells = 1,
                                                        .layers = layers,
                                                        .tilesetId = assetId(11),
                                                    }));
}

TEST(TypedPayloadMalformedCorpusTests, TileMapChunkRejectsBombCountsCoordinatesAndLengthDamage)
{
    const auto canonical = makeTileMapChunkPayload();
    ASSERT_TRUE(parseTileMapChunkPayload(canonical));

    auto oversized = canonical;
    putU16(oversized, 32U, static_cast<u16>(TileMapChunkWire::MaxDimension + 1U));
    expectAssetError(parseTileMapChunkPayload(oversized));

    auto coordinate = canonical;
    putU32(coordinate, 24U, TileMapChunkWire::MaxChunkCoordinate + 1U);
    expectAssetError(parseTileMapChunkPayload(coordinate));

    auto cellCount = canonical;
    putU32(cellCount, 36U, 3U);
    expectAssetError(parseTileMapChunkPayload(cellCount));

    auto nonEmptyCount = canonical;
    putU32(nonEmptyCount, 40U, 2U);
    expectAssetError(parseTileMapChunkPayload(nonEmptyCount));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseTileMapChunkPayload(truncated));

    const std::array<u16, 1> cells{1U};
    const TileMapChunkPayloadDesc desc{
        .parentTileMapId = assetId(12),
        .layerId = 1,
        .widthCells = 1,
        .heightCells = 1,
        .cells = cells,
    };
    expectAssetError(writeCookedTileMapChunkAsset(desc.parentTileMapId, desc));
}

TEST(TypedPayloadMalformedCorpusTests, SpriteAnimationRejectsIndicesBombsNonFiniteAndSelfDependency)
{
    const auto canonical = makeSpriteAnimationPayload();
    ASSERT_TRUE(parseSpriteAnimationClipPayload(canonical));
    constexpr usize Frame = SpriteAnimationClipWire::HeaderBytes;
    constexpr usize Event = Frame + SpriteAnimationClipWire::FrameBytes;

    auto frameBomb = canonical;
    putU32(frameBomb, 4U, SpriteAnimationClipWire::MaxFrames + 1U);
    expectAssetError(parseSpriteAnimationClipPayload(frameBomb));

    auto eventBomb = canonical;
    putU32(eventBomb, 12U, SpriteAnimationClipWire::MaxTotalEvents + 1U);
    expectAssetError(parseSpriteAnimationClipPayload(eventBomb));

    auto dependencyIndex = canonical;
    putU32(dependencyIndex, Frame, 1U);
    expectAssetError(parseSpriteAnimationClipPayload(dependencyIndex));

    auto eventRange = canonical;
    putU16(eventRange, Frame + 8U, 1U);
    expectAssetError(parseSpriteAnimationClipPayload(eventRange));

    auto eventCount = canonical;
    putU16(eventCount, Frame + 10U, static_cast<u16>(SpriteAnimationClipWire::MaxEventsPerFrame + 1U));
    expectAssetError(parseSpriteAnimationClipPayload(eventCount));

    auto duration = canonical;
    putF32(duration, Frame + 4U, std::numeric_limits<float>::infinity());
    expectAssetError(parseSpriteAnimationClipPayload(duration));

    auto eventTag = canonical;
    putU32(eventTag, Event, 0U);
    expectAssetError(parseSpriteAnimationClipPayload(eventTag));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseSpriteAnimationClipPayload(truncated));

    const std::array events{
        SpriteAnimationEventDesc{.eventTag = 1U, .normalizedOffset = 0.5F},
    };
    const auto spriteId = assetId(13);
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .events = events},
    };
    expectAssetError(writeCookedSpriteAnimationClipAsset(spriteId, makeAnimationDesc(frames)));
}

TEST(TypedPayloadMalformedCorpusTests, StaticMeshRejectsIndexBombNonFiniteAndLengthDamage)
{
    const auto canonical = makeStaticMeshPayload();
    ASSERT_TRUE(parseStaticMeshPayload(canonical));
    constexpr usize Vertex = StaticMeshWire::HeaderBytes + StaticMeshWire::SubmeshBytes;
    constexpr usize Index = Vertex + 3U * StaticMeshWire::BytesPerVertex;

    auto vertexBomb = canonical;
    putU32(vertexBomb, 8U, StaticMeshWire::MaxVertexCount + 1U);
    expectAssetError(parseStaticMeshPayload(vertexBomb));

    auto outOfBoundsIndex = canonical;
    putU16(outOfBoundsIndex, Index, 3U);
    expectAssetError(parseStaticMeshPayload(outOfBoundsIndex));

    auto submeshRange = canonical;
    putU32(submeshRange, StaticMeshWire::HeaderBytes, 3U);
    expectAssetError(parseStaticMeshPayload(submeshRange));

    auto bounds = canonical;
    putF32(bounds, 16U, std::numeric_limits<float>::infinity());
    expectAssetError(parseStaticMeshPayload(bounds));

    auto vertex = canonical;
    putF32(vertex, Vertex, std::numeric_limits<float>::quiet_NaN());
    expectAssetError(parseStaticMeshPayload(vertex));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseStaticMeshPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseStaticMeshPayload(trailing));

    std::vector<std::byte> misalignedStorage(canonical.size() + alignof(float));
    const auto storageAddress = reinterpret_cast<std::uintptr_t>(misalignedStorage.data());
    usize payloadOffset = 0U;
    while (((storageAddress + payloadOffset) % alignof(float)) == 0U)
    {
        ++payloadOffset;
    }
    std::memcpy(misalignedStorage.data() + payloadOffset, canonical.data(), canonical.size());
    expectAssetError(parseStaticMeshPayload(std::span<const std::byte>{
        misalignedStorage.data() + payloadOffset, canonical.size()}));
}

TEST(TypedPayloadMalformedCorpusTests, SkinnedMeshRejectsBombsIndicesNonFiniteAndLengthDamage)
{
    const auto canonical = makeSkinnedMeshPayload();
    ASSERT_TRUE(parseSkinnedMeshPayload(canonical));

    constexpr usize Joint = SkinnedMeshWire::HeaderBytes +
                            SkinnedMeshWire::InverseBindMatrixBytes;
    constexpr usize Submesh = Joint + SkinnedMeshWire::JointBytes;
    constexpr usize Vertex = Submesh + SkinnedMeshWire::SubmeshBytes;
    constexpr usize JointIndex = Vertex +
                                 3U * SkinnedMeshWire::FloatsPerVertex * sizeof(float);

    auto vertexBomb = canonical;
    putU32(vertexBomb, 8U, SkinnedMeshWire::MaxVertexCount + 1U);
    expectAssetError(parseSkinnedMeshPayload(vertexBomb));

    auto jointBomb = canonical;
    putU16(jointBomb, 32U, static_cast<u16>(SkinnedMeshWire::MaxJointCount + 1U));
    expectAssetError(parseSkinnedMeshPayload(jointBomb));

    auto selfParent = canonical;
    putU16(selfParent, Joint, 0U);
    expectAssetError(parseSkinnedMeshPayload(selfParent));

    auto jointIndex = canonical;
    putU16(jointIndex, JointIndex, 1U);
    expectAssetError(parseSkinnedMeshPayload(jointIndex));

    auto inverseBind = canonical;
    putF32(inverseBind, SkinnedMeshWire::HeaderBytes,
           std::numeric_limits<float>::quiet_NaN());
    expectAssetError(parseSkinnedMeshPayload(inverseBind));

    auto jointTransform = canonical;
    putF32(jointTransform, Joint + 4U, std::numeric_limits<float>::infinity());
    expectAssetError(parseSkinnedMeshPayload(jointTransform));

    auto reserved = canonical;
    putU32(reserved, 36U, 1U);
    expectAssetError(parseSkinnedMeshPayload(reserved));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseSkinnedMeshPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseSkinnedMeshPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, AnimationClip3DRejectsBombsRangesNonFiniteAndLengthDamage)
{
    const auto canonical = makeAnimationClip3DPayload();
    ASSERT_TRUE(parseAnimationClip3DPayload(canonical));
    constexpr usize Track = AnimationClip3DWire::HeaderBytes;
    constexpr usize Times = Track + AnimationClip3DWire::TrackBytes;
    constexpr usize Values = Times + 2U * sizeof(float);

    auto trackBomb = canonical;
    putU16(trackBomb, 6U, static_cast<u16>(AnimationClip3DWire::MaxTracks + 1U));
    expectAssetError(parseAnimationClip3DPayload(trackBomb));

    auto keyBomb = canonical;
    putU32(keyBomb, 8U, AnimationClip3DWire::MaxTotalKeyframes + 1U);
    expectAssetError(parseAnimationClip3DPayload(keyBomb));

    auto perTrackKeyBomb = canonical;
    putU16(perTrackKeyBomb, Track + 4U,
           static_cast<u16>(AnimationClip3DWire::MaxKeyframesPerTrack + 1U));
    expectAssetError(parseAnimationClip3DPayload(perTrackKeyBomb));

    auto jointIndex = canonical;
    putU16(jointIndex, Track, 1U);
    expectAssetError(parseAnimationClip3DPayload(jointIndex));

    auto keyRange = canonical;
    putU32(keyRange, Track + 8U, 1U);
    expectAssetError(parseAnimationClip3DPayload(keyRange));

    auto duration = canonical;
    putF32(duration, 16U, std::numeric_limits<float>::infinity());
    expectAssetError(parseAnimationClip3DPayload(duration));

    auto keyTime = canonical;
    putF32(keyTime, Times, std::numeric_limits<float>::quiet_NaN());
    expectAssetError(parseAnimationClip3DPayload(keyTime));

    auto keyValue = canonical;
    putF32(keyValue, Values, std::numeric_limits<float>::infinity());
    expectAssetError(parseAnimationClip3DPayload(keyValue));

    auto reserved = canonical;
    putU16(reserved, Track + 6U, 1U);
    expectAssetError(parseAnimationClip3DPayload(reserved));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseAnimationClip3DPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseAnimationClip3DPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, MaterialRejectsNonFiniteFlagsReservedAndLengthDamage)
{
    const auto canonical = makeMaterialPayload();
    ASSERT_TRUE(parseMaterialPayload(canonical));

    for (const usize offset : {4U, 20U, 24U})
    {
        auto nonFinite = canonical;
        putF32(nonFinite, offset, offset == 4U ? std::numeric_limits<float>::quiet_NaN()
                                               : std::numeric_limits<float>::infinity());
        SCOPED_TRACE(offset);
        expectAssetError(parseMaterialPayload(nonFinite));
    }

    auto flags = canonical;
    putU16(flags, 30U, 0x8000U);
    expectAssetError(parseMaterialPayload(flags));

    auto reserved = canonical;
    reserved[32U] = std::byte{1};
    expectAssetError(parseMaterialPayload(reserved));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseMaterialPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseMaterialPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, PrefabRejectsIdentityCycleBombNonFiniteAndPublishesAtomically)
{
    const auto canonical = makePrefabPayload();
    std::vector<PrefabNodeView> canonicalStorage;
    ASSERT_TRUE(parsePrefabPayload(canonical, canonicalStorage));

    auto nodeBomb = canonical;
    putU16(nodeBomb, 2U, static_cast<u16>(PrefabWire::MaxNodes + 1U));
    std::vector<PrefabNodeView> bombStorage;
    expectAssetError(parsePrefabPayload(nodeBomb, bombStorage));

    auto zeroStableId = canonical;
    putU32(zeroStableId, PrefabWire::HeaderBytes, 0U);
    std::vector<PrefabNodeView> zeroStorage;
    expectAssetError(parsePrefabPayload(zeroStableId, zeroStorage));

    auto duplicateStableId = canonical;
    constexpr usize SecondNode = PrefabWire::HeaderBytes + PrefabWire::NodeBytes;
    putU32(duplicateStableId, SecondNode, 1U);
    std::vector<PrefabNodeView> sentinel{
        PrefabNodeView{.stableNodeId = 99U, .positionX = 42.0F},
    };
    expectAssetError(parsePrefabPayload(duplicateStableId, sentinel));
    ASSERT_EQ(sentinel.size(), 1U);
    EXPECT_EQ(sentinel[0].stableNodeId, 99U);
    EXPECT_FLOAT_EQ(sentinel[0].positionX, 42.0F);

    auto selfParent = canonical;
    putU32(selfParent, SecondNode + 4U, 1U);
    std::vector<PrefabNodeView> parentStorage;
    expectAssetError(parsePrefabPayload(selfParent, parentStorage));

    auto forwardParent = canonical;
    putU32(forwardParent, SecondNode + 4U, 2U);
    std::vector<PrefabNodeView> forwardParentStorage;
    expectAssetError(parsePrefabPayload(forwardParent, forwardParentStorage));

    auto nonFinite = canonical;
    putF32(nonFinite, PrefabWire::HeaderBytes + 8U, std::numeric_limits<float>::quiet_NaN());
    std::vector<PrefabNodeView> nonFiniteStorage;
    expectAssetError(parsePrefabPayload(nonFinite, nonFiniteStorage));

    auto zeroRotation = canonical;
    for (usize offset = 20U; offset <= 32U; offset += 4U)
    {
        putF32(zeroRotation, PrefabWire::HeaderBytes + offset, 0.0F);
    }
    std::vector<PrefabNodeView> rotationStorage;
    expectAssetError(parsePrefabPayload(zeroRotation, rotationStorage));

    auto truncated = canonical;
    truncated.pop_back();
    std::vector<PrefabNodeView> truncatedStorage;
    expectAssetError(parsePrefabPayload(truncated, truncatedStorage));

    const std::array invalidNodes{
        PrefabNodeDesc{.stableNodeId = 0},
    };
    expectAssetError(writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = invalidNodes}));
}

TEST(TypedPayloadMalformedCorpusTests, EnvironmentMapRejectsBombMipCountsAndLengthDamage)
{
    const auto canonical = makeEnvironmentMapPayload();
    ASSERT_TRUE(parseEnvironmentMapPayload(canonical));

    auto bomb = canonical;
    putU16(bomb, 6U, (std::numeric_limits<u16>::max)());
    expectAssetError(parseEnvironmentMapPayload(bomb));

    auto mipMismatch = canonical;
    putU16(mipMismatch, 10U, 2U);
    expectAssetError(parseEnvironmentMapPayload(mipMismatch));

    auto byteCount = canonical;
    putU32(byteCount, 16U, 1U);
    expectAssetError(parseEnvironmentMapPayload(byteCount));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseEnvironmentMapPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseEnvironmentMapPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, AudioClipRejectsBombNonFiniteAndLengthDamage)
{
    const auto canonical = makeAudioClipPayload();
    ASSERT_TRUE(parseAudioClipPayload(canonical));

    auto frameBomb = canonical;
    putU32(frameBomb, 8U, AudioClipWire::MaxFrameCount + 1U);
    expectAssetError(parseAudioClipPayload(frameBomb));

    auto channelBomb = canonical;
    putU16(channelBomb, 2U, static_cast<u16>(AudioClipWire::MaxChannels + 1U));
    expectAssetError(parseAudioClipPayload(channelBomb));

    for (const float value : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()})
    {
        auto nonFinite = canonical;
        putF32(nonFinite, AudioClipWire::HeaderBytes, value);
        expectAssetError(parseAudioClipPayload(nonFinite));
    }

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseAudioClipPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseAudioClipPayload(trailing));

    std::vector<std::byte> misalignedStorage(canonical.size() + alignof(float));
    const auto storageAddress = reinterpret_cast<std::uintptr_t>(misalignedStorage.data());
    usize payloadOffset = 0U;
    while (((storageAddress + payloadOffset) % alignof(float)) == 0U)
    {
        ++payloadOffset;
    }
    std::memcpy(misalignedStorage.data() + payloadOffset, canonical.data(), canonical.size());
    expectAssetError(parseAudioClipPayload(std::span<const std::byte>{
        misalignedStorage.data() + payloadOffset, canonical.size()}));

    const std::array<float, 1> invalidSamples{std::numeric_limits<float>::infinity()};
    expectAssetError(writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .frameCount = 1,
        .interleavedPcm = invalidSamples,
    }));
}

TEST(TypedPayloadMalformedCorpusTests, NavigationGrid2DRejectsBombsTablesNonFiniteAndLengthDamage)
{
    const auto canonical = makeNavigationGrid2DPayload();
    ASSERT_TRUE(parseNavigationGrid2DPayload(canonical));

    auto dimensionBomb = canonical;
    putU32(dimensionBomb, 4U, NavigationGrid2DWire::MaximumDimension + 1U);
    expectAssetError(parseNavigationGrid2DPayload(dimensionBomb));

    auto cellBomb = canonical;
    putU32(cellBomb, 24U,
           static_cast<u32>(NavigationGrid2DWire::MaximumCellCount + 1U));
    expectAssetError(parseNavigationGrid2DPayload(cellBomb));

    auto nonFiniteOrigin = canonical;
    putF32(nonFiniteOrigin, 12U, std::numeric_limits<float>::quiet_NaN());
    expectAssetError(parseNavigationGrid2DPayload(nonFiniteOrigin));

    auto invalidFlags = canonical;
    invalidFlags[NavigationGrid2DWire::HeaderBytes] = std::byte{2};
    expectAssetError(parseNavigationGrid2DPayload(invalidFlags));

    auto invalidCost = canonical;
    invalidCost[NavigationGrid2DWire::HeaderBytes + 1U] = std::byte{0};
    expectAssetError(parseNavigationGrid2DPayload(invalidCost));

    auto reserved = canonical;
    putU32(reserved, 28U, 1U);
    expectAssetError(parseNavigationGrid2DPayload(reserved));

    auto truncated = canonical;
    truncated.resize(NavigationGrid2DWire::HeaderBytes - 1U);
    expectAssetError(parseNavigationGrid2DPayload(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseNavigationGrid2DPayload(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, Fx2DRejectsBombsDependencyNonFiniteAndLengthDamage)
{
    const auto canonical = makeFx2DPayload();
    ASSERT_TRUE(parseFx2DPayloadBytes(canonical));

    auto particleBomb = canonical;
    putU32(particleBomb, 16U, Fx2DWire::MaxParticleCapacity + 1U);
    expectAssetError(parseFx2DPayloadBytes(particleBomb));

    auto trailBomb = canonical;
    putU32(trailBomb, 128U, Fx2DWire::MaxTrailCapacity + 1U);
    expectAssetError(parseFx2DPayloadBytes(trailBomb));

    auto dependencyIndex = canonical;
    putU32(dependencyIndex, 32U, 1U);
    expectAssetError(parseFx2DPayloadBytes(dependencyIndex));

    auto missingDependencyId = canonical;
    for (usize offset = 0; offset < Core::AssetId::Bytes{}.size(); ++offset)
    {
        missingDependencyId[offset] = std::byte{0};
    }
    expectAssetError(parseFx2DPayloadBytes(missingDependencyId));

    auto nonFinite = canonical;
    putF32(nonFinite, 36U, std::numeric_limits<float>::infinity());
    expectAssetError(parseFx2DPayloadBytes(nonFinite));

    auto reserved = canonical;
    reserved[122U] = std::byte{1};
    expectAssetError(parseFx2DPayloadBytes(reserved));

    auto truncated = canonical;
    truncated.pop_back();
    expectAssetError(parseFx2DPayloadBytes(truncated));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    expectAssetError(parseFx2DPayloadBytes(trailing));
}

TEST(TypedPayloadMalformedCorpusTests, World2DRejectsCycleBombNonFiniteAndPublishesAtomically)
{
    const auto canonical = makeWorld2DSnapshot();
    std::vector<World2DEntityDesc> canonicalStorage;
    ASSERT_TRUE(parseWorld2DSnapshot(canonical, canonicalStorage));

    auto entityBomb = canonical;
    putU32(entityBomb, 4U, World2DSnapshotWire::MaximumEntities + 1U);
    std::vector<World2DEntityDesc> entityBombStorage;
    expectAssetError(parseWorld2DSnapshot(entityBomb, entityBombStorage));

    auto gameplayBomb = canonical;
    putU32(gameplayBomb, 20U, World2DSnapshotWire::MaximumGameplayBytes + 1U);
    std::vector<World2DEntityDesc> gameplayBombStorage;
    expectAssetError(parseWorld2DSnapshot(gameplayBomb, gameplayBombStorage));

    auto selfParent = canonical;
    putU32(selfParent, World2DSnapshotWire::HeaderBytes + 4U, 1U);
    std::vector<World2DEntityDesc> selfParentStorage;
    expectAssetError(parseWorld2DSnapshot(selfParent, selfParentStorage));

    auto forwardParent = canonical;
    putU32(forwardParent, World2DSnapshotWire::HeaderBytes + 4U, 2U);
    std::vector<World2DEntityDesc> forwardParentStorage;
    expectAssetError(parseWorld2DSnapshot(forwardParent, forwardParentStorage));

    auto nonFinite = canonical;
    putF32(nonFinite, World2DSnapshotWire::HeaderBytes + 16U,
           std::numeric_limits<float>::quiet_NaN());
    std::vector<World2DEntityDesc> nonFiniteStorage;
    expectAssetError(parseWorld2DSnapshot(nonFinite, nonFiniteStorage));

    auto duplicateStableId = canonical;
    const usize secondEntity = World2DSnapshotWire::HeaderBytes + World2DSnapshotWire::EntityBytes;
    putU32(duplicateStableId, secondEntity, 1U);
    std::vector<World2DEntityDesc> sentinel{
        World2DEntityDesc{.stableEntityId = 99U, .positionX = 42.0F},
    };
    expectAssetError(parseWorld2DSnapshot(duplicateStableId, sentinel));
    ASSERT_EQ(sentinel.size(), 1U);
    EXPECT_EQ(sentinel[0].stableEntityId, 99U);
    EXPECT_FLOAT_EQ(sentinel[0].positionX, 42.0F);

    auto truncated = canonical;
    truncated.pop_back();
    std::vector<World2DEntityDesc> truncatedStorage;
    expectAssetError(parseWorld2DSnapshot(truncated, truncatedStorage));

    auto trailing = canonical;
    trailing.push_back(std::byte{0});
    std::vector<World2DEntityDesc> trailingStorage;
    expectAssetError(parseWorld2DSnapshot(trailing, trailingStorage));
}

} // namespace
} // namespace Tina::AssetFormat
