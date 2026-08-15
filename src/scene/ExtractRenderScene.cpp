#include <tina/scene/ExtractRenderScene.hpp>

#include <tina/scene/Camera2D.hpp>
#include <tina/scene/DirectionalLight3D.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PointLight2D.hpp>
#include <tina/scene/PointLight3D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/ShadowOccluder2D.hpp>
#include <tina/scene/SpotLight3D.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>
#include <tina/scene/Transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace Tina::Scene {
namespace {

[[nodiscard]] u64 stableEntityKey(EntityId entity) noexcept
{
    return (static_cast<u64>(entity.index()) << 32U) | static_cast<u64>(entity.generation());
}

// WorldTransform stores a unit quaternion. For 2D XY content the authored spin
// is around +Z; extract the signed angle from the quaternion (Z-up rotation).
[[nodiscard]] float rotationRadiansAroundZ(const Quaternion& rotation) noexcept
{
    const Quaternion unit = normalized(rotation);
    // atan2(2*(w*z + x*y), 1 - 2*(y*y + z*z)) for yaw about Z with x~y~0.
    const float sinYCosX = 2.0F * (unit.w * unit.z + unit.x * unit.y);
    const float cosYCosX = 1.0F - 2.0F * (unit.y * unit.y + unit.z * unit.z);
    return std::atan2(sinYCosX, cosYCosX);
}

[[nodiscard]] float resolveWidthMeters(const SpriteRenderer2D& sprite) noexcept
{
    if (hasFlag(sprite.overrides, SpriteOverrideFlags::Size)) {
        return sprite.sizeOverrideMeters.x;
    }
    return 1.0F;
}

[[nodiscard]] float resolveHeightMeters(const SpriteRenderer2D& sprite) noexcept
{
    if (hasFlag(sprite.overrides, SpriteOverrideFlags::Size)) {
        return sprite.sizeOverrideMeters.y;
    }
    return 1.0F;
}

[[nodiscard]] Vec2 resolvePivot(const SpriteRenderer2D& sprite) noexcept
{
    if (hasFlag(sprite.overrides, SpriteOverrideFlags::Pivot)) {
        return sprite.pivotOverride;
    }
    return {0.5F, 0.5F};
}

struct DirectionalLightCandidate final {
    u64 stableKey = 0;
    Render::Mesh3DDirectionalLight light{};
    std::optional<CascadedDirectionalShadow3D> cascadedShadow{};
};

struct PointLight3DCandidate final {
    u64 stableKey = 0;
    Render::Mesh3DPointLight light{};
    std::optional<PointLightShadow3D> shadow{};
};

struct SpotLight3DCandidate final {
    u64 stableKey = 0;
    Render::Mesh3DSpotLight light{};
    std::optional<SpotLightShadow3D> shadow{};
};

struct PointLight2DCandidate final {
    u64 stableKey = 0;
    Render::Sprite2DPointLight light{};
};

struct ShadowOccluder2DCandidate final {
    u64 stableKey = 0;
    Render::Sprite2DShadowSegment segment{};
};

[[nodiscard]] float snapCameraCoordinate(float value, float pixelsPerMeter) noexcept
{
    const double snapped = std::round(static_cast<double>(value) * pixelsPerMeter) / pixelsPerMeter;
    return static_cast<float>(snapped);
}

[[nodiscard]] bool pointLightIntersectsCamera(
    const Render::Sprite2DPointLight& light,
    const Render::RenderCamera2DInput& camera) noexcept
{
    const double deltaX = static_cast<double>(light.positionX) - camera.centerX;
    const double deltaY = static_cast<double>(light.positionY) - camera.centerY;
    const double cosine = std::cos(static_cast<double>(camera.rotationRadians));
    const double sine = std::sin(static_cast<double>(camera.rotationRadians));
    const double cameraX = deltaX * cosine + deltaY * sine;
    const double cameraY = -deltaX * sine + deltaY * cosine;
    const double outsideX =
        std::max(std::abs(cameraX) - static_cast<double>(camera.worldWidth) * 0.5, 0.0);
    const double outsideY =
        std::max(std::abs(cameraY) - static_cast<double>(camera.worldHeight) * 0.5, 0.0);
    return std::hypot(outsideX, outsideY) <= static_cast<double>(light.radiusMeters);
}

[[nodiscard]] bool influenceSphereIntersectsPerspectiveCamera(
    float positionX,
    float positionY,
    float positionZ,
    float influenceRadius,
    const Render::RenderPerspectiveCamera& camera) noexcept
{
    const Vec3 cameraPosition{camera.positionX, camera.positionY, camera.positionZ};
    const Vec3 forward{camera.forwardX, camera.forwardY, camera.forwardZ};
    const Vec3 up{camera.upX, camera.upY, camera.upZ};
    const Vec3 right{
        forward.y * up.z - forward.z * up.y,
        forward.z * up.x - forward.x * up.z,
        forward.x * up.y - forward.y * up.x,
    };
    const Vec3 relative =
        Vec3{positionX, positionY, positionZ} - cameraPosition;
    const double x = static_cast<double>(dot(relative, right));
    const double y = static_cast<double>(dot(relative, up));
    const double depth = static_cast<double>(dot(relative, forward));
    const double radius = static_cast<double>(influenceRadius);

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth) ||
        !std::isfinite(radius) || depth + radius < camera.nearPlaneMeters ||
        depth - radius > camera.farPlaneMeters) {
        return false;
    }

    const double tangentY =
        std::tan(static_cast<double>(camera.verticalFovDegrees) * std::numbers::pi / 360.0);
    const double tangentX = tangentY * static_cast<double>(camera.aspectRatio);
    const double horizontalRadiusScale = std::hypot(tangentX, 1.0);
    const double verticalRadiusScale = std::hypot(tangentY, 1.0);
    return depth * tangentX + x >= -radius * horizontalRadiusScale &&
           depth * tangentX - x >= -radius * horizontalRadiusScale &&
           depth * tangentY + y >= -radius * verticalRadiusScale &&
           depth * tangentY - y >= -radius * verticalRadiusScale;
}

