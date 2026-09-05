#include <tina/scene/World2DSnapshot.hpp>

#include <tina/core/io/ReadFile.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/scene/Camera2D.hpp>
#include <tina/scene/PointLight2D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/ShadowOccluder2D.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>
#include <tina/scene/Transform.hpp>
#include <tina/scene/World.hpp>

#include <algorithm>
#include <exception>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace Tina::Scene {
namespace {

struct CaptureEntity final {
    EntityId entity{};
    Core::u32 stableEntityId = 0;
    Core::u32 parentStableEntityId = 0;
    Core::usize depth = 0;
};

struct PreparedEntity final {
    const AssetFormat::World2DEntityDesc* source = nullptr;
    LocalTransform local{};
    std::optional<SpriteRenderer2D> sprite{};
    std::optional<Camera2D> camera{};
    std::optional<PointLight2D> pointLight{};
    std::optional<ShadowOccluder2D> shadowOccluder{};
    std::optional<SpriteAnimationBinding2D> spriteAnimation{};
    std::optional<PhysicsBody2D> physicsBody{};
    std::optional<PhysicsShape2D> physicsShape{};
    std::optional<ResourceBinding2D> resourceBinding{};
};

// Body and resource kinds live in the wire node kind rather than the payload, so
// the mapping has to be explicit in both directions or capture cannot reproduce
// the authored node kind.
[[nodiscard]] std::optional<PhysicsBodyKind2D> physicsBodyKindFor(
    AssetFormat::World2DNodeKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::World2DNodeKind::StaticBody2D:
        return PhysicsBodyKind2D::Static;
    case AssetFormat::World2DNodeKind::RigidBody2D:
        return PhysicsBodyKind2D::Rigid;
    case AssetFormat::World2DNodeKind::CharacterBody2D:
        return PhysicsBodyKind2D::Character;
    case AssetFormat::World2DNodeKind::Area2D:
        return PhysicsBodyKind2D::Area;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] AssetFormat::World2DNodeKind nodeKindFor(PhysicsBodyKind2D kind) noexcept
{
    switch (kind)
    {
    case PhysicsBodyKind2D::Rigid:
        return AssetFormat::World2DNodeKind::RigidBody2D;
    case PhysicsBodyKind2D::Character:
        return AssetFormat::World2DNodeKind::CharacterBody2D;
    case PhysicsBodyKind2D::Area:
        return AssetFormat::World2DNodeKind::Area2D;
    case PhysicsBodyKind2D::Static:
    default:
        return AssetFormat::World2DNodeKind::StaticBody2D;
    }
}

[[nodiscard]] std::optional<ResourceBindingKind2D> resourceKindFor(
    AssetFormat::World2DNodeKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::World2DNodeKind::TileMap2D:
        return ResourceBindingKind2D::TileMap;
    case AssetFormat::World2DNodeKind::FxEmitter2D:
        return ResourceBindingKind2D::FxEmitter;
    case AssetFormat::World2DNodeKind::NavigationRegion2D:
        return ResourceBindingKind2D::NavigationRegion;
    case AssetFormat::World2DNodeKind::AudioPlayer2D:
        return ResourceBindingKind2D::AudioPlayer;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] AssetFormat::World2DNodeKind nodeKindFor(ResourceBindingKind2D kind) noexcept
{
    switch (kind)
    {
    case ResourceBindingKind2D::FxEmitter:
        return AssetFormat::World2DNodeKind::FxEmitter2D;
    case ResourceBindingKind2D::NavigationRegion:
        return AssetFormat::World2DNodeKind::NavigationRegion2D;
    case ResourceBindingKind2D::AudioPlayer:
        return AssetFormat::World2DNodeKind::AudioPlayer2D;
    case ResourceBindingKind2D::TileMap:
    default:
        return AssetFormat::World2DNodeKind::TileMap2D;
    }
}

[[nodiscard]] const CaptureEntity* findCaptureEntity(std::span<const CaptureEntity> entities, EntityId entity) noexcept
{
    const auto found = std::find_if(entities.begin(), entities.end(),
                                    [entity](const CaptureEntity& candidate) { return candidate.entity == entity; });
    return found == entities.end() ? nullptr : &*found;
}

[[nodiscard]] const CaptureEntity* findCaptureStableEntity(std::span<const CaptureEntity> entities,
                                                           Core::u32 stableEntityId) noexcept
{
    const auto found = std::find_if(entities.begin(), entities.end(), [stableEntityId](const CaptureEntity& candidate) {
        return candidate.stableEntityId == stableEntityId;
    });
    return found == entities.end() ? nullptr : &*found;
}

[[nodiscard]] EntityId findInstantiatedEntity(std::span<const World2DEntityBinding> bindings,
                                              Core::u32 stableEntityId) noexcept
{
    const auto found =
        std::find_if(bindings.begin(), bindings.end(), [stableEntityId](const World2DEntityBinding& candidate) {
            return candidate.stableEntityId == stableEntityId;
        });
    return found == bindings.end() ? EntityId{} : found->entity;
}

[[nodiscard]] Core::Result<AssetFormat::World2DSpriteDesc> captureSprite(const SpriteRenderer2D& sprite,
                                                                         const World2DSnapshotCaptureConfig& config)
{
    constexpr Core::u8 ValidOverrideFlags = static_cast<Core::u8>(SpriteOverrideFlags::Size) |
                                            static_cast<Core::u8>(SpriteOverrideFlags::Pivot) |
                                            static_cast<Core::u8>(SpriteOverrideFlags::UvRect);
    if (!isValid(sprite) ||
        (static_cast<Core::u8>(sprite.overrides) & static_cast<Core::u8>(~ValidOverrideFlags)) != 0U)
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D capture found an invalid SpriteRenderer2D");
    }
    if (!config.assetIdForHandle || !sprite.sprite)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D capture requires a stable AssetId for every sprite handle");
    }
    const Core::AssetId spriteId = config.assetIdForHandle(sprite.sprite);
    if (!spriteId)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D sprite handle did not resolve to a stable AssetId");
    }
    Core::AssetId normalTextureId{};
    if (sprite.normalTexture)
    {
        normalTextureId = config.assetIdForHandle(sprite.normalTexture);
        if (!normalTextureId)
        {
            return Core::failure(SceneErrorCode::UnresolvedSprite,
                                 "World2D normal texture handle did not resolve to a stable AssetId");
        }
    }
    Core::AssetId shaderId{};
    if (sprite.shader)
    {
        shaderId = config.assetIdForHandle(sprite.shader);
        if (!shaderId)
        {
            return Core::failure(SceneErrorCode::UnresolvedSprite,
                                 "World2D sprite shader handle did not resolve to a stable AssetId");
        }
    }

    AssetFormat::World2DSpriteOverrideFlags overrides = AssetFormat::World2DSpriteOverrideFlags::None;
    if (Scene::hasFlag(sprite.overrides, SpriteOverrideFlags::Size))
    {
        overrides = overrides | AssetFormat::World2DSpriteOverrideFlags::Size;
    }
    if (Scene::hasFlag(sprite.overrides, SpriteOverrideFlags::Pivot))
    {
        overrides = overrides | AssetFormat::World2DSpriteOverrideFlags::Pivot;
    }
    if (Scene::hasFlag(sprite.overrides, SpriteOverrideFlags::UvRect))
    {
        overrides = overrides | AssetFormat::World2DSpriteOverrideFlags::UvRect;
    }
    return AssetFormat::World2DSpriteDesc{
        .spriteId = spriteId,
        .normalTextureId = normalTextureId,
        .shaderId = shaderId,
        .overrides = overrides,
        .sizeX = sprite.sizeOverrideMeters.x,
        .sizeY = sprite.sizeOverrideMeters.y,
        .pivotX = sprite.pivotOverride.x,
        .pivotY = sprite.pivotOverride.y,
        .uvU0 = sprite.uvRectOverride.u0,
        .uvV0 = sprite.uvRectOverride.v0,
        .uvU1 = sprite.uvRectOverride.u1,
        .uvV1 = sprite.uvRectOverride.v1,
        .colorRed = sprite.color.red,
        .colorGreen = sprite.color.green,
        .colorBlue = sprite.color.blue,
        .colorAlpha = sprite.color.alpha,
        .sortingLayer = sprite.sortingLayer,
        .orderInLayer = sprite.orderInLayer,
        .flipX = sprite.flipX,
        .flipY = sprite.flipY,
        .visible = sprite.visible,
    };
}

