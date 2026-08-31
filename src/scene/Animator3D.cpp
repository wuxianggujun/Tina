#include <tina/scene/Animator3D.hpp>

#include <tina/math/Mat4.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Scene {
namespace {

// Joint matrices are kept in one flat float buffer so the whole palette can be
// handed to the render packet as a span. These two helpers move a single joint
// between that buffer and Math::Mat4.
[[nodiscard]] Math::Mat4 loadMatrix(std::span<const float, 16> source) noexcept
{
    Math::Mat4 result{};
    std::copy(source.begin(), source.end(), result.columns.begin());
    return result;
}

void storeMatrix(const Math::Mat4& source, std::span<float, 16> destination) noexcept
{
    std::copy(source.columns.begin(), source.columns.end(), destination.begin());
}

[[nodiscard]] bool isKnownPlaybackMode(
    AssetFormat::AnimationClip3DPlaybackMode mode) noexcept
{
    switch (mode) {
    case AssetFormat::AnimationClip3DPlaybackMode::Once:
    case AssetFormat::AnimationClip3DPlaybackMode::Loop:
    case AssetFormat::AnimationClip3DPlaybackMode::PingPong:
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownChannel(AssetFormat::AnimationChannel channel) noexcept
{
    switch (channel) {
    case AssetFormat::AnimationChannel::Translation:
    case AssetFormat::AnimationChannel::Rotation:
    case AssetFormat::AnimationChannel::Scale:
        return true;
    case AssetFormat::AnimationChannel::Invalid:
        return false;
    }
    return false;
}

[[nodiscard]] bool isKnownInterpolation(
    AssetFormat::AnimationInterpolation interpolation) noexcept
{
    return interpolation == AssetFormat::AnimationInterpolation::Linear
        || interpolation == AssetFormat::AnimationInterpolation::Step;
}

[[nodiscard]] u32 trackSortKey(
    u16 jointIndex,
    AssetFormat::AnimationChannel channel) noexcept
{
    return static_cast<u32>(jointIndex) * AssetFormat::AnimationClip3DWire::MaxChannelsPerJoint
        + static_cast<u32>(channel);
}

[[nodiscard]] Math::Mat4 transformMatrix(const LocalTransform& transform) noexcept
{
    return Math::fromTrs(
        transform.position,
        Math::normalized(transform.rotation),
        transform.scale);
}

} // namespace

Animator3D::Animator3D(std::pmr::memory_resource& resource) noexcept
    : m_parents(&resource),
      m_bindPose(&resource),
      m_inverseBindMatrices(&resource),
      m_tracks(&resource),
      m_times(&resource),
      m_values(&resource),
      m_stagedTracks(&resource),
      m_stagedTimes(&resource),
      m_stagedValues(&resource),
      m_localPose(&resource),
      m_globalMatrices(&resource),
      m_skinningMatrices(&resource),
      m_localScratch(&resource),
      m_globalScratch(&resource),
      m_skinningScratch(&resource)
{
}

Core::Result<Animator3D> Animator3D::Create(
    const AssetFormat::SkinnedMeshPayloadView& mesh,
    const AssetFormat::AnimationClip3DPayloadView& clip,
    std::pmr::memory_resource& resource)
{
    try {
        Animator3D animator(resource);
        if (const Core::Status status = animator.initializeSkeleton(mesh); !status) {
            return Core::failure(status.error());
        }
        if (const Core::Status status = animator.setClip(clip); !status) {
            return Core::failure(status.error());
        }
        return animator;
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Animator3D could not allocate fixed pose storage");
    } catch (const std::exception& exception) {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...) {
        return Core::failure(
            Core::CoreErrorCode::Internal,
            "Animator3D construction failed with an unknown exception");
    }
}

Core::Status Animator3D::initializeSkeleton(
    const AssetFormat::SkinnedMeshPayloadView& mesh)
{
    if (mesh.jointCount == 0
        || mesh.jointCount > AssetFormat::SkinnedMeshWire::MaxJointCount
        || mesh.inverseBindMatrices.size()
            != static_cast<usize>(mesh.jointCount)
                * AssetFormat::SkinnedMeshWire::FloatsPerInverseBindMatrix) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "Animator3D skeleton shape is invalid");
    }
    m_parents.reserve(mesh.jointCount);
    m_bindPose.reserve(mesh.jointCount);
    for (u16 jointIndex = 0; jointIndex < mesh.jointCount; ++jointIndex) {
        const auto joint = mesh.joint(jointIndex);
        if (!joint || (joint->parentJoint != AssetFormat::SkinnedMeshWire::JointIndexNone
                && joint->parentJoint >= jointIndex)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D skeleton hierarchy is invalid");
        }
        LocalTransform transform{
            .position = {joint->bindTranslation[0], joint->bindTranslation[1], joint->bindTranslation[2]},
            .rotation = {joint->bindRotation[0], joint->bindRotation[1],
                joint->bindRotation[2], joint->bindRotation[3]},
            .scale = {joint->bindScale[0], joint->bindScale[1], joint->bindScale[2]},
        };
        if (!isValid(transform)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D bind pose contains an invalid transform");
        }
        transform.rotation = Math::normalized(transform.rotation);
        m_parents.push_back(joint->parentJoint);
        m_bindPose.push_back(transform);
    }
    if (!std::ranges::all_of(
            mesh.inverseBindMatrices,
            [](float value) { return std::isfinite(value); })) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "Animator3D inverse bind matrices must be finite");
    }
    m_inverseBindMatrices.assign(
        mesh.inverseBindMatrices.begin(), mesh.inverseBindMatrices.end());
    m_localPose.resize(mesh.jointCount);
    m_localScratch.resize(mesh.jointCount);
    const usize matrixFloatCount = static_cast<usize>(mesh.jointCount) * 16U;
    m_globalMatrices.resize(matrixFloatCount);
    m_skinningMatrices.resize(matrixFloatCount);
    m_globalScratch.resize(matrixFloatCount);
    m_skinningScratch.resize(matrixFloatCount);
    return Core::success();
}

