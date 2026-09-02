#include <tina/editor/EditorMarqueeSelection.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>

namespace Tina::Editor {
namespace {

TEST(EditorMarqueeSelectionTests, ReplacePublishesOrderedIntersectionDiff)
{
    const std::array candidates{
        EditorMarqueeCandidate{.stableId = 30,
                               .screenBounds = {40.0F, 40.0F, 80.0F, 80.0F}},
        EditorMarqueeCandidate{.stableId = 10,
                               .screenBounds = {10.0F, 10.0F, 20.0F, 20.0F}},
        EditorMarqueeCandidate{.stableId = 20,
                               .screenBounds = {25.0F, 25.0F, 35.0F, 35.0F}},
    };
    const std::array<Core::u64, 2> current{40, 20};

    auto selection = EditorMarqueeSelection::Evaluate(
        {70.0F, 70.0F, 25.0F, 25.0F}, candidates, current,
        EditorMarqueeSelectionMode::Replace);
    ASSERT_TRUE(selection) << (selection ? "" : selection.error().message);
    const std::array<Core::u64, 2> expectedSelection{20, 30};
    const std::array<Core::u64, 1> expectedAdded{30};
    const std::array<Core::u64, 1> expectedRemoved{40};
    EXPECT_TRUE(std::ranges::equal(selection->selection(), expectedSelection));
    EXPECT_TRUE(std::ranges::equal(selection->added(), expectedAdded));
    EXPECT_TRUE(std::ranges::equal(selection->removed(), expectedRemoved));
    EXPECT_TRUE(selection->changed());
}

TEST(EditorMarqueeSelectionTests, ShiftAddsAndControlTogglesSelection)
{
    const std::array candidates{
        EditorMarqueeCandidate{.stableId = 3,
                               .screenBounds = {0.0F, 0.0F, 4.0F, 4.0F}},
        EditorMarqueeCandidate{.stableId = 2,
                               .screenBounds = {2.0F, 2.0F, 6.0F, 6.0F}},
    };
    const std::array<Core::u64, 2> current{1, 2};

    auto added = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 6.0F, 6.0F}, candidates, current,
        editorMarqueeSelectionMode({.shift = true}));
    ASSERT_TRUE(added);
    const std::array<Core::u64, 3> expectedAddedSelection{1, 2, 3};
    const std::array<Core::u64, 1> expectedAdded{3};
    EXPECT_TRUE(
        std::ranges::equal(added->selection(), expectedAddedSelection));
    EXPECT_TRUE(std::ranges::equal(added->added(), expectedAdded));
    EXPECT_TRUE(added->removed().empty());

    auto toggled = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 6.0F, 6.0F}, candidates, current,
        editorMarqueeSelectionMode({.shift = true, .control = true}));
    ASSERT_TRUE(toggled);
    const std::array<Core::u64, 2> expectedToggledSelection{1, 3};
    const std::array<Core::u64, 1> expectedToggledAdded{3};
    const std::array<Core::u64, 1> expectedToggledRemoved{2};
    EXPECT_TRUE(
        std::ranges::equal(toggled->selection(), expectedToggledSelection));
    EXPECT_TRUE(
        std::ranges::equal(toggled->added(), expectedToggledAdded));
    EXPECT_TRUE(
        std::ranges::equal(toggled->removed(), expectedToggledRemoved));
}

TEST(EditorMarqueeSelectionTests, EmptyHitReplaceClearsAndEmptyHitAddIsStable)
{
    const std::array candidates{
        EditorMarqueeCandidate{.stableId = 7,
                               .screenBounds = {50.0F, 50.0F, 60.0F, 60.0F}},
    };
    const std::array<Core::u64, 2> current{9, 4};

    auto cleared = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 10.0F, 10.0F}, candidates, current,
        EditorMarqueeSelectionMode::Replace);
    ASSERT_TRUE(cleared);
    const std::array<Core::u64, 2> expectedRemoved{4, 9};
    EXPECT_TRUE(cleared->selection().empty());
    EXPECT_TRUE(std::ranges::equal(cleared->removed(), expectedRemoved));

    auto stable = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 10.0F, 10.0F}, candidates, current,
        EditorMarqueeSelectionMode::Add);
    ASSERT_TRUE(stable);
    const std::array<Core::u64, 2> expectedSelection{4, 9};
    EXPECT_TRUE(std::ranges::equal(stable->selection(), expectedSelection));
    EXPECT_FALSE(stable->changed());
}

TEST(EditorMarqueeSelectionTests, RejectsInvalidOrDuplicateInput)
{
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const std::array candidates{
        EditorMarqueeCandidate{.stableId = 1,
                               .screenBounds = {0.0F, 0.0F, 1.0F, 1.0F}},
        EditorMarqueeCandidate{.stableId = 1,
                               .screenBounds = {2.0F, 2.0F, 3.0F, 3.0F}},
    };
    auto duplicate = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 4.0F, 4.0F}, candidates, {},
        EditorMarqueeSelectionMode::Replace);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, EditorErrorCode::InvalidConfiguration);

    auto invalidRect = EditorMarqueeSelection::Evaluate(
        {nan, 0.0F, 4.0F, 4.0F}, {}, {},
        EditorMarqueeSelectionMode::Replace);
    ASSERT_FALSE(invalidRect);
    EXPECT_EQ(invalidRect.error().code,
              EditorErrorCode::InvalidConfiguration);

    const std::array zeroCandidate{
        EditorMarqueeCandidate{.stableId = 0,
                               .screenBounds = {0.0F, 0.0F, 1.0F, 1.0F}},
    };
    auto zeroStableId = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 4.0F, 4.0F}, zeroCandidate, {},
        EditorMarqueeSelectionMode::Replace);
    ASSERT_FALSE(zeroStableId);
    EXPECT_EQ(zeroStableId.error().code,
              EditorErrorCode::InvalidConfiguration);
}

TEST(EditorMarqueeSelectionTests, RejectsSelectionUnionBeyondFixedCapacity)
{
    std::array<Core::u64, EditorMarqueeSelectionCapacity> current{};
    for (Core::usize index = 0; index < current.size(); ++index) {
        current[index] = static_cast<Core::u64>(index + 1U);
    }
    const std::array candidates{
        EditorMarqueeCandidate{
            .stableId = static_cast<Core::u64>(EditorMarqueeSelectionCapacity + 1U),
            .screenBounds = {0.0F, 0.0F, 1.0F, 1.0F}},
    };

    auto exhausted = EditorMarqueeSelection::Evaluate(
        {0.0F, 0.0F, 1.0F, 1.0F}, candidates, current,
        EditorMarqueeSelectionMode::Add);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code,
              EditorErrorCode::DocumentCapacityExceeded);
}

} // namespace
} // namespace Tina::Editor