[[nodiscard]] Core::Result<AssetFormat::World2DCameraDesc> captureCamera(const Camera2D& camera)
{
    if (!isValid(camera))
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D capture found an invalid Camera2D");
    }
    AssetFormat::World2DCameraDesc result{
        .viewportX = camera.normalizedViewport.x,
        .viewportY = camera.normalizedViewport.y,
        .viewportWidth = camera.normalizedViewport.width,
        .viewportHeight = camera.normalizedViewport.height,
        .active = camera.active,
    };
    switch (camera.pixelSnap)
    {
    case Render::RenderPixelSnapPolicy::Disabled:
        result.pixelSnap = AssetFormat::World2DPixelSnapPolicy::Disabled;
        break;
    case Render::RenderPixelSnapPolicy::CameraTranslation:
        result.pixelSnap = AssetFormat::World2DPixelSnapPolicy::CameraTranslation;
        break;
    case Render::RenderPixelSnapPolicy::CameraAndSprites:
        result.pixelSnap = AssetFormat::World2DPixelSnapPolicy::CameraAndSprites;
        break;
    default:
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D camera pixel snap policy is unsupported");
    }
    if (const auto* fixed = std::get_if<Render::FixedWorldHeight2D>(&camera.projection))
    {
        result.projection = AssetFormat::World2DCameraProjectionKind::FixedWorldHeight;
        result.fixedWorldHeightMeters = fixed->heightMeters;
    } else if (const auto* pixel = std::get_if<Render::PixelPerfect2D>(&camera.projection))
    {
        result.projection = AssetFormat::World2DCameraProjectionKind::PixelPerfect;
        result.referencePixelsPerMeter = pixel->referencePixelsPerMeter;
        result.referenceHeightPixels = pixel->referenceHeightPixels;
    } else
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D camera projection variant is unsupported");
    }
    return result;
}

