#include <tina/scene/SpriteAnimator2D.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <cmath>
#include <exception>
#include <new>

namespace Tina::Scene {
namespace {

struct ResolvedTimelinePosition final {
    usize frameIndex = 0;
    double elapsedSeconds = 0.0;
};

[[nodiscard]] bool isValidPlaybackMode(SpriteAnimationPlaybackMode mode) noexcept
{
    switch (mode) {
    case SpriteAnimationPlaybackMode::Once:
    case SpriteAnimationPlaybackMode::Loop:
    case SpriteAnimationPlaybackMode::PingPong:
        return true;
    }
    return false;
}

[[nodiscard]] Core::Status validateClip(SpriteAnimationClip2D clip) noexcept
{
    if (clip.frames.empty()) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimationClip2D must contain at least one frame");
    }
    if (!isValidPlaybackMode(clip.playbackMode)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimationClip2D playback mode is invalid");
    }
    double timelineSeconds = 0.0;
    for (const SpriteAnimationFrame2D& frame : clip.frames) {
        if (!isValid(frame.sprite)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "SpriteAnimationClip2D contains an invalid SpriteRenderer2D frame");
        }
        const double seconds = frame.duration.count();
        if (!(seconds > 0.0) || !std::isfinite(seconds)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "SpriteAnimationClip2D frame duration must be finite and greater than zero");
        }
        timelineSeconds += seconds;
        if (!std::isfinite(timelineSeconds)) {
            return Core::failure(
                SceneErrorCode::InvalidAnimation,
                "SpriteAnimationClip2D total duration overflowed");
        }
    }
    if (clip.playbackMode == SpriteAnimationPlaybackMode::PingPong && clip.frames.size() >= 3) {
        for (usize index = clip.frames.size() - 2; index > 0; --index) {
            timelineSeconds += clip.frames[index].duration.count();
            if (!std::isfinite(timelineSeconds)) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "SpriteAnimationClip2D ping-pong duration overflowed");
            }
        }
    }
    return Core::success();
}

[[nodiscard]] double forwardDuration(std::span<const SpriteAnimationFrame2D> frames) noexcept
{
    double total = 0.0;
    for (const SpriteAnimationFrame2D& frame : frames) {
        total += frame.duration.count();
    }
    return total;
}

[[nodiscard]] double timelineDuration(
    std::span<const SpriteAnimationFrame2D> frames,
    SpriteAnimationPlaybackMode mode) noexcept
{
    double total = forwardDuration(frames);
    if (mode != SpriteAnimationPlaybackMode::PingPong || frames.size() < 3) {
        return total;
    }
    for (usize index = frames.size() - 2; index > 0; --index) {
        total += frames[index].duration.count();
    }
    return total;
}

[[nodiscard]] bool consumeFrame(
    std::span<const SpriteAnimationFrame2D> frames,
    usize frameIndex,
    double& remainingSeconds,
    ResolvedTimelinePosition& resolved) noexcept
{
    const double duration = frames[frameIndex].duration.count();
    if (remainingSeconds < duration) {
        resolved = {.frameIndex = frameIndex, .elapsedSeconds = remainingSeconds};
        return true;
    }
    remainingSeconds -= duration;
    return false;
}

[[nodiscard]] ResolvedTimelinePosition resolveTimelinePosition(
    std::span<const SpriteAnimationFrame2D> frames,
    SpriteAnimationPlaybackMode mode,
    double playheadSeconds) noexcept
{
    double remainingSeconds = playheadSeconds;
    ResolvedTimelinePosition resolved{};
    for (usize index = 0; index < frames.size(); ++index) {
        if (consumeFrame(frames, index, remainingSeconds, resolved)) {
            return resolved;
        }
    }

    if (mode == SpriteAnimationPlaybackMode::PingPong && frames.size() >= 3) {
        for (usize index = frames.size() - 2; index > 0; --index) {
            if (consumeFrame(frames, index, remainingSeconds, resolved)) {
                return resolved;
            }
        }
    }

    // Floating-point roundoff at a cycle boundary resolves to the first frame.
    return {};
}

} // namespace

SpriteAnimator2D::SpriteAnimator2D(std::pmr::memory_resource& resource) noexcept
    : m_frames(&resource)
{
}

Core::Result<SpriteAnimator2D> SpriteAnimator2D::Create(
    SpriteAnimationClip2D clip,
    std::pmr::memory_resource& resource)
{
    SpriteAnimator2D animator(resource);
    if (const Core::Status status = animator.setClip(clip); !status) {
        return Core::failure(status.error());
    }
    return animator;
}

