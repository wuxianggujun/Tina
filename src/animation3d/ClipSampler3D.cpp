#include <tina/animation3d/ClipSampler3D.hpp>

#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Animation3D {

namespace {

using AssetFormat::AnimationChannel;
using AssetFormat::AnimationClip3DPlaybackMode;
using AssetFormat::AnimationInterpolation;

constexpr Core::usize AnimatedJointWordCount = MaximumJointCount / 64U;

[[nodiscard]] Core::u32 saturatedCycleCount(double count) noexcept
{
    const double maximum = static_cast<double>((std::numeric_limits<Core::u32>::max)());
    return count >= maximum ? (std::numeric_limits<Core::u32>::max)()
                            : static_cast<Core::u32>(count);
}

} // namespace

ClipSampler3D::ClipSampler3D(Core::u16 jointCount, float durationSeconds,
                             AnimationClip3DPlaybackMode playbackMode,
                             std::pmr::vector<Track> tracks, std::pmr::vector<float> times,
                             std::pmr::vector<float> values,
                             std::pmr::vector<Core::u64> animatedJointWords) noexcept
    : m_jointCount(jointCount), m_durationSeconds(durationSeconds), m_playbackMode(playbackMode),
      m_tracks(std::move(tracks)), m_times(std::move(times)), m_values(std::move(values)),
      m_animatedJointWords(std::move(animatedJointWords))
{
}

Core::Result<ClipSampler3D> ClipSampler3D::Create(
    const AssetFormat::AnimationClip3DPayloadView& clip, Core::u16 jointCount,
    std::pmr::memory_resource& resource)
{
    if (jointCount == 0U || jointCount > MaximumJointCount) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "clip sampler joint count is outside the supported range");
    }
    // jointCount equality is the only compatibility signal the wire format carries: a clip
    // has no skeleton identity or hash. So this check is the whole binding contract, and
    // getting it wrong drives joint N of one rig with joint N of another.
    if (clip.jointCount != jointCount) {
        return Core::failure(AnimationErrorCode::SkeletonMismatch,
                             "animation clip joint count does not match the skeleton");
    }
    if (!std::isfinite(clip.durationSeconds) || clip.durationSeconds < 0.0F) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "animation clip duration must be finite and non-negative");
    }

    try {
        std::pmr::vector<Track> tracks{std::pmr::polymorphic_allocator<Track>{&resource}};
        std::pmr::vector<float> times{std::pmr::polymorphic_allocator<float>{&resource}};
        std::pmr::vector<float> values{std::pmr::polymorphic_allocator<float>{&resource}};
        std::pmr::vector<Core::u64> animated{std::pmr::polymorphic_allocator<Core::u64>{&resource}};
        tracks.reserve(clip.trackCount);
        animated.resize(AnimatedJointWordCount, 0U);
        // Copied rather than borrowed, so the payload view need not outlive the sampler --
        // the same promise Animator3D already made.
        times.assign(clip.times.begin(), clip.times.end());
        values.assign(clip.values.begin(), clip.values.end());

        for (Core::u16 index = 0; index < clip.trackCount; ++index) {
            const auto track = clip.track(index);
            if (!track) {
                return Core::failure(AnimationErrorCode::InvalidArgument,
                                     "animation clip track table is truncated");
            }
            if (track->jointIndex >= jointCount) {
                return Core::failure(AnimationErrorCode::SkeletonMismatch,
                                     "animation clip track targets a joint outside the skeleton");
            }
            const Core::u16 components =
                AssetFormat::animationChannelComponentCount(track->channel);
            if (components == 0U || track->keyCount == 0U) {
                return Core::failure(AnimationErrorCode::InvalidArgument,
                                     "animation clip track channel or key count is invalid");
            }
            const Core::usize timeEnd =
                static_cast<Core::usize>(track->keyStartIndex) + track->keyCount;
            const Core::usize valueEnd = static_cast<Core::usize>(track->valueStartIndex) +
                                         (static_cast<Core::usize>(track->keyCount) * components);
            if (timeEnd > times.size() || valueEnd > values.size()) {
                return Core::failure(AnimationErrorCode::InvalidArgument,
                                     "animation clip track ranges exceed the key blocks");
            }
            tracks.push_back(Track{
                .jointIndex = track->jointIndex,
                .channel = track->channel,
                .interpolation = track->interpolation,
                .keyCount = track->keyCount,
                .timeOffset = track->keyStartIndex,
                .valueOffset = track->valueStartIndex,
            });
            animated[track->jointIndex / 64U] |= (Core::u64{1} << (track->jointIndex % 64U));
        }

        return ClipSampler3D(jointCount, clip.durationSeconds, clip.playbackMode, std::move(tracks),
                             std::move(times), std::move(values), std::move(animated));
    } catch (const std::bad_alloc&) {
        return Core::failure(AnimationErrorCode::AllocationFailed,
                             "clip sampler allocation failed");
    }
}

