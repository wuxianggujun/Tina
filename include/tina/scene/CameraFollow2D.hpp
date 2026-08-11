#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/MonotonicClock.hpp>

#include <optional>

namespace Tina::Scene {

struct CameraFollowPoint2D final {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(const CameraFollowPoint2D&, const CameraFollowPoint2D&) noexcept = default;
};

struct CameraFollowBounds2D final {
    CameraFollowPoint2D minimum{};
    CameraFollowPoint2D maximum{};

    friend constexpr bool operator==(const CameraFollowBounds2D&, const CameraFollowBounds2D&) noexcept = default;
};

struct CameraFollow2DConfig final {
    CameraFollowPoint2D initialCenter{};
    CameraFollowPoint2D deadZoneHalfExtentsMeters{};
    // Empty means immediate movement to the dead-zone edge.
    std::optional<float> maximumSpeedMetersPerSecond{};
};

struct CameraFollow2DStep final {
    CameraFollowPoint2D target{};
    CameraFollowPoint2D viewportHalfExtentsMeters{};
    std::optional<CameraFollowBounds2D> worldBounds{};
    Core::Duration fixedDelta{};
};

// Allocation-free, owner-thread presentation camera controller. Simulation
// updates publish previous/current centers; render extraction may interpolate
// them without changing the authored Camera2D projection component.
class CameraFollow2D final {
public:
    [[nodiscard]] static Core::Result<CameraFollow2D> Create(CameraFollow2DConfig config) noexcept;

    [[nodiscard]] Core::Status fixedUpdate(const CameraFollow2DStep& step) noexcept;
    [[nodiscard]] Core::Status snapTo(
        CameraFollowPoint2D center,
        CameraFollowPoint2D viewportHalfExtentsMeters,
        std::optional<CameraFollowBounds2D> worldBounds = {}) noexcept;
    [[nodiscard]] Core::Result<CameraFollowPoint2D> interpolatedCenter(double interpolation) const noexcept;

    [[nodiscard]] CameraFollowPoint2D previousCenter() const noexcept { return m_previousCenter; }
    [[nodiscard]] CameraFollowPoint2D currentCenter() const noexcept { return m_currentCenter; }
    [[nodiscard]] const CameraFollow2DConfig& config() const noexcept { return m_config; }

private:
    explicit CameraFollow2D(CameraFollow2DConfig config) noexcept
        : m_config(config),
          m_previousCenter(config.initialCenter),
          m_currentCenter(config.initialCenter)
    {
    }

    CameraFollow2DConfig m_config{};
    CameraFollowPoint2D m_previousCenter{};
    CameraFollowPoint2D m_currentCenter{};
};

} // namespace Tina::Scene
