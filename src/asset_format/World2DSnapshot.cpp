#include <tina/asset_format/World2DSnapshot.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace Tina::AssetFormat {
namespace {

using Core::i16;
using Core::u16;
using Core::u32;
using Core::u64;
using Core::u8;
using Core::usize;

constexpr usize SpriteOffset = 56;
constexpr usize CameraOffset = 136;
constexpr usize PointLightOffset = 168;
constexpr usize OccluderOffset = 200;
constexpr usize SpriteAnimationOffset = 224;

[[nodiscard]] u8 readU8(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::to_integer<u8>(bytes[offset]);
}

[[nodiscard]] u16 readU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(readU8(bytes, offset)) |
           static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] i16 readI16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<i16>(readU16(bytes, offset));
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u32>(readU8(bytes, offset)) | (static_cast<u32>(readU8(bytes, offset + 1U)) << 8U) |
           (static_cast<u32>(readU8(bytes, offset + 2U)) << 16U) |
           (static_cast<u32>(readU8(bytes, offset + 3U)) << 24U);
}

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void writeU8(std::vector<std::byte>& bytes, usize offset, u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value)
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
}

void writeI16(std::vector<std::byte>& bytes, usize offset, i16 value)
{
    writeU16(bytes, offset, static_cast<u16>(value));
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
    writeU8(bytes, offset + 2U, static_cast<u8>((value >> 16U) & 0xFFU));
    writeU8(bytes, offset + 3U, static_cast<u8>((value >> 24U) & 0xFFU));
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void writeAssetId(std::vector<std::byte>& bytes, usize offset, Core::AssetId assetId)
{
    const auto& idBytes = assetId.bytes();
    std::copy(idBytes.begin(), idBytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] Core::AssetId readAssetId(std::span<const std::byte> bytes, usize offset) noexcept
{
    Core::AssetId::Bytes idBytes{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), idBytes.size(), idBytes.begin());
    return Core::AssetId::fromBytes(idBytes).value_or(Core::AssetId{});
}

[[nodiscard]] bool bytesAreZero(std::span<const std::byte> bytes, usize offset, usize size) noexcept
{
    for (usize index = 0; index < size; ++index)
    {
        if (bytes[offset + index] != std::byte{0})
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isFinite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool isFiniteTransform(const World2DEntityDesc& entity) noexcept
{
    return isFinite(entity.positionX) && isFinite(entity.positionY) && isFinite(entity.positionZ) &&
           isFinite(entity.rotationX) && isFinite(entity.rotationY) && isFinite(entity.rotationZ) &&
           isFinite(entity.rotationW) && isFinite(entity.scaleX) && isFinite(entity.scaleY) && isFinite(entity.scaleZ);
}

[[nodiscard]] bool isValidRotation(const World2DEntityDesc& entity) noexcept
{
    const double lengthSquared = static_cast<double>(entity.rotationX) * entity.rotationX +
                                 static_cast<double>(entity.rotationY) * entity.rotationY +
                                 static_cast<double>(entity.rotationZ) * entity.rotationZ +
                                 static_cast<double>(entity.rotationW) * entity.rotationW;
    return std::isfinite(lengthSquared) && lengthSquared > 1.0e-12;
}

[[nodiscard]] Core::Status validateSprite(const World2DSpriteDesc& sprite) noexcept
{
    constexpr u8 ValidOverrideFlags = static_cast<u8>(World2DSpriteOverrideFlags::Size) |
                                      static_cast<u8>(World2DSpriteOverrideFlags::Pivot) |
                                      static_cast<u8>(World2DSpriteOverrideFlags::UvRect);
    if (!sprite.spriteId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "World2D sprite component requires a non-zero sprite AssetId");
    }
    const auto rawOverrides = static_cast<u8>(sprite.overrides);
    if ((rawOverrides & static_cast<u8>(~ValidOverrideFlags)) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "World2D sprite override flags contain reserved bits");
    }
    if (!isFinite(sprite.sizeX) || !isFinite(sprite.sizeY) || !isFinite(sprite.pivotX) || !isFinite(sprite.pivotY) ||
        !isFinite(sprite.uvU0) || !isFinite(sprite.uvV0) || !isFinite(sprite.uvU1) || !isFinite(sprite.uvV1))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "World2D sprite override values must be finite");
    }
    if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::Size) &&
        (!(sprite.sizeX > 0.0F) || !(sprite.sizeY > 0.0F)))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "World2D sprite size override must be positive");
    }
    if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::UvRect) &&
        (sprite.uvU0 < 0.0F || sprite.uvV0 < 0.0F || sprite.uvU1 > 1.0F || sprite.uvV1 > 1.0F ||
         !(sprite.uvU0 < sprite.uvU1) || !(sprite.uvV0 < sprite.uvV1)))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "World2D sprite UV override must be an increasing normalized rectangle");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateCamera(const World2DCameraDesc& camera) noexcept
{
    const auto projection = static_cast<u8>(camera.projection);
    if (projection < static_cast<u8>(World2DCameraProjectionKind::FixedWorldHeight) ||
        projection > static_cast<u8>(World2DCameraProjectionKind::PixelPerfect))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "World2D camera projection kind is unsupported");
    }
    if (static_cast<u8>(camera.pixelSnap) > static_cast<u8>(World2DPixelSnapPolicy::CameraAndSprites))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "World2D camera pixel snap policy is unsupported");
    }
    if (!isFinite(camera.viewportX) || !isFinite(camera.viewportY) || !isFinite(camera.viewportWidth) ||
        !isFinite(camera.viewportHeight) || camera.viewportX < 0.0F || camera.viewportY < 0.0F ||
        !(camera.viewportWidth > 0.0F) || !(camera.viewportHeight > 0.0F) ||
        camera.viewportX + camera.viewportWidth > 1.0F + 1.0e-6F ||
        camera.viewportY + camera.viewportHeight > 1.0F + 1.0e-6F)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "World2D camera normalized viewport is invalid");
    }
    if (camera.projection == World2DCameraProjectionKind::FixedWorldHeight)
    {
        if (!isFinite(camera.fixedWorldHeightMeters) || !(camera.fixedWorldHeightMeters > 0.0F))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "World2D fixed camera height must be positive and finite");
        }
    } else
    {
        if (!isFinite(camera.referencePixelsPerMeter) || !(camera.referencePixelsPerMeter > 0.0F) ||
            camera.referenceHeightPixels == 0U || camera.pixelSnap != World2DPixelSnapPolicy::CameraAndSprites)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "World2D pixel-perfect camera configuration is invalid");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validatePointLight(const World2DPointLightDesc& light) noexcept
{
    if (!isFinite(light.colorRed) || !isFinite(light.colorGreen) || !isFinite(light.colorBlue) ||
        !isFinite(light.colorAlpha) || light.colorRed < 0.0F || light.colorGreen < 0.0F || light.colorBlue < 0.0F ||
        light.colorAlpha != 1.0F || !isFinite(light.intensity) || light.intensity < 0.0F ||
        !isFinite(light.radiusMeters) || !(light.radiusMeters > 0.0F) || !isFinite(light.sourceRadiusMeters) ||
        light.sourceRadiusMeters < 0.0F || light.sourceRadiusMeters > light.radiusMeters)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "World2D point light values are invalid");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateOccluder(const World2DShadowOccluderDesc& occluder) noexcept
{
    if (!isFinite(occluder.localStartX) || !isFinite(occluder.localStartY) || !isFinite(occluder.localEndX) ||
        !isFinite(occluder.localEndY) ||
        (occluder.localStartX == occluder.localEndX && occluder.localStartY == occluder.localEndY))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "World2D shadow occluder must be finite and non-degenerate");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateSpriteAnimation(const World2DSpriteAnimationDesc& animation) noexcept
{
    if (!animation.clipId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "World2D sprite animation requires a non-zero clip AssetId");
    }
    if (!isFinite(animation.playbackSpeed) || !(animation.playbackSpeed > 0.0F))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "World2D sprite animation playback speed must be positive and finite");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateEntity(const World2DEntityDesc& entity) noexcept
{
    if (entity.stableEntityId == 0U || entity.stableEntityId == entity.parentStableEntityId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "World2D entity stable IDs are invalid");
    }
    if (!isFiniteTransform(entity) || !isValidRotation(entity))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "World2D entity transform is invalid");
    }
    if (entity.sprite)
    {
        if (const Core::Status status = validateSprite(*entity.sprite); !status)
        {
            return status;
        }
    }
    if (entity.camera)
    {
        if (const Core::Status status = validateCamera(*entity.camera); !status)
        {
            return status;
        }
    }
    if (entity.pointLight)
    {
        if (const Core::Status status = validatePointLight(*entity.pointLight); !status)
        {
            return status;
        }
    }
    if (entity.shadowOccluder)
    {
        if (const Core::Status status = validateOccluder(*entity.shadowOccluder); !status)
        {
            return status;
        }
    }
    if (entity.spriteAnimation)
    {
        if (!entity.sprite)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "World2D sprite animation requires a sprite component on the same entity");
        }
        if (const Core::Status status = validateSpriteAnimation(*entity.spriteAnimation); !status)
        {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateSnapshot(std::span<const World2DEntityDesc> entities, u32 gameplaySchema,
                                            u32 gameplayVersion, std::span<const std::byte> gameplayBytes) noexcept
{
    if (entities.size() > World2DSnapshotWire::MaximumEntities)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "World2D entity count exceeds the current-schema limit");
    }
    if (gameplayBytes.size() > World2DSnapshotWire::MaximumGameplayBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "World2D gameplay blob exceeds the current-schema limit");
    }
    if (gameplayBytes.empty() ? (gameplaySchema != 0U || gameplayVersion != 0U)
                              : (gameplaySchema == 0U || gameplayVersion == 0U))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "World2D gameplay schema/version must match blob presence");
    }
    for (usize index = 0; index < entities.size(); ++index)
    {
        const World2DEntityDesc& entity = entities[index];
        if (const Core::Status status = validateEntity(entity); !status)
        {
            return status;
        }
        if (entity.parentStableEntityId != 0U &&
            std::none_of(entities.begin(), entities.begin() + static_cast<std::ptrdiff_t>(index),
                         [&entity](const World2DEntityDesc& candidate) {
                             return candidate.stableEntityId == entity.parentStableEntityId;
                         }))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "World2D parent stable ID must refer to a prior entity");
        }
        if (std::any_of(entities.begin(), entities.begin() + static_cast<std::ptrdiff_t>(index),
                        [&entity](const World2DEntityDesc& candidate) {
                            return candidate.stableEntityId == entity.stableEntityId;
                        }))
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "World2D entity stable IDs must be unique");
        }
    }
    return Core::success();
}