[[nodiscard]] AssetFormat::World2DPointLightDesc capturePointLight(const PointLight2D& light) noexcept
{
    return AssetFormat::World2DPointLightDesc{
        .colorRed = light.color.red,
        .colorGreen = light.color.green,
        .colorBlue = light.color.blue,
        .colorAlpha = light.color.alpha,
        .intensity = light.intensity,
        .radiusMeters = light.radiusMeters,
        .sourceRadiusMeters = light.sourceRadiusMeters,
        .active = light.active,
    };
}

[[nodiscard]] AssetFormat::World2DShadowOccluderDesc captureOccluder(const ShadowOccluder2D& occluder) noexcept
{
    return AssetFormat::World2DShadowOccluderDesc{
        .localStartX = occluder.localStartX,
        .localStartY = occluder.localStartY,
        .localEndX = occluder.localEndX,
        .localEndY = occluder.localEndY,
        .active = occluder.active,
    };
}

[[nodiscard]] Core::Result<AssetFormat::World2DSpriteAnimationDesc>
captureSpriteAnimation(const SpriteAnimationBinding2D& binding,
                       const World2DSnapshotCaptureConfig& config)
{
    if (!isValid(binding))
    {
        return Core::failure(SceneErrorCode::InvalidComponent,
                             "World2D capture found an invalid SpriteAnimationBinding2D");
    }
    if (!config.assetIdForHandle)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D capture requires an AssetId mapper for animation clips");
    }
    const Core::AssetId clipId = config.assetIdForHandle(binding.clip);
    if (!clipId)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D animation clip handle did not map to an AssetId");
    }
    return AssetFormat::World2DSpriteAnimationDesc{
        .clipId = clipId,
        .playbackSpeed = binding.playbackSpeed,
        .autoPlay = binding.autoPlay,
    };
}

[[nodiscard]] Core::Result<SpriteAnimationBinding2D>
prepareSpriteAnimation(const AssetFormat::World2DSpriteAnimationDesc& source,
                       const World2DSnapshotAssetResolver& assets)
{
    if (!assets.resolveAnimationClip)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D restore requires an animation clip AssetId resolver");
    }
    const Asset::AssetHandle clipHandle = assets.resolveAnimationClip(source.clipId);
    if (!clipHandle)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D animation clip AssetId did not resolve to a weak handle");
    }
    SpriteAnimationBinding2D binding{
        .clip = clipHandle,
        .playbackSpeed = source.playbackSpeed,
        .autoPlay = source.autoPlay,
    };
    if (!isValid(binding))
    {
        return Core::failure(SceneErrorCode::InvalidComponent,
                             "World2D restored SpriteAnimationBinding2D is invalid");
    }
    return binding;
}

[[nodiscard]] Core::Result<SpriteRenderer2D> prepareSprite(const AssetFormat::World2DSpriteDesc& source,
                                                           const World2DSnapshotAssetResolver& assets)
{
    if (!assets.resolveSprite)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite, "World2D restore requires a sprite AssetId resolver");
    }
    const Asset::AssetHandle spriteHandle = assets.resolveSprite(source.spriteId);
    if (!spriteHandle)
    {
        return Core::failure(SceneErrorCode::UnresolvedSprite,
                             "World2D sprite AssetId did not resolve to a weak handle");
    }
    Asset::AssetHandle normalTexture{};
    if (source.normalTextureId)
    {
        if (!assets.resolveTexture)
        {
            return Core::failure(SceneErrorCode::UnresolvedSprite,
                                 "World2D restore requires a normal texture AssetId resolver");
        }
        normalTexture = assets.resolveTexture(source.normalTextureId);
        if (!normalTexture)
        {
            return Core::failure(SceneErrorCode::UnresolvedSprite,
                                 "World2D normal texture AssetId did not resolve to a weak handle");
        }
    }
    Asset::AssetHandle shader{};
    if (source.shaderId)
    {
        if (!assets.resolveShader)
        {
            return Core::failure(SceneErrorCode::UnresolvedSprite,
                                 "World2D restore requires a shader AssetId resolver");
        }
        shader = assets.resolveShader(source.shaderId);
        if (!shader)
        {
            return Core::failure(SceneErrorCode::UnresolvedSprite,
                                 "World2D sprite shader AssetId did not resolve to a weak handle");
        }
    }

    SpriteOverrideFlags overrides = SpriteOverrideFlags::None;
    if (AssetFormat::hasFlag(source.overrides, AssetFormat::World2DSpriteOverrideFlags::Size))
    {
        overrides = overrides | SpriteOverrideFlags::Size;
    }
    if (AssetFormat::hasFlag(source.overrides, AssetFormat::World2DSpriteOverrideFlags::Pivot))
    {
        overrides = overrides | SpriteOverrideFlags::Pivot;
    }
    if (AssetFormat::hasFlag(source.overrides, AssetFormat::World2DSpriteOverrideFlags::UvRect))
    {
        overrides = overrides | SpriteOverrideFlags::UvRect;
    }
    SpriteRenderer2D result{
        .sprite = spriteHandle,
        .normalTexture = normalTexture,
        .shader = shader,
        .overrides = overrides,
        .sizeOverrideMeters = {source.sizeX, source.sizeY},
        .pivotOverride = {source.pivotX, source.pivotY},
        .uvRectOverride = {source.uvU0, source.uvV0, source.uvU1, source.uvV1},
        .color = {source.colorRed, source.colorGreen, source.colorBlue, source.colorAlpha},
        .sortingLayer = source.sortingLayer,
        .orderInLayer = source.orderInLayer,
        .flipX = source.flipX,
        .flipY = source.flipY,
        .visible = source.visible,
    };
    if (!isValid(result))
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D restored SpriteRenderer2D is invalid");
    }
    return result;
}

