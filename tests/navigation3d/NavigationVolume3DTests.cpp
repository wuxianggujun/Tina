#include "NavigationTestSupport3D.hpp"

#include <tina/navigation3d/NavigationErrors3D.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace Tina::Navigation3D {
namespace {

using TestSupport::VolumeBuilder;
using TestSupport::WalkerProfile;
using TestSupport::makeFlatVolume;

TEST(NavigationVolume3DDataTest, RejectsFieldsThatDoNotMatchTheDeclaredDimensions)
{
    std::vector<Core::u8> flags(7, 0);
    std::vector<Core::u8> costs(8, 1);
    const auto data = NavigationVolume3DData::Create({
        .widthCells = 2, .heightCells = 2, .depthCells = 2,
        .cellFlags = flags, .traversalCosts = costs,
    });
    ASSERT_FALSE(data.has_value());
    EXPECT_EQ(data.error().code, Navigation3DErrorCode::InvalidData);
}

TEST(NavigationVolume3DDataTest, RejectsUnsupportedCellFlagsAndOutOfRangeCosts)
{
    std::vector<Core::u8> flags(8, 0);
    std::vector<Core::u8> costs(8, 1);
    flags[3] = 0x02;
    const auto badFlags = NavigationVolume3DData::Create({
        .widthCells = 2, .heightCells = 2, .depthCells = 2,
        .cellFlags = flags, .traversalCosts = costs,
    });
    ASSERT_FALSE(badFlags.has_value());
    EXPECT_EQ(badFlags.error().code, Navigation3DErrorCode::InvalidData);

    flags[3] = 0;
    costs[5] = 0;
    const auto zeroCost = NavigationVolume3DData::Create({
        .widthCells = 2, .heightCells = 2, .depthCells = 2,
        .cellFlags = flags, .traversalCosts = costs,
    });
    ASSERT_FALSE(zeroCost.has_value());
    EXPECT_EQ(zeroCost.error().code, Navigation3DErrorCode::InvalidData);

    costs[5] = NavigationVolume3DContract::MaximumTraversalCost + 1;
    const auto tooExpensive = NavigationVolume3DData::Create({
        .widthCells = 2, .heightCells = 2, .depthCells = 2,
        .cellFlags = flags, .traversalCosts = costs,
    });
    ASSERT_FALSE(tooExpensive.has_value());
    EXPECT_EQ(tooExpensive.error().code, Navigation3DErrorCode::InvalidData);
}

// The layout is x + z * width + y * width * depth (ADR 0048 D11). Getting it wrong would
// silently transpose the world, so assert one interior cell in each axis independently.
TEST(NavigationVolume3DDataTest, IndexesCellsByXThenZThenY)
{
    VolumeBuilder builder(3, 4, 5);
    builder.solid(1, 0, 0).solid(0, 2, 0).solid(0, 0, 3);
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());

    EXPECT_TRUE(volume->isBaseSolid({1, 0, 0}));
    EXPECT_TRUE(volume->isBaseSolid({0, 2, 0}));
    EXPECT_TRUE(volume->isBaseSolid({0, 0, 3}));
    EXPECT_FALSE(volume->isBaseSolid({0, 0, 1}));
    EXPECT_FALSE(volume->isBaseSolid({0, 1, 0}));
    EXPECT_FALSE(volume->isBaseSolid({3 - 1, 4 - 1, 5 - 1}));
}

// ADR 0048 D4. Out of bounds is not solid, and the two directions have deliberately
// different consequences: sky above, no support below.
TEST(NavigationVolume3DTest, OutOfBoundsIsNotSolidSoTheTopLayerStaysUsable)
{
    const auto volume = VolumeBuilder(3, 3, 3).solidLayer(0).build();
    ASSERT_TRUE(volume.has_value());

    EXPECT_FALSE(volume->isSolid({1, 3, 1}));
    EXPECT_FALSE(volume->isSolid({99, 99, 99}));
    // A 2-cell agent standing on the top layer has its head outside the volume, which is
    // open sky rather than a ceiling.
    const auto ceiling = VolumeBuilder(3, 2, 3).solidLayer(0).build();
    ASSERT_TRUE(ceiling.has_value());
    EXPECT_TRUE(ceiling->isStandable({1, 1, 1}, WalkerProfile));
}

