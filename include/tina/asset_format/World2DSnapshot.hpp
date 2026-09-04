#pragma once

#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::AssetFormat {

// Current-only World2D snapshot schema. Runtime EntityId/generation values are
// never serialized; stableEntityId and AssetId are the persistence boundary.
namespace World2DSnapshotWire {

inline constexpr Core::u16 SchemaVersion = 5;
inline constexpr Core::u16 HeaderBytes = 32;
inline constexpr Core::u32 EntityBytes = 464;
inline constexpr Core::u32 NameOffset = 400;
inline constexpr Core::u32 NameBytes = 64;
inline constexpr Core::u32 MaximumNameBytes = NameBytes - 1U;
inline constexpr Core::u32 MaximumEntities = 4096;
inline constexpr Core::u32 MaximumGameplayBytes = 4U * 1024U * 1024U;

inline constexpr Core::u32 ComponentSprite = 1U << 0U;
inline constexpr Core::u32 ComponentCamera = 1U << 1U;
inline constexpr Core::u32 ComponentPointLight = 1U << 2U;
inline constexpr Core::u32 ComponentShadowOccluder = 1U << 3U;
inline constexpr Core::u32 ComponentSpriteAnimation = 1U << 4U;
inline constexpr Core::u32 PayloadResource = 1U << 5U;
inline constexpr Core::u32 PayloadPhysicsBody = 1U << 6U;
inline constexpr Core::u32 PayloadPhysicsShape = 1U << 7U;
inline constexpr Core::u32 ValidComponentFlags =
    ComponentSprite | ComponentCamera | ComponentPointLight | ComponentShadowOccluder |
    ComponentSpriteAnimation | PayloadResource | PayloadPhysicsBody |
    PayloadPhysicsShape;

} // namespace World2DSnapshotWire

// Current authoring kind. The value is persisted explicitly so transform-only
// node kinds remain distinguishable after save/load.
enum class World2DNodeKind : Core::u16 {
    Node2D = 0,
    Marker2D = 1,
    Sprite2D = 2,
    AnimatedSprite2D = 3,
    TileMap2D = 4,
    FxEmitter2D = 5,
    Camera2D = 6,
    PointLight2D = 7,
    ShadowOccluder2D = 8,
    StaticBody2D = 9,
    RigidBody2D = 10,
    CharacterBody2D = 11,
    Area2D = 12,
    CollisionShape2D = 13,
    NavigationRegion2D = 14,
    AudioPlayer2D = 15,
};

inline constexpr Core::usize World2DNodeKindCount = 16;

enum class World2DSpriteOverrideFlags : Core::u8 {
    None = 0,
    Size = 1U << 0U,
    Pivot = 1U << 1U,
    UvRect = 1U << 2U,
};

TINA_ENUM_FLAG_OPERATORS(World2DSpriteOverrideFlags);

[[nodiscard]] constexpr bool hasFlag(World2DSpriteOverrideFlags value, World2DSpriteOverrideFlags flag) noexcept
{
    return hasAnyFlag(value, flag);
}

enum class World2DCameraProjectionKind : Core::u8 {
    FixedWorldHeight = 1,
    PixelPerfect = 2,
};

enum class World2DPixelSnapPolicy : Core::u8 {
    Disabled = 0,
    CameraTranslation = 1,
    CameraAndSprites = 2,
};

struct World2DSpriteDesc final {
    Core::AssetId spriteId{};
    Core::AssetId normalTextureId{};
    // Optional Shader asset selecting an author fragment program. Uniform values are
    // owned by the shader asset's runtime binding, so nothing about them is persisted.
    Core::AssetId shaderId{};
    World2DSpriteOverrideFlags overrides = World2DSpriteOverrideFlags::None;
    float sizeX = 1.0F;
    float sizeY = 1.0F;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
    float uvU0 = 0.0F;
    float uvV0 = 0.0F;
    float uvU1 = 1.0F;
    float uvV1 = 1.0F;
    Core::u8 colorRed = 255;
    Core::u8 colorGreen = 255;
    Core::u8 colorBlue = 255;
    Core::u8 colorAlpha = 255;
    Core::i16 sortingLayer = 0;
    Core::i32 orderInLayer = 0;
    bool flipX = false;
    bool flipY = false;
    bool visible = true;

