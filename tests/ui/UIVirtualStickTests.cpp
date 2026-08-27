#include <tina/ui/UIVirtualStick.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace Tina::UI {
namespace {

// Radii chosen so travel is exactly 40, which makes the expected offsets in these
// tests readable rather than derived.
[[nodiscard]] UIVirtualStickConfig config(float deadzone = 0.0F)
{
    return UIVirtualStickConfig{
        .baseRadius = 60.0F,
        .knobRadius = 20.0F,
        .deadzone = deadzone,
    };
}

constexpr UILogicalPoint Center{.x = 100.0F, .y = 200.0F};

TEST(UIVirtualStickTests, TravelKeepsTheKnobInsideTheBase)
{
    // The knob must not hang over the ring: travel is the difference of the radii,
    // not the base radius. Letting the knob centre reach the edge is what makes a
    // stick look broken at full deflection.
    EXPECT_FLOAT_EQ(virtualStickTravelRadius(config()), 40.0F);
    EXPECT_FLOAT_EQ(virtualStickTravelRadius(UIVirtualStickConfig{}), 32.0F);

    // A knob that cannot travel is rejected rather than dividing by zero later.
    EXPECT_FALSE(isValidVirtualStickConfig(
        UIVirtualStickConfig{.baseRadius = 20.0F, .knobRadius = 20.0F}));
    EXPECT_FALSE(isValidVirtualStickConfig(
        UIVirtualStickConfig{.baseRadius = 60.0F, .knobRadius = 20.0F, .deadzone = 0.95F}));
}

// A circular hit test, not the Element rectangle. With a rectangle, pressing a
// corner of the bounding box would steer the character from outside the visible
// ring.
TEST(UIVirtualStickTests, PressEngagesOnlyInsideTheBaseDisc)
{
    UIVirtualStickState state{};
    // Just inside the corner of the bounding box but outside the disc:
    // (42, 42) is at distance ~59.4 -- inside. (44, 44) is ~62.2 -- outside.
    EXPECT_FALSE(virtualStickContains(config(), Center,
                                      UILogicalPoint{.x = Center.x + 44.0F, .y = Center.y + 44.0F}));
    EXPECT_TRUE(virtualStickContains(config(), Center,
                                     UILogicalPoint{.x = Center.x + 42.0F, .y = Center.y + 42.0F}));

    EXPECT_FALSE(pressVirtualStick(state, config(), Center, 0U,
                                   UILogicalPoint{.x = Center.x + 44.0F, .y = Center.y + 44.0F}));
    EXPECT_FALSE(state.engaged);

    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 7U,
                                  UILogicalPoint{.x = Center.x + 40.0F, .y = Center.y}));
    EXPECT_TRUE(state.engaged);
    EXPECT_EQ(state.pointer, 7U);
    EXPECT_FLOAT_EQ(state.x, 1.0F);
    EXPECT_FLOAT_EQ(state.magnitude, 1.0F);
}

// cocos2d-x's ControlSaturationBrightnessPicker freezes the knob when the finger
// leaves the base -- its move handler early-returns past the radius, and the fix is
// commented out in its source. A frozen stick reads as stuck input, so dragging
// outward must clamp to the ring and hold full deflection instead.
TEST(UIVirtualStickTests, DraggingOutsideClampsToTheRingInsteadOfFreezing)
{
    UIVirtualStickState state{};
    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 0U,
                                  UILogicalPoint{.x = Center.x + 10.0F, .y = Center.y}));
    EXPECT_FLOAT_EQ(state.knobOffset.x, 10.0F);

    // Far outside the base, not merely outside the travel radius.
    ASSERT_TRUE(dragVirtualStick(state, config(), Center, 0U,
                                 UILogicalPoint{.x = Center.x + 500.0F, .y = Center.y}));
    EXPECT_TRUE(state.engaged);
    EXPECT_FLOAT_EQ(state.knobOffset.x, 40.0F);
    EXPECT_FLOAT_EQ(state.knobOffset.y, 0.0F);
    EXPECT_FLOAT_EQ(state.x, 1.0F);
    EXPECT_FLOAT_EQ(state.magnitude, 1.0F);

    // And it keeps tracking direction out there rather than latching the last
    // in-circle vector.
    ASSERT_TRUE(dragVirtualStick(state, config(), Center, 0U,
                                 UILogicalPoint{.x = Center.x, .y = Center.y - 500.0F}));
    EXPECT_FLOAT_EQ(state.knobOffset.y, -40.0F);
    EXPECT_FLOAT_EQ(state.y, -1.0F);
}