[[nodiscard]] Core::Result<Camera2D> prepareCamera(const AssetFormat::World2DCameraDesc& source)
{
    Camera2D result{
        .normalizedViewport =
            {
                .x = source.viewportX,
                .y = source.viewportY,
                .width = source.viewportWidth,
                .height = source.viewportHeight,
            },
        .active = source.active,
    };
    switch (source.pixelSnap)
    {
    case AssetFormat::World2DPixelSnapPolicy::Disabled:
        result.pixelSnap = Render::RenderPixelSnapPolicy::Disabled;
        break;
    case AssetFormat::World2DPixelSnapPolicy::CameraTranslation:
        result.pixelSnap = Render::RenderPixelSnapPolicy::CameraTranslation;
        break;
    case AssetFormat::World2DPixelSnapPolicy::CameraAndSprites:
        result.pixelSnap = Render::RenderPixelSnapPolicy::CameraAndSprites;
        break;
    default:
        return Core::failure(SceneErrorCode::InvalidComponent,
                             "World2D restored camera pixel snap policy is unsupported");
    }
    switch (source.projection)
    {
    case AssetFormat::World2DCameraProjectionKind::FixedWorldHeight:
        result.projection = Render::FixedWorldHeight2D{.heightMeters = source.fixedWorldHeightMeters};
        break;
    case AssetFormat::World2DCameraProjectionKind::PixelPerfect:
        result.projection = Render::PixelPerfect2D{
            .referencePixelsPerMeter = source.referencePixelsPerMeter,
            .referenceHeightPixels = source.referenceHeightPixels,
        };
        break;
    default:
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D restored camera projection is unsupported");
    }
    if (!isValid(result))
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "World2D restored Camera2D is invalid");
    }
    return result;
}

[[nodiscard]] PointLight2D preparePointLight(const AssetFormat::World2DPointLightDesc& source) noexcept
{
    return PointLight2D{
        .color =
            {
                .red = source.colorRed,
                .green = source.colorGreen,
                .blue = source.colorBlue,
                .alpha = source.colorAlpha,
            },
        .intensity = source.intensity,
        .radiusMeters = source.radiusMeters,
        .sourceRadiusMeters = source.sourceRadiusMeters,
        .active = source.active,
    };
}

[[nodiscard]] ShadowOccluder2D prepareOccluder(const AssetFormat::World2DShadowOccluderDesc& source) noexcept
{
    return ShadowOccluder2D{
        .localStartX = source.localStartX,
        .localStartY = source.localStartY,
        .localEndX = source.localEndX,
        .localEndY = source.localEndY,
        .active = source.active,
    };
}

} // namespace