    friend bool operator==(const World2DSpriteDesc&, const World2DSpriteDesc&) = default;
};

struct World2DCameraDesc final {
    World2DCameraProjectionKind projection = World2DCameraProjectionKind::FixedWorldHeight;
    World2DPixelSnapPolicy pixelSnap = World2DPixelSnapPolicy::Disabled;
    float viewportX = 0.0F;
    float viewportY = 0.0F;
    float viewportWidth = 1.0F;
    float viewportHeight = 1.0F;
    float fixedWorldHeightMeters = 18.0F;
    float referencePixelsPerMeter = 16.0F;
    Core::u32 referenceHeightPixels = 288;
    bool active = true;

    friend bool operator==(const World2DCameraDesc&, const World2DCameraDesc&) = default;
};

struct World2DPointLightDesc final {
    float colorRed = 1.0F;
    float colorGreen = 1.0F;
    float colorBlue = 1.0F;
    float colorAlpha = 1.0F;
    float intensity = 1.0F;
    float radiusMeters = 4.0F;
    float sourceRadiusMeters = 0.0F;
    bool active = true;

    friend bool operator==(const World2DPointLightDesc&, const World2DPointLightDesc&) = default;
};

struct World2DShadowOccluderDesc final {
    float localStartX = -0.5F;
    float localStartY = 0.0F;
    float localEndX = 0.5F;
    float localEndY = 0.0F;
    bool active = true;

    friend bool operator==(const World2DShadowOccluderDesc&, const World2DShadowOccluderDesc&) = default;
};

// Binds a cooked SpriteAnimationClip asset to the entity's SpriteRenderer2D.
struct World2DSpriteAnimationDesc final {
    Core::AssetId clipId{};
    float playbackSpeed = 1.0F;
    bool autoPlay = true;

    friend bool operator==(const World2DSpriteAnimationDesc&, const World2DSpriteAnimationDesc&) = default;
};

struct World2DResourceNodeDesc final {
    Core::AssetId assetId{};
    bool active = true;

    friend bool operator==(const World2DResourceNodeDesc&,
                           const World2DResourceNodeDesc&) = default;
};

struct World2DPhysicsBodyDesc final {
    float linearVelocityX = 0.0F;
    float linearVelocityY = 0.0F;
    float angularVelocityRadiansPerSecond = 0.0F;
    float linearDamping = 0.0F;
    float angularDamping = 0.0F;
    float gravityScale = 1.0F;
    bool enabled = true;
    bool enableSleep = true;
    bool initiallyAwake = true;
    bool fixedRotation = false;
    bool continuousCollision = false;

    friend bool operator==(const World2DPhysicsBodyDesc&,
                           const World2DPhysicsBodyDesc&) = default;
};

enum class World2DPhysicsShapeKind : Core::u8 {
    Box = 0,
    Circle = 1,
    Capsule = 2,
};

struct World2DPhysicsShapeDesc final {
    World2DPhysicsShapeKind kind = World2DPhysicsShapeKind::Box;
    float halfExtentX = 0.5F;
    float halfExtentY = 0.5F;
    float radius = 0.5F;
    float localCenterX = 0.0F;
    float localCenterY = 0.0F;
    float localAngleRadians = 0.0F;
    float localPointAX = -0.5F;
    float localPointAY = 0.0F;
    float localPointBX = 0.5F;
    float localPointBY = 0.0F;
    float density = 1.0F;
    float friction = 0.6F;
    float restitution = 0.0F;
    bool enabled = true;
    bool sensor = false;
    bool sensorEvents = false;
    bool contactEvents = true;
    bool hitEvents = false;

    friend bool operator==(const World2DPhysicsShapeDesc&,
                           const World2DPhysicsShapeDesc&) = default;
};

struct World2DEntityDesc final {
    Core::u32 stableEntityId = 0;
    Core::u32 parentStableEntityId = 0;
    World2DNodeKind nodeKind = World2DNodeKind::Node2D;
    // Empty uses the Editor's deterministic kind/#id fallback label.
    std::string name{};
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float rotationX = 0.0F;
    float rotationY = 0.0F;
    float rotationZ = 0.0F;
    float rotationW = 1.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
    std::optional<World2DSpriteDesc> sprite{};
    std::optional<World2DCameraDesc> camera{};
    std::optional<World2DPointLightDesc> pointLight{};
    std::optional<World2DShadowOccluderDesc> shadowOccluder{};
    std::optional<World2DSpriteAnimationDesc> spriteAnimation{};
    std::optional<World2DResourceNodeDesc> resource{};
    std::optional<World2DPhysicsBodyDesc> physicsBody{};
    std::optional<World2DPhysicsShapeDesc> physicsShape{};

