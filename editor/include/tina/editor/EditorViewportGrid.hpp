#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <span>

namespace Tina::Editor {

inline constexpr Core::usize EditorViewportGridSegmentCapacity = 160;

enum class EditorViewportGridProjection : Core::u8 {
    Orthographic2D,
    Perspective3D,
};

enum class EditorViewportGridSegmentKind : Core::u8 {
    Minor,
    Major,
    AxisX,
    AxisY,
    AxisZ,
};

// Normalized projected segment. Coordinates are relative to the viewport
// content box and remain in [0, 1], so adapters need no backend camera types.
struct EditorViewportGridSegment final {
    float startX = 0.0F;
    float startY = 0.0F;
    float endX = 0.0F;
    float endY = 0.0F;
    EditorViewportGridSegmentKind kind = EditorViewportGridSegmentKind::Minor;

    friend bool operator==(const EditorViewportGridSegment&,
                           const EditorViewportGridSegment&) = default;
};

struct EditorViewportGridConfig final {
    EditorViewportGridProjection projection =
        EditorViewportGridProjection::Orthographic2D;
    float logicalWidth = 0.0F;
    float logicalHeight = 0.0F;
    float zoomPercent = 100.0F;
    float cameraCenterX = 0.0F;
    float cameraCenterY = 0.0F;
    float cameraTargetX = 0.0F;
    float cameraTargetY = 0.0F;
    float cameraTargetZ = 0.0F;
    float cameraYawRadians = 0.0F;
    float cameraPitchRadians = 0.35F;
    float cameraDistance = 10.0F;
    float verticalFovDegrees = 55.0F;
    float worldGridStep = 1.0F;
    Core::u32 majorLineEvery = 5;

    friend bool operator==(const EditorViewportGridConfig&,
                           const EditorViewportGridConfig&) = default;
};

struct EditorViewportGridStats final {
    Core::u64 revision = 0;
    Core::u32 segmentCount = 0;
    Core::u32 minorSegmentCount = 0;
    Core::u32 majorSegmentCount = 0;
    Core::u32 axisSegmentCount = 0;

    friend bool operator==(const EditorViewportGridStats&,
                           const EditorViewportGridStats&) = default;
};

// Fixed-capacity grid publication used by Editor adapters. update() is
// transactional: invalid input or capacity failure preserves the prior view.
class EditorViewportGrid final {
  public:
    [[nodiscard]] Core::Result<bool> update(const EditorViewportGridConfig& config);

    [[nodiscard]] std::span<const EditorViewportGridSegment> segments() const noexcept
    {
        return std::span(m_segments.data(), m_stats.segmentCount);
    }

    [[nodiscard]] const EditorViewportGridStats& stats() const noexcept
    {
        return m_stats;
    }

    [[nodiscard]] const EditorViewportGridConfig* config() const noexcept
    {
        return m_hasConfig ? &m_config : nullptr;
    }

  private:
    EditorViewportGridConfig m_config{};
    std::array<EditorViewportGridSegment, EditorViewportGridSegmentCapacity>
        m_segments{};
    EditorViewportGridStats m_stats{};
    bool m_hasConfig = false;
};

} // namespace Tina::Editor
