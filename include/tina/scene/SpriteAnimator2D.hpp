#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Scene {

enum class SpriteAnimationPlaybackMode : u8 {
    Once = 0,
    Loop = 1,
    PingPong = 2,
};

// A resolved animation frame. AssetId -> SpriteRenderer2D resolution happens at
// the Asset/Scene integration boundary, before the frame enters the animator.
struct SpriteAnimationFrame2D final {
    SpriteRenderer2D sprite{};
    Core::Duration duration{};
};

// Borrowed create/setClip input. SpriteAnimator2D copies every frame, so the
// caller only needs to keep this span alive for the duration of the call.
struct SpriteAnimationClip2D final {
    std::span<const SpriteAnimationFrame2D> frames{};
    SpriteAnimationPlaybackMode playbackMode = SpriteAnimationPlaybackMode::Loop;
};

struct SpriteAnimator2DUpdate final {
    usize previousFrameIndex = 0;
    usize currentFrameIndex = 0;
    bool currentFrameChanged = false;
    bool completedThisUpdate = false;
};

// Owner-thread playback cursor for resolved SpriteRenderer2D frames. Clip
// replacement may allocate; update() is allocation-free.
class SpriteAnimator2D final {
public:
    [[nodiscard]] static Core::Result<SpriteAnimator2D> Create(
        SpriteAnimationClip2D clip,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~SpriteAnimator2D() noexcept = default;

    SpriteAnimator2D(const SpriteAnimator2D&) = delete;
    SpriteAnimator2D& operator=(const SpriteAnimator2D&) = delete;
    SpriteAnimator2D(SpriteAnimator2D&&) noexcept = default;
    SpriteAnimator2D& operator=(SpriteAnimator2D&&) = delete;

    [[nodiscard]] Core::Status setClip(SpriteAnimationClip2D clip);
    [[nodiscard]] Core::Status setPlaybackSpeed(float speed) noexcept;
    [[nodiscard]] Core::Result<SpriteAnimator2DUpdate> update(Core::Duration delta) noexcept;

    void play() noexcept;
    void pause() noexcept;
    void restart() noexcept;
    void stop() noexcept;

    [[nodiscard]] const SpriteRenderer2D* currentSprite() const noexcept;
    [[nodiscard]] usize frameIndex() const noexcept { return m_frameIndex; }
    [[nodiscard]] usize frameCount() const noexcept { return m_frames.size(); }
    [[nodiscard]] Core::Duration elapsedInCurrentFrame() const noexcept
    {
        return Core::Duration{m_frameElapsedSeconds};
    }
    [[nodiscard]] float playbackSpeed() const noexcept { return m_playbackSpeed; }
    [[nodiscard]] SpriteAnimationPlaybackMode playbackMode() const noexcept { return m_playbackMode; }
    [[nodiscard]] bool isPlaying() const noexcept { return m_playing; }
    [[nodiscard]] bool isCompleted() const noexcept { return m_completed; }

private:
    explicit SpriteAnimator2D(std::pmr::memory_resource& resource) noexcept;

    std::pmr::vector<SpriteAnimationFrame2D> m_frames;
    SpriteAnimationPlaybackMode m_playbackMode = SpriteAnimationPlaybackMode::Loop;
    usize m_frameIndex = 0;
    double m_playheadSeconds = 0.0;
    double m_frameElapsedSeconds = 0.0;
    float m_playbackSpeed = 1.0F;
    bool m_playing = true;
    bool m_completed = false;
};

} // namespace Tina::Scene
