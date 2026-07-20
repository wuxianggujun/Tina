#include <gtest/gtest.h>

#include "Imm32CompositionSession.hpp"

namespace Tina::Tests {

TEST(Imm32CompositionSessionTests, UpdateStartsThenUpdatesAndEndsWithCommit)
{
    Platform::Detail::Imm32CompositionSession session;
    EXPECT_FALSE(session.active());

    auto started = session.updatePreedit("ni", 2);
    ASSERT_TRUE(started.has_value()) << (started ? "" : started.error().message);
    EXPECT_EQ(started->stage, Platform::TextCompositionStage::Started);
    EXPECT_EQ(started->preeditUtf8, "ni");
    EXPECT_EQ(started->cursorCodepoint, 2U);
    EXPECT_TRUE(session.active());

    auto updated = session.updatePreedit("你", 1);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->stage, Platform::TextCompositionStage::Updated);
    EXPECT_EQ(updated->preeditUtf8, "你");
    EXPECT_EQ(updated->cursorCodepoint, 1U);

    auto ended = session.end("你好");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->stage, Platform::TextCompositionStage::Ended);
    EXPECT_EQ(ended->committedUtf8, "你好");
    EXPECT_TRUE(ended->preeditUtf8.empty());
    EXPECT_FALSE(session.active());
}

TEST(Imm32CompositionSessionTests, CancelAndFocusLostClearActiveSession)
{
    Platform::Detail::Imm32CompositionSession session;
    ASSERT_TRUE(session.updatePreedit("hao", 3).has_value());
    EXPECT_TRUE(session.active());

    auto cancelled = session.cancel();
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->stage, Platform::TextCompositionStage::Cancelled);
    EXPECT_FALSE(session.active());
    EXPECT_FALSE(session.cancel().has_value());

    ASSERT_TRUE(session.updatePreedit("x", 1).has_value());
    auto lost = session.onFocusLost();
    ASSERT_TRUE(lost.has_value());
    EXPECT_EQ(lost->stage, Platform::TextCompositionStage::Cancelled);
    EXPECT_FALSE(session.active());
}

TEST(Imm32CompositionSessionTests, RejectsInvalidUtf8AndCapacity)
{
    Platform::Detail::Imm32CompositionSession session;
    const auto invalid = session.updatePreedit(std::string_view("\xC3\x28", 2), 0);
    ASSERT_FALSE(invalid.has_value());

    std::string oversized(Platform::Detail::DefaultImePreeditByteCapacity + 1, 'a');
    const auto capacity = session.updatePreedit(oversized, 0);
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, Core::CoreErrorCode::CapacityExceeded);
}

TEST(Imm32CompositionSessionTests, EncodeUtf16ToUtf8HandlesBmpAndSurrogate)
{
    std::array<char, 32> out{};
    const char16_t ascii[] = {u'A', u'B'};
    auto n = Platform::Detail::encodeUtf16ToUtf8(ascii, out);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 2U);
    EXPECT_EQ(std::string_view(out.data(), *n), "AB");

    // U+1F600 😀 as surrogate pair
    const char16_t emoji[] = {0xD83D, 0xDE00};
    n = Platform::Detail::encodeUtf16ToUtf8(emoji, out);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 4U);

    // Lone low surrogate is invalid.
    const char16_t bad[] = {0xDE00};
    EXPECT_FALSE(Platform::Detail::encodeUtf16ToUtf8(bad, out).has_value());
}

} // namespace Tina::Tests