// A diagonal must not be faster than a cardinal push. Clamping each axis
// independently would give a magnitude of sqrt(2) at the corner.
TEST(UIVirtualStickTests, DiagonalDeflectionIsNotFasterThanCardinal)
{
    UIVirtualStickState state{};
    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 0U, Center));
    ASSERT_TRUE(dragVirtualStick(state, config(), Center, 0U,
                                 UILogicalPoint{.x = Center.x + 300.0F, .y = Center.y + 300.0F}));

    EXPECT_FLOAT_EQ(state.magnitude, 1.0F);
    EXPECT_NEAR(std::sqrt(state.x * state.x + state.y * state.y), 1.0F, 1.0e-5F);
    const float diagonal = 1.0F / std::sqrt(2.0F);
    EXPECT_NEAR(state.x, diagonal, 1.0e-5F);
    EXPECT_NEAR(state.y, diagonal, 1.0e-5F);
    // The knob also stays on the ring, not at the corner of a square.
    EXPECT_NEAR(std::sqrt(state.knobOffset.x * state.knobOffset.x
                          + state.knobOffset.y * state.knobOffset.y),
                40.0F, 1.0e-4F);
}

// cocos2d-x has no deadzone anywhere in the engine; raw values reach the game. A
// stick that cannot report true zero makes a character drift. The rescale matches
// the gamepad backend so a finger and a physical stick feel the same.
TEST(UIVirtualStickTests, RadialDeadzoneReportsTrueZeroAndRescalesTheRemainder)
{
    const UIVirtualStickConfig deadzoned = config(0.25F);
    UIVirtualStickState state{};
    ASSERT_TRUE(pressVirtualStick(state, deadzoned, Center, 0U, Center));

    // 8 / 40 = 0.2 normalized, inside the 0.25 deadzone.
    ASSERT_TRUE(dragVirtualStick(state, deadzoned, Center, 0U,
                                 UILogicalPoint{.x = Center.x + 8.0F, .y = Center.y}));
    EXPECT_FLOAT_EQ(state.magnitude, 0.0F);
    EXPECT_FLOAT_EQ(state.x, 0.0F);
    // The knob still follows the finger even where the value reads zero, or the
    // control would look unresponsive near centre.
    EXPECT_FLOAT_EQ(state.knobOffset.x, 8.0F);

    // 20 / 40 = 0.5 normalized -> (0.5 - 0.25) / 0.75.
    ASSERT_TRUE(dragVirtualStick(state, deadzoned, Center, 0U,
                                 UILogicalPoint{.x = Center.x + 20.0F, .y = Center.y}));
    EXPECT_NEAR(state.magnitude, (0.5F - 0.25F) / 0.75F, 1.0e-5F);

    // Full deflection still reaches exactly 1.0 despite the deadzone.
    ASSERT_TRUE(dragVirtualStick(state, deadzoned, Center, 0U,
                                 UILogicalPoint{.x = Center.x + 40.0F, .y = Center.y}));
    EXPECT_FLOAT_EQ(state.magnitude, 1.0F);

    // The deadzone is round, not square: a diagonal just inside the radius is also
    // zero, which a per-axis deadzone would have reported as movement.
    ASSERT_TRUE(dragVirtualStick(state, deadzoned, Center, 0U,
                                 UILogicalPoint{.x = Center.x + 7.0F, .y = Center.y + 7.0F}));
    EXPECT_FLOAT_EQ(state.magnitude, 0.0F);
}

// cocos2d-x's EventListenerTouchOneByOne claims every touch that hits a control, so
// two fingers on one stick both drive the knob and fight over it. The engaging
// pointer owns the stick until it releases.
TEST(UIVirtualStickTests, ASecondPointerCannotStealOrMoveAnEngagedStick)
{
    UIVirtualStickState state{};
    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 3U,
                                  UILogicalPoint{.x = Center.x + 40.0F, .y = Center.y}));
    const UIVirtualStickState owned = state;

    // A second press inside the base is refused and changes nothing.
    EXPECT_FALSE(pressVirtualStick(state, config(), Center, 9U, Center));
    EXPECT_EQ(state, owned);
    // A drag from the wrong pointer is ignored.
    EXPECT_FALSE(dragVirtualStick(state, config(), Center, 9U,
                                  UILogicalPoint{.x = Center.x, .y = Center.y + 40.0F}));
    EXPECT_EQ(state, owned);
    // And so is a release from the wrong pointer -- otherwise lifting an unrelated
    // finger would drop the stick mid-move.
    EXPECT_FALSE(releaseVirtualStick(state, 9U));
    EXPECT_TRUE(state.engaged);

    EXPECT_TRUE(releaseVirtualStick(state, 3U));
    EXPECT_FALSE(state.engaged);
}

