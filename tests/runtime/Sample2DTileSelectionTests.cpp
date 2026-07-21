#include "TileSelection.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace {

using Tina::DigitalActionTransition;
using Tina::DigitalActionTransitionKind;
using Tina::InputActionId;
using Tina::SimulationActionTransition;
using Tina::Render::WorldPointerSample;
using Tina::Sample2D::TileSelectionCounters;
using Tina::Sample2D::TileSelectionGrid;
using Tina::Sample2D::consumeTileSelectionTransitions;

inline constexpr InputActionId SelectTileAction{3};
inline constexpr TileSelectionGrid SampleGrid{8, 4, 1.0F};

[[nodiscard]] SimulationActionTransition pointerTransition(DigitalActionTransitionKind kind, InputActionId action,
                                                            std::optional<WorldPointerSample> sample,
                                                            Tina::Core::u64 sequence = 1)
{
    return DigitalActionTransition{
        .action = action,
        .kind = kind,
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

TEST(Sample2DTileSelectionTest, PressedHitSelectsHalfOpenMapCellAndRetainsPayload)
{
    TileSelectionCounters counters{};
    const auto transitions = std::array{
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction, hit(0.99F, 1.01F, 42), 42),
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
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction, noHit, 2),
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction, hit(-0.01F, 1.0F, 3), 3),
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction, hit(8.0F, 1.0F, 4), 4),
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction, hit(1.0F, 4.0F, 5), 5),
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction,
                         hit(std::numeric_limits<float>::quiet_NaN(), 1.0F, 6), 6),
    };

    consumeTileSelectionTransitions(transitions, SelectTileAction, SampleGrid, counters);

    EXPECT_EQ(counters.pointerPresses, 5U);
    EXPECT_EQ(counters.viewportMisses, 1U);
    EXPECT_EQ(counters.mapMisses, 4U);
    EXPECT_EQ(counters.selectionHits, 0U);
    EXPECT_FALSE(counters.lastSelection.has_value());
}

TEST(Sample2DTileSelectionTest, IgnoresNonPressedOtherActionsMissingPayloadAndCatchUpBatch)
{
    TileSelectionCounters counters{};
    const auto transitions = std::array{
        pointerTransition(DigitalActionTransitionKind::Released, SelectTileAction, hit(1.0F, 1.0F), 1),
        pointerTransition(DigitalActionTransitionKind::Cancelled, SelectTileAction, hit(1.0F, 1.0F), 2),
        pointerTransition(DigitalActionTransitionKind::Pressed, InputActionId{99}, hit(1.0F, 1.0F), 3),
        pointerTransition(DigitalActionTransitionKind::Pressed, SelectTileAction, std::nullopt, 4),
    };

    consumeTileSelectionTransitions(transitions, SelectTileAction, SampleGrid, counters);
    consumeTileSelectionTransitions({}, SelectTileAction, SampleGrid, counters);

    EXPECT_EQ(counters.pointerPresses, 1U);
    EXPECT_EQ(counters.missingWorldPointerSamples, 1U);
    EXPECT_EQ(counters.selectionHits, 0U);
    EXPECT_FALSE(counters.lastSelection.has_value());
}
