#include <gtest/gtest.h>

#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>

namespace Tina::Tests {

TEST(HeadlessPlatformBackendTest, PollsWithoutRequestingExitAndRejectsPollAfterShutdown)
{
    const Platform::PlatformBackendCreateParams params{
        .applicationName = "Tina Runtime Tests",
    };
    auto backendResult = Platform::createHeadlessPlatformBackend(params);
    ASSERT_TRUE(backendResult.has_value());
    ASSERT_NE(*backendResult, nullptr);

    auto firstPoll = (*backendResult)->pollEvents();
    ASSERT_TRUE(firstPoll.has_value());
    EXPECT_FALSE(firstPoll->exitRequested);
    EXPECT_FALSE(firstPoll->surfaceSuspended);

    (*backendResult)->shutdown();
    (*backendResult)->shutdown();

    auto stoppedPoll = (*backendResult)->pollEvents();
    ASSERT_FALSE(stoppedPoll.has_value());
    EXPECT_EQ(stoppedPoll.error().code, Platform::PlatformErrorCode::BackendStopped);
}

} // namespace Tina::Tests