[[nodiscard]] Core::Status publishPointLights2D(
    World& world,
    Render::RenderSceneWriter& writer,
    float ambientLightScale,
    const Render::RenderCamera2DInput* cullingCamera) noexcept
{
    if (!std::isfinite(ambientLightScale) || ambientLightScale < 0.0F) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene 2D ambient light scale must be finite and non-negative");
    }

    std::array<PointLight2DCandidate, Render::Sprite2DLightingDesc::MaximumPointLightCount>
        candidates{};
    usize lightCount = 0;
    bool hasPointLight = false;

    for (const EntityId entity : world.liveEntities()) {
        const PointLight2D* component = world.pointLight2D(entity);
        if (component == nullptr) {
            continue;
        }
        hasPointLight = true;
        if (!component->active) {
            continue;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (!isValid(*component) || transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active PointLight2D or its WorldTransform is invalid");
        }

        const float colorR = component->color.red * component->intensity;
        const float colorG = component->color.green * component->intensity;
        const float colorB = component->color.blue * component->intensity;
        if (!std::isfinite(colorR) || !std::isfinite(colorG) || !std::isfinite(colorB)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene PointLight2D extraction overflowed");
        }
    }

    for (const EntityId entity : world.liveEntities()) {
        const PointLight2D* component = world.pointLight2D(entity);
        if (component == nullptr || !component->active) {
            continue;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        const float colorR = component->color.red * component->intensity;
        const float colorG = component->color.green * component->intensity;
        const float colorB = component->color.blue * component->intensity;
        const Render::Sprite2DPointLight light{
            .positionX = transform->position.x,
            .positionY = transform->position.y,
            .radiusMeters = component->radiusMeters,
            .sourceRadiusMeters = component->sourceRadiusMeters,
            .colorR = colorR,
            .colorG = colorG,
            .colorB = colorB,
        };
        if (cullingCamera != nullptr && !pointLightIntersectsCamera(light, *cullingCamera)) {
            continue;
        }
        if (lightCount >= candidates.size()) {
            return Core::failure(
                SceneErrorCode::TooManyActivePointLights2D,
                "Scene extract exceeded the fixed visible PointLight2D limit");
        }
        candidates[lightCount] = PointLight2DCandidate{
            .stableKey = stableEntityKey(entity),
            .light = light,
        };
        ++lightCount;
    }

    // No authored component preserves the existing unlit Sprite2D path. An
    // authored but fully inactive set publishes ambient-only lighting.
    if (!hasPointLight) {
        return Core::success();
    }

    std::array<ShadowOccluder2DCandidate,
               Render::Sprite2DLightingDesc::MaximumShadowSegmentCount>
        occluderCandidates{};
    usize occluderCount = 0;
    for (const EntityId entity : world.liveEntities()) {
        const ShadowOccluder2D* component = world.shadowOccluder2D(entity);
        if (component == nullptr || !component->active) {
            continue;
        }
        if (occluderCount >= occluderCandidates.size()) {
            return Core::failure(
                SceneErrorCode::TooManyActiveShadowOccluders2D,
                "Scene extract exceeded the fixed active ShadowOccluder2D limit");
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (!isValid(*component) || transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active ShadowOccluder2D or its WorldTransform is invalid");
        }

        const Vec3 scaledStart{
            component->localStartX * transform->scale.x,
            component->localStartY * transform->scale.y,
            0.0F,
        };
        const Vec3 scaledEnd{
            component->localEndX * transform->scale.x,
            component->localEndY * transform->scale.y,
            0.0F,
        };
        const Vec3 worldStart = transform->position + rotate(transform->rotation, scaledStart);
        const Vec3 worldEnd = transform->position + rotate(transform->rotation, scaledEnd);
        if (!isFinite(worldStart) || !isFinite(worldEnd) ||
            (worldStart.x == worldEnd.x && worldStart.y == worldEnd.y)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene ShadowOccluder2D extraction produced an invalid projected segment");
        }

        occluderCandidates[occluderCount] = ShadowOccluder2DCandidate{
            .stableKey = stableEntityKey(entity),
            .segment =
                Render::Sprite2DShadowSegment{
                    .startX = worldStart.x,
                    .startY = worldStart.y,
                    .endX = worldEnd.x,
                    .endY = worldEnd.y,
                },
        };
        ++occluderCount;
    }

    std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(lightCount),
              [](const PointLight2DCandidate& left,
                 const PointLight2DCandidate& right) noexcept {
                  return left.stableKey < right.stableKey;
              });
    std::array<Render::Sprite2DPointLight, Render::Sprite2DLightingDesc::MaximumPointLightCount>
        lights{};
    for (usize index = 0; index < lightCount; ++index) {
        lights[index] = candidates[index].light;
    }
    std::sort(
        occluderCandidates.begin(),
        occluderCandidates.begin() + static_cast<std::ptrdiff_t>(occluderCount),
        [](const ShadowOccluder2DCandidate& left,
           const ShadowOccluder2DCandidate& right) noexcept {
            return left.stableKey < right.stableKey;
        });
    std::array<Render::Sprite2DShadowSegment,
               Render::Sprite2DLightingDesc::MaximumShadowSegmentCount>
        shadowSegments{};
    for (usize index = 0; index < occluderCount; ++index) {
        shadowSegments[index] = occluderCandidates[index].segment;
    }
    return writer.setSprite2DLighting(Render::Sprite2DLightingDesc{
        .pointLights = std::span<const Render::Sprite2DPointLight>{lights.data(), lightCount},
        .shadowSegments =
            std::span<const Render::Sprite2DShadowSegment>{shadowSegments.data(), occluderCount},
        .ambientScale = ambientLightScale,
    });
}

[[nodiscard]] Core::Status publishMesh3DLights(
    World& world,
    Render::RenderSceneWriter& writer,
    float ambientLightScale,
    const Render::RenderPerspectiveCamera* cullingCamera) noexcept
{
    if (!std::isfinite(ambientLightScale) || ambientLightScale < 0.0F) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "Scene ambient light scale must be finite and non-negative");
    }

    std::array<DirectionalLightCandidate,
               Render::Mesh3DLightingDesc::MaximumDirectionalLightCount>
        candidates{};
    usize lightCount = 0;
    bool hasDirectionalLight = false;

    for (const EntityId entity : world.liveEntities()) {
        const DirectionalLight3D* component = world.directionalLight3D(entity);
        if (component == nullptr) {
            continue;
        }
        hasDirectionalLight = true;
        if (!component->active) {
            continue;
        }
        if (lightCount >= candidates.size()) {
            return Core::failure(
                SceneErrorCode::TooManyActiveDirectionalLights,
                "Scene extract exceeded the fixed active DirectionalLight3D limit");
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (!isValid(*component) || transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active DirectionalLight3D or its WorldTransform is invalid");
        }

        const Vec3 directionTowardLight =
            rotate(normalized(transform->rotation), Vec3{0.0F, 0.0F, 1.0F});
        const float colorR = component->color.red * component->intensity;
        const float colorG = component->color.green * component->intensity;
        const float colorB = component->color.blue * component->intensity;
        if (!isFinite(directionTowardLight) || !std::isfinite(colorR) ||
            !std::isfinite(colorG) || !std::isfinite(colorB)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene DirectionalLight3D extraction overflowed");
        }

        candidates[lightCount] = DirectionalLightCandidate{
            .stableKey = stableEntityKey(entity),
            .light =
                Render::Mesh3DDirectionalLight{
                    .directionTowardLightX = directionTowardLight.x,
                    .directionTowardLightY = directionTowardLight.y,
                    .directionTowardLightZ = directionTowardLight.z,
                    .colorR = colorR,
                    .colorG = colorG,
                    .colorB = colorB,
                },
            .cascadedShadow = component->cascadedShadow,
        };
        ++lightCount;
    }

    // Sort only the live prefix. Sorting the whole fixed array confuses GCC's
    // bounds analysis when lightCount is a runtime usize under Maximum*Count.
    if (lightCount > 1)
    {
        std::sort(candidates.data(), candidates.data() + lightCount,
                  [](const DirectionalLightCandidate& left,
                     const DirectionalLightCandidate& right) noexcept {
                      return left.stableKey < right.stableKey;
                  });
    }
    std::array<Render::Mesh3DDirectionalLight,
               Render::Mesh3DLightingDesc::MaximumDirectionalLightCount>
        lights{};
    std::optional<Render::Mesh3DCascadedDirectionalShadow> cascadedDirectionalShadow;
    for (usize index = 0; index < lightCount; ++index) {
        lights[index] = candidates[index].light;
        if (!candidates[index].cascadedShadow.has_value()) {
            continue;
        }
        if (cascadedDirectionalShadow.has_value()) {
            return Core::failure(
                SceneErrorCode::TooManyActiveCascadedDirectionalShadows,
                "Scene extract supports at most one active CascadedDirectionalShadow3D");
        }
        const CascadedDirectionalShadow3D& source = *candidates[index].cascadedShadow;
        cascadedDirectionalShadow = Render::Mesh3DCascadedDirectionalShadow{
            .directionalLightIndex = static_cast<u32>(index),
            .maximumDistanceMeters = source.maximumDistanceMeters,
            .depthBias = source.depthBias,
            .normalBiasMeters = source.normalBiasMeters,
        };
    }

    std::array<PointLight3DCandidate, Render::Mesh3DLightingDesc::MaximumPointLightCount>
        pointCandidates{};
    usize pointLightCount = 0;
    bool hasPointLight = false;
    for (const EntityId entity : world.liveEntities()) {
        const PointLight3D* component = world.pointLight3D(entity);
        if (component == nullptr) {
            continue;
        }
        hasPointLight = true;
        if (!component->active) {
            continue;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (!isValid(*component) || transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active PointLight3D or its WorldTransform is invalid");
        }
        const float colorR = component->color.red * component->intensity;
        const float colorG = component->color.green * component->intensity;
        const float colorB = component->color.blue * component->intensity;
        if (!std::isfinite(colorR) || !std::isfinite(colorG) || !std::isfinite(colorB)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene PointLight3D extraction overflowed");
        }
    }

    for (const EntityId entity : world.liveEntities()) {
        const PointLight3D* component = world.pointLight3D(entity);
        if (component == nullptr || !component->active) {
            continue;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        const Render::Mesh3DPointLight light{
            .positionX = transform->position.x,
            .positionY = transform->position.y,
            .positionZ = transform->position.z,
            .influenceRadius = component->influenceRadiusMeters,
            .colorR = component->color.red * component->intensity,
            .colorG = component->color.green * component->intensity,
            .colorB = component->color.blue * component->intensity,
        };
        if (cullingCamera != nullptr &&
            !influenceSphereIntersectsPerspectiveCamera(
                light.positionX,
                light.positionY,
                light.positionZ,
                light.influenceRadius,
                *cullingCamera)) {
            continue;
        }
        if (pointLightCount >= pointCandidates.size()) {
            return Core::failure(
                SceneErrorCode::TooManyActivePointLights3D,
                "Scene extract exceeded the fixed camera-affecting PointLight3D limit");
        }
        pointCandidates[pointLightCount] = PointLight3DCandidate{
            .stableKey = stableEntityKey(entity),
            .light = light,
            .shadow = component->shadow,
        };
        ++pointLightCount;
    }

    std::array<SpotLight3DCandidate, Render::Mesh3DLightingDesc::MaximumSpotLightCount>
        spotCandidates{};
    usize spotLightCount = 0;
    bool hasSpotLight = false;
    for (const EntityId entity : world.liveEntities()) {
        const SpotLight3D* component = world.spotLight3D(entity);
        if (component == nullptr) {
            continue;
        }
        hasSpotLight = true;
        if (!component->active) {
            continue;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (!isValid(*component) || transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active SpotLight3D or its WorldTransform is invalid");
        }

        const Vec3 directionFromLight =
            rotate(normalized(transform->rotation), Vec3{0.0F, 0.0F, -1.0F});
        const float colorR = component->color.red * component->intensity;
        const float colorG = component->color.green * component->intensity;
        const float colorB = component->color.blue * component->intensity;
        const float innerConeCosine =
            spotLightConeCosine(component->innerConeHalfAngleDegrees);
        const float outerConeCosine =
            spotLightConeCosine(component->outerConeHalfAngleDegrees);
        const float directionLengthSquared = dot(directionFromLight, directionFromLight);
        if (!isFinite(directionFromLight) || !std::isfinite(directionLengthSquared) ||
            directionLengthSquared <= 0.0F || !std::isfinite(colorR) ||
            !std::isfinite(colorG) || !std::isfinite(colorB) ||
            !std::isfinite(innerConeCosine) || !std::isfinite(outerConeCosine)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene SpotLight3D extraction overflowed");
        }
    }

    for (const EntityId entity : world.liveEntities()) {
        const SpotLight3D* component = world.spotLight3D(entity);
        if (component == nullptr || !component->active) {
            continue;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        const Vec3 directionFromLight =
            rotate(normalized(transform->rotation), Vec3{0.0F, 0.0F, -1.0F});
        const Render::Mesh3DSpotLight light{
            .positionX = transform->position.x,
            .positionY = transform->position.y,
            .positionZ = transform->position.z,
            .influenceRadius = component->influenceRadiusMeters,
            .directionFromLightX = directionFromLight.x,
            .directionFromLightY = directionFromLight.y,
            .directionFromLightZ = directionFromLight.z,
            .innerConeCosine = spotLightConeCosine(component->innerConeHalfAngleDegrees),
            .outerConeCosine = spotLightConeCosine(component->outerConeHalfAngleDegrees),
            .colorR = component->color.red * component->intensity,
            .colorG = component->color.green * component->intensity,
            .colorB = component->color.blue * component->intensity,
        };
        if (cullingCamera != nullptr &&
            !influenceSphereIntersectsPerspectiveCamera(
                light.positionX,
                light.positionY,
                light.positionZ,
                light.influenceRadius,
                *cullingCamera)) {
            continue;
        }
        if (spotLightCount >= spotCandidates.size()) {
            return Core::failure(
                SceneErrorCode::TooManyActiveSpotLights3D,
                "Scene extract exceeded the fixed camera-affecting SpotLight3D limit");
        }
        spotCandidates[spotLightCount] = SpotLight3DCandidate{
            .stableKey = stableEntityKey(entity),
            .light = light,
            .shadow = component->shadow,
        };
        ++spotLightCount;
    }

    if (!hasDirectionalLight && !hasPointLight && !hasSpotLight) {
        return Core::success();
    }
    if (pointLightCount > 1) {
        std::sort(pointCandidates.data(), pointCandidates.data() + pointLightCount,
                  [](const PointLight3DCandidate& left,
                     const PointLight3DCandidate& right) noexcept {
                      return left.stableKey < right.stableKey;
                  });
    }
    std::array<Render::Mesh3DPointLight, Render::Mesh3DLightingDesc::MaximumPointLightCount>
        pointLights{};
    std::optional<Render::Mesh3DPointLightShadow> pointLightShadow;
    for (usize index = 0; index < pointLightCount; ++index) {
        pointLights[index] = pointCandidates[index].light;
        if (!pointCandidates[index].shadow.has_value()) {
            continue;
        }
        if (pointLightShadow.has_value()) {
            return Core::failure(
                SceneErrorCode::TooManyActivePointLightShadows,
                "Scene extract supports at most one camera-affecting PointLightShadow3D");
        }
        const PointLightShadow3D& source = *pointCandidates[index].shadow;
        pointLightShadow = Render::Mesh3DPointLightShadow{
            .pointLightIndex = static_cast<u32>(index),
            .nearPlaneMeters = source.nearPlaneMeters,
            .depthBias = source.depthBias,
            .normalBiasMeters = source.normalBiasMeters,
        };
    }
    if (spotLightCount > 1) {
        std::sort(spotCandidates.data(), spotCandidates.data() + spotLightCount,
                  [](const SpotLight3DCandidate& left,
                     const SpotLight3DCandidate& right) noexcept {
                      return left.stableKey < right.stableKey;
                  });
    }
    std::array<Render::Mesh3DSpotLight, Render::Mesh3DLightingDesc::MaximumSpotLightCount>
        spotLights{};
    std::optional<Render::Mesh3DSpotLightShadow> spotLightShadow;
    for (usize index = 0; index < spotLightCount; ++index) {
        spotLights[index] = spotCandidates[index].light;
        if (!spotCandidates[index].shadow.has_value()) {
            continue;
        }
        if (spotLightShadow.has_value()) {
            return Core::failure(
                SceneErrorCode::TooManyActiveSpotLightShadows,
                "Scene extract supports at most one camera-affecting SpotLightShadow3D");
        }
        const SpotLightShadow3D& source = *spotCandidates[index].shadow;
        spotLightShadow = Render::Mesh3DSpotLightShadow{
            .spotLightIndex = static_cast<u32>(index),
            .nearPlaneMeters = source.nearPlaneMeters,
            .depthBias = source.depthBias,
            .normalBiasMeters = source.normalBiasMeters,
        };
    }
    return writer.setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights =
            std::span<const Render::Mesh3DDirectionalLight>{lights.data(), lightCount},
        .pointLights =
            std::span<const Render::Mesh3DPointLight>{pointLights.data(), pointLightCount},
        .spotLights =
            std::span<const Render::Mesh3DSpotLight>{spotLights.data(), spotLightCount},
        .cascadedDirectionalShadow = cascadedDirectionalShadow,
        .pointLightShadow = pointLightShadow,
        .spotLightShadow = spotLightShadow,
        .ambientScale = ambientLightScale,
    });
}

} // namespace

Core::Status extractRenderSceneFromWorld(
    World& world,
    Render::RenderSceneWriter& writer,
    Render::FrameResourceSink& frameResources,
    ExtractRenderSceneParams params) noexcept
{
    if (const Core::Status status = world.updateWorldTransforms(); !status) {
        return status;
    }

    EntityId activeCameraEntity{};
    usize activeCameraCount = 0;
    std::optional<Render::RenderCamera2DInput> resolvedCamera2D;
    for (const EntityId entity : world.liveEntities()) {
        const Camera2D* camera = world.camera2D(entity);
        if (camera == nullptr || !camera->active) {
            continue;
        }
        ++activeCameraCount;
        if (activeCameraCount == 1) {
            activeCameraEntity = entity;
        }
    }

    if (activeCameraCount > 1) {
        return Core::failure(
            SceneErrorCode::MultipleActiveCameras,
            "Scene extract requires at most one active Camera2D");
    }

    if (activeCameraCount == 1) {
        const Camera2D* camera = world.camera2D(activeCameraEntity);
        const WorldTransform* transform = world.worldTransform(activeCameraEntity);
        if (camera == nullptr || transform == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene active Camera2D lost its entity during extract");
        }
        if (!isValid(*camera) || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active Camera2D or its WorldTransform is invalid");
        }

        // Suspended surface: skip World camera without failing the extract.
        if (params.surfaceViewport.pixelWidth == 0
            || params.surfaceViewport.pixelHeight == 0) {
            // Continue to sprites only when a camera was authored but surface is
            // suspended - still skip setCamera2D so pure-UI / suspended frames
            // remain valid.
        } else {
            const Render::Camera2DProjectionQuery query{
                .stableCameraKey = stableEntityKey(activeCameraEntity),
                .centerX = transform->position.x,
                .centerY = transform->position.y,
                .rotationRadians = rotationRadiansAroundZ(transform->rotation),
                .projection = camera->projection,
                .normalizedViewport = camera->normalizedViewport,
                .pixelSnap = camera->pixelSnap,
                .surfaceViewport = params.surfaceViewport,
            };
            auto resolved = Render::makeResolvedCamera2DInput(query);
            if (!resolved) {
                return Core::failure(std::move(resolved.error()).withContext(
                    "extractRenderSceneFromWorld", "Camera2D projection resolve"));
            }
            resolvedCamera2D = *resolved;
            if (resolvedCamera2D->pixelSnap != Render::RenderPixelSnapPolicy::Disabled) {
                resolvedCamera2D->centerX =
                    snapCameraCoordinate(resolvedCamera2D->centerX, resolvedCamera2D->actualPixelsPerMeter);
                resolvedCamera2D->centerY =
                    snapCameraCoordinate(resolvedCamera2D->centerY, resolvedCamera2D->actualPixelsPerMeter);
            }
            if (const Core::Status status = writer.setCamera2D(*resolved); !status) {
                return status;
            }
        }
    }

    for (const EntityId entity : world.liveEntities()) {
        const SpriteRenderer2D* sprite = world.spriteRenderer2D(entity);
        if (sprite == nullptr) {
            continue;
        }
        if (!sprite->visible) {
            continue;
        }
        if (!isValid(*sprite)) {
            return Core::failure(
                SceneErrorCode::UnresolvedSprite,
                "Scene SpriteRenderer2D has invalid render properties");
        }
        if (!sprite->sprite || !params.spriteBindingResolver) {
            return Core::failure(
                SceneErrorCode::UnresolvedSprite,
                "Scene SpriteRenderer2D has no resolvable sprite asset");
        }
        auto texture = params.spriteBindingResolver(sprite->sprite, frameResources);
        if (!texture) {
            return Core::failure(std::move(texture.error()));
        }
        if (!texture->hasValue()) {
            return Core::failure(
                SceneErrorCode::UnresolvedSprite,
                "Scene SpriteRenderer2D asset has no render binding");
        }
        Render::FrameResourceRef normalTexture{};
        if (sprite->normalTexture) {
            if (!params.normalTextureBindingResolver) {
                return Core::failure(
                    SceneErrorCode::UnresolvedSprite,
                    "Scene SpriteRenderer2D has no normal texture resolver");
            }
            auto resolvedNormal =
                params.normalTextureBindingResolver(sprite->normalTexture, frameResources);
            if (!resolvedNormal) {
                return Core::failure(std::move(resolvedNormal.error()));
            }
            if (!resolvedNormal->hasValue()) {
                return Core::failure(
                    SceneErrorCode::UnresolvedSprite,
                    "Scene SpriteRenderer2D normal texture has no render binding");
            }
            normalTexture = *resolvedNormal;
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidTransform,
                "Scene SpriteRenderer2D WorldTransform is unavailable or invalid");
        }

        const float widthMeters = resolveWidthMeters(*sprite);
        const float heightMeters = resolveHeightMeters(*sprite);
        const Vec2 pivot = resolvePivot(*sprite);
        // Pivot (0.5,0.5) is geometric center of the entity position. Other
        // pivots shift the render center in local sprite space before world
        // scale (uniform XY) and Z rotation are applied.
        const float localOffsetX = (0.5F - pivot.x) * widthMeters;
        const float localOffsetY = (0.5F - pivot.y) * heightMeters;
        const float scaledOffsetX = localOffsetX * transform->scale.x;
        const float scaledOffsetY = localOffsetY * transform->scale.y;
        const float radians = rotationRadiansAroundZ(transform->rotation);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const float centerX =
            transform->position.x + scaledOffsetX * cosine - scaledOffsetY * sine;
        const float centerY =
            transform->position.y + scaledOffsetX * sine + scaledOffsetY * cosine;

        if (!std::isfinite(centerX) || !std::isfinite(centerY)
            || !std::isfinite(widthMeters) || !std::isfinite(heightMeters)
            || !std::isfinite(transform->scale.x) || !std::isfinite(transform->scale.y)
            || !std::isfinite(radians)) {
            return Core::failure(
                SceneErrorCode::TransformOverflow,
                "Scene sprite extract produced non-finite render values");
        }

        float u0 = 0.0F;
        float v0 = 0.0F;
        float u1 = 1.0F;
        float v1 = 1.0F;
        if (hasFlag(sprite->overrides, SpriteOverrideFlags::UvRect)) {
            u0 = sprite->uvRectOverride.u0;
            v0 = sprite->uvRectOverride.v0;
            u1 = sprite->uvRectOverride.u1;
            v1 = sprite->uvRectOverride.v1;
        }

        const Render::RenderSprite2DInput input{
            .texture = *texture,
            .normalTexture = normalTexture,
            .stableEntityKey = stableEntityKey(entity),
            .centerX = centerX,
            .centerY = centerY,
            .rotationRadians = radians,
            .widthMeters = widthMeters,
            .heightMeters = heightMeters,
            .scaleX = transform->scale.x,
            .scaleY = transform->scale.y,
            .u0 = u0,
            .v0 = v0,
            .u1 = u1,
            .v1 = v1,
            .sortingLayer = sprite->sortingLayer,
            .orderInLayer = sprite->orderInLayer,
            .red = sprite->color.red,
            .green = sprite->color.green,
            .blue = sprite->color.blue,
            .alpha = sprite->color.alpha,
            .flipX = sprite->flipX,
            .flipY = sprite->flipY,
            .visible = true,
        };
        if (const Core::Status status = writer.addSprite2D(input); !status) {
            return status;
        }
    }

    EntityId activePerspectiveEntity{};
    usize activePerspectiveCount = 0;
    std::optional<Render::RenderPerspectiveCamera> resolvedPerspectiveCamera;
    for (const EntityId entity : world.liveEntities()) {
        const PerspectiveCamera3D* camera = world.perspectiveCamera3D(entity);
        if (camera == nullptr || !camera->active) {
            continue;
        }
        ++activePerspectiveCount;
        if (activePerspectiveCount == 1) {
            activePerspectiveEntity = entity;
        }
    }

    if (activePerspectiveCount > 1) {
        return Core::failure(
            SceneErrorCode::MultipleActiveCameras,
            "Scene extract requires at most one active PerspectiveCamera3D");
    }

    if (activePerspectiveCount == 1) {
        const PerspectiveCamera3D* camera = world.perspectiveCamera3D(activePerspectiveEntity);
        const WorldTransform* transform = world.worldTransform(activePerspectiveEntity);
        if (camera == nullptr || transform == nullptr) {
            return Core::failure(
                SceneErrorCode::CorruptHierarchy,
                "Scene active PerspectiveCamera3D lost its entity during extract");
        }
        if (!isValid(*camera) || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "Scene active PerspectiveCamera3D or its WorldTransform is invalid");
        }

        // Suspended surface: skip 3D camera without failing (aspect is injected by
        // RenderScene frame parameters; 0x0 is still a no-op set for this slice).
        if (params.surfaceViewport.pixelWidth != 0 && params.surfaceViewport.pixelHeight != 0) {
            const Render::RenderPerspectiveCameraInput input{
                .stableCameraKey = stableEntityKey(activePerspectiveEntity),
                .worldPose =
                    Render::RenderPose3DInput{
                        .positionX = transform->position.x,
                        .positionY = transform->position.y,
                        .positionZ = transform->position.z,
                        .rotationX = transform->rotation.x,
                        .rotationY = transform->rotation.y,
                        .rotationZ = transform->rotation.z,
                        .rotationW = transform->rotation.w,
                    },
                .verticalFovDegrees = camera->verticalFovDegrees,
                .nearPlaneMeters = camera->nearPlaneMeters,
                .farPlaneMeters = camera->farPlaneMeters,
                .normalizedViewport = camera->normalizedViewport,
            };
            if (const Core::Status status = writer.setPerspectiveCamera(input); !status) {
                return status;
            }
            const Quaternion rotation = normalized(transform->rotation);
            const Vec3 forward = rotate(rotation, Vec3{0.0F, 0.0F, -1.0F});
            const Vec3 up = rotate(rotation, Vec3{0.0F, 1.0F, 0.0F});
            const float surfaceAspect =
                static_cast<float>(params.surfaceViewport.pixelWidth) /
                static_cast<float>(params.surfaceViewport.pixelHeight);
            resolvedPerspectiveCamera = Render::RenderPerspectiveCamera{
                .stableCameraKey = input.stableCameraKey,
                .positionX = input.worldPose.positionX,
                .positionY = input.worldPose.positionY,
                .positionZ = input.worldPose.positionZ,
                .forwardX = forward.x,
                .forwardY = forward.y,
                .forwardZ = forward.z,
                .upX = up.x,
                .upY = up.y,
                .upZ = up.z,
                .verticalFovDegrees = input.verticalFovDegrees,
                .nearPlaneMeters = input.nearPlaneMeters,
                .farPlaneMeters = input.farPlaneMeters,
                .aspectRatio = surfaceAspect * input.normalizedViewport.width /
                               input.normalizedViewport.height,
                .normalizedViewport = input.normalizedViewport,
            };
        }
    }

    if (const Core::Status status =
            publishMesh3DLights(
                world,
                writer,
                params.ambientLightScale,
                resolvedPerspectiveCamera ? &*resolvedPerspectiveCamera : nullptr);
        !status) {
        return status;
    }

    if (const Core::Status status =
            publishPointLights2D(
                world,
                writer,
                params.ambientLight2DScale,
                resolvedCamera2D ? &*resolvedCamera2D : nullptr);
        !status) {
        return status;
    }

    for (const EntityId entity : world.liveEntities()) {
        const MeshRenderer3D* mesh = world.meshRenderer3D(entity);
        if (mesh == nullptr) {
            continue;
        }
        if (!mesh->visible) {
            continue;
        }
        if (!isValid(*mesh)) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene MeshRenderer3D has invalid render properties");
        }
        if (!mesh->mesh || !mesh->material || !params.mesh3DBindingResolver
            || !params.material3DBindingResolver) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene MeshRenderer3D has no resolvable mesh and material assets");
        }
        auto meshResource = params.mesh3DBindingResolver(mesh->mesh, frameResources);
        if (!meshResource) {
            return Core::failure(std::move(meshResource.error()));
        }
        if (!meshResource->hasValue()) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene MeshRenderer3D mesh asset has no render binding");
        }
        auto materialResource = params.material3DBindingResolver(mesh->material, frameResources);
        if (!materialResource) {
            return Core::failure(std::move(materialResource.error()));
        }
        if (!materialResource->hasValue()) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene MeshRenderer3D material asset has no render binding");
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidTransform,
                "Scene MeshRenderer3D WorldTransform is unavailable or invalid");
        }

        const Render::RenderMesh3DInput input{
            .mesh = *meshResource,
            .material = *materialResource,
            .submeshIndex = mesh->submeshIndex,
            .stableEntityKey = stableEntityKey(entity),
            .worldTransform =
                Render::RenderTransform3DInput{
                    .pose =
                        Render::RenderPose3DInput{
                            .positionX = transform->position.x,
                            .positionY = transform->position.y,
                            .positionZ = transform->position.z,
                            .rotationX = transform->rotation.x,
                            .rotationY = transform->rotation.y,
                            .rotationZ = transform->rotation.z,
                            .rotationW = transform->rotation.w,
                        },
                    .scaleX = transform->scale.x,
                    .scaleY = transform->scale.y,
                    .scaleZ = transform->scale.z,
                },
            .localBounds = mesh->localBounds,
            .baseColorFactor = mesh->baseColorFactor,
            .alphaMode = mesh->alphaMode,
            .doubleSided = mesh->doubleSided,
            .visible = true,
        };
        if (const Core::Status status = writer.addMesh3D(input); !status) {
            return status;
        }
    }

    for (const EntityId entity : world.liveEntities()) {
        const SkinnedMeshRenderer3D* mesh = world.skinnedMeshRenderer3D(entity);
        if (mesh == nullptr) {
            continue;
        }
        if (!mesh->visible) {
            continue;
        }
        if (!isValid(*mesh)) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene SkinnedMeshRenderer3D has invalid render properties");
        }
        if (!mesh->mesh || !mesh->material || !params.skinnedMesh3DBindingResolver
            || !params.material3DBindingResolver) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene SkinnedMeshRenderer3D has no resolvable mesh and material assets");
        }
        if (!params.skinnedPose3DProvider) {
            return Core::failure(
                SceneErrorCode::UnresolvedSkinnedPose,
                "Scene SkinnedMeshRenderer3D extraction requires a skinned pose provider");
        }
        auto meshResource = params.skinnedMesh3DBindingResolver(mesh->mesh, frameResources);
        if (!meshResource) {
            return Core::failure(std::move(meshResource.error()));
        }
        if (!meshResource->hasValue()) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene SkinnedMeshRenderer3D mesh asset has no skinned render binding");
        }
        auto materialResource = params.material3DBindingResolver(mesh->material, frameResources);
        if (!materialResource) {
            return Core::failure(std::move(materialResource.error()));
        }
        if (!materialResource->hasValue()) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene SkinnedMeshRenderer3D material asset has no render binding");
        }
        const std::span<const float> palette = params.skinnedPose3DProvider(entity);
        if (palette.empty()
            || (palette.size() % Render::SkinnedMesh3DPaletteFloatsPerJoint) != 0
            || palette.size() / Render::SkinnedMesh3DPaletteFloatsPerJoint
                   > Render::MaxSkinnedMesh3DPaletteJointCount
            || !std::ranges::all_of(palette, [](float value) noexcept {
                   return std::isfinite(value);
               })) {
            return Core::failure(
                SceneErrorCode::UnresolvedSkinnedPose,
                "Scene SkinnedMeshRenderer3D pose provider returned no usable joint palette");
        }
        const WorldTransform* transform = world.worldTransform(entity);
        if (transform == nullptr || !isValid(*transform)) {
            return Core::failure(
                SceneErrorCode::InvalidTransform,
                "Scene SkinnedMeshRenderer3D WorldTransform is unavailable or invalid");
        }

        const Render::RenderSkinnedMesh3DInput input{
            .mesh = *meshResource,
            .material = *materialResource,
            .submeshIndex = mesh->submeshIndex,
            .stableEntityKey = stableEntityKey(entity),
            .worldTransform =
                Render::RenderTransform3DInput{
                    .pose =
                        Render::RenderPose3DInput{
                            .positionX = transform->position.x,
                            .positionY = transform->position.y,
                            .positionZ = transform->position.z,
                            .rotationX = transform->rotation.x,
                            .rotationY = transform->rotation.y,
                            .rotationZ = transform->rotation.z,
                            .rotationW = transform->rotation.w,
                        },
                    .scaleX = transform->scale.x,
                    .scaleY = transform->scale.y,
                    .scaleZ = transform->scale.z,
                },
            .localBounds = mesh->localBounds,
            .baseColorFactor = mesh->baseColorFactor,
            .paletteColumnMajorJointMatrices = palette,
            .alphaMode = mesh->alphaMode,
            .doubleSided = mesh->doubleSided,
            .visible = true,
        };
        if (const Core::Status status = writer.addSkinnedMesh3D(input); !status) {
            return status;
        }
    }

    return Core::success();
}

} // namespace Tina::Scene
