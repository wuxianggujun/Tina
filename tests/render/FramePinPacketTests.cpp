#include <gtest/gtest.h>

#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderErrors.hpp>

#include <atomic>
#include <type_traits>
#include <utility>

namespace Tina::Tests {
namespace {

static_assert(!std::is_copy_constructible_v<Render::SubmissionTicket>);
static_assert(!std::is_copy_assignable_v<Render::SubmissionTicket>);
static_assert(std::is_move_constructible_v<Render::SubmissionTicket>);
static_assert(!std::is_move_assignable_v<Render::SubmissionTicket>);

std::atomic<int> g_releaseCount{0};

void countingRelease(void* userData) noexcept
{
    ++g_releaseCount;
    delete static_cast<int*>(userData);
}

TEST(FramePinTest, ReleaseRunsExactlyOnce)
{
    g_releaseCount.store(0);
    {
        Render::FramePin pin{
            Render::FramePinKind::Custom,
            7,
            new int{42},
            &countingRelease,
        };
        EXPECT_TRUE(pin.hasValue());
        pin.release();
        EXPECT_FALSE(pin.hasValue());
        EXPECT_EQ(g_releaseCount.load(), 1);
        pin.release();
        EXPECT_EQ(g_releaseCount.load(), 1);
    }
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(FramePinTest, MoveTransfersOwnershipWithoutDoubleRelease)
{
    g_releaseCount.store(0);
    Render::FramePin a{
        Render::FramePinKind::AssetLease,
        1,
        new int{1},
        &countingRelease,
    };
    Render::FramePin b{std::move(a)};
    EXPECT_FALSE(a.hasValue());
    EXPECT_TRUE(b.hasValue());
    b.release();
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(CpuSubmissionCompletionLedgerTest, BeginCompleteBalancesInflight)
{
    Render::CpuSubmissionCompletionLedger ledger{4};
    auto t1 = ledger.beginSubmitted(1);
    ASSERT_TRUE(t1.has_value());
    auto t2 = ledger.beginSubmitted(2);
    ASSERT_TRUE(t2.has_value());
    EXPECT_EQ(ledger.inflightCount(), 2U);

    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_EQ(ledger.inflightCount(), 1U);
    EXPECT_EQ(ledger.lastCompletedSubmission(), 1U);

    ASSERT_TRUE(ledger.complete(*t2).has_value());
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.completedCount(), 2U);
}

TEST(CpuSubmissionCompletionLedgerTest, CapacityFailureDoesNotLeakInflight)
{
    Render::CpuSubmissionCompletionLedger ledger{1};
    auto t1 = ledger.beginSubmitted(10);
    ASSERT_TRUE(t1.has_value());
    auto t2 = ledger.beginSubmitted(11);
    ASSERT_FALSE(t2.has_value());
    EXPECT_EQ(t2.error().code, Render::RenderErrorCode::SubmissionCompletionLedgerFull);
    EXPECT_EQ(ledger.inflightCount(), 1U);
    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_TRUE(ledger.allClear());
}

TEST(CpuSubmissionCompletionLedgerTest, RepeatedCompleteCannotConsumeAnotherOpenTicket)
{
    Render::CpuSubmissionCompletionLedger ledger{2};
    auto first = ledger.beginSubmitted(1);
    auto second = ledger.beginSubmitted(2);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    ASSERT_TRUE(ledger.complete(*first).has_value());
    EXPECT_FALSE(first->hasValue());
    ASSERT_TRUE(ledger.complete(*first).has_value());
    EXPECT_EQ(ledger.inflightCount(), 1U);
    EXPECT_FALSE(ledger.allClear());

    ASSERT_TRUE(ledger.complete(*second).has_value());
    EXPECT_TRUE(ledger.allClear());
}

TEST(CpuSubmissionCompletionLedgerTest, WrongLedgerCannotConsumeTicket)
{
    Render::CpuSubmissionCompletionLedger issuingLedger{1};
    Render::CpuSubmissionCompletionLedger otherLedger{1};
    auto ticket = issuingLedger.beginSubmitted(7);
    ASSERT_TRUE(ticket.has_value());

    auto wrongLedgerStatus = otherLedger.complete(*ticket);
    ASSERT_FALSE(wrongLedgerStatus.has_value());
    EXPECT_EQ(wrongLedgerStatus.error().code, Render::RenderErrorCode::InvalidSubmissionTicket);
    EXPECT_TRUE(ticket->hasValue());
    EXPECT_EQ(issuingLedger.inflightCount(), 1U);
    EXPECT_TRUE(otherLedger.allClear());

    ASSERT_TRUE(issuingLedger.complete(*ticket).has_value());
    EXPECT_TRUE(issuingLedger.allClear());
}

TEST(CpuSubmissionCompletionLedgerTest, DroppedTicketAbandonsIssuingLedger)
{
    Render::CpuSubmissionCompletionLedger ledger{1};
    {
        auto ticket = ledger.beginSubmitted(9);
        ASSERT_TRUE(ticket.has_value());
        EXPECT_EQ(ledger.inflightCount(), 1U);
    }

    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
}

// Counting mock proves Host/packet call sites use the SPI, not a concrete Null type only.
class CountingMockSubmissionCompletionLedger final : public Render::ISubmissionCompletionLedger {
public:
    [[nodiscard]] Core::Result<Render::SubmissionTicket> beginSubmitted(Core::u64 submissionIndex) override
    {
        ++beginCalls;
        if (full)
        {
            return Core::failure(Render::RenderErrorCode::SubmissionCompletionLedgerFull, "mock full");
        }
        ++inflight;
        return makeSubmissionTicket(submissionIndex);
    }

protected:
    [[nodiscard]] Core::Status completeOwned(Core::u64 submissionIndex) noexcept override
    {
        ++completeCalls;
        if (inflight == 0)
        {
            return Core::failure(Render::RenderErrorCode::InvalidSubmissionTicket, "mock empty");
        }
        --inflight;
        lastCompleted = submissionIndex;
        return Core::success();
    }

    [[nodiscard]] Core::Status abandonOwned(Core::u64) noexcept override
    {
        ++abandonCalls;
        if (inflight == 0)
        {
            return Core::failure(Render::RenderErrorCode::InvalidSubmissionTicket, "mock empty");
        }
        --inflight;
        return Core::success();
    }

public:
    [[nodiscard]] Core::u32 inflightCount() const noexcept override { return inflight; }
    [[nodiscard]] bool allClear() const noexcept override { return inflight == 0; }

    Core::u32 beginCalls = 0;
    Core::u32 completeCalls = 0;
    Core::u32 abandonCalls = 0;
    Core::u32 inflight = 0;
    Core::u64 lastCompleted = 0;
    bool full = false;
};

TEST(ISubmissionCompletionLedgerPolymorphismTest, PacketCompleteUsesInjectedMock)
{
    g_releaseCount.store(0);
    CountingMockSubmissionCompletionLedger mock;
    Render::ISubmissionCompletionLedger& ledger = mock;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{11}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(42);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    ASSERT_TRUE(packet.complete().has_value());

    EXPECT_EQ(mock.beginCalls, 1U);
    EXPECT_EQ(mock.completeCalls, 1U);
    EXPECT_EQ(mock.abandonCalls, 0U);
    EXPECT_EQ(mock.lastCompleted, 42U);
    EXPECT_TRUE(mock.allClear());
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(ISubmissionCompletionLedgerPolymorphismTest, PacketAbandonUsesInjectedMock)
{
    g_releaseCount.store(0);
    CountingMockSubmissionCompletionLedger mock;
    Render::ISubmissionCompletionLedger& ledger = mock;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{12}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(7);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    ASSERT_TRUE(packet.abandon().has_value());

    EXPECT_EQ(mock.beginCalls, 1U);
    EXPECT_EQ(mock.completeCalls, 0U);
    EXPECT_EQ(mock.abandonCalls, 1U);
    EXPECT_TRUE(mock.allClear());
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(RenderFramePacketTest, PinsReleaseWhenCpuSubmissionCompletes)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger{};
    Render::ISubmissionCompletionLedger& ledgerSpi = ledger;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(3).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::GlyphAtlas,
                         Render::FramePin{Render::FramePinKind::GlyphAtlas, 0, new int{1}, &countingRelease})
                    .has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Surface,
                         Render::FramePin{Render::FramePinKind::Surface, 9, new int{2}, &countingRelease})
                    .has_value());
    EXPECT_EQ(packet.pinCount(), 2U);

