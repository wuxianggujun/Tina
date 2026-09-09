#pragma once

#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/core/error/Result.hpp>

#include <span>

namespace Tina::Scene {

struct AnimationEventCrossing3D final {
    Core::u32 eventTag = 0;
    float clipTimeSeconds = 0.0F;
    double travelSeconds = 0.0;
    bool playingBackward = false;
};

struct AnimationEventBatch3D final {
    Core::usize written = 0;
    Core::u64 dropped = 0;
};

// Traverses (start, end] in playback order, including wraps and bounces. phase
// is the unfolded phase within a cycle (duration * 2 for PingPong). No event
// fires just by seeking/restarting. Work is bounded by events * output capacity,
// not by the number of loops in a large delta. Excess occurrences are counted.
[[nodiscard]] Core::Result<AnimationEventBatch3D> collectAnimationEvents3D(
    std::span<const AssetFormat::AnimationEvent3D> events,
    AssetFormat::AnimationClip3DPlaybackMode mode, float durationSeconds,
    double phaseSeconds, double travelSeconds, std::span<AnimationEventCrossing3D> output) noexcept;

} // namespace Tina::Scene
