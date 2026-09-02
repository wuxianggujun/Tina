#include <tina/editor/EditorPlaySession.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] EditorPlaySession createSession(
    EditorPlaySessionConfig config = {})
{
    auto session = EditorPlaySession::Create(config);
    EXPECT_TRUE(session) << (session ? "" : session.error().message);
    return std::move(*session);
}

[[nodiscard]] std::vector<std::byte> sessionBytes(
    const EditorPlaySession& session)
{
    return {session.canonicalBytes().begin(), session.canonicalBytes().end()};
}

TEST(EditorPlaySessionTests, CreatesBoundedClockAndRejectsUnsafeConfiguration)
{
    auto session = EditorPlaySession::Create({
        .canonicalByteCapacity = 64,
        .fixedStepSeconds = 0.1,
        .maximumFrameDeltaSeconds = 0.25,
        .maximumStepsPerFrame = 2,
    });
    ASSERT_TRUE(session);
    EXPECT_FALSE(session->active());
    EXPECT_EQ(session->snapshot().state, EditorPlayState::Editing);
    EXPECT_EQ(session->snapshot().revision, 1U);
    EXPECT_TRUE(session->canonicalBytes().empty());

    auto zeroCapacity =
        EditorPlaySession::Create({.canonicalByteCapacity = 0});
    ASSERT_FALSE(zeroCapacity);
    EXPECT_EQ(zeroCapacity.error().code,
              EditorErrorCode::InvalidConfiguration);

    auto invalidStep = EditorPlaySession::Create({.fixedStepSeconds = 0.0});
    ASSERT_FALSE(invalidStep);
    EXPECT_EQ(invalidStep.error().code,
              EditorErrorCode::InvalidConfiguration);

    const double maximum = (std::numeric_limits<double>::max)();
    auto overflowingBudget = EditorPlaySession::Create({
        .canonicalByteCapacity = 1,
        .fixedStepSeconds = maximum,
        .maximumFrameDeltaSeconds = maximum,
        .maximumStepsPerFrame = 2,
    });
    ASSERT_FALSE(overflowingBudget);
    EXPECT_EQ(overflowingBudget.error().code,
              EditorErrorCode::InvalidConfiguration);
}

TEST(EditorPlaySessionTests, RunsPlayPauseStepResumeAndStopStateFlow)
{
    auto session = createSession({
        .canonicalByteCapacity = 64,
        .fixedStepSeconds = 0.1,
        .maximumFrameDeltaSeconds = 0.25,
        .maximumStepsPerFrame = 2,
    });
    std::array source{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
    };

    ASSERT_TRUE(session.start(EditorPlayWorkspace::ThreeD, 42, source));
    EXPECT_TRUE(session.active());
    EXPECT_EQ(session.snapshot().workspace, EditorPlayWorkspace::ThreeD);
    EXPECT_EQ(session.snapshot().state, EditorPlayState::Playing);
    EXPECT_EQ(session.snapshot().sourceDocumentRevision, 42U);
    EXPECT_EQ(session.snapshot().revision, 2U);
    source[0] = std::byte{0x7f};
    EXPECT_EQ(session.canonicalBytes()[0], std::byte{0x11});

    const Core::u64 beforeAccumulatorOnly = session.snapshot().revision;
    auto noStep = session.advance(0.05);
    ASSERT_TRUE(noStep);
    EXPECT_EQ(*noStep, 0U);
    EXPECT_NEAR(session.snapshot().accumulatorSeconds, 0.05, 0.0000001);
    EXPECT_EQ(session.snapshot().revision, beforeAccumulatorOnly);

    auto oneStep = session.advance(0.06);
    ASSERT_TRUE(oneStep);
    EXPECT_EQ(*oneStep, 1U);
    EXPECT_EQ(session.snapshot().simulationTickCount, 1U);
    EXPECT_NEAR(session.snapshot().simulatedSeconds, 0.1, 0.0000001);
    EXPECT_NEAR(session.snapshot().accumulatorSeconds, 0.01, 0.0000001);
    EXPECT_EQ(session.snapshot().revision, 3U);

    ASSERT_TRUE(session.pause());
    EXPECT_EQ(session.snapshot().state, EditorPlayState::Paused);
    EXPECT_DOUBLE_EQ(session.snapshot().accumulatorSeconds, 0.0);
    EXPECT_EQ(session.snapshot().revision, 4U);

    ASSERT_TRUE(session.requestStep());
    EXPECT_TRUE(session.snapshot().stepPending);
    EXPECT_EQ(session.snapshot().revision, 5U);
    ASSERT_TRUE(session.requestStep());
    EXPECT_EQ(session.snapshot().revision, 5U);

    auto pausedStep = session.advance(10.0);
    ASSERT_TRUE(pausedStep);
    EXPECT_EQ(*pausedStep, 1U);
    EXPECT_FALSE(session.snapshot().stepPending);
    EXPECT_EQ(session.snapshot().simulationTickCount, 2U);
    EXPECT_EQ(session.snapshot().revision, 6U);
    auto pausedNoOp = session.advance(0.1);
    ASSERT_TRUE(pausedNoOp);
    EXPECT_EQ(*pausedNoOp, 0U);
    EXPECT_EQ(session.snapshot().revision, 6U);

    ASSERT_TRUE(session.resume());
    EXPECT_EQ(session.snapshot().state, EditorPlayState::Playing);
    EXPECT_EQ(session.snapshot().revision, 7U);
    auto boundedCatchUp = session.advance(100.0);
    ASSERT_TRUE(boundedCatchUp);
    EXPECT_EQ(*boundedCatchUp, 2U);
    EXPECT_EQ(session.snapshot().simulationTickCount, 4U);
    EXPECT_NEAR(session.snapshot().simulatedSeconds, 0.4, 0.0000001);
    EXPECT_DOUBLE_EQ(session.snapshot().accumulatorSeconds, 0.0);
    EXPECT_EQ(session.snapshot().revision, 8U);

    ASSERT_TRUE(session.stop());
    EXPECT_FALSE(session.active());
    EXPECT_EQ(session.snapshot().state, EditorPlayState::Editing);
    EXPECT_EQ(session.snapshot().sourceDocumentRevision, 0U);
    EXPECT_EQ(session.snapshot().simulationTickCount, 0U);
    EXPECT_TRUE(session.canonicalBytes().empty());
    EXPECT_EQ(session.snapshot().revision, 9U);
    ASSERT_TRUE(session.stop());
    EXPECT_EQ(session.snapshot().revision, 9U);
}

