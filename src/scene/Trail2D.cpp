#include <tina/scene/Trail2D.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Tina::Scene {
namespace {

[[nodiscard]] Core::Status validateConfig(const Trail2DConfig& config) noexcept
{
    if (config.segmentCapacity == 0) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D segment capacity must be greater than zero");
    }
    const double lifetimeSeconds = config.segmentLifetime.count();
    if (!(lifetimeSeconds > 0.0) || !std::isfinite(lifetimeSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D segment lifetime must be finite and greater than zero");
    }
    if (!(config.startWidthMeters > 0.0F) || !std::isfinite(config.startWidthMeters)
        || !(config.endWidthMeters > 0.0F) || !std::isfinite(config.endWidthMeters)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D widths must be finite and greater than zero");
    }
    if (!config.sprite) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D Sprite AssetHandle must be non-empty");
    }
    if (config.stableEntityKeyBase == 0) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D stable entity key base must be non-zero");
    }
    if (!isValidUvRect(config.uvRect)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D UV rectangle is invalid");
    }
    return Core::success();
}

[[nodiscard]] bool isFinitePoint(Vec2 point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

struct SegmentGeometry final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float length = 0.0F;
    float rotationRadians = 0.0F;
};

[[nodiscard]] Core::Result<SegmentGeometry> resolveGeometry(Vec2 start, Vec2 end) noexcept
{
    if (!isFinitePoint(start) || !isFinitePoint(end)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D point coordinates must be finite");
    }
    const double deltaX = static_cast<double>(end.x) - static_cast<double>(start.x);
    const double deltaY = static_cast<double>(end.y) - static_cast<double>(start.y);
    const double length = std::hypot(deltaX, deltaY);
    if (!(length > 0.0) || !std::isfinite(length)
        || length > static_cast<double>((std::numeric_limits<float>::max)())) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D segment geometry is degenerate or exceeds finite render bounds");
    }

    const double centerX =
        (static_cast<double>(start.x) + static_cast<double>(end.x)) * 0.5;
    const double centerY =
        (static_cast<double>(start.y) + static_cast<double>(end.y)) * 0.5;
    const double rotation = std::atan2(deltaY, deltaX);
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(rotation)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D segment geometry could not be represented by RenderSprite2DInput");
    }
    return SegmentGeometry{
        .centerX = static_cast<float>(centerX),
        .centerY = static_cast<float>(centerY),
        .length = static_cast<float>(length),
        .rotationRadians = static_cast<float>(rotation),
    };
}

[[nodiscard]] float widthAtAge(
    const Trail2DConfig& config,
    const Trail2DSegment& segment) noexcept
{
    const double normalizedAge = std::clamp(
        segment.age.count() / segment.lifetime.count(),
        0.0,
        1.0);
    return static_cast<float>(
        static_cast<double>(config.startWidthMeters)
        + (static_cast<double>(config.endWidthMeters) - config.startWidthMeters) * normalizedAge);
}

} // namespace

Trail2D::Trail2D(
    Trail2DConfig config,
    std::pmr::vector<Trail2DSegment> segments) noexcept
    : m_config(config),
      m_segments(std::move(segments)),
      m_nextStableEntityKey(config.stableEntityKeyBase)
{
}

Core::Result<Trail2D> Trail2D::Create(
    Trail2DConfig config,
    std::pmr::memory_resource& resource)
{
    if (const Core::Status status = validateConfig(config); !status) {
        return Core::failure(status.error());
    }

    std::pmr::vector<Trail2DSegment> segments{&resource};
    if (config.segmentCapacity > segments.max_size()) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Trail2D segment capacity exceeds addressable PMR storage");
    }
    try {
        segments.reserve(config.segmentCapacity);
    } catch (const std::bad_alloc&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Trail2D could not allocate fixed segment storage");
    } catch (const std::length_error&) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Trail2D could not allocate fixed segment storage");
    } catch (...) {
        return Core::failure(
            Core::CoreErrorCode::Internal,
            "Trail2D fixed segment storage allocation failed unexpectedly");
    }
    return Trail2D{config, std::move(segments)};
}

