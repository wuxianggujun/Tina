#include "IosCompositionSession.hpp"

#include <gtest/gtest.h>

namespace Tina::Platform::Detail {
namespace {

[[nodiscard]] IosCompositionEvent marked(std::u16string_view utf16, i32 cursor = 0) noexcept
{
    IosCompositionEvent event{};
    (void)makeIosCompositionEventFromUtf16(utf16, cursor, IosCompositionAction::SetMarkedText, event);
    return event;
}

[[nodiscard]] IosCompositionEvent unmark() noexcept
{
    IosCompositionEvent event{};
    event.action = IosCompositionAction::Unmark;
    return event;
}

[[nodiscard]] IosCompositionEvent commit(std::u16string_view utf16) noexcept
{
    IosCompositionEvent event{};
    (void)makeIosCompositionEventFromUtf16(utf16, 0, IosCompositionAction::Commit, event);
    return event;
}

TEST(IosCompositionSessionTest, FirstMarkedTextStartsAndLaterOnesUpdate)
{
    IosCompositionSession session;
    EXPECT_FALSE(session.active());

    const IosCompositionOutcome started = session.apply(marked(u"ni", 2));
    ASSERT_TRUE(started.stage.has_value());
    EXPECT_EQ(*started.stage, TextCompositionStage::Started);
    EXPECT_EQ(started.preeditUtf8, "ni");
    EXPECT_EQ(started.cursorCodepoint, 2U);
    EXPECT_TRUE(session.active());

    const IosCompositionOutcome updated = session.apply(marked(u"nihao", 5));
    ASSERT_TRUE(updated.stage.has_value());
    EXPECT_EQ(*updated.stage, TextCompositionStage::Updated);
    EXPECT_EQ(updated.preeditUtf8, "nihao");
    EXPECT_TRUE(updated.committedUtf8.empty());
}

TEST(IosCompositionSessionTest, ACommitEndsTheCompositionBeforeExposingItsText)
{
    IosCompositionSession session;
    (void)session.apply(marked(u"nihao", 5));

    const IosCompositionOutcome outcome = session.apply(commit(u"你好"));
    ASSERT_TRUE(outcome.stage.has_value());
    EXPECT_EQ(*outcome.stage, TextCompositionStage::Ended);
    EXPECT_TRUE(outcome.preeditUtf8.empty()) << "Ended carries no preedit";
    EXPECT_EQ(outcome.committedUtf8, "你好");
    EXPECT_FALSE(session.active());
}

TEST(IosCompositionSessionTest, ACommitWithNoMarkedTextPublishesTextAlone)
{
    IosCompositionSession session;
    const IosCompositionOutcome outcome = session.apply(commit(u"hi"));
    EXPECT_FALSE(outcome.stage.has_value()) << "inventing Ended would announce a composition that never started";
    EXPECT_EQ(outcome.committedUtf8, "hi");
    EXPECT_FALSE(session.active());
}

TEST(IosCompositionSessionTest, AnEmptiedMarkedRegionCancelsRatherThanEnds)
{
    IosCompositionSession session;
    (void)session.apply(marked(u"ni", 2));

    const IosCompositionOutcome outcome = session.apply(marked(u""));
    ASSERT_TRUE(outcome.stage.has_value());
    EXPECT_EQ(*outcome.stage, TextCompositionStage::Cancelled);
    EXPECT_TRUE(outcome.committedUtf8.empty());
    EXPECT_FALSE(session.active());
}

TEST(IosCompositionSessionTest, UnmarkCancelsAnActivePassAndIsSilentOtherwise)
{
    IosCompositionSession session;
    EXPECT_FALSE(session.apply(unmark()).stage.has_value())
        << "UIKit calls unmarkText on every focus change; that must not emit a stage";

    (void)session.apply(marked(u"ni", 2));
    const IosCompositionOutcome outcome = session.apply(unmark());
    ASSERT_TRUE(outcome.stage.has_value());
    EXPECT_EQ(*outcome.stage, TextCompositionStage::Cancelled);
    EXPECT_FALSE(session.active());
}

TEST(IosCompositionSessionTest, ClearingWithNothingInFlightPublishesNothing)
{
    IosCompositionSession session;
    EXPECT_FALSE(session.apply(marked(u"")).stage.has_value());
    EXPECT_FALSE(session.apply(unmark()).stage.has_value());
    EXPECT_FALSE(session.active());
}

TEST(IosCompositionSessionTest, CancelClearsAnActivePassAndIsIdempotent)
{
    IosCompositionSession session;
    EXPECT_FALSE(session.cancel().has_value());

    (void)session.apply(marked(u"hao", 3));
    const auto cancelled = session.cancel();
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(*cancelled->stage, TextCompositionStage::Cancelled);
    EXPECT_FALSE(session.active());
    EXPECT_FALSE(session.cancel().has_value());
}

TEST(IosCompositionSessionTest, AfterCancelTheNextMarkedTextStartsANewPass)
{
    IosCompositionSession session;
    (void)session.apply(marked(u"ni", 2));
    (void)session.cancel();

    const IosCompositionOutcome restarted = session.apply(marked(u"hao", 3));
    ASSERT_TRUE(restarted.stage.has_value());
    EXPECT_EQ(*restarted.stage, TextCompositionStage::Started);
}

} // namespace
} // namespace Tina::Platform::Detail