Core::Status Animator3D::setClip(
    const AssetFormat::AnimationClip3DPayloadView& clip)
{
    if (m_bindPose.empty() || clip.jointCount != m_bindPose.size()
        || clip.trackCount == 0
        || clip.trackCount > AssetFormat::AnimationClip3DWire::MaxTracks
        || !isKnownPlaybackMode(clip.playbackMode)
        || !(clip.durationSeconds > 0.0F)
        || !std::isfinite(clip.durationSeconds)
        || clip.durationSeconds > AssetFormat::AnimationClip3DWire::MaxDurationSeconds) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "Animator3D clip header or skeleton joint count is invalid");
    }

    auto& replacementTracks = m_stagedTracks;
    auto& replacementTimes = m_stagedTimes;
    auto& replacementValues = m_stagedValues;
    replacementTracks.clear();
    replacementTimes.clear();
    replacementValues.clear();
    try {
        replacementTracks.reserve(clip.trackCount);
        replacementTimes.reserve(clip.totalKeyframeCount);
        replacementValues.reserve(clip.totalValueFloatCount);
        u32 previousSortKey = 0;
        bool hasPrevious = false;
        float maximumLastTime = 0.0F;
        for (u16 trackIndex = 0; trackIndex < clip.trackCount; ++trackIndex) {
            const auto source = clip.track(trackIndex);
            if (!source || source->jointIndex >= clip.jointCount
                || !isKnownChannel(source->channel)
                || !isKnownInterpolation(source->interpolation)
                || source->keyCount == 0
                || source->keyCount > AssetFormat::AnimationClip3DWire::MaxKeyframesPerTrack) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "Animator3D clip contains an invalid track");
            }
            const u32 sortKey = trackSortKey(source->jointIndex, source->channel);
            if (hasPrevious && sortKey <= previousSortKey) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "Animator3D clip tracks are not strictly ordered");
            }
            previousSortKey = sortKey;
            hasPrevious = true;

            const auto times = clip.trackTimes(trackIndex);
            const auto values = clip.trackValues(trackIndex);
            const u16 componentCount = AssetFormat::animationChannelComponentCount(source->channel);
            if (times.size() != source->keyCount
                || values.size() != static_cast<usize>(source->keyCount) * componentCount) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "Animator3D clip track ranges are invalid");
            }
            float previousTime = -1.0F;
            for (const float time : times) {
                if (!std::isfinite(time) || time < 0.0F || time <= previousTime
                    || time > clip.durationSeconds) {
                    return Core::failure(
                        SceneErrorCode::InvalidAnimation,
                        "Animator3D clip key times are invalid");
                }
                previousTime = time;
            }
            maximumLastTime = std::max(maximumLastTime, times.back());
            if (!std::ranges::all_of(
                    values,
                    [](float value) { return std::isfinite(value); })) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "Animator3D clip values must be finite");
            }
            if (source->channel == AssetFormat::AnimationChannel::Rotation) {
                for (usize key = 0; key < source->keyCount; ++key) {
                    const usize base = key * 4U;
                    const Math::Quaternion rotation{
                        values[base], values[base + 1U], values[base + 2U], values[base + 3U]};
                    if (!isValid(LocalTransform{.rotation = rotation})) {
                        return Core::failure(
                            SceneErrorCode::InvalidAnimation,
                            "Animator3D rotation keys must be normalizable");
                    }
                }
            }
            replacementTracks.push_back(Track{
                .jointIndex = source->jointIndex,
                .channel = source->channel,
                .interpolation = source->interpolation,
                .keyCount = source->keyCount,
                .timeOffset = static_cast<u32>(replacementTimes.size()),
                .valueOffset = static_cast<u32>(replacementValues.size()),
            });
            replacementTimes.insert(replacementTimes.end(), times.begin(), times.end());
            replacementValues.insert(replacementValues.end(), values.begin(), values.end());
        }
        if (replacementTimes.size() != clip.totalKeyframeCount
            || replacementValues.size() != clip.totalValueFloatCount
            || maximumLastTime != clip.durationSeconds) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D clip blocks or duration are not canonical");
        }
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Animator3D could not allocate clip storage");
    } catch (const std::exception& exception) {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...) {
        return Core::failure(
            Core::CoreErrorCode::Internal,
            "Animator3D clip copy failed with an unknown exception");
    }

    const auto oldMode = m_playbackMode;
    const float oldDuration = m_durationSeconds;
    const float oldTime = m_timeSeconds;
    const double oldCyclePosition = m_cyclePositionSeconds;
    const bool oldPlaying = m_playing;
    const bool oldCompleted = m_completed;
    m_tracks.swap(replacementTracks);
    m_times.swap(replacementTimes);
    m_values.swap(replacementValues);
    m_playbackMode = clip.playbackMode;
    m_durationSeconds = clip.durationSeconds;
    m_timeSeconds = 0.0F;
    m_cyclePositionSeconds = 0.0;
    m_playing = true;
    m_completed = false;
    if (const Core::Status status = evaluatePose(0.0F); !status) {
        m_tracks.swap(replacementTracks);
        m_times.swap(replacementTimes);
        m_values.swap(replacementValues);
        m_playbackMode = oldMode;
        m_durationSeconds = oldDuration;
        m_timeSeconds = oldTime;
        m_cyclePositionSeconds = oldCyclePosition;
        m_playing = oldPlaying;
        m_completed = oldCompleted;
        replacementTracks.clear();
        replacementTimes.clear();
        replacementValues.clear();
        return status;
    }
    replacementTracks.clear();
    replacementTimes.clear();
    replacementValues.clear();
    return Core::success();
}