Core::Result<std::vector<std::byte>> captureWorld2DSnapshotBytes(const World& world,
                                                                 const World2DSnapshotCaptureConfig& config)
{
    if (world.entityCapacity() == 0U)
    {
        return Core::failure(SceneErrorCode::WrongOwnerThread,
                             "World2D capture requires access from the World owner thread");
    }
    const std::span<const EntityId> live = world.liveEntities();
    if (live.size() > AssetFormat::World2DSnapshotWire::MaximumEntities)
    {
        return Core::failure(SceneErrorCode::CapacityExceeded, "World2D capture exceeds the schema-v1 entity limit");
    }
    if (!live.empty() && !config.stableEntityId)
    {
        return Core::failure(SceneErrorCode::ConstructionFailed,
                             "World2D capture requires a stable entity ID callback");
    }

    try
    {
        std::vector<CaptureEntity> ordered;
        ordered.reserve(live.size());
        for (const EntityId entity : live)
        {
            const Core::u32 stableEntityId = config.stableEntityId(entity);
            if (stableEntityId == 0U ||
                std::any_of(ordered.begin(), ordered.end(), [stableEntityId](const CaptureEntity& candidate) {
                    return candidate.stableEntityId == stableEntityId;
                }))
            {
                return Core::failure(SceneErrorCode::ConstructionFailed,
                                     "World2D capture stable entity IDs must be unique and non-zero");
            }
            ordered.push_back(CaptureEntity{.entity = entity, .stableEntityId = stableEntityId});
        }

        for (CaptureEntity& entity : ordered)
        {
            const EntityId parent = world.parent(entity.entity);
            if (!parent)
            {
                continue;
            }
            const CaptureEntity* parentCapture = findCaptureEntity(ordered, parent);
            if (parentCapture == nullptr)
            {
                return Core::failure(SceneErrorCode::CorruptHierarchy, "World2D capture parent is not a live entity");
            }
            entity.parentStableEntityId = parentCapture->stableEntityId;
        }

        for (CaptureEntity& entity : ordered)
        {
            Core::u32 parentStableEntityId = entity.parentStableEntityId;
            while (parentStableEntityId != 0U)
            {
                const CaptureEntity* parent = findCaptureStableEntity(ordered, parentStableEntityId);
                if (parent == nullptr || ++entity.depth > ordered.size())
                {
                    return Core::failure(SceneErrorCode::CorruptHierarchy,
                                         "World2D capture hierarchy is corrupt or cyclic");
                }
                parentStableEntityId = parent->parentStableEntityId;
            }
        }
        std::sort(ordered.begin(), ordered.end(), [](const CaptureEntity& left, const CaptureEntity& right) {
            return left.depth != right.depth ? left.depth < right.depth : left.stableEntityId < right.stableEntityId;
        });

        std::vector<AssetFormat::World2DEntityDesc> entities;
        entities.reserve(ordered.size());
        for (const CaptureEntity& captured : ordered)
        {
            if (world.perspectiveCamera3D(captured.entity) != nullptr ||
                world.meshRenderer3D(captured.entity) != nullptr ||
                world.skinnedMeshRenderer3D(captured.entity) != nullptr ||
                world.directionalLight3D(captured.entity) != nullptr ||
                world.pointLight3D(captured.entity) != nullptr || world.spotLight3D(captured.entity) != nullptr)
            {
                return Core::failure(SceneErrorCode::InvalidComponent,
                                     "World2D capture refuses entities with unsupported 3D components");
            }
            const LocalTransform* local = world.localTransform(captured.entity);
            if (local == nullptr || !isValid(*local))
            {
                return Core::failure(SceneErrorCode::InvalidTransform,
                                     "World2D capture could not read a valid local transform");
            }
            // Body and resource kinds are not part of their payloads; they are
            // recovered here so the derived node kind matches what was authored.
            PhysicsBodyKind2D capturedBodyKind = PhysicsBodyKind2D::Static;
            ResourceBindingKind2D capturedResourceKind = ResourceBindingKind2D::TileMap;
            AssetFormat::World2DEntityDesc entity{
                .stableEntityId = captured.stableEntityId,
                .parentStableEntityId = captured.parentStableEntityId,
                .name = std::string(world.runtimeName(captured.entity)),
                .positionX = local->position.x,
                .positionY = local->position.y,
                .positionZ = local->position.z,
                .rotationX = local->rotation.x,
                .rotationY = local->rotation.y,
                .rotationZ = local->rotation.z,
                .rotationW = local->rotation.w,
                .scaleX = local->scale.x,
                .scaleY = local->scale.y,
                .scaleZ = local->scale.z,
            };
            if (const SpriteRenderer2D* sprite = world.spriteRenderer2D(captured.entity))
            {
                auto capturedSprite = captureSprite(*sprite, config);
                if (!capturedSprite)
                {
                    return Core::failure(std::move(capturedSprite.error()));
                }
                entity.sprite = std::move(*capturedSprite);
            }
            if (const Camera2D* camera = world.camera2D(captured.entity))
            {
                auto capturedCamera = captureCamera(*camera);
                if (!capturedCamera)
                {
                    return Core::failure(std::move(capturedCamera.error()));
                }
                entity.camera = std::move(*capturedCamera);
            }
            if (const PointLight2D* light = world.pointLight2D(captured.entity))
            {
                if (!isValid(*light))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D capture found an invalid PointLight2D");
                }
                entity.pointLight = capturePointLight(*light);
            }
            if (const ShadowOccluder2D* occluder = world.shadowOccluder2D(captured.entity))
            {
                if (!isValid(*occluder))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D capture found an invalid ShadowOccluder2D");
                }
                entity.shadowOccluder = captureOccluder(*occluder);
            }
            if (const SpriteAnimationBinding2D* animation =
                    world.spriteAnimationBinding2D(captured.entity))
            {
                auto capturedAnimation = captureSpriteAnimation(*animation, config);
                if (!capturedAnimation)
                {
                    return Core::failure(std::move(capturedAnimation.error()));
                }
                entity.spriteAnimation = std::move(*capturedAnimation);
            }
            if (const PhysicsBody2D* body = world.physicsBody2D(captured.entity))
            {
                if (!isValid(*body))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D capture found an invalid PhysicsBody2D");
                }
                entity.physicsBody = AssetFormat::World2DPhysicsBodyDesc{
                    .linearVelocityX = body->linearVelocityX,
                    .linearVelocityY = body->linearVelocityY,
                    .angularVelocityRadiansPerSecond = body->angularVelocityRadiansPerSecond,
                    .linearDamping = body->linearDamping,
                    .angularDamping = body->angularDamping,
                    .gravityScale = body->gravityScale,
                    .enabled = body->enabled,
                    .enableSleep = body->enableSleep,
                    .initiallyAwake = body->initiallyAwake,
                    .fixedRotation = body->fixedRotation,
                    .continuousCollision = body->continuousCollision,
                };
                capturedBodyKind = body->kind;
            }
            if (const PhysicsShape2D* shape = world.physicsShape2D(captured.entity))
            {
                if (!isValid(*shape))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D capture found an invalid PhysicsShape2D");
                }
                entity.physicsShape = AssetFormat::World2DPhysicsShapeDesc{
                    .kind = static_cast<AssetFormat::World2DPhysicsShapeKind>(shape->kind),
                    .halfExtentX = shape->halfExtentX,
                    .halfExtentY = shape->halfExtentY,
                    .radius = shape->radius,
                    .localCenterX = shape->localCenterX,
                    .localCenterY = shape->localCenterY,
                    .localAngleRadians = shape->localAngleRadians,
                    .localPointAX = shape->localPointAX,
                    .localPointAY = shape->localPointAY,
                    .localPointBX = shape->localPointBX,
                    .localPointBY = shape->localPointBY,
                    .density = shape->density,
                    .friction = shape->friction,
                    .restitution = shape->restitution,
                    .enabled = shape->enabled,
                    .sensor = shape->sensor,
                    .sensorEvents = shape->sensorEvents,
                    .contactEvents = shape->contactEvents,
                    .hitEvents = shape->hitEvents,
                };
            }
            if (const ResourceBinding2D* binding = world.resourceBinding2D(captured.entity))
            {
                if (!isValid(*binding))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D capture found an invalid ResourceBinding2D");
                }
                entity.resource = AssetFormat::World2DResourceNodeDesc{
                    .assetId = binding->assetId,
                    .active = binding->active,
                };
                capturedResourceKind = binding->kind;
            }
            const Core::usize payloadCount =
                static_cast<Core::usize>(entity.sprite.has_value()) +
                static_cast<Core::usize>(entity.camera.has_value()) +
                static_cast<Core::usize>(entity.pointLight.has_value()) +
                static_cast<Core::usize>(entity.shadowOccluder.has_value()) +
                static_cast<Core::usize>(entity.spriteAnimation.has_value()) +
                static_cast<Core::usize>(entity.physicsBody.has_value()) +
                static_cast<Core::usize>(entity.physicsShape.has_value()) +
                static_cast<Core::usize>(entity.resource.has_value());
            if (payloadCount == 0U)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::Node2D;
            }
            else if (payloadCount == 2U && entity.sprite &&
                     entity.spriteAnimation)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::AnimatedSprite2D;
            }
            else if (payloadCount == 1U && entity.sprite)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::Sprite2D;
            }
            else if (payloadCount == 1U && entity.camera)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::Camera2D;
            }
            else if (payloadCount == 1U && entity.pointLight)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::PointLight2D;
            }
            else if (payloadCount == 1U && entity.shadowOccluder)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::ShadowOccluder2D;
            }
            else if (payloadCount == 1U && entity.physicsBody)
            {
                entity.nodeKind = nodeKindFor(capturedBodyKind);
            }
            else if (payloadCount == 1U && entity.physicsShape)
            {
                entity.nodeKind = AssetFormat::World2DNodeKind::CollisionShape2D;
            }
            else if (payloadCount == 1U && entity.resource)
            {
                entity.nodeKind = nodeKindFor(capturedResourceKind);
            }
            else
            {
                return Core::failure(
                    SceneErrorCode::InvalidComponent,
                    "World2D capture requires one exact current node kind per entity");
            }
            entities.push_back(std::move(entity));
        }

        return AssetFormat::writeWorld2DSnapshotBytes(AssetFormat::World2DSnapshotDesc{
            .entities = entities,
            .gameplaySchema = config.gameplaySchema,
            .gameplayVersion = config.gameplayVersion,
            .gameplayBytes = config.gameplayBytes,
        });
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D capture allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(SceneErrorCode::ConstructionFailed, exception.what());
    } catch (...)
    {
        return Core::failure(SceneErrorCode::ConstructionFailed, "World2D capture callback threw an unknown exception");
    }
}

