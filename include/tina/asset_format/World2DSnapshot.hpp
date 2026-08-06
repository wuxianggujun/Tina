#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Current-only World2D snapshot schema. Runtime EntityId/generation values are
// never serialized; stableEntityId and AssetId are the persistence boundary.
namespace World2DSnapshotWire {

inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u16 HeaderBytes = 32;
inline constexpr Core::u32 EntityBytes = 224;
inline constexpr Core::u32 MaximumEntities = 4096;
inline constexpr Core::u32 MaximumGameplayBytes = 4U * 1024U * 1024U;

inline constexpr Core::u32 ComponentSprite = 1U << 0U;
inline constexpr Core::u32 ComponentCamera = 1U << 1U;
inline constexpr Core::u32 ComponentPointLight = 1U << 2U;
inline constexpr Core::u32 ComponentShadowOccluder = 1U << 3U;
inline constexpr Core::u32 ValidComponentFlags =
    ComponentSprite | ComponentCamera | ComponentPointLight | ComponentShadowOccluder;

} // namespace World2DSnapshotWire

enum class World2DSpriteOverrideFlags : Core::u8 {
    None = 0,
    Size = 1U << 0U,
    Pivot = 1U << 1U,
    UvRect = 1U << 2U,
};

[[nodiscard]] constexpr World2DSpriteOverrideFlags operator|(World2DSpriteOverrideFlags left,
                                                             World2DSpriteOverrideFlags right) noexcept
{
    return static_cast<World2DSpriteOverrideFlags>(static_cast<Core::u8>(left) | static_cast<Core::u8>(right));
}

[[nodiscard]] constexpr bool hasFlag(World2DSpriteOverrideFlags value, World2DSpriteOverrideFlags flag) noexcept
{
    return (static_cast<Core::u8>(value) & static_cast<Core::u8>(flag)) != 0U;
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

struct World2DEntityDesc final {
    Core::u32 stableEntityId = 0;
    Core::u32 parentStableEntityId = 0;
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

[[nodiscard]] Core::Status validateWorld2DSnapshotDesc(const World2DSnapshotDesc& desc) noexcept;

[[nodiscard]] Core::Result<std::vector<std::byte>> writeWorld2DSnapshotBytes(const World2DSnapshotDesc& desc);

// Entity values are published to caller-owned storage only after the entire
// payload validates. A parse failure preserves the previous storage unchanged.
[[nodiscard]] Core::Result<World2DSnapshotView> parseWorld2DSnapshot(std::span<const std::byte> payload,
                                                                     std::vector<World2DEntityDesc>& entityStorage);

} // namespace Tina::AssetFormat