bool ClipSampler3D::animatesJoint(Core::u16 joint) const noexcept
{
    if (joint >= m_jointCount || m_animatedJointWords.empty()) {
        return false;
    }
    return (m_animatedJointWords[joint / 64U] & (Core::u64{1} << (joint % 64U))) != 0U;
}

Core::Status ClipSampler3D::sample(float timeSeconds, const Skeleton3D& skeleton,
                                   Pose3D& pose) const noexcept
{
    if (skeleton.jointCount() != m_jointCount || pose.jointCount() != m_jointCount) {
        return Core::failure(AnimationErrorCode::SkeletonMismatch,
                             "sample target pose or skeleton does not match the clip");
    }
    if (!std::isfinite(timeSeconds)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "sample time must be finite");
    }

    // Untracked joints keep their bind pose. Leaving them at whatever the pose previously
    // held would make sampling order observable: the same clip would produce a different
    // result depending on which pose buffer it happened to be handed.
    if (Core::Status status = skeleton.writeBindPose(pose); !status) {
        return status;
    }

    for (const Track& track : m_tracks) {
        const std::span<const float> trackTimes{m_times.data() + track.timeOffset, track.keyCount};
        const Core::u16 components = AssetFormat::animationChannelComponentCount(track.channel);
        const std::span<const float> trackValues{
            m_values.data() + track.valueOffset,
            static_cast<Core::usize>(track.keyCount) * components};

        // Clamped at both ends rather than extrapolated: a clip sampled past its duration
        // holds its last key, which is what every playback mode's arithmetic assumes.
        Core::u32 firstKey = 0;
        Core::u32 secondKey = 0;
        float alpha = 0.0F;
        if (timeSeconds <= trackTimes.front()) {
            firstKey = 0;
            secondKey = 0;
        } else if (timeSeconds >= trackTimes.back()) {
            firstKey = track.keyCount - 1U;
            secondKey = firstKey;
        } else {
            const auto upper = std::upper_bound(trackTimes.begin(), trackTimes.end(), timeSeconds);
            const auto upperIndex =
                static_cast<Core::u32>(std::distance(trackTimes.begin(), upper));
            firstKey = upperIndex - 1U;
            secondKey = upperIndex;
            const float span = trackTimes[secondKey] - trackTimes[firstKey];
            // A zero span would divide by zero; two keys at the same time means the second
            // one wins, which is what Step already does.
            alpha = span > 0.0F ? ((timeSeconds - trackTimes[firstKey]) / span) : 1.0F;
        }
        if (track.interpolation == AnimationInterpolation::Step) {
            secondKey = firstKey;
            alpha = 0.0F;
        }

        Scene::LocalTransform& target = pose.at(track.jointIndex);
        const Core::usize firstOffset = static_cast<Core::usize>(firstKey) * components;
        const Core::usize secondOffset = static_cast<Core::usize>(secondKey) * components;
        switch (track.channel) {
        case AnimationChannel::Translation: {
            const Math::Vec3 from{trackValues[firstOffset], trackValues[firstOffset + 1U],
                                  trackValues[firstOffset + 2U]};
            const Math::Vec3 to{trackValues[secondOffset], trackValues[secondOffset + 1U],
                                trackValues[secondOffset + 2U]};
            target.position = firstKey == secondKey ? from : Math::lerp(from, to, alpha);
            break;
        }
        case AnimationChannel::Scale: {
            const Math::Vec3 from{trackValues[firstOffset], trackValues[firstOffset + 1U],
                                  trackValues[firstOffset + 2U]};
            const Math::Vec3 to{trackValues[secondOffset], trackValues[secondOffset + 1U],
                                trackValues[secondOffset + 2U]};
            target.scale = firstKey == secondKey ? from : Math::lerp(from, to, alpha);
            break;
        }
        case AnimationChannel::Rotation: {
            const Math::Quaternion from{trackValues[firstOffset], trackValues[firstOffset + 1U],
                                        trackValues[firstOffset + 2U], trackValues[firstOffset + 3U]};
            const Math::Quaternion to{trackValues[secondOffset], trackValues[secondOffset + 1U],
                                      trackValues[secondOffset + 2U], trackValues[secondOffset + 3U]};
            // slerp, never component-wise lerp: the latter collapses toward zero length near
            // 180 degrees apart and the joint snaps.
            target.rotation = firstKey == secondKey ? Math::normalized(from)
                                                    : Math::slerp(from, to, alpha);
            break;
        }
        case AnimationChannel::Invalid:
        default:
            return Core::failure(AnimationErrorCode::InvalidArgument,
                                 "animation clip track has an invalid channel");
        }

        if (!Scene::isValid(target)) {
            return Core::failure(AnimationErrorCode::EvaluationFailed,
                                 "sampled joint transform is not finite");
        }
    }
    return Core::success();
}

ClipPlayhead3D ClipSampler3D::startPlayhead(bool backward) const noexcept
{
    const float start = backward ? m_durationSeconds : 0.0F;
    return ClipPlayhead3D{
        .timeSeconds = start,
        .previousTimeSeconds = start,
        .playingBackward = backward,
    };
}

