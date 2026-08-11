#include <tina/scene/SpriteAnimator2D.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
        if (!frame.sprite.sprite || !isValid(frame.sprite)) {
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
        for (const SpriteAnimationEvent2D& event : frame.events) {
            if (event.tag == 0) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "SpriteAnimationEvent2D tag must be non-zero");
            }
            if (!std::isfinite(event.normalizedOffset) || event.normalizedOffset < 0.0F
                || event.normalizedOffset > 1.0F) {
                return Core::failure(
                    SceneErrorCode::InvalidAnimation,
                    "SpriteAnimationEvent2D offset must be finite and within [0,1]");
            }
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

SpriteAnimator2D::SpriteAnimator2D(std::pmr::memory_resource& resource, usize eventCapacity)
    : m_frames(&resource),
      m_events(&resource),
      m_frameEvents(&resource),
      m_segments(&resource),
      m_crossedEvents(eventCapacity, SpriteAnimationEventCrossing2D{}, &resource)
{
}

Core::Result<SpriteAnimator2D> SpriteAnimator2D::Create(
    SpriteAnimationClip2D clip,
    std::pmr::memory_resource& resource,
    SpriteAnimator2DConfig config)
{
    if (config.eventCapacity == 0) {
        return Core::failure(
            SceneErrorCode::InvalidAnimation,
            "SpriteAnimator2DConfig event capacity must be greater than zero");
    }
    try {
        SpriteAnimator2D animator(resource, config.eventCapacity);
        if (const Core::Status status = animator.setClip(clip); !status) {
            return Core::failure(status.error());
        }
        return animator;
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "SpriteAnimator2D could not allocate event crossing storage");
    }
}