void writeTransform(std::vector<std::byte>& bytes, usize base, const World2DEntityDesc& entity)
{
    writeF32(bytes, base + 16U, entity.positionX);
    writeF32(bytes, base + 20U, entity.positionY);
    writeF32(bytes, base + 24U, entity.positionZ);
    writeF32(bytes, base + 28U, entity.rotationX);
    writeF32(bytes, base + 32U, entity.rotationY);
    writeF32(bytes, base + 36U, entity.rotationZ);
    writeF32(bytes, base + 40U, entity.rotationW);
    writeF32(bytes, base + 44U, entity.scaleX);
    writeF32(bytes, base + 48U, entity.scaleY);
    writeF32(bytes, base + 52U, entity.scaleZ);
}

void writeSprite(std::vector<std::byte>& bytes, usize base, const World2DSpriteDesc& sprite)
{
    writeAssetId(bytes, base + SpriteOffset, sprite.spriteId);
    writeAssetId(bytes, base + SpriteOffset + 16U, sprite.normalTextureId);
    writeU8(bytes, base + SpriteOffset + 32U, static_cast<u8>(sprite.overrides));
    u8 behavior = 0U;
    if (sprite.flipX)
        behavior |= 1U;
    if (sprite.flipY)
        behavior |= 2U;
    if (sprite.visible)
        behavior |= 4U;
    writeU8(bytes, base + SpriteOffset + 33U, behavior);
    writeI16(bytes, base + SpriteOffset + 34U, sprite.sortingLayer);
    writeU32(bytes, base + SpriteOffset + 36U, static_cast<u32>(sprite.orderInLayer));
    if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::Size))
    {
        writeF32(bytes, base + SpriteOffset + 40U, sprite.sizeX);
        writeF32(bytes, base + SpriteOffset + 44U, sprite.sizeY);
    }
    if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::Pivot))
    {
        writeF32(bytes, base + SpriteOffset + 48U, sprite.pivotX);
        writeF32(bytes, base + SpriteOffset + 52U, sprite.pivotY);
    }
    if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::UvRect))
    {
        writeF32(bytes, base + SpriteOffset + 56U, sprite.uvU0);
        writeF32(bytes, base + SpriteOffset + 60U, sprite.uvV0);
        writeF32(bytes, base + SpriteOffset + 64U, sprite.uvU1);
        writeF32(bytes, base + SpriteOffset + 68U, sprite.uvV1);
    }
    writeU8(bytes, base + SpriteOffset + 72U, sprite.colorRed);
    writeU8(bytes, base + SpriteOffset + 73U, sprite.colorGreen);
    writeU8(bytes, base + SpriteOffset + 74U, sprite.colorBlue);
    writeU8(bytes, base + SpriteOffset + 75U, sprite.colorAlpha);
}