TEST(NavigationVolume3DTest, AnEmptyVolumeHasNoStandableCellsBecauseThereIsNoImplicitFloor)
{
    const auto volume = VolumeBuilder(4, 4, 4).build();
    ASSERT_TRUE(volume.has_value());

    for (Core::u32 y = 0; y < 4; ++y)
    {
        for (Core::u32 z = 0; z < 4; ++z)
        {
            for (Core::u32 x = 0; x < 4; ++x)
            {
                EXPECT_FALSE(volume->isStandable({x, y, z}, WalkerProfile))
                    << "cell " << x << "," << y << "," << z;
            }
        }
    }
}

TEST(NavigationVolume3DTest, StandableRequiresBothHeadClearanceAndSupport)
{
    // Floor at y=0, a slab at y=3 leaving exactly two open cells at y=1 and y=2.
    VolumeBuilder builder(3, 5, 3);
    builder.solidLayer(0).solidLayer(3);
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());

    EXPECT_TRUE(volume->isStandable({1, 1, 1}, WalkerProfile));
    // y=2 has support only if y=1 is solid, which it is not.
    EXPECT_FALSE(volume->isStandable({1, 2, 1}, WalkerProfile));
    // y=0 is solid itself.
    EXPECT_FALSE(volume->isStandable({1, 0, 1}, WalkerProfile));
    // y=4 sits on the slab and its head leaves the volume: sky, so standable.
    EXPECT_TRUE(volume->isStandable({1, 4, 1}, WalkerProfile));

    // A 3-cell agent does not fit between the floor and the slab.
    const NavigationAgentProfile3D tall{.heightCells = 3, .maxStepUpCells = 1, .maxFallCells = 3};
    EXPECT_FALSE(volume->isStandable({1, 1, 1}, tall));
    // A 1-cell crawler does, but still only where something supports it.
    const NavigationAgentProfile3D crawler{.heightCells = 1, .maxStepUpCells = 0, .maxFallCells = 0};
    EXPECT_TRUE(volume->isStandable({1, 1, 1}, crawler));
    EXPECT_FALSE(volume->isStandable({1, 2, 1}, crawler));
}

TEST(NavigationVolume3DTest, AnInvalidAgentProfileIsNeverStandableAnywhere)
{
    const auto volume = makeFlatVolume(3, 3, 3);
    ASSERT_TRUE(volume.has_value());

    const NavigationAgentProfile3D zeroHeight{.heightCells = 0};
    EXPECT_FALSE(isValidNavigationAgentProfile3D(zeroHeight));
    EXPECT_FALSE(volume->isStandable({1, 1, 1}, zeroHeight));
    EXPECT_FALSE(volume->hasClearance({1, 1, 1}, zeroHeight));

    const NavigationAgentProfile3D tooTall{
        .heightCells = NavigationVolume3DContract::MaximumAgentHeightCells + 1};
    EXPECT_FALSE(isValidNavigationAgentProfile3D(tooTall));
    EXPECT_FALSE(volume->isStandable({1, 1, 1}, tooTall));
}

// The boundary values the contract advertises must actually be usable.
TEST(NavigationVolume3DTest, AgentProfileBoundaryValuesAreAccepted)
{
    EXPECT_TRUE(isValidNavigationAgentProfile3D({
        .heightCells = NavigationVolume3DContract::MinimumAgentHeightCells,
        .maxStepUpCells = 0,
        .maxFallCells = 0,
    }));
    EXPECT_TRUE(isValidNavigationAgentProfile3D({
        .heightCells = NavigationVolume3DContract::MaximumAgentHeightCells,
        .maxStepUpCells = NavigationVolume3DContract::MaximumAgentStepUpCells,
        .maxFallCells = NavigationVolume3DContract::MaximumAgentFallCells,
    }));
    EXPECT_FALSE(isValidNavigationAgentProfile3D({
        .heightCells = 2,
        .maxStepUpCells = NavigationVolume3DContract::MaximumAgentStepUpCells + 1,
    }));
    EXPECT_FALSE(isValidNavigationAgentProfile3D({
        .heightCells = 2,
        .maxFallCells = NavigationVolume3DContract::MaximumAgentFallCells + 1,
    }));
}