Core::Result<ClipPlayhead3D> ClipSampler3D::advance(ClipPlayhead3D playhead, Core::Duration delta,
                                                    float speed) const noexcept
{
    if (!std::isfinite(delta.count()) || delta.count() < 0.0) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "advance delta must be finite and non-negative");
    }
    if (!std::isfinite(speed)) {
        return Core::failure(AnimationErrorCode::InvalidArgument, "playback speed must be finite");
    }
    if (!std::isfinite(playhead.timeSeconds)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "playhead time must be finite");
    }
    if (playhead.timeSeconds < 0.0F || playhead.timeSeconds > m_durationSeconds) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "playhead time is outside the clip duration");
    }

    ClipPlayhead3D next = playhead;
    next.previousTimeSeconds = playhead.timeSeconds;
    next.advancedBackwardThisAdvance = false;
    next.completed = false;
    next.wrappedThisAdvance = false;
    next.cyclesCompleted = 0;

    const double signedDelta = delta.count() * static_cast<double>(speed);
    if (!std::isfinite(signedDelta)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "advance delta and speed product must be finite");
    }
    // No movement must not synthesize completion or a wrap merely because a playhead is
    // resting exactly on a boundary. This check deliberately precedes the zero-duration
    // case for the same reason.
    if (signedDelta == 0.0) {
        next.completed = playhead.completed;
        return next;
    }

    // A zero-duration clip is a single pose. Advancing it can only complete it, never move
    // it, and dividing by the duration below would be a division by zero.
    if (m_durationSeconds <= 0.0F) {
        next.timeSeconds = 0.0F;
        next.completed = m_playbackMode == AnimationClip3DPlaybackMode::Once;
        return next;
    }

    const double direction = next.playingBackward ? -1.0 : 1.0;
    const double directedDelta = signedDelta * direction;
    next.advancedBackwardThisAdvance = directedDelta < 0.0;
    double position = static_cast<double>(playhead.timeSeconds) + directedDelta;
    const double duration = static_cast<double>(m_durationSeconds);
    if (!std::isfinite(position)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "advanced playhead position must be finite");
    }

    switch (m_playbackMode) {
    case AnimationClip3DPlaybackMode::Once:
        if (directedDelta > 0.0 && position >= duration) {
            position = duration;
            next.completed = true;
        } else if (directedDelta < 0.0 && position <= 0.0) {
            position = 0.0;
            // A backward Once clip finishes at the start, not at the end.
            next.completed = true;
        }
        break;

    case AnimationClip3DPlaybackMode::Loop: {
        // std::fmod rather than a while loop: one advance may span many cycles at a low
        // frame rate, and looping per cycle turns a hitch into an unbounded iteration.
        // cyclesCompleted is reported because root motion accumulates per cycle -- a delta
        // computed only from (previous, current) loses a whole cycle's translation whenever
        // an advance crosses the loop point.
        const double cycles = std::floor(position / duration);
        if (cycles != 0.0) {
            next.wrappedThisAdvance = true;
            next.cyclesCompleted = saturatedCycleCount(std::abs(cycles));
        }
        // Using cycles * duration can overflow even though both inputs are finite. fmod keeps
        // the remainder finite for extreme deltas and does not iterate once per cycle.
        position = std::fmod(position, duration);
        if (position < 0.0) {
            position += duration;
        }
        break;
    }

    case AnimationClip3DPlaybackMode::PingPong: {
        // Map the folded playhead to a monotonically increasing phase, advance that phase,
        // then fold it back over [0,duration]. Keeping the phase direction separate is what
        // lets a backward leg remain backward on the next call.
        const double period = duration * 2.0;
        const double phaseStart = playhead.playingBackward
                                      ? period - static_cast<double>(playhead.timeSeconds)
                                      : static_cast<double>(playhead.timeSeconds);
        const double phaseEnd = phaseStart + signedDelta;
        if (!std::isfinite(phaseEnd)) {
            return Core::failure(AnimationErrorCode::InvalidArgument,
                                 "advanced ping-pong phase must be finite");
        }
        const double boundaries = signedDelta > 0.0
                                      ? std::floor(phaseEnd / duration) -
                                            std::floor(phaseStart / duration)
                                      : std::ceil(phaseStart / duration) -
                                            std::ceil(phaseEnd / duration);
        if (boundaries > 0.0) {
            next.wrappedThisAdvance = true;
            next.cyclesCompleted = saturatedCycleCount(boundaries);
        }

        double folded = std::fmod(phaseEnd, period);
        if (folded < 0.0) {
            folded += period;
        }
        if (folded > duration) {
            position = period - folded;
            next.playingBackward = true;
        } else {
            position = folded;
            // At the far endpoint the following positive-speed step is the backward leg.
            next.playingBackward = folded == duration;
        }
        break;
    }
    default:
        return Core::failure(AnimationErrorCode::InvalidConfiguration,
                             "clip sampler has an unknown playback mode");
    }

    next.timeSeconds = static_cast<float>(std::clamp(position, 0.0, duration));
    return next;
}

} // namespace Tina::Animation3D
