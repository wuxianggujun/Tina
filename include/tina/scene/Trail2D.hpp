#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Scene {

struct Trail2DConfig final {
    usize segmentCapacity = 0;
    Core::Duration segmentLifetime{};
    float startWidthMeters = 1.0F;
    float endWidthMeters = 1.0F;
    u32 spriteKey = 0;
    // First render-facing stable key. Successful segments consume monotonically
    // increasing keys starting here; expired segment keys are never reused.
    u64 stableEntityKeyBase = 1;
    SpriteUvRect uvRect{};
    ColorRgba8 color{};
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
};

struct Trail2DSegment final {
    Vec2 start{};
    Vec2 end{};
    Core::Duration age{};
    Core::Duration lifetime{};
    u64 stableEntityKey = 0;
};

// Owner-thread fixed-capacity trail. Create performs the only persistent PMR
// allocation; successful appendPoint(), update(), and extract() calls do not
// grow trail storage.
class Trail2D final {
public:
    [[nodiscard]] static Core::Result<Trail2D> Create(
        Trail2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~Trail2D() noexcept = default;

    Trail2D(const Trail2D&) = delete;
    Trail2D& operator=(const Trail2D&) = delete;
    Trail2D(Trail2D&&) noexcept = default;
    Trail2D& operator=(Trail2D&&) = delete;

    // The first point establishes an anchor. Each later point appends one
    // segment from the previous anchor and becomes the new anchor.
    [[nodiscard]] Core::Status appendPoint(Vec2 point) noexcept;
    // The next append establishes a new anchor without connecting to the old one.
    void breakTrail() noexcept;
    [[nodiscard]] Core::Status update(Core::Duration delta) noexcept;
    [[nodiscard]] Core::Status extract(Render::RenderSceneWriter& writer) const noexcept;

    [[nodiscard]] std::span<const Trail2DSegment> segments() const noexcept { return m_segments; }
    [[nodiscard]] usize segmentCount() const noexcept { return m_segments.size(); }
    [[nodiscard]] usize segmentCapacity() const noexcept { return m_config.segmentCapacity; }
    [[nodiscard]] bool hasAnchor() const noexcept { return m_hasAnchor; }
    [[nodiscard]] Trail2DConfig config() const noexcept { return m_config; }

private:
    Trail2D(
        Trail2DConfig config,
        std::pmr::vector<Trail2DSegment> segments) noexcept;

    Trail2DConfig m_config{};
    std::pmr::vector<Trail2DSegment> m_segments;
    Vec2 m_anchor{};
    u64 m_nextStableEntityKey = 1;
    bool m_hasAnchor = false;
    bool m_stableEntityKeysExhausted = false;
};

} // namespace Tina::Scene
