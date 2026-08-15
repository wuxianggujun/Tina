#pragma once

#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/scene/Transform.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Scene {

struct Animator3DUpdate final {
    float previousTimeSeconds = 0.0F;
    float currentTimeSeconds = 0.0F;
    bool poseChanged = false;
    bool completedThisUpdate = false;
};

// Owner-thread CPU evaluator for one frozen SkinnedMesh v1 skeleton and one
// AnimationClip3D v1 clip. Create()/setClip() copy all borrowed wire data;
// update() performs no allocation. Matrices are column-major and ordered by
// joint index, with skinningMatrices() containing globalPose * inverseBind.
class Animator3D final {
public:
    [[nodiscard]] static Core::Result<Animator3D> Create(
        const AssetFormat::SkinnedMeshPayloadView& mesh,
        const AssetFormat::AnimationClip3DPayloadView& clip,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~Animator3D() noexcept = default;

    Animator3D(const Animator3D&) = delete;
    Animator3D& operator=(const Animator3D&) = delete;
    Animator3D(Animator3D&&) noexcept = default;
    Animator3D& operator=(Animator3D&&) = delete;

    // Replacement is transactional and must target the existing skeleton's
    // exact joint count. Success restarts playback at t=0.
    [[nodiscard]] Core::Status setClip(
        const AssetFormat::AnimationClip3DPayloadView& clip);
    [[nodiscard]] Core::Status setPlaybackSpeed(float speed) noexcept;
    [[nodiscard]] Core::Result<Animator3DUpdate> update(Core::Duration delta) noexcept;

    void play() noexcept;
    void pause() noexcept;
    void restart() noexcept;
    void stop() noexcept;

    [[nodiscard]] std::span<const LocalTransform> localPose() const noexcept
    {
        return m_localPose;
    }

    [[nodiscard]] std::span<const float> skinningMatrices() const noexcept
    {
        return m_skinningMatrices;
    }

    [[nodiscard]] u16 jointCount() const noexcept
    {
        return static_cast<u16>(m_bindPose.size());
    }

    [[nodiscard]] usize trackCount() const noexcept { return m_tracks.size(); }
    [[nodiscard]] float durationSeconds() const noexcept { return m_durationSeconds; }
    [[nodiscard]] float timeSeconds() const noexcept { return m_timeSeconds; }
    [[nodiscard]] float playbackSpeed() const noexcept { return m_playbackSpeed; }
    [[nodiscard]] AssetFormat::AnimationClip3DPlaybackMode playbackMode() const noexcept
    {
        return m_playbackMode;
    }
    [[nodiscard]] bool isPlaying() const noexcept { return m_playing; }
    [[nodiscard]] bool isCompleted() const noexcept { return m_completed; }

private:
    struct Track final {
        u16 jointIndex = 0;
        AssetFormat::AnimationChannel channel = AssetFormat::AnimationChannel::Invalid;
        AssetFormat::AnimationInterpolation interpolation = AssetFormat::AnimationInterpolation::Invalid;
        u32 keyCount = 0;
        u32 timeOffset = 0;
        u32 valueOffset = 0;
    };

    explicit Animator3D(std::pmr::memory_resource& resource) noexcept;

    [[nodiscard]] Core::Status initializeSkeleton(
        const AssetFormat::SkinnedMeshPayloadView& mesh);
    [[nodiscard]] Core::Status evaluatePose(float timeSeconds) noexcept;

    std::pmr::vector<u16> m_parents;
    std::pmr::vector<LocalTransform> m_bindPose;
    std::pmr::vector<float> m_inverseBindMatrices;
    std::pmr::vector<Track> m_tracks;
    std::pmr::vector<float> m_times;
    std::pmr::vector<float> m_values;
    // Preconstructed staging containers keep setClip() allocation failures
    // catchable on debug standard libraries that allocate iterator proxies in
    // vector construction.
    std::pmr::vector<Track> m_stagedTracks;
    std::pmr::vector<float> m_stagedTimes;
    std::pmr::vector<float> m_stagedValues;
    std::pmr::vector<LocalTransform> m_localPose;
    std::pmr::vector<float> m_globalMatrices;
    std::pmr::vector<float> m_skinningMatrices;
    std::pmr::vector<LocalTransform> m_localScratch;
    std::pmr::vector<float> m_globalScratch;
    std::pmr::vector<float> m_skinningScratch;
    AssetFormat::AnimationClip3DPlaybackMode m_playbackMode =
        AssetFormat::AnimationClip3DPlaybackMode::Loop;
    float m_durationSeconds = 0.0F;
    float m_timeSeconds = 0.0F;
    double m_cyclePositionSeconds = 0.0;
    float m_playbackSpeed = 1.0F;
    bool m_playing = true;
    bool m_completed = false;
};

} // namespace Tina::Scene