    friend bool operator==(const World2DEntityDesc&, const World2DEntityDesc&) = default;
};

struct World2DSnapshotDesc final {
    std::span<const World2DEntityDesc> entities{};
    Core::u32 gameplaySchema = 0;
    Core::u32 gameplayVersion = 0;
    std::span<const std::byte> gameplayBytes{};
};

struct World2DSnapshotView final {
    Core::u16 schemaVersion = 0;
    std::span<const World2DEntityDesc> entities{};
    Core::u32 gameplaySchema = 0;
    Core::u32 gameplayVersion = 0;
    // Borrowed from the parsed payload until that payload is changed or destroyed.
    std::span<const std::byte> gameplayBytes{};
};

namespace Detail {

// Counts the declared members of an aggregate by finding the largest number of
// initializers it still accepts. Valid only because none of the Desc members is
// itself an aggregate: brace elision would let a nested aggregate absorb several
// initializers and inflate the count.
struct AnyField final {
    template <typename Field>
    constexpr operator Field() const noexcept; // NOLINT(google-explicit-constructor)
};

template <typename Aggregate, typename Indices>
struct AcceptsInitializers;

template <typename Aggregate, Core::usize... Index>
struct AcceptsInitializers<Aggregate, std::index_sequence<Index...>>
    : std::bool_constant<requires { Aggregate{(static_cast<void>(Index), AnyField{})...}; }> {
};

template <typename Aggregate, Core::usize Count = 0>
[[nodiscard]] consteval Core::usize aggregateFieldCount() noexcept
{
    if constexpr (AcceptsInitializers<Aggregate, std::make_index_sequence<Count + 1U>>::value)
    {
        return aggregateFieldCount<Aggregate, Count + 1U>();
    }
    else
    {
        return Count;
    }
}

} // namespace Detail

// Adding a member to any Desc below trips its assertion. Each one is serialized by
// hand at five sites, none of which the compiler can check: writeWorld2DSnapshotBytes
// and parseWorld2DSnapshot in src/asset_format/World2DSnapshot.cpp, plus capture and
// restore in src/scene/World2DSnapshot.cpp, and validate* alongside the writer. The
// capture path uses designated initialization, so an unhandled member is accepted
// there and silently persists its default instead of the authored value.
//
// After adding a member: update all five sites, extend World2DSnapshotWire offsets and
// EntityBytes, bump SchemaVersion, then raise the count here.
static_assert(Detail::aggregateFieldCount<World2DSpriteDesc>() == 21);
static_assert(Detail::aggregateFieldCount<World2DCameraDesc>() == 10);
static_assert(Detail::aggregateFieldCount<World2DPointLightDesc>() == 8);
static_assert(Detail::aggregateFieldCount<World2DShadowOccluderDesc>() == 5);
static_assert(Detail::aggregateFieldCount<World2DSpriteAnimationDesc>() == 3);
static_assert(Detail::aggregateFieldCount<World2DResourceNodeDesc>() == 2);
static_assert(Detail::aggregateFieldCount<World2DPhysicsBodyDesc>() == 11);
static_assert(Detail::aggregateFieldCount<World2DPhysicsShapeDesc>() == 19);
static_assert(Detail::aggregateFieldCount<World2DEntityDesc>() == 22);

[[nodiscard]] Core::Status validateWorld2DSnapshotDesc(const World2DSnapshotDesc& desc) noexcept;

[[nodiscard]] Core::Result<std::vector<std::byte>> writeWorld2DSnapshotBytes(const World2DSnapshotDesc& desc);

// Entity values are published to caller-owned storage only after the entire
// payload validates. A parse failure preserves the previous storage unchanged.
[[nodiscard]] Core::Result<World2DSnapshotView> parseWorld2DSnapshot(std::span<const std::byte> payload,
                                                                     std::vector<World2DEntityDesc>& entityStorage);

} // namespace Tina::AssetFormat