// ADR 0048 D8: a blocker makes cells solid, so placing one ADDS the standable cell on top
// of it. This is the behaviour a voxel world needs and the reason revision must advance.
TEST(NavigationVolume3DTest, ABlockerObstructsItsOwnCellAndSupportsTheCellAbove)
{
    auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());
    const Core::u64 initialRevision = volume->revision();

    EXPECT_TRUE(volume->isStandable({2, 1, 2}, WalkerProfile));
    EXPECT_FALSE(volume->isStandable({2, 2, 2}, WalkerProfile));

    const auto blocker = volume->addBlocker({.x = 2, .y = 1, .z = 2,
                                             .width = 1, .height = 1, .depth = 1});
    ASSERT_TRUE(blocker.has_value());
    EXPECT_GT(volume->revision(), initialRevision);
    EXPECT_FALSE(volume->isStandable({2, 1, 2}, WalkerProfile));
    EXPECT_TRUE(volume->isStandable({2, 2, 2}, WalkerProfile));

    ASSERT_TRUE(volume->removeBlocker(*blocker).has_value());
    EXPECT_TRUE(volume->isStandable({2, 1, 2}, WalkerProfile));
    EXPECT_FALSE(volume->isStandable({2, 2, 2}, WalkerProfile));
}

TEST(NavigationVolume3DTest, OverlappingBlockersUseReferenceCountsSoOneRemovalDoesNotClear)
{
    auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());

    const auto first = volume->addBlocker({.x = 1, .y = 1, .z = 1,
                                           .width = 2, .height = 1, .depth = 2});
    const auto second = volume->addBlocker({.x = 2, .y = 1, .z = 2,
                                            .width = 2, .height = 1, .depth = 2});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(volume->dynamicBlockerCountAt({2, 1, 2}), 2U);

    ASSERT_TRUE(volume->removeBlocker(*first).has_value());
    EXPECT_EQ(volume->dynamicBlockerCountAt({2, 1, 2}), 1U);
    EXPECT_TRUE(volume->isSolid({2, 1, 2}));
    EXPECT_FALSE(volume->isSolid({1, 1, 1}));
}

TEST(NavigationVolume3DTest, RejectsBlockerBoxesThatAreEmptyOrLeaveTheVolume)
{
    auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());

    const auto empty = volume->addBlocker({.x = 0, .y = 0, .z = 0,
                                           .width = 0, .height = 1, .depth = 1});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, Navigation3DErrorCode::InvalidCell);

    const auto overhang = volume->addBlocker({.x = 3, .y = 0, .z = 0,
                                              .width = 2, .height = 1, .depth = 1});
    ASSERT_FALSE(overhang.has_value());
    EXPECT_EQ(overhang.error().code, Navigation3DErrorCode::InvalidCell);

    // The exact-fit box at the far corner must be accepted: an advertised bound that is
    // off by one silently costs the last cell.
    const auto exactFit = volume->addBlocker({.x = 3, .y = 3, .z = 3,
                                              .width = 1, .height = 1, .depth = 1});
    EXPECT_TRUE(exactFit.has_value());
}

TEST(NavigationVolume3DTest, StaleBlockerIdsAreRejectedRatherThanAffectingAReusedSlot)
{
    auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());

    const auto blocker = volume->addBlocker({.x = 1, .y = 1, .z = 1,
                                             .width = 1, .height = 1, .depth = 1});
    ASSERT_TRUE(blocker.has_value());
    ASSERT_TRUE(volume->removeBlocker(*blocker).has_value());

    EXPECT_FALSE(volume->containsBlocker(*blocker));
    EXPECT_FALSE(volume->blockerBox(*blocker).has_value());
    const auto staleUpdate = volume->updateBlocker(*blocker, {.x = 2, .y = 1, .z = 2,
                                                              .width = 1, .height = 1, .depth = 1});
    ASSERT_FALSE(staleUpdate.has_value());
    EXPECT_EQ(staleUpdate.error().code, Navigation3DErrorCode::InvalidBlocker);
    const auto staleRemove = volume->removeBlocker(*blocker);
    ASSERT_FALSE(staleRemove.has_value());
    EXPECT_EQ(staleRemove.error().code, Navigation3DErrorCode::InvalidBlocker);
}

TEST(NavigationVolume3DTest, UpdatingABlockerToTheSameBoxDoesNotAdvanceRevision)
{
    auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());
    const NavigationCellBox3D box{.x = 1, .y = 1, .z = 1, .width = 1, .height = 1, .depth = 1};
    const auto blocker = volume->addBlocker(box);
    ASSERT_TRUE(blocker.has_value());
    const Core::u64 revision = volume->revision();

    ASSERT_TRUE(volume->updateBlocker(*blocker, box).has_value());
    EXPECT_EQ(volume->revision(), revision);
}