// cocos2d-x's pickers have no touch-ended handler at all, so their knob never
// returns to centre. A knob left deflected keeps steering a character nobody is
// touching.
TEST(UIVirtualStickTests, ReleaseAndCancelBothRecentre)
{
    UIVirtualStickState state{};
    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 0U,
                                  UILogicalPoint{.x = Center.x + 40.0F, .y = Center.y}));
    ASSERT_TRUE(releaseVirtualStick(state, 0U));
    EXPECT_EQ(state, UIVirtualStickState{});

    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 0U,
                                  UILogicalPoint{.x = Center.x, .y = Center.y + 40.0F}));
    // Cancel needs no pointer identity: focus loss and device loss do not name one.
    cancelVirtualStick(state);
    EXPECT_EQ(state, UIVirtualStickState{});
    // Releasing an already-released stick is refused rather than asserted.
    EXPECT_FALSE(releaseVirtualStick(state, 0U));
}

// Keys and a finger must produce the same downstream values, or gameplay tuned
// against one feels wrong on the other.
TEST(UIVirtualStickTests, DigitalKeysMatchPointerOutputAndNormalizeDiagonals)
{
    const UIVirtualStickConfig cfg = config(0.25F);

    const UIVirtualStickState idle = virtualStickFromDigital(cfg, false, false, false, false);
    EXPECT_FALSE(idle.engaged);
    EXPECT_FLOAT_EQ(idle.magnitude, 0.0F);

    // Opposite keys cancel instead of one winning by evaluation order.
    EXPECT_FALSE(virtualStickFromDigital(cfg, true, true, false, false).engaged);
    EXPECT_FALSE(virtualStickFromDigital(cfg, false, false, true, true).engaged);

    const UIVirtualStickState right = virtualStickFromDigital(cfg, false, true, false, false);
    EXPECT_TRUE(right.engaged);
    EXPECT_FLOAT_EQ(right.x, 1.0F);
    EXPECT_FLOAT_EQ(right.magnitude, 1.0F);
    // A key is already full deflection, so the deadzone must not shorten it.
    EXPECT_FLOAT_EQ(right.knobOffset.x, 40.0F);

    // Up is negative, matching the pointer path and the gamepad convention.
    const UIVirtualStickState up = virtualStickFromDigital(cfg, false, false, true, false);
    EXPECT_FLOAT_EQ(up.y, -1.0F);
    EXPECT_FLOAT_EQ(up.knobOffset.y, -40.0F);

    // W+D must not be sqrt(2) faster than D alone.
    const UIVirtualStickState upRight = virtualStickFromDigital(cfg, false, true, true, false);
    EXPECT_FLOAT_EQ(upRight.magnitude, 1.0F);
    EXPECT_NEAR(std::sqrt(upRight.x * upRight.x + upRight.y * upRight.y), 1.0F, 1.0e-5F);
    const float diagonal = 1.0F / std::sqrt(2.0F);
    EXPECT_NEAR(upRight.x, diagonal, 1.0e-5F);
    EXPECT_NEAR(upRight.y, -diagonal, 1.0e-5F);

    // And the keyboard diagonal equals the pointer diagonal exactly.
    UIVirtualStickState dragged{};
    ASSERT_TRUE(pressVirtualStick(dragged, cfg, Center, 0U, Center));
    ASSERT_TRUE(dragVirtualStick(dragged, cfg, Center, 0U,
                                 UILogicalPoint{.x = Center.x + 300.0F, .y = Center.y - 300.0F}));
    EXPECT_NEAR(dragged.x, upRight.x, 1.0e-5F);
    EXPECT_NEAR(dragged.y, upRight.y, 1.0e-5F);
    EXPECT_NEAR(dragged.magnitude, upRight.magnitude, 1.0e-5F);
}

// A press exactly on the centre has no direction. Reporting zero is correct and
// avoids normalizing a zero-length vector.
TEST(UIVirtualStickTests, CentrePressAndNonFinitePositionsReportZero)
{
    UIVirtualStickState state{};
    ASSERT_TRUE(pressVirtualStick(state, config(), Center, 0U, Center));
    EXPECT_TRUE(state.engaged);
    EXPECT_FLOAT_EQ(state.magnitude, 0.0F);
    EXPECT_FLOAT_EQ(state.knobOffset.x, 0.0F);
    EXPECT_FLOAT_EQ(state.knobOffset.y, 0.0F);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(virtualStickContains(config(), Center, UILogicalPoint{.x = nan, .y = 0.0F}));
    const UIVirtualStickState garbage =
        resolveVirtualStick(config(), Center, UILogicalPoint{.x = nan, .y = nan});
    EXPECT_FLOAT_EQ(garbage.magnitude, 0.0F);
}

} // namespace
} // namespace Tina::UI
