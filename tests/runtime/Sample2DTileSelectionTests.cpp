#include "TileSelection.hpp"

#include <gtest/gtest.h>

#include <tina/render/RenderFramePacket.hpp>

#include <array>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace {

using Tina::InputActionTransition;
using Tina::InputActionTransitionKind;
using Tina::InputActionId;
using Tina::SimulationActionTransition;
using Tina::Render::WorldPointerSample;
using Tina::Sample2D::TileSelectionCounters;
using Tina::Sample2D::TileSelectionGrid;
using Tina::Sample2D::consumeTileSelectionTransitions;

inline constexpr InputActionId SelectTileAction{3};
inline constexpr TileSelectionGrid SampleGrid{8, 4, 1.0F};

[[nodiscard]] SimulationActionTransition pointerTransition(InputActionTransitionKind kind, InputActionId action,
                                                            std::optional<WorldPointerSample> sample,
                                                            Tina::Core::u64 sequence = 1)
{
    return InputActionTransition{
        .action = action,
        .kind = kind,
        .value = kind == InputActionTransitionKind::Started ? 1.0F : 0.0F,
        .sourceSequence = sequence,
        .worldPointerSample = std::move(sample),
    };
}

[[nodiscard]] WorldPointerSample hit(float x, float y, Tina::Core::u64 sequence = 1)
{
    return WorldPointerSample{
        .worldX = x,
        .worldY = y,
        .cameraRevision = 7,
        .surfaceRevision = 11,
        .inputSequence = sequence,
        .stableCameraKey = 1,
        .hit = true,
    };
}

} // namespace

TEST(Sample2DTileSelectionTest, StartedHitSelectsHalfOpenMapCellAndRetainsPayload)
{
    TileSelectionCounters counters{};
    const auto transitions = std::array{
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction, hit(0.99F, 1.01F, 42), 42),
    };

    consumeTileSelectionTransitions(transitions, SelectTileAction, TileSelectionGrid{8, 4, 0.5F}, counters);

    EXPECT_EQ(counters.pointerPresses, 1U);
    EXPECT_EQ(counters.selectionHits, 1U);
    EXPECT_EQ(counters.viewportMisses, 0U);
    EXPECT_EQ(counters.mapMisses, 0U);
    ASSERT_TRUE(counters.lastSelection.has_value());
    EXPECT_EQ(counters.lastSelection->cellX, 1U);
    EXPECT_EQ(counters.lastSelection->cellY, 2U);
    EXPECT_EQ(counters.lastSelection->worldPointer.inputSequence, 42U);
    EXPECT_EQ(counters.lastSelection->worldPointer.cameraRevision, 7U);
    EXPECT_EQ(counters.lastSelection->worldPointer.surfaceRevision, 11U);

    consumeTileSelectionTransitions({}, SelectTileAction, TileSelectionGrid{8, 4, 0.5F}, counters);
    ASSERT_TRUE(counters.lastSelection.has_value());
    EXPECT_EQ(counters.lastSelection->cellX, 1U);
    EXPECT_EQ(counters.lastSelection->cellY, 2U);
    EXPECT_EQ(counters.selectionHits, 1U);
}

TEST(Sample2DTileSelectionTest, ViewportAndMapMissesDoNotSelect)
{
    TileSelectionCounters counters{};
    const auto noHit = WorldPointerSample{.inputSequence = 2, .hit = false};
    const auto transitions = std::array{
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction, noHit, 2),
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction, hit(-0.01F, 1.0F, 3), 3),
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction, hit(8.0F, 1.0F, 4), 4),
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction, hit(1.0F, 4.0F, 5), 5),
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction,
                         hit(std::numeric_limits<float>::quiet_NaN(), 1.0F, 6), 6),
    };

    consumeTileSelectionTransitions(transitions, SelectTileAction, SampleGrid, counters);

    EXPECT_EQ(counters.pointerPresses, 5U);
    EXPECT_EQ(counters.viewportMisses, 1U);
    EXPECT_EQ(counters.mapMisses, 4U);
    EXPECT_EQ(counters.selectionHits, 0U);
    EXPECT_FALSE(counters.lastSelection.has_value());
}

TEST(Sample2DTileSelectionTest, IgnoresNonStartedOtherActionsMissingPayloadAndCatchUpBatch)
{
    TileSelectionCounters counters{};
    const auto transitions = std::array{
        pointerTransition(InputActionTransitionKind::Completed, SelectTileAction, hit(1.0F, 1.0F), 1),
        pointerTransition(InputActionTransitionKind::Cancelled, SelectTileAction, hit(1.0F, 1.0F), 2),
        pointerTransition(InputActionTransitionKind::Started, InputActionId{99}, hit(1.0F, 1.0F), 3),
        pointerTransition(InputActionTransitionKind::Started, SelectTileAction, std::nullopt, 4),
    };

    consumeTileSelectionTransitions(transitions, SelectTileAction, SampleGrid, counters);
    consumeTileSelectionTransitions({}, SelectTileAction, SampleGrid, counters);

    EXPECT_EQ(counters.pointerPresses, 1U);
    EXPECT_EQ(counters.missingWorldPointerSamples, 1U);
    EXPECT_EQ(counters.selectionHits, 0U);
    EXPECT_FALSE(counters.lastSelection.has_value());
}