Core::Status Animator3D::evaluatePose(float timeSeconds) noexcept
{
    std::copy(m_bindPose.begin(), m_bindPose.end(), m_localScratch.begin());
    for (const Track& track : m_tracks) {
        const auto times = std::span<const float>{m_times}.subspan(track.timeOffset, track.keyCount);
        const u16 componentCount = AssetFormat::animationChannelComponentCount(track.channel);
        const auto values = std::span<const float>{m_values}.subspan(
            track.valueOffset, static_cast<usize>(track.keyCount) * componentCount);
        usize firstKey = 0;
        usize secondKey = 0;
        float alpha = 0.0F;
        if (timeSeconds <= times.front()) {
            firstKey = secondKey = 0;
        } else if (timeSeconds >= times.back()) {
            firstKey = secondKey = times.size() - 1U;
        } else {
            const auto upper = std::upper_bound(times.begin(), times.end(), timeSeconds);
            secondKey = static_cast<usize>(upper - times.begin());
            firstKey = secondKey - 1U;
            if (track.interpolation == AssetFormat::AnimationInterpolation::Linear) {
                alpha = (timeSeconds - times[firstKey]) / (times[secondKey] - times[firstKey]);
            } else {
                secondKey = firstKey;
            }
        }
        const usize firstValue = firstKey * componentCount;
        const usize secondValue = secondKey * componentCount;
        LocalTransform& transform = m_localScratch[track.jointIndex];
        switch (track.channel) {
        case AssetFormat::AnimationChannel::Translation:
            transform.position = {
                values[firstValue] + (values[secondValue] - values[firstValue]) * alpha,
                values[firstValue + 1U] + (values[secondValue + 1U] - values[firstValue + 1U]) * alpha,
                values[firstValue + 2U] + (values[secondValue + 2U] - values[firstValue + 2U]) * alpha,
            };
            break;
        case AssetFormat::AnimationChannel::Scale:
            transform.scale = {
                values[firstValue] + (values[secondValue] - values[firstValue]) * alpha,
                values[firstValue + 1U] + (values[secondValue + 1U] - values[firstValue + 1U]) * alpha,
                values[firstValue + 2U] + (values[secondValue + 2U] - values[firstValue + 2U]) * alpha,
            };
            break;
        case AssetFormat::AnimationChannel::Rotation: {
            const Math::Quaternion from{
                values[firstValue], values[firstValue + 1U],
                values[firstValue + 2U], values[firstValue + 3U]};
            const Math::Quaternion to{
                values[secondValue], values[secondValue + 1U],
                values[secondValue + 2U], values[secondValue + 3U]};
            transform.rotation = firstKey == secondKey
                ? Math::normalized(from)
                : Math::slerp(from, to, alpha);
            break;
        }
        case AssetFormat::AnimationChannel::Invalid:
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D encountered an invalid channel");
        }
        if (!isValid(transform)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D evaluation produced an invalid local pose");
        }
    }

    for (usize jointIndex = 0; jointIndex < m_localScratch.size(); ++jointIndex) {
        const Math::Mat4 local = transformMatrix(m_localScratch[jointIndex]);
        Math::Mat4 global = local;
        const u16 parent = m_parents[jointIndex];
        if (parent != AssetFormat::SkinnedMeshWire::JointIndexNone) {
            const auto parentMatrix = std::span<const float, 16>{
                m_globalScratch.data() + static_cast<usize>(parent) * 16U, 16U};
            global = Math::multiply(loadMatrix(parentMatrix), local);
        }
        if (!Math::isFinite(global)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D global pose matrix overflowed");
        }
        storeMatrix(global,
            std::span<float, 16>{m_globalScratch.data() + jointIndex * 16U, 16U});
        const auto inverseBind = std::span<const float, 16>{
            m_inverseBindMatrices.data() + jointIndex * 16U, 16U};
        const Math::Mat4 skinning = Math::multiply(global, loadMatrix(inverseBind));
        if (!Math::isFinite(skinning)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "Animator3D skinning matrix overflowed");
        }
        storeMatrix(skinning,
            std::span<float, 16>{m_skinningScratch.data() + jointIndex * 16U, 16U});
    }
    m_localPose.swap(m_localScratch);
    m_globalMatrices.swap(m_globalScratch);
    m_skinningMatrices.swap(m_skinningScratch);
    return Core::success();
}