    auto ticket = ledgerSpi.beginSubmitted(5);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledgerSpi, std::move(*ticket)).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Submitted);

    ASSERT_TRUE(packet.complete().has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
    EXPECT_EQ(packet.pinCount(), 0U);
    EXPECT_EQ(g_releaseCount.load(), 2);
    EXPECT_TRUE(ledgerSpi.allClear());
}

TEST(RenderFramePacketTest, AbandonReleasesPinsAndTicket)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger{};
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{3}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(8);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    EXPECT_FALSE(ticket->hasValue());
    ASSERT_TRUE(packet.abandon().has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Abandoned);
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
}

TEST(RenderFramePacketTest, DestructorAbandonsSubmittedTicket)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger{};
    {
        Render::RenderFramePacket packet;
        ASSERT_TRUE(packet.beginFrame(1).has_value());
        ASSERT_TRUE(packet
                        .add(Render::FramePinKind::Custom,
                             Render::FramePin{Render::FramePinKind::Custom, 0, new int{5}, &countingRelease})
                        .has_value());
        auto ticket = ledger.beginSubmitted(10);
        ASSERT_TRUE(ticket.has_value());
        ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
        EXPECT_EQ(packet.submissionIndex(), 10U);
    }

    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(RenderFramePacketTest, BeginFrameAbandonsPreviousSubmittedTicket)
{
    Render::CpuSubmissionCompletionLedger ledger{};
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    auto ticket = ledger.beginSubmitted(11);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());

    ASSERT_TRUE(packet.beginFrame(2).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Building);
    EXPECT_EQ(packet.frameIndex(), 2U);
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

TEST(RenderFramePacketTest, CompleteSkippedReleasesWithoutTicket)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{4}, &countingRelease})
                    .has_value());
    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
}

TEST(RenderFramePacketTest, PinCapacityIsFixed)
{
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    for (Core::u32 i = 0; i < Render::RenderFramePacket::MaxPins; ++i)
    {
        ASSERT_TRUE(packet
                        .add(Render::FramePinKind::Custom,
                             Render::FramePin{Render::FramePinKind::Custom, i, new int{static_cast<int>(i)},
                                              &countingRelease})
                        .has_value())
            << i;
    }
    auto overflow = packet.add(
        Render::FramePinKind::Custom,
        Render::FramePin{Render::FramePinKind::Custom, 99, new int{99}, &countingRelease});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Render::RenderErrorCode::FramePinCapacityExceeded);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

} // namespace
} // namespace Tina::Tests