void writeCamera(std::vector<std::byte>& bytes, usize base, const World2DCameraDesc& camera)
{
    writeU8(bytes, base + CameraOffset, static_cast<u8>(camera.projection));
    writeU8(bytes, base + CameraOffset + 1U, static_cast<u8>(camera.pixelSnap));
    writeU8(bytes, base + CameraOffset + 2U, camera.active ? 1U : 0U);
    writeF32(bytes, base + CameraOffset + 4U, camera.viewportX);
    writeF32(bytes, base + CameraOffset + 8U, camera.viewportY);
    writeF32(bytes, base + CameraOffset + 12U, camera.viewportWidth);
    writeF32(bytes, base + CameraOffset + 16U, camera.viewportHeight);
    writeF32(bytes, base + CameraOffset + 20U, camera.fixedWorldHeightMeters);
    writeF32(bytes, base + CameraOffset + 24U, camera.referencePixelsPerMeter);
    writeU32(bytes, base + CameraOffset + 28U, camera.referenceHeightPixels);
}

void writePointLight(std::vector<std::byte>& bytes, usize base, const World2DPointLightDesc& light)
{
    writeF32(bytes, base + PointLightOffset, light.colorRed);
    writeF32(bytes, base + PointLightOffset + 4U, light.colorGreen);
    writeF32(bytes, base + PointLightOffset + 8U, light.colorBlue);
    writeF32(bytes, base + PointLightOffset + 12U, light.colorAlpha);
    writeF32(bytes, base + PointLightOffset + 16U, light.intensity);
    writeF32(bytes, base + PointLightOffset + 20U, light.radiusMeters);
    writeF32(bytes, base + PointLightOffset + 24U, light.sourceRadiusMeters);
    writeU8(bytes, base + PointLightOffset + 28U, light.active ? 1U : 0U);
}