TEST(Sample2DTileSelectionTest, SelectionHighlightSpriteCentersOnCellAndRejectsInvalid)
{
    Tina::Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    Tina::Render::FramePin pin{
        Tina::Render::FramePinKind::Custom,
        1,
        nullptr,
        [](void*) noexcept {},
    };
    auto texture = packet.intern(
        Tina::Render::FrameResourceDescriptor{
            .kind = Tina::Render::FrameResourceKind::Texture2D,
            .deviceBindingKey = 1,
        },
        std::move(pin));
    ASSERT_TRUE(texture);

    const auto sprite =
        Tina::Sample2D::makeSelectionHighlightSprite({.cellX = 2, .cellY = 1}, SampleGrid, *texture);
    ASSERT_TRUE(sprite.has_value());
    EXPECT_EQ(sprite->texture, *texture);
    EXPECT_FLOAT_EQ(sprite->centerX, 2.5F);
    EXPECT_FLOAT_EQ(sprite->centerY, 1.5F);
    EXPECT_FLOAT_EQ(sprite->widthMeters, 0.92F);
    EXPECT_FLOAT_EQ(sprite->heightMeters, 0.92F);
    EXPECT_EQ(sprite->sortingLayer, 2);
    EXPECT_EQ(sprite->alpha, 160);

    EXPECT_FALSE(
        Tina::Sample2D::makeSelectionHighlightSprite({.cellX = 8, .cellY = 0}, SampleGrid, *texture).has_value());
    EXPECT_FALSE(Tina::Sample2D::makeSelectionHighlightSprite(
                     {.cellX = 0, .cellY = 0}, SampleGrid, Tina::Render::FrameResourceRef{})
                     .has_value());
    EXPECT_FALSE(
        Tina::Sample2D::makeSelectionHighlightSprite(
            {.cellX = 0, .cellY = 0}, TileSelectionGrid{8, 4, 0.0F}, *texture)
            .has_value());
}

TEST(Sample2DTileSelectionTest, SeedTileSelectionLocksCellCenterPayload)
{
    TileSelectionCounters counters{};
    ASSERT_TRUE(Tina::Sample2D::seedTileSelection(3, 1, SampleGrid, counters, /*inputSequence=*/77));
    EXPECT_EQ(counters.selectionHits, 1U);
    EXPECT_EQ(counters.pointerPresses, 1U);
    ASSERT_TRUE(counters.lastSelection.has_value());
    EXPECT_EQ(counters.lastSelection->cellX, 3U);
    EXPECT_EQ(counters.lastSelection->cellY, 1U);
    EXPECT_TRUE(counters.lastSelection->worldPointer.hit);
    EXPECT_FLOAT_EQ(counters.lastSelection->worldPointer.worldX, 3.5F);
    EXPECT_FLOAT_EQ(counters.lastSelection->worldPointer.worldY, 1.5F);
    EXPECT_EQ(counters.lastSelection->worldPointer.inputSequence, 77U);

    EXPECT_FALSE(Tina::Sample2D::seedTileSelection(8, 0, SampleGrid, counters));
    EXPECT_EQ(counters.selectionHits, 1U);
}

TEST(Sample2DTileSelectionTest, ScriptedWorldCellPressFeedsSameConsumerAsA42Edge)
{
    TileSelectionCounters counters{};
    auto press = Tina::Sample2D::makeScriptedWorldCellPress(SelectTileAction, SampleGrid, 2, 0, 55);
    ASSERT_TRUE(press.has_value());
    const SimulationActionTransition transition = *press;
    consumeTileSelectionTransitions(std::span{&transition, 1}, SelectTileAction, SampleGrid, counters);

    EXPECT_EQ(counters.selectionHits, 1U);
    ASSERT_TRUE(counters.lastSelection.has_value());
    EXPECT_EQ(counters.lastSelection->cellX, 2U);
    EXPECT_EQ(counters.lastSelection->cellY, 0U);
    EXPECT_FLOAT_EQ(counters.lastSelection->worldPointer.worldX, 2.5F);
    EXPECT_FLOAT_EQ(counters.lastSelection->worldPointer.worldY, 0.5F);
    EXPECT_EQ(counters.lastSelection->worldPointer.inputSequence, 55U);

    EXPECT_FALSE(Tina::Sample2D::makeScriptedWorldCellPress(SelectTileAction, SampleGrid, 99, 0).has_value());
}
