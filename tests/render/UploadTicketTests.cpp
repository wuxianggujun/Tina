#include <gtest/gtest.h>

#include <tina/render/RenderErrors.hpp>
#include <tina/render/UploadTicket.hpp>

#include <array>
#include <memory_resource>

namespace Tina::Tests {

TEST(NullUploadLedgerTest, SubmitPollRetireLifecycle)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto ledger = Render::NullUploadLedger::Create(Render::UploadLedgerConfig{.capacity = 2, .memoryResource = &memory});
    ASSERT_TRUE(ledger.has_value()) << ledger.error().message;

    constexpr std::array<std::byte, 4> Bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto ticket = ledger->submit(Render::UploadSubmitParams{.bytes = Bytes, .userTag = 42});
    ASSERT_TRUE(ticket.has_value()) << ticket.error().message;
    EXPECT_EQ(ledger->state(*ticket), Render::UploadTicketState::Pending);
    EXPECT_EQ(ledger->pendingCount(), 1U);
    EXPECT_EQ(ledger->staging(*ticket).size(), 4U);
    EXPECT_EQ(ledger->userTag(*ticket), 42U);

    ASSERT_TRUE(ledger->poll(*ticket).has_value());
    EXPECT_EQ(ledger->state(*ticket), Render::UploadTicketState::Ready);
    EXPECT_EQ(ledger->readyCount(), 1U);
    EXPECT_EQ(ledger->pendingCount(), 0U);

    ASSERT_TRUE(ledger->retire(*ticket).has_value());
    EXPECT_EQ(ledger->state(*ticket), Render::UploadTicketState::Invalid);
    EXPECT_EQ(ledger->liveCount(), 0U);
    EXPECT_TRUE(ledger->staging(*ticket).empty());
}

TEST(NullUploadLedgerTest, RejectsRetireWhilePendingAndCapacity)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto ledger = Render::NullUploadLedger::Create(Render::UploadLedgerConfig{.capacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(ledger.has_value());

    constexpr std::array<std::byte, 1> A{std::byte{9}};
    auto first = ledger->submit(Render::UploadSubmitParams{.bytes = A});
    ASSERT_TRUE(first.has_value());
    auto retirePending = ledger->retire(*first);
    ASSERT_FALSE(retirePending.has_value());
    EXPECT_EQ(retirePending.error().code, Render::RenderErrorCode::UploadTicketNotRetirable);

    constexpr std::array<std::byte, 1> B{std::byte{8}};
    auto second = ledger->submit(Render::UploadSubmitParams{.bytes = B});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Render::RenderErrorCode::UploadLedgerFull);

    ASSERT_TRUE(ledger->poll(*first).has_value());
    ASSERT_TRUE(ledger->retire(*first).has_value());
    auto again = ledger->submit(Render::UploadSubmitParams{.bytes = B});
    ASSERT_TRUE(again.has_value());
    EXPECT_NE(*again, *first);
}

} // namespace Tina::Tests
