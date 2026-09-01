#pragma once

#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Animation3D {

// Where a clip's playhead is, and what the last advance did to it.
struct ClipPlayhead3D final {
    float timeSeconds = 0.0F;
    float previousTimeSeconds = 0.0F;
    // PingPong runs backwards on alternate cycles; root motion has to know, because a
    // backward cycle's root delta is the negation of the forward one.
    bool playingBackward = false;
    // Actual time direction at the start of the last non-zero advance. This can differ from
    // playingBackward when speed is negative, and root motion needs the distinction at a
    // Loop wrap or PingPong bounce.
    bool advancedBackwardThisAdvance = false;
    // A Once clip that reached the boundary it was moving toward.
    bool completed = false;
    bool wrappedThisAdvance = false;
    // Loop boundaries crossed, or endpoint bounces for PingPong. Root motion needs every
    // crossing: a delta computed only from (previous, current) loses whole traversals at low
    // frame rates. Saturates at u32 max for an extreme finite delta.
    Core::u32 cyclesCompleted = 0;
};

// Stateless sampling of one cooked clip, plus the playhead arithmetic for the three
// playback modes.
//
// Deliberately separated from playback state: a crossfade samples the same clip at two
// different times, a blend tree samples several clips at one time, and a state machine
// needs to sample a clip it is not currently playing to know what it would look like.
// Animator3D could not do any of those because its evaluate step was private and always
// wrote its own buffers.
//
// Copies the clip's wire data at Create, so the payload view need not outlive the sampler
// -- matching what Animator3D already promised.
class ClipSampler3D final {
  public:
    // The clip's jointCount must equal the skeleton's; that equality is the only
    // compatibility signal the wire format carries (see AnimationClip3DPayload.hpp), so it
    // is checked here rather than trusted.
    [[nodiscard]] static Core::Result<ClipSampler3D> Create(
        const AssetFormat::AnimationClip3DPayloadView& clip, Core::u16 jointCount,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ClipSampler3D(const ClipSampler3D&) = delete;
    ClipSampler3D& operator=(const ClipSampler3D&) = delete;
    ClipSampler3D(ClipSampler3D&&) noexcept = default;
    ClipSampler3D& operator=(ClipSampler3D&&) = delete;

    [[nodiscard]] Core::u16 jointCount() const noexcept { return m_jointCount; }
    [[nodiscard]] float durationSeconds() const noexcept { return m_durationSeconds; }
    [[nodiscard]] Core::usize trackCount() const noexcept { return m_tracks.size(); }
    [[nodiscard]] AssetFormat::AnimationClip3DPlaybackMode playbackMode() const noexcept
    {
        return m_playbackMode;
    }
    // True when this clip drives the joint at all. A layer whose mask includes a joint no
    // clip animates would otherwise blend toward the bind pose and look like a hitch.
    [[nodiscard]] bool animatesJoint(Core::u16 joint) const noexcept;

    // Samples at an absolute clip time into `pose`, which must have the skeleton's joint
    // count. Joints the clip does not drive receive the skeleton's bind pose, matching what
    // Animator3D did -- an untracked joint has no animated value, and leaving it at
    // whatever the pose previously held would make sampling order observable.
    //
    // Const: sampling does not advance anything. That is what makes one clip samplable at
    // two times in the same frame, which is what a crossfade is.
    [[nodiscard]] Core::Status sample(float timeSeconds, const Skeleton3D& skeleton,
                                      Pose3D& pose) const noexcept;

    // Advances a playhead this sampler owns no state for. The caller holds the playhead, so
    // two states in a state machine can each keep their own position in the same clip.
    //
    // `speed` may be negative here, unlike Animator3D's setPlaybackSpeed: a state machine
    // that plays a clip backwards is ordinary, and refusing it would force callers to cook
    // a reversed clip.
    [[nodiscard]] Core::Result<ClipPlayhead3D> advance(ClipPlayhead3D playhead,
                                                       Core::Duration delta,
                                                       float speed) const noexcept;

    // Playhead at the clip's start, for the given direction.
    [[nodiscard]] ClipPlayhead3D startPlayhead(bool backward = false) const noexcept;

  private:
    struct Track final {
        Core::u16 jointIndex = 0;
        AssetFormat::AnimationChannel channel = AssetFormat::AnimationChannel::Invalid;
        AssetFormat::AnimationInterpolation interpolation =
            AssetFormat::AnimationInterpolation::Invalid;
        Core::u32 keyCount = 0;
        Core::u32 timeOffset = 0;
        Core::u32 valueOffset = 0;
    };

    ClipSampler3D(Core::u16 jointCount, float durationSeconds,
                  AssetFormat::AnimationClip3DPlaybackMode playbackMode,
                  std::pmr::vector<Track> tracks, std::pmr::vector<float> times,
                  std::pmr::vector<float> values,
                  std::pmr::vector<Core::u64> animatedJointWords) noexcept;

    Core::u16 m_jointCount = 0;
    float m_durationSeconds = 0.0F;
    AssetFormat::AnimationClip3DPlaybackMode m_playbackMode =
        AssetFormat::AnimationClip3DPlaybackMode::Loop;
    std::pmr::vector<Track> m_tracks;
    std::pmr::vector<float> m_times;
    std::pmr::vector<float> m_values;
    // One bit per joint, so animatesJoint is a shift rather than a track scan.
    std::pmr::vector<Core::u64> m_animatedJointWords;
};

} // namespace Tina::Animation3D