Core::Status Animator3D::setPlaybackSpeed(float speed) noexcept
{
    if (!(speed > 0.0F) || !std::isfinite(speed)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "Animator3D playback speed must be finite and greater than zero");
    }
    m_playbackSpeed = speed;
    return Core::success();
}

Core::Result<Animator3DUpdate> Animator3D::update(Core::Duration delta) noexcept
{
    const double deltaSeconds = delta.count();
    if (deltaSeconds < 0.0 || !std::isfinite(deltaSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "Animator3D update delta must be finite and non-negative");
    }
    const float previousTime = m_timeSeconds;
    if (!m_playing || deltaSeconds == 0.0) {
        return Animator3DUpdate{
            .previousTimeSeconds = previousTime,
            .currentTimeSeconds = m_timeSeconds,
        };
    }
    const double advance = deltaSeconds * static_cast<double>(m_playbackSpeed);
    const double nextCyclePosition = m_cyclePositionSeconds + advance;
    if (!std::isfinite(nextCyclePosition)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "Animator3D playhead overflowed");
    }

    float nextTime = 0.0F;
    double committedCyclePosition = nextCyclePosition;
    bool completed = false;
    if (m_playbackMode == AssetFormat::AnimationClip3DPlaybackMode::Once) {
        if (nextCyclePosition >= m_durationSeconds) {
            committedCyclePosition = m_durationSeconds;
            nextTime = m_durationSeconds;
            completed = true;
        } else {
            nextTime = static_cast<float>(nextCyclePosition);
        }
    } else if (m_playbackMode == AssetFormat::AnimationClip3DPlaybackMode::Loop) {
        committedCyclePosition = std::fmod(nextCyclePosition, m_durationSeconds);
        nextTime = static_cast<float>(committedCyclePosition);
    } else {
        const double cycleDuration = static_cast<double>(m_durationSeconds) * 2.0;
        committedCyclePosition = std::fmod(nextCyclePosition, cycleDuration);
        nextTime = static_cast<float>(committedCyclePosition <= m_durationSeconds
            ? committedCyclePosition
            : cycleDuration - committedCyclePosition);
    }

    if (const Core::Status status = evaluatePose(nextTime); !status) {
        return Core::failure(status.error());
    }
    const bool wasCompleted = m_completed;
    m_cyclePositionSeconds = committedCyclePosition;
    m_timeSeconds = nextTime;
    m_completed = completed;
    if (completed) {
        m_playing = false;
    }
    return Animator3DUpdate{
        .previousTimeSeconds = previousTime,
        .currentTimeSeconds = m_timeSeconds,
        .poseChanged = previousTime != m_timeSeconds,
        .completedThisUpdate = !wasCompleted && completed,
    };
}

void Animator3D::play() noexcept
{
    if (m_completed) {
        restart();
        return;
    }
    m_playing = true;
}

void Animator3D::pause() noexcept
{
    m_playing = false;
}

void Animator3D::restart() noexcept
{
    m_timeSeconds = 0.0F;
    m_cyclePositionSeconds = 0.0;
    m_playing = true;
    m_completed = false;
    (void)evaluatePose(0.0F);
}

void Animator3D::stop() noexcept
{
    restart();
    m_playing = false;
}

} // namespace Tina::Scene