Core::Result<std::vector<World2DEntityBinding>>
instantiateWorld2DSnapshot(World& world, const AssetFormat::World2DSnapshotView& snapshot,
                           const World2DSnapshotAssetResolver& assets)
{
    if (snapshot.schemaVersion != AssetFormat::World2DSnapshotWire::SchemaVersion)
    {
        return Core::failure(SceneErrorCode::ConstructionFailed, "World2D restore requires the current schema version");
    }
    if (const Core::Status status = AssetFormat::validateWorld2DSnapshotDesc(AssetFormat::World2DSnapshotDesc{
            .entities = snapshot.entities,
            .gameplaySchema = snapshot.gameplaySchema,
            .gameplayVersion = snapshot.gameplayVersion,
            .gameplayBytes = snapshot.gameplayBytes,
        });
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    const Core::usize capacity = world.entityCapacity();
    if (capacity == 0U)
    {
        return Core::failure(SceneErrorCode::WrongOwnerThread,
                             "World2D restore requires access from the World owner thread");
    }
    const Core::usize existingCount = world.entityCount();
    if (capacity < existingCount || snapshot.entities.size() > capacity - existingCount)
    {
        return Core::failure(SceneErrorCode::CapacityExceeded,
                             "World2D restore exceeds remaining World entity capacity");
    }

    std::vector<World2DEntityBinding> bindings;
    const auto rollback = [&]() noexcept {
        if (bindings.empty())
        {
            return;
        }
        for (auto it = bindings.rbegin(); it != bindings.rend(); ++it)
        {
            if (world.contains(it->entity))
            {
                // destroyEntity() first republishes dirty transforms so it
                // cannot recover a hierarchy whose publication just failed.
                (void)world.destroySubtree(it->entity);
            }
        }
        bindings.clear();
        (void)world.updateWorldTransforms();
    };

    try
    {
        std::vector<PreparedEntity> prepared;
        prepared.reserve(snapshot.entities.size());
        for (const AssetFormat::World2DEntityDesc& source : snapshot.entities)
        {
            PreparedEntity entity{
                .source = &source,
                .local =
                    {
                        .position = {source.positionX, source.positionY, source.positionZ},
                        .rotation = {source.rotationX, source.rotationY, source.rotationZ, source.rotationW},
                        .scale = {source.scaleX, source.scaleY, source.scaleZ},
                    },
            };
            if (!isValid(entity.local))
            {
                return Core::failure(SceneErrorCode::InvalidTransform, "World2D restore local transform is invalid");
            }
            if (source.sprite)
            {
                auto sprite = prepareSprite(*source.sprite, assets);
                if (!sprite)
                {
                    return Core::failure(std::move(sprite.error()));
                }
                entity.sprite = std::move(*sprite);
            }
            if (source.camera)
            {
                auto camera = prepareCamera(*source.camera);
                if (!camera)
                {
                    return Core::failure(std::move(camera.error()));
                }
                entity.camera = std::move(*camera);
            }
            if (source.pointLight)
            {
                entity.pointLight = preparePointLight(*source.pointLight);
                if (!isValid(*entity.pointLight))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent, "World2D restored PointLight2D is invalid");
                }
            }
            if (source.shadowOccluder)
            {
                entity.shadowOccluder = prepareOccluder(*source.shadowOccluder);
                if (!isValid(*entity.shadowOccluder))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D restored ShadowOccluder2D is invalid");
                }
            }
            if (source.spriteAnimation)
            {
                auto animation = prepareSpriteAnimation(*source.spriteAnimation, assets);
                if (!animation)
                {
                    return Core::failure(std::move(animation.error()));
                }
                entity.spriteAnimation = std::move(*animation);
            }
            if (source.physicsBody)
            {
                const auto bodyKind = physicsBodyKindFor(source.nodeKind);
                if (!bodyKind.has_value())
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D physics body payload does not match a body node kind");
                }
                entity.physicsBody = PhysicsBody2D{
                    .kind = *bodyKind,
                    .linearVelocityX = source.physicsBody->linearVelocityX,
                    .linearVelocityY = source.physicsBody->linearVelocityY,
                    .angularVelocityRadiansPerSecond = source.physicsBody->angularVelocityRadiansPerSecond,
                    .linearDamping = source.physicsBody->linearDamping,
                    .angularDamping = source.physicsBody->angularDamping,
                    .gravityScale = source.physicsBody->gravityScale,
                    .enabled = source.physicsBody->enabled,
                    .enableSleep = source.physicsBody->enableSleep,
                    .initiallyAwake = source.physicsBody->initiallyAwake,
                    .fixedRotation = source.physicsBody->fixedRotation,
                    .continuousCollision = source.physicsBody->continuousCollision,
                };
                if (!isValid(*entity.physicsBody))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D restored PhysicsBody2D is invalid");
                }
            }
            if (source.physicsShape)
            {
                entity.physicsShape = PhysicsShape2D{
                    .kind = static_cast<PhysicsShapeKind2D>(source.physicsShape->kind),
                    .halfExtentX = source.physicsShape->halfExtentX,
                    .halfExtentY = source.physicsShape->halfExtentY,
                    .radius = source.physicsShape->radius,
                    .localCenterX = source.physicsShape->localCenterX,
                    .localCenterY = source.physicsShape->localCenterY,
                    .localAngleRadians = source.physicsShape->localAngleRadians,
                    .localPointAX = source.physicsShape->localPointAX,
                    .localPointAY = source.physicsShape->localPointAY,
                    .localPointBX = source.physicsShape->localPointBX,
                    .localPointBY = source.physicsShape->localPointBY,
                    .density = source.physicsShape->density,
                    .friction = source.physicsShape->friction,
                    .restitution = source.physicsShape->restitution,
                    .enabled = source.physicsShape->enabled,
                    .sensor = source.physicsShape->sensor,
                    .sensorEvents = source.physicsShape->sensorEvents,
                    .contactEvents = source.physicsShape->contactEvents,
                    .hitEvents = source.physicsShape->hitEvents,
                };
                if (!isValid(*entity.physicsShape))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D restored PhysicsShape2D is invalid");
                }
            }
            if (source.resource)
            {
                const auto resourceKind = resourceKindFor(source.nodeKind);
                if (!resourceKind.has_value())
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D resource payload does not match a resource node kind");
                }
                entity.resourceBinding = ResourceBinding2D{
                    .assetId = source.resource->assetId,
                    .kind = *resourceKind,
                    .active = source.resource->active,
                };
                if (!isValid(*entity.resourceBinding))
                {
                    return Core::failure(SceneErrorCode::InvalidComponent,
                                         "World2D restored ResourceBinding2D is invalid");
                }
            }
            prepared.push_back(std::move(entity));
        }

        bindings.reserve(prepared.size());
        for (const PreparedEntity& preparedEntity : prepared)
        {
            auto entity = world.createEntity(preparedEntity.local);
            if (!entity)
            {
                rollback();
                return Core::failure(std::move(entity.error()));
            }
            bindings.push_back(World2DEntityBinding{
                .stableEntityId = preparedEntity.source->stableEntityId,
                .entity = *entity,
            });
            if (Core::Status status =
                    world.setRuntimeName(*entity, preparedEntity.source->name);
                !status)
            {
                rollback();
                return Core::failure(std::move(status.error()));
            }
            if (preparedEntity.source->parentStableEntityId != 0U)
            {
                const EntityId parent = findInstantiatedEntity(bindings, preparedEntity.source->parentStableEntityId);
                if (!parent)
                {
                    rollback();
                    return Core::failure(SceneErrorCode::CorruptHierarchy,
                                         "World2D restore parent was not instantiated first");
                }
                if (Core::Status status = world.setParent(*entity, parent, ReparentMode::KeepLocal); !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.sprite)
            {
                if (Core::Status status = world.setSpriteRenderer2D(*entity, *preparedEntity.sprite); !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.camera)
            {
                if (Core::Status status = world.setCamera2D(*entity, *preparedEntity.camera); !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.pointLight)
            {
                if (Core::Status status = world.setPointLight2D(*entity, *preparedEntity.pointLight); !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.shadowOccluder)
            {
                if (Core::Status status = world.setShadowOccluder2D(*entity, *preparedEntity.shadowOccluder); !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.spriteAnimation)
            {
                if (Core::Status status =
                        world.setSpriteAnimationBinding2D(*entity, *preparedEntity.spriteAnimation);
                    !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.physicsBody)
            {
                if (Core::Status status = world.setPhysicsBody2D(*entity, *preparedEntity.physicsBody);
                    !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.physicsShape)
            {
                if (Core::Status status = world.setPhysicsShape2D(*entity, *preparedEntity.physicsShape);
                    !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
            if (preparedEntity.resourceBinding)
            {
                if (Core::Status status =
                        world.setResourceBinding2D(*entity, *preparedEntity.resourceBinding);
                    !status)
                {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }
        }
        if (Core::Status status = world.updateWorldTransforms(); !status)
        {
            rollback();
            return Core::failure(std::move(status.error()));
        }
        return bindings;
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D restore allocation failed");
    } catch (const std::exception& exception)
    {
        rollback();
        return Core::failure(SceneErrorCode::ConstructionFailed, exception.what());
    } catch (...)
    {
        rollback();
        return Core::failure(SceneErrorCode::ConstructionFailed, "World2D restore callback threw an unknown exception");
    }
}

Core::Result<World2DSceneLoadResult>
loadWorld2DSceneFromFile(World& world, std::string_view utf8Path, const World2DSnapshotAssetResolver& assets)
try
{
    // readFile requires an explicit resource. The buffer is scoped to this call,
    // so the default resource is correct and no allocator has to be threaded
    // through the public signature.
    auto bytes = Core::readFile(utf8Path, {.memoryResource = std::pmr::get_default_resource()});
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()));
    }
    // entityStorage must outlive the view, and the view borrows `bytes`, so both
    // stay alive until instantiate has finished copying what it needs.
    std::vector<AssetFormat::World2DEntityDesc> entityStorage;
    auto snapshot = AssetFormat::parseWorld2DSnapshot(*bytes, entityStorage);
    if (!snapshot)
    {
        return Core::failure(std::move(snapshot.error()));
    }
    auto bindings = instantiateWorld2DSnapshot(world, *snapshot, assets);
    if (!bindings)
    {
        return Core::failure(std::move(bindings.error()));
    }

    auto restoredBindings = std::move(*bindings);
    const auto rollback = [&world, &restoredBindings]() noexcept {
        for (auto it = restoredBindings.rbegin(); it != restoredBindings.rend(); ++it)
        {
            if (world.contains(it->entity))
            {
                (void)world.destroySubtree(it->entity);
            }
        }
        restoredBindings.clear();
        (void)world.updateWorldTransforms();
    };

    try
    {
        auto index = World2DSceneIndex::Create(*snapshot, restoredBindings);
        if (!index)
        {
            auto error = std::move(index.error());
            rollback();
            return Core::failure(std::move(error));
        }
        World2DSceneLoadResult result{
            .bindings = {},
            .index = std::move(*index),
            .gameplaySchema = snapshot->gameplaySchema,
            .gameplayVersion = snapshot->gameplayVersion,
        };
        result.gameplayBytes.assign(snapshot->gameplayBytes.begin(), snapshot->gameplayBytes.end());
        result.bindings = std::move(restoredBindings);
        return result;
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D scene load allocation failed");
    }
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D scene load allocation failed");
}

} // namespace Tina::Scene
