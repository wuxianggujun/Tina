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

// A notify marker placed inside one frame. normalizedOffset is the position
// within that frame's own duration, so retiming a frame moves its events with
// it. Offset 0 fires as the frame is entered; offset 1 fires as it is left.
struct SpriteAnimationEvent2D final {
    // Non-zero gameplay identity. The animator never interprets the tag.
    u32 tag = 0;
    float normalizedOffset = 0.0F;

    friend constexpr bool operator==(
        const SpriteAnimationEvent2D&,
        const SpriteAnimationEvent2D&) noexcept = default;
};

// A resolved animation frame. AssetId -> SpriteRenderer2D resolution happens at
// the Asset/Scene integration boundary, before the frame enters the animator.
// Events are borrowed for the duration of the Create()/setClip() call only;
// the animator copies them into its own storage.
struct SpriteAnimationFrame2D final {
    SpriteRenderer2D sprite{};
    Core::Duration duration{};
    std::span<const SpriteAnimationEvent2D> events{};
};

// Borrowed create/setClip input. SpriteAnimator2D copies every frame, so the
// caller only needs to keep this span alive for the duration of the call.
struct SpriteAnimationClip2D final {
    std::span<const SpriteAnimationFrame2D> frames{};
    SpriteAnimationPlaybackMode playbackMode = SpriteAnimationPlaybackMode::Loop;
};

// One event the playhead crossed during a single update(), in the temporal
// order it was crossed. PingPong reverse segments report the frame index they
// belong to; the offset is the authored frame-local offset, not the mirrored
// timeline position.
struct SpriteAnimationEventCrossing2D final {
    u32 tag = 0;
    usize frameIndex = 0;
    float normalizedOffset = 0.0F;
    // False when the crossing happened while the playhead was moving backwards
    // through a PingPong reverse segment.
    bool forward = true;
};

struct SpriteAnimator2DUpdate final {
    usize previousFrameIndex = 0;
    usize currentFrameIndex = 0;
    bool currentFrameChanged = false;
    bool completedThisUpdate = false;
    // Events crossed by this update, in temporal order. Borrowed from the
    // animator and valid until the next update()/setClip()/restart()/stop() or
    // animator destruction.
    std::span<const SpriteAnimationEventCrossing2D> crossedEvents{};
    // True when more events were crossed than the configured event capacity.
    // crossedEvents then holds the first eventCapacity crossings in order.
    bool crossedEventOverflow = false;
};

struct SpriteAnimator2DConfig final {
    // Per-update crossing budget. update() never allocates, so a full buffer
    // truncates and reports crossedEventOverflow instead of growing.
    usize eventCapacity = 32;
};

// Owner-thread playback cursor for resolved SpriteRenderer2D frames. Clip
// replacement may allocate; update() is allocation-free.
class SpriteAnimator2D final {
public:
    [[nodiscard]] static Core::Result<SpriteAnimator2D> Create(
        SpriteAnimationClip2D clip,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource(),
        SpriteAnimator2DConfig config = {});

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
    [[nodiscard]] usize eventCapacity() const noexcept { return m_crossedEvents.size(); }
    [[nodiscard]] usize clipEventCount() const noexcept { return m_events.size(); }

private:
    // A frame's events after they are copied into animator-owned storage,
    // sorted by ascending offset so a segment sweep is already temporal.
    struct FrameEventRange final {
        usize begin = 0;
        usize count = 0;
    };

    // One traversal of one frame on the flattened timeline. A Loop/Once cycle is
    // frames 0..n-1 forward; PingPong appends frames n-2..1 in reverse. The
    // playhead always advances through segments, even reverse ones.
    struct TimelineSegment final {
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
        usize frameIndex = 0;
        bool forward = true;
    };

    SpriteAnimator2D(std::pmr::memory_resource& resource, usize eventCapacity);

    // Appends every event whose timeline position lies in [fromSeconds,
    // toSeconds), plus exactly toSeconds when includeEnd. Both bounds must be
    // inside one cycle. Stops early once the output budget is full.
    void collectSpanCrossings(
        double fromSeconds,
        double toSeconds,
        bool includeEnd,
        usize& outCount,
        bool& outOverflow) noexcept;

    std::pmr::vector<SpriteAnimationFrame2D> m_frames;
    // Flat event storage; m_frameEvents[i] indexes into it for frame i. Frames
    // stored in m_frames keep an empty events span so no borrowed pointer escapes.
    std::pmr::vector<SpriteAnimationEvent2D> m_events;
    std::pmr::vector<FrameEventRange> m_frameEvents;
    std::pmr::vector<TimelineSegment> m_segments;
    // Fixed-capacity per-update crossing output.
    std::pmr::vector<SpriteAnimationEventCrossing2D> m_crossedEvents;
    SpriteAnimationPlaybackMode m_playbackMode = SpriteAnimationPlaybackMode::Loop;
    usize m_frameIndex = 0;
    double m_playheadSeconds = 0.0;
    double m_frameElapsedSeconds = 0.0;
    float m_playbackSpeed = 1.0F;
    bool m_playing = true;
    bool m_completed = false;
};

} // namespace Tina::Scene