void writeOccluder(std::vector<std::byte>& bytes, usize base, const World2DShadowOccluderDesc& occluder)
{
    writeF32(bytes, base + OccluderOffset, occluder.localStartX);
    writeF32(bytes, base + OccluderOffset + 4U, occluder.localStartY);
    writeF32(bytes, base + OccluderOffset + 8U, occluder.localEndX);
    writeF32(bytes, base + OccluderOffset + 12U, occluder.localEndY);
    writeU8(bytes, base + OccluderOffset + 16U, occluder.active ? 1U : 0U);
}

void writeSpriteAnimation(std::vector<std::byte>& bytes, usize base, const World2DSpriteAnimationDesc& animation)
{
    writeAssetId(bytes, base + SpriteAnimationOffset, animation.clipId);
    writeF32(bytes, base + SpriteAnimationOffset + 16U, animation.playbackSpeed);
    writeU8(bytes, base + SpriteAnimationOffset + 20U, animation.autoPlay ? 1U : 0U);
}

} // namespace

Core::Status validateWorld2DSnapshotDesc(const World2DSnapshotDesc& desc) noexcept
{
    return validateSnapshot(desc.entities, desc.gameplaySchema, desc.gameplayVersion, desc.gameplayBytes);
}

Core::Result<std::vector<std::byte>> writeWorld2DSnapshotBytes(const World2DSnapshotDesc& desc)
{
    if (const Core::Status status = validateWorld2DSnapshotDesc(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    const usize entityBytes = static_cast<usize>(desc.entities.size()) * World2DSnapshotWire::EntityBytes;
    const usize totalBytes =
        static_cast<usize>(World2DSnapshotWire::HeaderBytes) + entityBytes + desc.gameplayBytes.size();
    if (totalBytes < entityBytes)
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "World2D snapshot byte size overflowed");
    }

    try
    {
        std::vector<std::byte> payload(totalBytes, std::byte{0});
        writeU16(payload, 0U, World2DSnapshotWire::SchemaVersion);
        writeU16(payload, 2U, World2DSnapshotWire::HeaderBytes);
        writeU32(payload, 4U, static_cast<u32>(desc.entities.size()));
        writeU32(payload, 8U, World2DSnapshotWire::EntityBytes);
        writeU32(payload, 12U, desc.gameplaySchema);
        writeU32(payload, 16U, desc.gameplayVersion);
        writeU32(payload, 20U, static_cast<u32>(desc.gameplayBytes.size()));

        for (usize index = 0; index < desc.entities.size(); ++index)
        {
            const World2DEntityDesc& entity = desc.entities[index];
            const usize base = World2DSnapshotWire::HeaderBytes + index * World2DSnapshotWire::EntityBytes;
            writeU32(payload, base + 0U, entity.stableEntityId);
            writeU32(payload, base + 4U, entity.parentStableEntityId);
            u32 componentFlags = 0U;
            if (entity.sprite)
                componentFlags |= World2DSnapshotWire::ComponentSprite;
            if (entity.camera)
                componentFlags |= World2DSnapshotWire::ComponentCamera;
            if (entity.pointLight)
                componentFlags |= World2DSnapshotWire::ComponentPointLight;
            if (entity.shadowOccluder)
                componentFlags |= World2DSnapshotWire::ComponentShadowOccluder;
            if (entity.spriteAnimation)
                componentFlags |= World2DSnapshotWire::ComponentSpriteAnimation;
            writeU32(payload, base + 8U, componentFlags);
            writeTransform(payload, base, entity);
            if (entity.sprite)
                writeSprite(payload, base, *entity.sprite);
            if (entity.camera)
                writeCamera(payload, base, *entity.camera);
            if (entity.pointLight)
                writePointLight(payload, base, *entity.pointLight);
            if (entity.shadowOccluder)
                writeOccluder(payload, base, *entity.shadowOccluder);
            if (entity.spriteAnimation)
                writeSpriteAnimation(payload, base, *entity.spriteAnimation);
        }
        std::copy(desc.gameplayBytes.begin(), desc.gameplayBytes.end(),
                  payload.begin() + static_cast<std::ptrdiff_t>(World2DSnapshotWire::HeaderBytes + entityBytes));
        return payload;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D snapshot serialization allocation failed");
    }
}