Core::Status Trail2D::appendPoint(Vec2 point) noexcept
{
    if (!isFinitePoint(point)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D point coordinates must be finite");
    }
    if (!m_hasAnchor) {
        m_anchor = point;
        m_hasAnchor = true;
        return Core::success();
    }

    const auto geometry = resolveGeometry(m_anchor, point);
    if (!geometry) {
        return Core::failure(geometry.error());
    }
    if (m_segments.size() >= m_config.segmentCapacity) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Trail2D fixed segment capacity is exhausted");
    }
    if (m_stableEntityKeysExhausted) {
        return Core::failure(
            SceneErrorCode::CapacityExceeded,
            "Trail2D stable segment key space is exhausted");
    }

    const u64 stableEntityKey = m_nextStableEntityKey;
    m_segments.push_back(Trail2DSegment{
        .start = m_anchor,
        .end = point,
        .age = Core::Duration{0.0},
        .lifetime = m_config.segmentLifetime,
        .stableEntityKey = stableEntityKey,
    });
    m_anchor = point;
    if (stableEntityKey == (std::numeric_limits<u64>::max)()) {
        m_stableEntityKeysExhausted = true;
    } else {
        m_nextStableEntityKey = stableEntityKey + 1U;
    }
    return Core::success();
}

void Trail2D::breakTrail() noexcept
{
    m_hasAnchor = false;
}

Core::Status Trail2D::update(Core::Duration delta) noexcept
{
    const double deltaSeconds = delta.count();
    if (deltaSeconds < 0.0 || !std::isfinite(deltaSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Trail2D update delta must be finite and non-negative");
    }
    if (deltaSeconds == 0.0 || m_segments.empty()) {
        return Core::success();
    }
    for (const Trail2DSegment& segment : m_segments) {
        if (!std::isfinite(segment.age.count() + deltaSeconds)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Trail2D segment age overflowed");
        }
    }
    for (Trail2DSegment& segment : m_segments) {
        segment.age = Core::Duration{segment.age.count() + deltaSeconds};
    }
    const auto expired = std::remove_if(
        m_segments.begin(),
        m_segments.end(),
        [](const Trail2DSegment& segment) noexcept {
            return segment.age >= segment.lifetime;
        });
    m_segments.erase(expired, m_segments.end());
    return Core::success();
}

Core::Status Trail2D::extract(
    Render::RenderSceneWriter& writer,
    Render::FrameResourceSink& frameResources,
    Sprite2DBindingResolver spriteBindingResolver) const noexcept
{
    if (m_segments.empty()) {
        return Core::success();
    }
    auto texture = spriteBindingResolver(m_config.sprite, frameResources);
    if (!texture) {
        return Core::failure(std::move(texture.error()));
    }
    if (!texture->hasValue()) {
        return Core::failure(
            SceneErrorCode::UnresolvedSprite,
            "Trail2D Sprite AssetHandle has no live render binding");
    }

    for (const Trail2DSegment& segment : m_segments) {
        auto geometry = resolveGeometry(segment.start, segment.end);
        if (!geometry) {
            return Core::failure(std::move(geometry).error());
        }
        const float width = widthAtAge(m_config, segment);
        Core::Status status = writer.addSprite2D(Render::RenderSprite2DInput{
            .texture = *texture,
            .stableEntityKey = segment.stableEntityKey,
            .centerX = geometry->centerX,
            .centerY = geometry->centerY,
            .rotationRadians = geometry->rotationRadians,
            .widthMeters = geometry->length,
            .heightMeters = width,
            .u0 = m_config.uvRect.u0,
            .v0 = m_config.uvRect.v0,
            .u1 = m_config.uvRect.u1,
            .v1 = m_config.uvRect.v1,
            .sortingLayer = m_config.sortingLayer,
            .orderInLayer = m_config.orderInLayer,
            .red = m_config.color.red,
            .green = m_config.color.green,
            .blue = m_config.color.blue,
            .alpha = m_config.color.alpha,
        });
        if (!status) {
            return Core::failure(std::move(status).error());
        }
    }
    return Core::success();
}

} // namespace Tina::Scene
