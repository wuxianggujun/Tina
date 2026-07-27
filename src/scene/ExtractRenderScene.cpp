#include <tina/scene/ExtractRenderScene.hpp>

#include <tina/scene/Camera2D.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>
#include <tina/scene/Transform.hpp>

#include <cmath>

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
        }
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
        const u32 meshKey = params.mesh3DBindingResolver(mesh->mesh);
        if (meshKey == 0U) {
            return Core::failure(
                SceneErrorCode::UnresolvedMesh,
                "Scene MeshRenderer3D mesh asset has no render binding");
        }
        const u32 materialKey = params.material3DBindingResolver(mesh->material);
        if (materialKey == 0U) {
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
            .meshKey = meshKey,
            .materialKey = materialKey,
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
            .doubleSided = mesh->doubleSided,
            .visible = true,
        };
        if (const Core::Status status = writer.addMesh3D(input); !status) {
            return status;
        }
    }

    return Core::success();
}

} // namespace Tina::Scene