TEST(EditorPlaySessionTests, InvalidCommandsPreserveSessionAndOwnedSnapshot)
{
    auto session = createSession({.canonicalByteCapacity = 4});
    const std::array validBytes{std::byte{1}, std::byte{2}};
    const std::array oversized{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};

    EXPECT_FALSE(session.pause());
    EXPECT_FALSE(session.resume());
    EXPECT_FALSE(session.requestStep());
    EXPECT_FALSE(session.advance(0.0));
    EXPECT_EQ(session.snapshot().revision, 1U);

    EXPECT_FALSE(session.start(EditorPlayWorkspace::TwoD, 0, validBytes));
    EXPECT_FALSE(session.start(EditorPlayWorkspace::TwoD, 1, {}));
    EXPECT_FALSE(session.start(EditorPlayWorkspace::TwoD, 1, oversized));
    EXPECT_FALSE(session.start(static_cast<EditorPlayWorkspace>(255), 1,
                               validBytes));
    EXPECT_EQ(session.snapshot().state, EditorPlayState::Editing);
    EXPECT_EQ(session.snapshot().revision, 1U);
    EXPECT_TRUE(session.canonicalBytes().empty());

    ASSERT_TRUE(session.start(EditorPlayWorkspace::TwoD, 7, validBytes));
    const auto beforeBytes = sessionBytes(session);
    const EditorPlaySessionSnapshot before = session.snapshot();
    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    EXPECT_FALSE(session.advance(nan));
    EXPECT_FALSE(session.resume());
    EXPECT_FALSE(session.requestStep());
    EXPECT_FALSE(session.start(EditorPlayWorkspace::ThreeD, 8, validBytes));
    EXPECT_EQ(session.snapshot().state, before.state);
    EXPECT_EQ(session.snapshot().sourceDocumentRevision,
              before.sourceDocumentRevision);
    EXPECT_EQ(session.snapshot().simulationTickCount,
              before.simulationTickCount);
    EXPECT_EQ(session.snapshot().accumulatorSeconds,
              before.accumulatorSeconds);
    EXPECT_EQ(session.snapshot().revision, before.revision);
    EXPECT_EQ(sessionBytes(session), beforeBytes);
}

} // namespace
} // namespace Tina::Editor
