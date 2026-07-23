#include "AudioControlState.hpp"

#include <gtest/gtest.h>

namespace {

using Tina::Sample2D::AudioMuteControlState;
using Tina::Sample2D::commitPendingAudioMute;
using Tina::Sample2D::togglePendingAudioMute;

TEST(Sample2DAudioControlStateTest, SingleToggleUsesCommittedStateWhenNothingIsPending)
{
    AudioMuteControlState state{.committed = false};

    EXPECT_TRUE(togglePendingAudioMute(state));

    EXPECT_FALSE(state.committed);
    ASSERT_TRUE(state.pending.has_value());
    EXPECT_TRUE(*state.pending);

    AudioMuteControlState alreadyMuted{.committed = true};
    EXPECT_FALSE(togglePendingAudioMute(alreadyMuted));
    ASSERT_TRUE(alreadyMuted.pending.has_value());
    EXPECT_FALSE(*alreadyMuted.pending);
}

TEST(Sample2DAudioControlStateTest, SameFrameDoubleAndTripleToggleUseLatestPendingState)
{
    AudioMuteControlState state{.committed = false};

    EXPECT_TRUE(togglePendingAudioMute(state));
    EXPECT_FALSE(togglePendingAudioMute(state));
    ASSERT_TRUE(state.pending.has_value());
    EXPECT_FALSE(*state.pending);
    EXPECT_FALSE(state.committed);

    EXPECT_TRUE(togglePendingAudioMute(state));
    ASSERT_TRUE(state.pending.has_value());
    EXPECT_TRUE(*state.pending);
    EXPECT_FALSE(state.committed);
}

TEST(Sample2DAudioControlStateTest, CommitMakesNextToggleUseTheAppliedState)
{
    AudioMuteControlState state{.committed = false};
    ASSERT_TRUE(togglePendingAudioMute(state));

    commitPendingAudioMute(state);

    EXPECT_TRUE(state.committed);
    EXPECT_FALSE(state.pending.has_value());
    EXPECT_FALSE(togglePendingAudioMute(state));
    ASSERT_TRUE(state.pending.has_value());
    EXPECT_FALSE(*state.pending);

    commitPendingAudioMute(state);
    EXPECT_FALSE(state.committed);
    EXPECT_FALSE(state.pending.has_value());
}

} // namespace
