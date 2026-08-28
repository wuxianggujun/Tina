#include "GlfwDigitalFocusFilter.hpp"

#include <gtest/gtest.h>

namespace Tina::Platform::Detail {
namespace {

[[nodiscard]] WindowInputSnapshot heldInput()
{
    WindowInputSnapshot input;
    input.heldKeys.set(static_cast<usize>(Key::A));
    input.pointers[Platform::PrimaryPointerId].heldButtons.set(static_cast<usize>(PointerButton::Primary));
    return input;
}

TEST(GlfwDigitalFocusFilterTests, FocusCancelSuppressesSyntheticDigitalReleases)
{
    GlfwDigitalFocusFilter filter;
    filter.reset(true);
    const WindowInputSnapshot input = heldInput();

    filter.onFocusLost(input);

    EXPECT_FALSE(filter.shouldAccept(Key::A, DigitalTransition::Up));
    EXPECT_FALSE(filter.shouldAccept(PointerButton::Primary, DigitalTransition::Up));
    EXPECT_FALSE(filter.shouldAccept(Key::B, DigitalTransition::Down));
}

TEST(GlfwDigitalFocusFilterTests, GenuineInputResumesAfterFocusReturns)
{
    GlfwDigitalFocusFilter filter;
    filter.reset(true);
    filter.onFocusLost(heldInput());
    filter.onFocusGained();

    EXPECT_TRUE(filter.shouldAccept(Key::B, DigitalTransition::Down));
    EXPECT_TRUE(filter.shouldAccept(Key::B, DigitalTransition::Up));
    EXPECT_TRUE(filter.shouldAccept(PointerButton::Secondary, DigitalTransition::Down));
    EXPECT_TRUE(filter.shouldAccept(PointerButton::Secondary, DigitalTransition::Up));
}

TEST(GlfwDigitalFocusFilterTests, NewPressSupersedesAStaleReleaseMask)
{
    GlfwDigitalFocusFilter filter;
    filter.reset(true);
    filter.onFocusLost(heldInput());
    filter.onFocusGained();

    EXPECT_TRUE(filter.shouldAccept(Key::A, DigitalTransition::Down));
    EXPECT_TRUE(filter.shouldAccept(Key::A, DigitalTransition::Up));
    EXPECT_TRUE(filter.shouldAccept(PointerButton::Primary, DigitalTransition::Down));
    EXPECT_TRUE(filter.shouldAccept(PointerButton::Primary, DigitalTransition::Up));
}

TEST(GlfwDigitalFocusFilterTests, RejectsInvalidDigitalEnumsWithoutThrowing)
{
    GlfwDigitalFocusFilter filter;
    filter.reset(true);

    EXPECT_FALSE(filter.shouldAccept(static_cast<Key>(255), DigitalTransition::Down));
    EXPECT_FALSE(filter.shouldAccept(static_cast<PointerButton>(255), DigitalTransition::Down));
}

} // namespace
} // namespace Tina::Platform::Detail