TEST(NavigationVolume3DTest, ExhaustedBlockerCapacityFailsWithoutChangingOccupancy)
{
    VolumeBuilder builder(4, 4, 4);
    auto volume = builder.solidLayer(0).build({}, 1.0F, 1);
    ASSERT_TRUE(volume.has_value());

    const auto first = volume->addBlocker({.x = 0, .y = 1, .z = 0,
                                           .width = 1, .height = 1, .depth = 1});
    ASSERT_TRUE(first.has_value());
    const Core::u64 revision = volume->revision();

    const auto second = volume->addBlocker({.x = 1, .y = 1, .z = 1,
                                            .width = 1, .height = 1, .depth = 1});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Navigation3DErrorCode::CapacityExceeded);
    EXPECT_EQ(volume->revision(), revision);
    EXPECT_FALSE(volume->isSolid({1, 1, 1}));
}

TEST(NavigationVolume3DTest, WorldPositionsRoundTripThroughCellCentersOnAHalfOpenBox)
{
    VolumeBuilder builder(4, 4, 4);
    const auto volume = builder.solidLayer(0).build({.x = -8.0F, .y = 2.0F, .z = 5.0F}, 2.0F);
    ASSERT_TRUE(volume.has_value());

    // Lower faces included, upper faces excluded.
    EXPECT_EQ(volume->worldToCell({-8.0F, 2.0F, 5.0F}), (NavigationCell3D{0, 0, 0}));
    EXPECT_EQ(volume->worldToCell({-6.001F, 2.0F, 5.0F}), (NavigationCell3D{0, 0, 0}));
    EXPECT_EQ(volume->worldToCell({-6.0F, 2.0F, 5.0F}), (NavigationCell3D{1, 0, 0}));
    EXPECT_FALSE(volume->worldToCell({-8.001F, 2.0F, 5.0F}).has_value());
    EXPECT_FALSE(volume->worldToCell({0.0F, 2.0F, 5.0F}).has_value());

    const NavigationCell3D cell{2, 1, 3};
    const auto center = volume->cellCenter(cell);
    ASSERT_TRUE(center.has_value());
    EXPECT_EQ(volume->worldToCell(*center), cell);
    EXPECT_FALSE(volume->cellCenter({4, 0, 0}).has_value());
}

TEST(NavigationVolume3DTest, NonFiniteWorldPositionsAreRejected)
{
    const auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());
    const float infinity = std::numeric_limits<float>::infinity();
    const float notANumber = std::numeric_limits<float>::quiet_NaN();

    EXPECT_FALSE(volume->worldToCell({infinity, 1.0F, 1.0F}).has_value());
    EXPECT_FALSE(volume->worldToCell({1.0F, notANumber, 1.0F}).has_value());
    EXPECT_FALSE(volume->worldToCell({1.0F, 1.0F, -infinity}).has_value());
}

TEST(NavigationVolume3DTest, VolumeDataCreationReportsAllocationFailureRatherThanTerminating)
{
    std::vector<Core::u8> flags(64, 0);
    std::vector<Core::u8> costs(64, 1);
    for (Core::usize limit = 0; limit < 4; ++limit)
    {
        TestSupport::FailAfterMemoryResource memory{limit};
        const auto data = NavigationVolume3DData::Create({
            .widthCells = 4, .heightCells = 4, .depthCells = 4,
            .cellFlags = flags, .traversalCosts = costs,
        }, memory);
        if (!data.has_value())
        {
            EXPECT_EQ(data.error().code, Navigation3DErrorCode::AllocationFailed);
            EXPECT_EQ(memory.liveBytes(), 0U) << "failed create leaked at limit " << limit;
        }
    }
}

TEST(NavigationVolume3DTest, SteadyStateQueriesDoNotAllocate)
{
    TestSupport::SealedMemoryResource memory;
    auto volume = makeFlatVolume(8, 8, 8, memory);
    ASSERT_TRUE(volume.has_value());
    const auto blocker = volume->addBlocker({.x = 1, .y = 1, .z = 1,
                                             .width = 2, .height = 2, .depth = 2});
    ASSERT_TRUE(blocker.has_value());
    memory.seal();

    for (Core::u32 z = 0; z < 8; ++z)
    {
        for (Core::u32 x = 0; x < 8; ++x)
        {
            (void)volume->isStandable({x, 1, z}, WalkerProfile);
            (void)volume->traversalCostAt({x, 1, z});
        }
    }
    ASSERT_TRUE(volume->updateBlocker(*blocker, {.x = 3, .y = 1, .z = 3,
                                                 .width = 2, .height = 2, .depth = 2}).has_value());
    ASSERT_TRUE(volume->removeBlocker(*blocker).has_value());
}

} // namespace
} // namespace Tina::Navigation3D