Core::Result<World2DSnapshotView> parseWorld2DSnapshot(std::span<const std::byte> payload,
                                                       std::vector<World2DEntityDesc>& entityStorage)
{
    if (payload.size() < World2DSnapshotWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "World2D snapshot payload is shorter than its header");
    }
    if (readU16(payload, 0U) != World2DSnapshotWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "World2D snapshot schema version is unsupported");
    }
    if (readU16(payload, 2U) != World2DSnapshotWire::HeaderBytes ||
        readU32(payload, 8U) != World2DSnapshotWire::EntityBytes || readU32(payload, 24U) != 0U ||
        readU32(payload, 28U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "World2D snapshot header contains invalid or reserved fields");
    }
    const u32 entityCount = readU32(payload, 4U);
    const u32 gameplaySchema = readU32(payload, 12U);
    const u32 gameplayVersion = readU32(payload, 16U);
    const u32 gameplayBytes = readU32(payload, 20U);
    if (entityCount > World2DSnapshotWire::MaximumEntities || gameplayBytes > World2DSnapshotWire::MaximumGameplayBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "World2D snapshot exceeds current-schema limits");
    }
    const u64 entityBytes = static_cast<u64>(entityCount) * World2DSnapshotWire::EntityBytes;
    const u64 expectedBytes = static_cast<u64>(World2DSnapshotWire::HeaderBytes) + entityBytes + gameplayBytes;
    if (expectedBytes != payload.size())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "World2D snapshot payload size does not match its header");
    }

    try
    {
        std::vector<World2DEntityDesc> parsed;
        parsed.reserve(entityCount);
        for (u32 index = 0; index < entityCount; ++index)
        {
            const usize base =
                World2DSnapshotWire::HeaderBytes + static_cast<usize>(index) * World2DSnapshotWire::EntityBytes;
            const u32 componentFlags = readU32(payload, base + 8U);
            if ((componentFlags & ~World2DSnapshotWire::ValidComponentFlags) != 0U ||
                readU32(payload, base + 12U) != 0U)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "World2D entity component flags or reserved field is invalid");
            }
            World2DEntityDesc entity{};
            entity.stableEntityId = readU32(payload, base + 0U);
            entity.parentStableEntityId = readU32(payload, base + 4U);
            entity.positionX = readF32(payload, base + 16U);
            entity.positionY = readF32(payload, base + 20U);
            entity.positionZ = readF32(payload, base + 24U);
            entity.rotationX = readF32(payload, base + 28U);
            entity.rotationY = readF32(payload, base + 32U);
            entity.rotationZ = readF32(payload, base + 36U);
            entity.rotationW = readF32(payload, base + 40U);
            entity.scaleX = readF32(payload, base + 44U);
            entity.scaleY = readF32(payload, base + 48U);
            entity.scaleZ = readF32(payload, base + 52U);

            if ((componentFlags & World2DSnapshotWire::ComponentSprite) != 0U)
            {
                World2DSpriteDesc sprite{};
                sprite.spriteId = readAssetId(payload, base + SpriteOffset);
                sprite.normalTextureId = readAssetId(payload, base + SpriteOffset + 16U);
                sprite.overrides = static_cast<World2DSpriteOverrideFlags>(readU8(payload, base + SpriteOffset + 32U));
                const u8 behavior = readU8(payload, base + SpriteOffset + 33U);
                if ((behavior & ~u8{7U}) != 0U || readU16(payload, base + SpriteOffset + 76U) != 0U ||
                    readU16(payload, base + SpriteOffset + 78U) != 0U)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D sprite behavior or reserved bytes are invalid");
                }
                sprite.flipX = (behavior & 1U) != 0U;
                sprite.flipY = (behavior & 2U) != 0U;
                sprite.visible = (behavior & 4U) != 0U;
                sprite.sortingLayer = readI16(payload, base + SpriteOffset + 34U);
                sprite.orderInLayer = static_cast<Core::i32>(readU32(payload, base + SpriteOffset + 36U));
                const auto overrideFlags = static_cast<u8>(sprite.overrides);
                if ((overrideFlags & ~u8{7U}) != 0U)
                {
                    return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                         "World2D sprite override flags contain reserved bits");
                }
                if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::Size))
                {
                    sprite.sizeX = readF32(payload, base + SpriteOffset + 40U);
                    sprite.sizeY = readF32(payload, base + SpriteOffset + 44U);
                } else if (!bytesAreZero(payload, base + SpriteOffset + 40U, 8U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D absent sprite size override is not canonical");
                }
                if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::Pivot))
                {
                    sprite.pivotX = readF32(payload, base + SpriteOffset + 48U);
                    sprite.pivotY = readF32(payload, base + SpriteOffset + 52U);
                } else if (!bytesAreZero(payload, base + SpriteOffset + 48U, 8U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D absent sprite pivot override is not canonical");
                }
                if (hasFlag(sprite.overrides, World2DSpriteOverrideFlags::UvRect))
                {
                    sprite.uvU0 = readF32(payload, base + SpriteOffset + 56U);
                    sprite.uvV0 = readF32(payload, base + SpriteOffset + 60U);
                    sprite.uvU1 = readF32(payload, base + SpriteOffset + 64U);
                    sprite.uvV1 = readF32(payload, base + SpriteOffset + 68U);
                } else if (!bytesAreZero(payload, base + SpriteOffset + 56U, 16U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D absent sprite UV override is not canonical");
                }
                sprite.colorRed = readU8(payload, base + SpriteOffset + 72U);
                sprite.colorGreen = readU8(payload, base + SpriteOffset + 73U);
                sprite.colorBlue = readU8(payload, base + SpriteOffset + 74U);
                sprite.colorAlpha = readU8(payload, base + SpriteOffset + 75U);
                entity.sprite = sprite;
            } else if (!bytesAreZero(payload, base + SpriteOffset, CameraOffset - SpriteOffset))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "World2D absent sprite component is not canonical");
            }

            if ((componentFlags & World2DSnapshotWire::ComponentCamera) != 0U)
            {
                World2DCameraDesc camera{};
                camera.projection = static_cast<World2DCameraProjectionKind>(readU8(payload, base + CameraOffset));
                camera.pixelSnap = static_cast<World2DPixelSnapPolicy>(readU8(payload, base + CameraOffset + 1U));
                const u8 active = readU8(payload, base + CameraOffset + 2U);
                if (active > 1U || readU8(payload, base + CameraOffset + 3U) != 0U)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D camera active or reserved field is invalid");
                }
                camera.active = active != 0U;
                camera.viewportX = readF32(payload, base + CameraOffset + 4U);
                camera.viewportY = readF32(payload, base + CameraOffset + 8U);
                camera.viewportWidth = readF32(payload, base + CameraOffset + 12U);
                camera.viewportHeight = readF32(payload, base + CameraOffset + 16U);
                camera.fixedWorldHeightMeters = readF32(payload, base + CameraOffset + 20U);
                camera.referencePixelsPerMeter = readF32(payload, base + CameraOffset + 24U);
                camera.referenceHeightPixels = readU32(payload, base + CameraOffset + 28U);
                if (!bytesAreZero(payload, base + CameraOffset + 32U, PointLightOffset - CameraOffset - 32U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D camera reserved bytes are not zero");
                }
                entity.camera = camera;
            } else if (!bytesAreZero(payload, base + CameraOffset, PointLightOffset - CameraOffset))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "World2D absent camera component is not canonical");
            }

            if ((componentFlags & World2DSnapshotWire::ComponentPointLight) != 0U)
            {
                World2DPointLightDesc light{};
                light.colorRed = readF32(payload, base + PointLightOffset);
                light.colorGreen = readF32(payload, base + PointLightOffset + 4U);
                light.colorBlue = readF32(payload, base + PointLightOffset + 8U);
                light.colorAlpha = readF32(payload, base + PointLightOffset + 12U);
                light.intensity = readF32(payload, base + PointLightOffset + 16U);
                light.radiusMeters = readF32(payload, base + PointLightOffset + 20U);
                light.sourceRadiusMeters = readF32(payload, base + PointLightOffset + 24U);
                const u8 active = readU8(payload, base + PointLightOffset + 28U);
                if (active > 1U || !bytesAreZero(payload, base + PointLightOffset + 29U, 3U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D point light active or reserved bytes are invalid");
                }
                light.active = active != 0U;
                entity.pointLight = light;
            } else if (!bytesAreZero(payload, base + PointLightOffset, OccluderOffset - PointLightOffset))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "World2D absent point light component is not canonical");
            }

            if ((componentFlags & World2DSnapshotWire::ComponentShadowOccluder) != 0U)
            {
                World2DShadowOccluderDesc occluder{};
                occluder.localStartX = readF32(payload, base + OccluderOffset);
                occluder.localStartY = readF32(payload, base + OccluderOffset + 4U);
                occluder.localEndX = readF32(payload, base + OccluderOffset + 8U);
                occluder.localEndY = readF32(payload, base + OccluderOffset + 12U);
                const u8 active = readU8(payload, base + OccluderOffset + 16U);
                if (active > 1U || !bytesAreZero(payload, base + OccluderOffset + 17U, 7U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D occluder active or reserved bytes are invalid");
                }
                occluder.active = active != 0U;
                entity.shadowOccluder = occluder;
            } else if (!bytesAreZero(payload, base + OccluderOffset, SpriteAnimationOffset - OccluderOffset))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "World2D absent occluder component is not canonical");
            }

            if ((componentFlags & World2DSnapshotWire::ComponentSpriteAnimation) != 0U)
            {
                World2DSpriteAnimationDesc animation{};
                animation.clipId = readAssetId(payload, base + SpriteAnimationOffset);
                animation.playbackSpeed = readF32(payload, base + SpriteAnimationOffset + 16U);
                const u8 autoPlay = readU8(payload, base + SpriteAnimationOffset + 20U);
                if (autoPlay > 1U ||
                    !bytesAreZero(payload, base + SpriteAnimationOffset + 21U,
                                  World2DSnapshotWire::EntityBytes - SpriteAnimationOffset - 21U))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "World2D sprite animation autoPlay or reserved bytes are invalid");
                }
                animation.autoPlay = autoPlay != 0U;
                entity.spriteAnimation = animation;
            } else if (!bytesAreZero(payload, base + SpriteAnimationOffset,
                                     World2DSnapshotWire::EntityBytes - SpriteAnimationOffset))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "World2D absent sprite animation component is not canonical");
            }
            parsed.push_back(std::move(entity));
        }

        const usize gameplayOffset =
            World2DSnapshotWire::HeaderBytes + static_cast<usize>(entityCount) * World2DSnapshotWire::EntityBytes;
        const std::span<const std::byte> gameplay(payload.data() + gameplayOffset, gameplayBytes);
        if (const Core::Status status = validateSnapshot(parsed, gameplaySchema, gameplayVersion, gameplay); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        entityStorage = std::move(parsed);
        return World2DSnapshotView{
            .schemaVersion = World2DSnapshotWire::SchemaVersion,
            .entities = entityStorage,
            .gameplaySchema = gameplaySchema,
            .gameplayVersion = gameplayVersion,
            .gameplayBytes = gameplay,
        };
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "World2D snapshot parsing allocation failed");
    }
}

} // namespace Tina::AssetFormat
