#include <tina/editor/EditorTransformGizmo.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Editor {
namespace {

inline constexpr Core::usize AxisX = 0;
inline constexpr Core::usize AxisY = 1;
inline constexpr Core::usize AxisZ = 2;
inline constexpr double ProjectionEpsilon = 0.000001;
inline constexpr double RadiansToDegrees = 57.295779513082320876;

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(EditorTransformGizmoPoint value) noexcept
{
    return finite(value.x) && finite(value.y);
}

[[nodiscard]] bool finite(EditorTransformGizmoVector value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool validDimension(EditorTransformGizmoDimension value) noexcept
{
    return value == EditorTransformGizmoDimension::TwoD ||
           value == EditorTransformGizmoDimension::ThreeD;
}

[[nodiscard]] bool validMode(EditorTransformGizmoMode value) noexcept
{
    return value == EditorTransformGizmoMode::Translate ||
           value == EditorTransformGizmoMode::Rotate ||
           value == EditorTransformGizmoMode::Scale;
}

[[nodiscard]] bool validOrientation(EditorTransformGizmoOrientation value) noexcept
{
    return value == EditorTransformGizmoOrientation::World ||
           value == EditorTransformGizmoOrientation::Local;
}

[[nodiscard]] bool validConfig(const EditorTransformGizmoConfig& config) noexcept
{
    return finite(config.axisLengthPixels) && finite(config.axisHitRadiusPixels) &&
           finite(config.planeOffsetPixels) && finite(config.planeExtentPixels) &&
           finite(config.rotationRadiusPixels) &&
           finite(config.rotationRingSpacingPixels) &&
           finite(config.rotationHitRadiusPixels) &&
           finite(config.uniformHandleExtentPixels) &&
           finite(config.uniformScalePixelsPerUnit) &&
           finite(config.minimumScaleFactor) && finite(config.maximumScaleFactor) &&
           config.axisLengthPixels >= 8.0F && config.axisLengthPixels <= 4'096.0F &&
           config.axisHitRadiusPixels > 0.0F && config.axisHitRadiusPixels <= 64.0F &&
           config.planeOffsetPixels >= 0.0F && config.planeOffsetPixels <= 2'048.0F &&
           config.planeExtentPixels > 0.0F && config.planeExtentPixels <= 2'048.0F &&
           config.rotationRadiusPixels >= 8.0F &&
           config.rotationRadiusPixels <= 4'096.0F &&
           config.rotationRingSpacingPixels >= 0.0F &&
           config.rotationRingSpacingPixels <= 1'024.0F &&
           config.rotationHitRadiusPixels > 0.0F &&
           config.rotationHitRadiusPixels <= 64.0F &&
           config.rotationRadiusPixels - 2.0F * config.rotationRingSpacingPixels >
               config.rotationHitRadiusPixels &&
           config.uniformHandleExtentPixels > 0.0F &&
           config.uniformHandleExtentPixels <= 128.0F &&
           config.uniformScalePixelsPerUnit > 0.0F &&
           config.uniformScalePixelsPerUnit <= 100'000.0F &&
           config.minimumScaleFactor > 0.0F &&
           config.maximumScaleFactor > config.minimumScaleFactor &&
           config.maximumScaleFactor <= 1'000'000.0F;
}

[[nodiscard]] bool validSnap(const EditorTransformGizmoSnap& snap) noexcept
{
    return finite(snap.translationStep) && finite(snap.rotationStepDegrees) &&
           finite(snap.scaleStep) && snap.translationStep > 0.0F &&
           snap.rotationStepDegrees > 0.0F && snap.rotationStepDegrees <= 360.0F &&
           snap.scaleStep > 0.0F;
}

[[nodiscard]] double lengthSquared(EditorTransformGizmoPoint value) noexcept
{
    return static_cast<double>(value.x) * value.x +
           static_cast<double>(value.y) * value.y;
}

[[nodiscard]] double lengthSquared(EditorTransformGizmoVector value) noexcept
{
    return static_cast<double>(value.x) * value.x +
           static_cast<double>(value.y) * value.y +
           static_cast<double>(value.z) * value.z;
}

[[nodiscard]] EditorTransformGizmoPoint normalized(
    EditorTransformGizmoPoint value) noexcept
{
    const double length = std::sqrt(lengthSquared(value));
    if (!(length > ProjectionEpsilon)) {
        return {};
    }
    return {
        .x = static_cast<float>(value.x / length),
        .y = static_cast<float>(value.y / length),
    };
}

[[nodiscard]] EditorTransformGizmoVector normalized(
    EditorTransformGizmoVector value) noexcept
{
    const double length = std::sqrt(lengthSquared(value));
    if (!(length > ProjectionEpsilon)) {
        return {};
    }
    return {
        .x = static_cast<float>(value.x / length),
        .y = static_cast<float>(value.y / length),
        .z = static_cast<float>(value.z / length),
    };
}

[[nodiscard]] EditorTransformGizmoPoint add(EditorTransformGizmoPoint left,
                                             EditorTransformGizmoPoint right) noexcept
{
    return {.x = left.x + right.x, .y = left.y + right.y};
}

[[nodiscard]] EditorTransformGizmoPoint scale(EditorTransformGizmoPoint value,
                                               float factor) noexcept
{
    return {.x = value.x * factor, .y = value.y * factor};
}

[[nodiscard]] double cross(EditorTransformGizmoPoint left,
                           EditorTransformGizmoPoint right) noexcept
{
    return static_cast<double>(left.x) * right.y -
           static_cast<double>(left.y) * right.x;
}

[[nodiscard]] double pointSegmentDistanceSquared(
    EditorTransformGizmoPoint point,
    EditorTransformGizmoPoint start,
    EditorTransformGizmoPoint end) noexcept
{
    const double segmentX = static_cast<double>(end.x) - start.x;
    const double segmentY = static_cast<double>(end.y) - start.y;
    const double segmentLengthSquared = segmentX * segmentX + segmentY * segmentY;
    if (!(segmentLengthSquared > ProjectionEpsilon)) {
        const double deltaX = static_cast<double>(point.x) - start.x;
        const double deltaY = static_cast<double>(point.y) - start.y;
        return deltaX * deltaX + deltaY * deltaY;
    }
    const double t = std::clamp(
        ((static_cast<double>(point.x) - start.x) * segmentX +
         (static_cast<double>(point.y) - start.y) * segmentY) /
            segmentLengthSquared,
        0.0, 1.0);
    const double closestX = start.x + segmentX * t;
    const double closestY = start.y + segmentY * t;
    const double deltaX = point.x - closestX;
    const double deltaY = point.y - closestY;
    return deltaX * deltaX + deltaY * deltaY;
}

[[nodiscard]] bool pointInQuad(
    EditorTransformGizmoPoint point,
    const std::array<EditorTransformGizmoPoint, 4>& quad) noexcept
{
    const EditorTransformGizmoPoint first{
        .x = quad[1].x - quad[0].x,
        .y = quad[1].y - quad[0].y,
    };
    const EditorTransformGizmoPoint second{
        .x = quad[3].x - quad[0].x,
        .y = quad[3].y - quad[0].y,
    };
    const EditorTransformGizmoPoint relative{
        .x = point.x - quad[0].x,
        .y = point.y - quad[0].y,
    };
    const double determinant = cross(first, second);
    if (std::abs(determinant) <= ProjectionEpsilon) {
        return false;
    }
    const double u = cross(relative, second) / determinant;
    const double v = cross(first, relative) / determinant;
    return u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0;
}

[[nodiscard]] Core::usize axisIndex(EditorTransformGizmoHandle handle) noexcept
{
    switch (handle) {
    case EditorTransformGizmoHandle::AxisX:
        return AxisX;
    case EditorTransformGizmoHandle::AxisY:
        return AxisY;
    case EditorTransformGizmoHandle::AxisZ:
        return AxisZ;
    default:
        return EditorTransformGizmoAxisCount;
    }
}

[[nodiscard]] bool planeIndices(EditorTransformGizmoHandle handle,
                                Core::usize& first,
                                Core::usize& second) noexcept
{
    switch (handle) {
    case EditorTransformGizmoHandle::PlaneXY:
        first = AxisX;
        second = AxisY;
        return true;
    case EditorTransformGizmoHandle::PlaneXZ:
        first = AxisX;
        second = AxisZ;
        return true;
    case EditorTransformGizmoHandle::PlaneYZ:
        first = AxisY;
        second = AxisZ;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] EditorTransformGizmoHandle handleForAxis(Core::usize axis) noexcept
{
    constexpr std::array Handles{
        EditorTransformGizmoHandle::AxisX,
        EditorTransformGizmoHandle::AxisY,
        EditorTransformGizmoHandle::AxisZ,
    };
    return axis < Handles.size() ? Handles[axis]
                                 : EditorTransformGizmoHandle::None;
}

[[nodiscard]] bool finiteFloat(double value) noexcept
{
    return std::isfinite(value) &&
           value >= -(std::numeric_limits<float>::max)() &&
           value <= (std::numeric_limits<float>::max)();
}

[[nodiscard]] bool snapped(double value, bool enabled, float step,
                           double& result) noexcept
{
    result = value;
    if (enabled) {
        result = std::round(value / step) * step;
    }
    return finiteFloat(result);
}

} // namespace

EditorTransformGizmoOperation EditorTransformGizmo::configure(
    const EditorTransformGizmoConfig& config) noexcept
{
    if (!validConfig(config)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    if (m_config == config) {
        return EditorTransformGizmoOperation::Success;
    }
    if (!m_snapshot.framePublished) {
        m_config = config;
        advanceRevision();
        return EditorTransformGizmoOperation::Success;
    }
    return rebuild(config, m_snapshot.mode, m_snapshot.orientation, m_frame);
}

EditorTransformGizmoOperation EditorTransformGizmo::setMode(
    EditorTransformGizmoMode mode) noexcept
{
    if (!validMode(mode)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    if (m_snapshot.mode == mode) {
        return EditorTransformGizmoOperation::Success;
    }
    if (!m_snapshot.framePublished) {
        m_snapshot.mode = mode;
        m_snapshot.delta = {};
        m_snapshot.delta.orientation = m_snapshot.orientation;
        advanceRevision();
        return EditorTransformGizmoOperation::Success;
    }
    return rebuild(m_config, mode, m_snapshot.orientation, m_frame);
}

EditorTransformGizmoOperation EditorTransformGizmo::setOrientation(
    EditorTransformGizmoOrientation orientation) noexcept
{
    if (!validOrientation(orientation)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    if (m_snapshot.orientation == orientation) {
        return EditorTransformGizmoOperation::Success;
    }
    if (!m_snapshot.framePublished) {
        m_snapshot.orientation = orientation;
        m_snapshot.delta = {};
        m_snapshot.delta.orientation = orientation;
        advanceRevision();
        return EditorTransformGizmoOperation::Success;
    }
    return rebuild(m_config, m_snapshot.mode, orientation, m_frame);
}

EditorTransformGizmoOperation EditorTransformGizmo::setSnap(
    const EditorTransformGizmoSnap& snap) noexcept
{
    if (!validSnap(snap)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    if (m_snap == snap) {
        return EditorTransformGizmoOperation::Success;
    }
    m_snap = snap;
    m_snapshot.delta = {};
    m_snapshot.delta.orientation = m_snapshot.orientation;
    advanceRevision();
    return EditorTransformGizmoOperation::Success;
}

EditorTransformGizmoOperation EditorTransformGizmo::publishFrame(
    const EditorTransformGizmoFrame& frame) noexcept
{
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    if (m_snapshot.framePublished && m_frame == frame) {
        return EditorTransformGizmoOperation::Success;
    }
    return rebuild(m_config, m_snapshot.mode, m_snapshot.orientation, frame);
}

EditorTransformGizmoOperation EditorTransformGizmo::rebuild(
    const EditorTransformGizmoConfig& config,
    EditorTransformGizmoMode mode,
    EditorTransformGizmoOrientation orientation,
    const EditorTransformGizmoFrame& frame) noexcept
{
    if (!validConfig(config) || !validMode(mode) || !validOrientation(orientation) ||
        !validDimension(frame.dimension) || !finite(frame.screenOrigin)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }

    const auto& sourceAxes = orientation == EditorTransformGizmoOrientation::World
                                 ? frame.worldAxes
                                 : frame.localAxes;
    std::array<ResolvedAxis, EditorTransformGizmoAxisCount> resolved{};
    for (Core::usize index = 0; index < resolved.size(); ++index) {
        if (!finite(sourceAxes[index].screenPerWorldUnit) ||
            !finite(sourceAxes[index].worldDirection) ||
            lengthSquared(sourceAxes[index].worldDirection) <= ProjectionEpsilon) {
            return EditorTransformGizmoOperation::InvalidInput;
        }
        resolved[index] = {
            .screenPerWorldUnit = sourceAxes[index].screenPerWorldUnit,
            .worldDirection = normalized(sourceAxes[index].worldDirection),
            .screenUsable = lengthSquared(sourceAxes[index].screenPerWorldUnit) >
                            ProjectionEpsilon,
        };
    }

    EditorTransformGizmoSnapshot candidate{};
    candidate.revision = m_snapshot.revision;
    candidate.framePublished = true;
    candidate.dimension = frame.dimension;
    candidate.mode = mode;
    candidate.orientation = orientation;
    candidate.delta.orientation = orientation;

    const auto append = [&candidate](EditorTransformGizmoHandleGeometry geometry) {
        if (candidate.handleCount >= EditorTransformGizmoHandleCapacity) {
            return false;
        }
        candidate.handleStorage[candidate.handleCount++] = geometry;
        return true;
    };
    const auto appendAxis = [&](Core::usize axis) {
        if (!resolved[axis].screenUsable) {
            return true;
        }
        const EditorTransformGizmoPoint direction =
            normalized(resolved[axis].screenPerWorldUnit);
        EditorTransformGizmoHandleGeometry geometry{
            .handle = handleForAxis(axis),
            .shape = EditorTransformGizmoHandleShape::Segment,
            .pointCount = 2,
            .hitRadiusPixels = config.axisHitRadiusPixels,
        };
        geometry.points[0] = frame.screenOrigin;
        geometry.points[1] = add(frame.screenOrigin,
                                 scale(direction, config.axisLengthPixels));
        return append(geometry);
    };
    const auto appendPlane = [&](Core::usize first, Core::usize second,
                                 EditorTransformGizmoHandle handle) {
        if (!resolved[first].screenUsable || !resolved[second].screenUsable) {
            return true;
        }
        const EditorTransformGizmoPoint firstDirection =
            normalized(resolved[first].screenPerWorldUnit);
        const EditorTransformGizmoPoint secondDirection =
            normalized(resolved[second].screenPerWorldUnit);
        if (std::abs(cross(firstDirection, secondDirection)) <= 0.08) {
            return true;
        }
        EditorTransformGizmoHandleGeometry geometry{
            .handle = handle,
            .shape = EditorTransformGizmoHandleShape::Quad,
            .pointCount = 4,
        };
        geometry.points[0] = add(
            frame.screenOrigin,
            add(scale(firstDirection, config.planeOffsetPixels),
                scale(secondDirection, config.planeOffsetPixels)));
        geometry.points[1] =
            add(geometry.points[0], scale(firstDirection, config.planeExtentPixels));
        geometry.points[2] =
            add(geometry.points[1], scale(secondDirection, config.planeExtentPixels));
        geometry.points[3] =
            add(geometry.points[0], scale(secondDirection, config.planeExtentPixels));
        return append(geometry);
    };

    if (mode == EditorTransformGizmoMode::Rotate) {
        const Core::usize firstAxis = frame.dimension == EditorTransformGizmoDimension::TwoD
                                          ? AxisZ
                                          : AxisX;
        const Core::usize axisCount = frame.dimension == EditorTransformGizmoDimension::TwoD
                                          ? 1U
                                          : EditorTransformGizmoAxisCount;
        for (Core::usize offset = 0; offset < axisCount; ++offset) {
            EditorTransformGizmoHandleGeometry geometry{
                .handle = handleForAxis(firstAxis + offset),
                .shape = EditorTransformGizmoHandleShape::Ring,
                .pointCount = 1,
                .radiusPixels = config.rotationRadiusPixels -
                                static_cast<float>(offset) *
                                    config.rotationRingSpacingPixels,
                .hitRadiusPixels = config.rotationHitRadiusPixels,
            };
            geometry.points[0] = frame.screenOrigin;
            if (!append(geometry)) {
                return EditorTransformGizmoOperation::InvalidInput;
            }
        }
    } else {
        const Core::usize axisCount = frame.dimension == EditorTransformGizmoDimension::TwoD
                                          ? 2U
                                          : EditorTransformGizmoAxisCount;
        for (Core::usize axis = 0; axis < axisCount; ++axis) {
            if (!appendAxis(axis)) {
                return EditorTransformGizmoOperation::InvalidInput;
            }
        }
        if (!appendPlane(AxisX, AxisY, EditorTransformGizmoHandle::PlaneXY)) {
            return EditorTransformGizmoOperation::InvalidInput;
        }
        if (frame.dimension == EditorTransformGizmoDimension::ThreeD) {
            if (!appendPlane(AxisX, AxisZ, EditorTransformGizmoHandle::PlaneXZ) ||
                !appendPlane(AxisY, AxisZ, EditorTransformGizmoHandle::PlaneYZ)) {
                return EditorTransformGizmoOperation::InvalidInput;
            }
        }
        if (mode == EditorTransformGizmoMode::Scale) {
            EditorTransformGizmoHandleGeometry uniform{
                .handle = EditorTransformGizmoHandle::Uniform,
                .shape = EditorTransformGizmoHandleShape::Quad,
                .pointCount = 4,
            };
            const float extent = config.uniformHandleExtentPixels;
            uniform.points[0] = {frame.screenOrigin.x - extent,
                                 frame.screenOrigin.y - extent};
            uniform.points[1] = {frame.screenOrigin.x + extent,
                                 frame.screenOrigin.y - extent};
            uniform.points[2] = {frame.screenOrigin.x + extent,
                                 frame.screenOrigin.y + extent};
            uniform.points[3] = {frame.screenOrigin.x - extent,
                                 frame.screenOrigin.y + extent};
            if (!append(uniform)) {
                return EditorTransformGizmoOperation::InvalidInput;
            }
        }
    }

    if (candidate.handleCount == 0U) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    m_config = config;
    m_frame = frame;
    m_resolvedAxes = resolved;
    m_snapshot = candidate;
    advanceRevision();
    return EditorTransformGizmoOperation::Success;
}

EditorTransformGizmoHandle EditorTransformGizmo::hitTest(
    EditorTransformGizmoPoint pointer) const noexcept
{
    if (!m_snapshot.framePublished || !finite(pointer)) {
        return EditorTransformGizmoHandle::None;
    }

    EditorTransformGizmoHandle best = EditorTransformGizmoHandle::None;
    double bestScore = (std::numeric_limits<double>::max)();
    for (const auto& geometry : m_snapshot.handles()) {
        if (geometry.shape == EditorTransformGizmoHandleShape::Quad) {
            if (pointInQuad(pointer, geometry.points)) {
                return geometry.handle;
            }
            continue;
        }

        double score = (std::numeric_limits<double>::max)();
        if (geometry.shape == EditorTransformGizmoHandleShape::Segment &&
            geometry.pointCount >= 2U) {
            score = std::sqrt(pointSegmentDistanceSquared(
                        pointer, geometry.points[0], geometry.points[1])) /
                    geometry.hitRadiusPixels;
        } else if (geometry.shape == EditorTransformGizmoHandleShape::Ring &&
                   geometry.pointCount >= 1U) {
            const double deltaX = static_cast<double>(pointer.x) - geometry.points[0].x;
            const double deltaY = static_cast<double>(pointer.y) - geometry.points[0].y;
            score = std::abs(std::sqrt(deltaX * deltaX + deltaY * deltaY) -
                             geometry.radiusPixels) /
                    geometry.hitRadiusPixels;
        }
        if (score <= 1.0 && score < bestScore) {
            best = geometry.handle;
            bestScore = score;
        }
    }
    return best;
}

EditorTransformGizmoOperation EditorTransformGizmo::updateHover(
    EditorTransformGizmoPoint pointer) noexcept
{
    if (!finite(pointer) || !m_snapshot.framePublished) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    const EditorTransformGizmoHandle handle = hitTest(pointer);
    if (m_snapshot.hoveredHandle != handle) {
        m_snapshot.hoveredHandle = handle;
        advanceRevision();
    }
    return EditorTransformGizmoOperation::Success;
}

EditorTransformGizmoOperation EditorTransformGizmo::beginDrag(
    Core::u64 pointer, EditorTransformGizmoPoint position) noexcept
{
    if (!finite(position) || !m_snapshot.framePublished) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::DragAlreadyActive;
    }
    const EditorTransformGizmoHandle handle = hitTest(position);
    if (handle == EditorTransformGizmoHandle::None) {
        return EditorTransformGizmoOperation::NoHandleAtPointer;
    }
    m_snapshot.hoveredHandle = handle;
    m_snapshot.activeHandle = handle;
    m_snapshot.activePointer = pointer;
    m_snapshot.dragStart = position;
    m_snapshot.dragCurrent = position;
    m_snapshot.delta = {};
    m_snapshot.delta.handle = handle;
    m_snapshot.delta.orientation = m_snapshot.orientation;
    advanceRevision();
    return EditorTransformGizmoOperation::Success;
}

bool EditorTransformGizmo::computeDragDelta(
    EditorTransformGizmoPoint position,
    EditorTransformGizmoDelta& delta) const noexcept
{
    if (!finite(position) || !m_snapshot.dragging()) {
        return false;
    }
    const EditorTransformGizmoPoint screenDelta{
        .x = position.x - m_snapshot.dragStart.x,
        .y = position.y - m_snapshot.dragStart.y,
    };
    if (!finite(screenDelta)) {
        return false;
    }

    delta = {};
    delta.handle = m_snapshot.activeHandle;
    delta.orientation = m_snapshot.orientation;

    if (m_snapshot.mode == EditorTransformGizmoMode::Rotate) {
        const Core::usize axis = axisIndex(m_snapshot.activeHandle);
        if (axis >= m_resolvedAxes.size()) {
            return false;
        }
        const double startX = static_cast<double>(m_snapshot.dragStart.x) -
                              m_frame.screenOrigin.x;
        const double startY = static_cast<double>(m_snapshot.dragStart.y) -
                              m_frame.screenOrigin.y;
        const double currentX = static_cast<double>(position.x) - m_frame.screenOrigin.x;
        const double currentY = static_cast<double>(position.y) - m_frame.screenOrigin.y;
        if (startX * startX + startY * startY <= ProjectionEpsilon ||
            currentX * currentX + currentY * currentY <= ProjectionEpsilon) {
            return false;
        }
        const double radians =
            std::atan2(startX * currentY - startY * currentX,
                       startX * currentX + startY * currentY);
        double degrees = 0.0;
        if (!snapped(radians * RadiansToDegrees, m_snap.enabled,
                     m_snap.rotationStepDegrees, degrees)) {
            return false;
        }
        delta.rotationAxis = m_resolvedAxes[axis].worldDirection;
        delta.rotationDegrees = static_cast<float>(degrees);
        return finite(delta.rotationAxis) && finite(delta.rotationDegrees);
    }

    const auto projectedAxisAmount = [&](Core::usize axis, double& amount) {
        if (axis >= m_resolvedAxes.size() || !m_resolvedAxes[axis].screenUsable) {
            return false;
        }
        const auto projection = m_resolvedAxes[axis].screenPerWorldUnit;
        const double denominator = lengthSquared(projection);
        const double raw =
            (static_cast<double>(screenDelta.x) * projection.x +
             static_cast<double>(screenDelta.y) * projection.y) /
            denominator;
        const float step = m_snapshot.mode == EditorTransformGizmoMode::Translate
                               ? m_snap.translationStep
                               : m_snap.scaleStep;
        return snapped(raw, m_snap.enabled, step, amount);
    };
    const auto projectedPlaneAmounts = [&](Core::usize first, Core::usize second,
                                           double& firstAmount,
                                           double& secondAmount) {
        if (!m_resolvedAxes[first].screenUsable ||
            !m_resolvedAxes[second].screenUsable) {
            return false;
        }
        const auto firstProjection = m_resolvedAxes[first].screenPerWorldUnit;
        const auto secondProjection = m_resolvedAxes[second].screenPerWorldUnit;
        const double determinant = cross(firstProjection, secondProjection);
        if (std::abs(determinant) <= ProjectionEpsilon) {
            return false;
        }
        const double rawFirst =
            (static_cast<double>(screenDelta.x) * secondProjection.y -
             static_cast<double>(screenDelta.y) * secondProjection.x) /
            determinant;
        const double rawSecond =
            (static_cast<double>(firstProjection.x) * screenDelta.y -
             static_cast<double>(firstProjection.y) * screenDelta.x) /
            determinant;
        const float step = m_snapshot.mode == EditorTransformGizmoMode::Translate
                               ? m_snap.translationStep
                               : m_snap.scaleStep;
        return snapped(rawFirst, m_snap.enabled, step, firstAmount) &&
               snapped(rawSecond, m_snap.enabled, step, secondAmount);
    };

    double firstAmount = 0.0;
    double secondAmount = 0.0;
    Core::usize firstAxis = axisIndex(m_snapshot.activeHandle);
    Core::usize secondAxis = EditorTransformGizmoAxisCount;
    const bool plane = planeIndices(m_snapshot.activeHandle, firstAxis, secondAxis);
    if (m_snapshot.activeHandle == EditorTransformGizmoHandle::Uniform) {
        const double raw =
            (static_cast<double>(screenDelta.x) - screenDelta.y) /
            m_config.uniformScalePixelsPerUnit;
        if (!snapped(raw, m_snap.enabled, m_snap.scaleStep, firstAmount)) {
            return false;
        }
        const float factor = static_cast<float>(std::clamp(
            1.0 + firstAmount, static_cast<double>(m_config.minimumScaleFactor),
            static_cast<double>(m_config.maximumScaleFactor)));
        delta.scaleFactors = {
            factor,
            factor,
            m_snapshot.dimension == EditorTransformGizmoDimension::ThreeD ? factor
                                                                          : 1.0F,
        };
        return true;
    }
    if (plane) {
        if (!projectedPlaneAmounts(firstAxis, secondAxis, firstAmount,
                                   secondAmount)) {
            return false;
        }
    } else if (firstAxis < EditorTransformGizmoAxisCount) {
        if (!projectedAxisAmount(firstAxis, firstAmount)) {
            return false;
        }
    } else {
        return false;
    }

    if (m_snapshot.mode == EditorTransformGizmoMode::Translate) {
        const auto addTranslation = [&](Core::usize axis, double amount) {
            delta.translation.x +=
                static_cast<float>(m_resolvedAxes[axis].worldDirection.x * amount);
            delta.translation.y +=
                static_cast<float>(m_resolvedAxes[axis].worldDirection.y * amount);
            delta.translation.z +=
                static_cast<float>(m_resolvedAxes[axis].worldDirection.z * amount);
        };
        addTranslation(firstAxis, firstAmount);
        if (plane) {
            addTranslation(secondAxis, secondAmount);
        }
        return finite(delta.translation);
    }

    const auto setScaleFactor = [&](Core::usize axis, double amount) {
        const double factor = std::clamp(
            1.0 + amount, static_cast<double>(m_config.minimumScaleFactor),
            static_cast<double>(m_config.maximumScaleFactor));
        if (!finiteFloat(factor)) {
            return false;
        }
        float* const values[] = {
            &delta.scaleFactors.x,
            &delta.scaleFactors.y,
            &delta.scaleFactors.z,
        };
        *values[axis] = static_cast<float>(factor);
        return true;
    };
    return setScaleFactor(firstAxis, firstAmount) &&
           (!plane || setScaleFactor(secondAxis, secondAmount));
}

EditorTransformGizmoOperation EditorTransformGizmo::updateDrag(
    Core::u64 pointer, EditorTransformGizmoPoint position) noexcept
{
    if (!m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::NoActiveDrag;
    }
    if (pointer != m_snapshot.activePointer) {
        return EditorTransformGizmoOperation::PointerMismatch;
    }
    EditorTransformGizmoDelta candidate{};
    if (!computeDragDelta(position, candidate)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    if (m_snapshot.dragCurrent == position && m_snapshot.delta == candidate) {
        return EditorTransformGizmoOperation::Success;
    }
    m_snapshot.dragCurrent = position;
    m_snapshot.delta = candidate;
    advanceRevision();
    return EditorTransformGizmoOperation::Success;
}

EditorTransformGizmoOperation EditorTransformGizmo::endDrag(
    Core::u64 pointer, EditorTransformGizmoPoint position) noexcept
{
    if (!m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::NoActiveDrag;
    }
    if (pointer != m_snapshot.activePointer) {
        return EditorTransformGizmoOperation::PointerMismatch;
    }
    EditorTransformGizmoDelta candidate{};
    if (!computeDragDelta(position, candidate)) {
        return EditorTransformGizmoOperation::InvalidInput;
    }
    m_snapshot.dragCurrent = position;
    m_snapshot.delta = candidate;
    m_snapshot.activeHandle = EditorTransformGizmoHandle::None;
    m_snapshot.activePointer = 0;
    m_snapshot.hoveredHandle = hitTest(position);
    advanceRevision();
    return EditorTransformGizmoOperation::Success;
}

EditorTransformGizmoOperation EditorTransformGizmo::cancelDrag(
    Core::u64 pointer) noexcept
{
    if (!m_snapshot.dragging()) {
        return EditorTransformGizmoOperation::NoActiveDrag;
    }
    if (pointer != m_snapshot.activePointer) {
        return EditorTransformGizmoOperation::PointerMismatch;
    }
    m_snapshot.activeHandle = EditorTransformGizmoHandle::None;
    m_snapshot.activePointer = 0;
    m_snapshot.hoveredHandle = EditorTransformGizmoHandle::None;
    m_snapshot.dragCurrent = m_snapshot.dragStart;
    m_snapshot.delta = {};
    m_snapshot.delta.orientation = m_snapshot.orientation;
    advanceRevision();
    return EditorTransformGizmoOperation::Success;
}

void EditorTransformGizmo::advanceRevision() noexcept
{
    if (m_snapshot.revision != (std::numeric_limits<Core::u64>::max)()) {
        ++m_snapshot.revision;
    }
}

} // namespace Tina::Editor