Core::Status SpriteAnimator2D::setClip(SpriteAnimationClip2D clip)
{
    if (const Core::Status status = validateClip(clip); !status) {
        return status;
    }

    std::pmr::vector<SpriteAnimationFrame2D> replacement{m_frames.get_allocator().resource()};
    try {
        replacement.assign(clip.frames.begin(), clip.frames.end());
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "SpriteAnimator2D could not allocate clip frame storage");
    } catch (const std::exception& exception) {
        return Core::failure(Core::CoreErrorCode::Internal, exception.what());
    } catch (...) {
        return Core::failure(
            Core::CoreErrorCode::Internal,
            "SpriteAnimator2D clip copy failed with an unknown exception");
    }

    m_frames.swap(replacement);
    m_playbackMode = clip.playbackMode;
    restart();
    return Core::success();
}

Core::Status SpriteAnimator2D::setPlaybackSpeed(float speed) noexcept
{
    if (!(speed > 0.0F) || !std::isfinite(speed)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimator2D playback speed must be finite and greater than zero");
    }
    m_playbackSpeed = speed;
    return Core::success();
}

Core::Result<SpriteAnimator2DUpdate> SpriteAnimator2D::update(Core::Duration delta) noexcept
{
    const usize previousFrameIndex = m_frameIndex;
    const bool wasCompleted = m_completed;
    const double deltaSeconds = delta.count();
    if (deltaSeconds < 0.0 || !std::isfinite(deltaSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimator2D update delta must be finite and non-negative");
    }
    if (!m_playing || deltaSeconds == 0.0 || m_frames.empty()) {
        return SpriteAnimator2DUpdate{
            .previousFrameIndex = previousFrameIndex,
            .currentFrameIndex = m_frameIndex,
            .currentFrameChanged = false,
            .completedThisUpdate = false,
        };
    }

    const double advanceSeconds = deltaSeconds * static_cast<double>(m_playbackSpeed);
    if (!std::isfinite(advanceSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimator2D scaled update delta overflowed");
    }

    const auto frames = std::span<const SpriteAnimationFrame2D>{m_frames};
    const double totalSeconds = timelineDuration(frames, m_playbackMode);
    double nextPlayhead = m_playheadSeconds + advanceSeconds;
    if (!std::isfinite(nextPlayhead)) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimator2D playhead overflowed");
    }

    if (m_playbackMode == SpriteAnimationPlaybackMode::Once && nextPlayhead >= totalSeconds) {
        m_playheadSeconds = totalSeconds;
        m_frameIndex = m_frames.size() - 1;
        m_frameElapsedSeconds = m_frames.back().duration.count();
        m_playing = false;
        m_completed = true;
    } else {
        nextPlayhead = std::fmod(nextPlayhead, totalSeconds);
        if (nextPlayhead < 0.0) {
            nextPlayhead += totalSeconds;
        }
        m_playheadSeconds = nextPlayhead;
        const ResolvedTimelinePosition position =
            resolveTimelinePosition(frames, m_playbackMode, m_playheadSeconds);
        m_frameIndex = position.frameIndex;
        m_frameElapsedSeconds = position.elapsedSeconds;
        m_completed = false;
    }

    return SpriteAnimator2DUpdate{
        .previousFrameIndex = previousFrameIndex,
        .currentFrameIndex = m_frameIndex,
        .currentFrameChanged = previousFrameIndex != m_frameIndex,
        .completedThisUpdate = !wasCompleted && m_completed,
    };
}

void SpriteAnimator2D::play() noexcept
{
    if (m_completed) {
        restart();
        return;
    }
    m_playing = true;
}

void SpriteAnimator2D::pause() noexcept
{
    m_playing = false;
}

void SpriteAnimator2D::restart() noexcept
{
    m_frameIndex = 0;
    m_playheadSeconds = 0.0;
    m_frameElapsedSeconds = 0.0;
    m_playing = true;
    m_completed = false;
}

void SpriteAnimator2D::stop() noexcept
{
    restart();
    m_playing = false;
}

const SpriteRenderer2D* SpriteAnimator2D::currentSprite() const noexcept
{
    return m_frameIndex < m_frames.size() ? &m_frames[m_frameIndex].sprite : nullptr;
}

} // namespace Tina::Scene
