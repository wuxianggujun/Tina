#include <gtest/gtest.h>

#include "detail/UIImeCompositionState.hpp"

#include <tina/ui/UIErrors.hpp>

#include <string>

namespace Tina::Tests {

TEST(UIImeCompositionStateTests, DefaultsInactiveAndEmpty)
{
    const UI::Detail::UIImeCompositionState state;

    EXPECT_FALSE(state.active());
    EXPECT_TRUE(state.preeditUtf8().empty());
    EXPECT_EQ(state.cursorCodepoint(), 0U);
}

TEST(UIImeCompositionStateTests, AssignStoresPreeditAndClampsCursor)
{
    UI::Detail::UIImeCompositionState state;

    state.assign("Tina", 8, 4);

    EXPECT_TRUE(state.active());
    EXPECT_EQ(state.preeditUtf8(), "Tina");
    EXPECT_EQ(state.cursorCodepoint(), 4U);
}

TEST(UIImeCompositionStateTests, EmptyPreeditCanRemainAnActiveComposition)
{
    UI::Detail::UIImeCompositionState state;

    state.assign({}, 3, 0);

    EXPECT_TRUE(state.active());
    EXPECT_TRUE(state.preeditUtf8().empty());
    EXPECT_EQ(state.cursorCodepoint(), 0U);
}

TEST(UIImeCompositionStateTests, ResetClearsPublishedState)
{
    UI::Detail::UIImeCompositionState state;
    state.assign("input", 2, 5);

    state.reset();

    EXPECT_FALSE(state.active());
    EXPECT_TRUE(state.preeditUtf8().empty());
    EXPECT_EQ(state.cursorCodepoint(), 0U);
}

TEST(UIImeCompositionStateTests, CapacityValidationRejectsOversizeWithoutMutatingState)
{
    UI::Detail::UIImeCompositionState state;
    state.assign("kept", 2, 4);
    const std::string oversized(UI::Detail::UIImeCompositionState::MaximumPreeditBytes + 1, 'x');

    const Core::Status status = UI::Detail::UIImeCompositionState::validateCapacity(oversized);

    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(state.preeditUtf8(), "kept");
    EXPECT_EQ(state.cursorCodepoint(), 2U);
}

TEST(UIImeCompositionStateTests, ValueCopyRestoresWholeTransactionState)
{
    UI::Detail::UIImeCompositionState state;
    state.assign("before", 3, 6);
    const UI::Detail::UIImeCompositionState snapshot = state;

    state.assign("after", 5, 5);
    state = snapshot;

    EXPECT_TRUE(state.active());
    EXPECT_EQ(state.preeditUtf8(), "before");
    EXPECT_EQ(state.cursorCodepoint(), 3U);
}

} // namespace Tina::Tests