Core::Status SpriteAnimator2D::setClip(SpriteAnimationClip2D clip)
{
    if (const Core::Status status = validateClip(clip); !status) {
        return status;
    }

    std::pmr::memory_resource* resource = m_frames.get_allocator().resource();
    std::pmr::vector<SpriteAnimationFrame2D> replacement{resource};
    std::pmr::vector<SpriteAnimationEvent2D> events{resource};
    std::pmr::vector<FrameEventRange> frameEvents{resource};
    std::pmr::vector<TimelineSegment> segments{resource};
    try {
        replacement.assign(clip.frames.begin(), clip.frames.end());
        frameEvents.reserve(clip.frames.size());
        for (usize index = 0; index < clip.frames.size(); ++index) {
            const auto& sourceEvents = clip.frames[index].events;
            const usize begin = events.size();
            events.insert(events.end(), sourceEvents.begin(), sourceEvents.end());
            // Ascending offset makes a forward segment sweep temporally ordered.
            // Equal offsets keep authored order, so crossings stay deterministic.
            std::stable_sort(
                events.begin() + static_cast<std::ptrdiff_t>(begin),
                events.end(),
                [](const SpriteAnimationEvent2D& left, const SpriteAnimationEvent2D& right) {
                    return left.normalizedOffset < right.normalizedOffset;
                });
            frameEvents.push_back(FrameEventRange{.begin = begin, .count = sourceEvents.size()});
            // The stored frame must not keep the caller's borrowed span alive.
            replacement[index].events = {};
        }

        const bool pingPong = clip.playbackMode == SpriteAnimationPlaybackMode::PingPong
            && clip.frames.size() >= 3;
        segments.reserve(clip.frames.size() + (pingPong ? clip.frames.size() - 2 : 0));
        double cursor = 0.0;
        for (usize index = 0; index < clip.frames.size(); ++index) {
            const double duration = clip.frames[index].duration.count();
            segments.push_back(TimelineSegment{
                .startSeconds = cursor,
                .durationSeconds = duration,
                .frameIndex = index,
                .forward = true});
            cursor += duration;
        }
        if (pingPong) {
            for (usize index = clip.frames.size() - 2; index > 0; --index) {
                const double duration = clip.frames[index].duration.count();
                segments.push_back(TimelineSegment{
                    .startSeconds = cursor,
                    .durationSeconds = duration,
                    .frameIndex = index,
                    .forward = false});
                cursor += duration;
            }
        }
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
    m_events.swap(events);
    m_frameEvents.swap(frameEvents);
    m_segments.swap(segments);
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

void SpriteAnimator2D::collectSpanCrossings(
    double fromSeconds,
    double toSeconds,
    bool includeEnd,
    usize& outCount,
    bool& outOverflow) noexcept
{
    if (m_events.empty() || !(toSeconds >= fromSeconds)) {
        return;
    }

    for (const TimelineSegment& segment : m_segments) {
        // Segments are ordered and non-overlapping, so stop once past the span.
        if (segment.startSeconds >= toSeconds && !(includeEnd && segment.startSeconds == toSeconds)) {
            break;
        }
        const double segmentEnd = segment.startSeconds + segment.durationSeconds;
        if (segmentEnd < fromSeconds) {
            continue;
        }
        const FrameEventRange range = m_frameEvents[segment.frameIndex];
        if (range.count == 0) {
            continue;
        }

        for (usize step = 0; step < range.count; ++step) {
            // A reverse segment plays the frame backwards, so its authored
            // offsets map to mirrored positions and must be swept in reverse.
            const usize eventIndex = segment.forward
                ? range.begin + step
                : range.begin + (range.count - 1U - step);
            const SpriteAnimationEvent2D& event = m_events[eventIndex];
            const double localOffset = segment.forward
                ? static_cast<double>(event.normalizedOffset)
                : 1.0 - static_cast<double>(event.normalizedOffset);
            const double position = segment.startSeconds + localOffset * segment.durationSeconds;

            // Half-open [from, to) keeps a boundary event from firing twice
            // across consecutive updates; includeEnd closes the final instant of
            // a Once clip so an offset-1.0 event on the last frame still fires.
            const bool crossed = position >= fromSeconds
                && (position < toSeconds || (includeEnd && position == toSeconds));
            if (!crossed) {
                continue;
            }
            if (outCount >= m_crossedEvents.size()) {
                outOverflow = true;
                return;
            }
            m_crossedEvents[outCount++] = SpriteAnimationEventCrossing2D{
                .tag = event.tag,
                .frameIndex = segment.frameIndex,
                .normalizedOffset = event.normalizedOffset,
                .forward = segment.forward};
        }
    }
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
            .crossedEvents = {},
            .crossedEventOverflow = false,
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

    const double startPlayhead = m_playheadSeconds;
    usize crossedCount = 0;
    bool crossedOverflow = false;

    if (m_playbackMode == SpriteAnimationPlaybackMode::Once && nextPlayhead >= totalSeconds) {
        // Sweep to the very end and close the final instant so an event authored
        // at offset 1.0 of the last frame still fires on the completing update.
        collectSpanCrossings(startPlayhead, totalSeconds, true, crossedCount, crossedOverflow);
        m_playheadSeconds = totalSeconds;
        m_frameIndex = m_frames.size() - 1;
        m_frameElapsedSeconds = m_frames.back().duration.count();
        m_playing = false;
        m_completed = true;
    } else {
        // A delta covering whole cycles would otherwise report the same events
        // once per lap. Sweep the tail of the current cycle, then at most one
        // full cycle, so each authored event fires at most once per update.
        const double travelled = nextPlayhead - startPlayhead;
        if (travelled >= totalSeconds) {
            collectSpanCrossings(0.0, totalSeconds, false, crossedCount, crossedOverflow);
        } else {
            const double rawEnd = nextPlayhead;
            if (rawEnd <= totalSeconds) {
                collectSpanCrossings(startPlayhead, rawEnd, false, crossedCount, crossedOverflow);
            } else {
                // Wrapped: finish this cycle, then sweep the new cycle's head.
                collectSpanCrossings(startPlayhead, totalSeconds, false, crossedCount, crossedOverflow);
                collectSpanCrossings(
                    0.0, rawEnd - totalSeconds, false, crossedCount, crossedOverflow);
            }
        }

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
        .crossedEvents = std::span<const SpriteAnimationEventCrossing2D>{
            m_crossedEvents.data(), crossedCount},
        .crossedEventOverflow = crossedOverflow,
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
