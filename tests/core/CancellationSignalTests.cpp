#include <tina/core/base/CancellationSignal.hpp>

#include <gtest/gtest.h>

#include <thread>

namespace Tina::Tests {
namespace {

// A caller with nothing to cancel passes {}, so the polling side needs no null handling.
TEST(CancellationTokenTest, AnEmptyTokenNeverReportsCancellation)
{
    const Core::CancellationToken empty;
    EXPECT_FALSE(empty.cancellationRequested());
}

TEST(CancellationTokenTest, ObservesTheSignalItViews)
{
    Core::CancellationSignal signal;
    const Core::CancellationToken token{signal};
    EXPECT_FALSE(token.cancellationRequested());

    signal.requestCancellation();
    EXPECT_TRUE(token.cancellationRequested());
}

// Latching is the whole contract: an operation that polls after a cancel must never see
// "not cancelled", whenever it happens to poll.
TEST(CancellationSignalTest, CancellationLatchesAndRepeatedRequestsAreIdempotent)
{
    Core::CancellationSignal signal;
    signal.requestCancellation();
    signal.requestCancellation();
    EXPECT_TRUE(signal.cancellationRequested());

    const Core::CancellationToken late{signal};
    EXPECT_TRUE(late.cancellationRequested())
        << "a token created after the request must still see it";
}

// The real shape: one thread requests while another polls between work items.
TEST(CancellationSignalTest, CrossesThreadsToAPollingWorker)
{
    Core::CancellationSignal signal;
    const Core::CancellationToken token{signal};

    std::thread requester([&signal] { signal.requestCancellation(); });
    requester.join();

    EXPECT_TRUE(token.cancellationRequested());
}

} // namespace
} // namespace Tina::Tests
