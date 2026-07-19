#include <tina/render/RenderScene.hpp>

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina::Render {
namespace {

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finiteViewport(const RenderNormalizedViewport& viewport) noexcept
{
    if (!finite(viewport.x) || !finite(viewport.y) || !finite(viewport.width) || !finite(viewport.height) ||
        viewport.x < 0.0F || viewport.y < 0.0F || viewport.width <= 0.0F || viewport.height <= 0.0F)
    {
        return false;
    }
    const double right = static_cast<double>(viewport.x) + static_cast<double>(viewport.width);
    const double bottom = static_cast<double>(viewport.y) + static_cast<double>(viewport.height);
    return right <= 1.0 && bottom <= 1.0;
}

[[nodiscard]] bool validPixelSnapPolicy(RenderPixelSnapPolicy policy) noexcept
{
    return policy == RenderPixelSnapPolicy::Disabled || policy == RenderPixelSnapPolicy::CameraTranslation ||
           policy == RenderPixelSnapPolicy::CameraAndSprites;
}

[[nodiscard]] float snapCoordinate(float value, float pixelsPerMeter) noexcept
{
    if (!finite(value) || !finite(pixelsPerMeter) || pixelsPerMeter <= 0.0F)
    {
        return value;
    }
    const double snapped = std::round(static_cast<double>(value) * pixelsPerMeter) / pixelsPerMeter;
    return finite(static_cast<float>(snapped)) ? static_cast<float>(snapped) : value;
}

inline constexpr u64 FnvOffset = 14695981039346656037ULL;
inline constexpr u64 FnvPrime = 1099511628211ULL;

void hashByte(u64& hash, u8 value) noexcept
{
    hash ^= value;
    hash *= FnvPrime;
}

template <typename Value>
void hashUnsigned(u64& hash, Value value) noexcept
{
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned unsignedValue = static_cast<Unsigned>(value);
    for (usize shift = 0; shift < sizeof(Value); ++shift)
    {
        hashByte(hash, static_cast<u8>(unsignedValue >> (shift * 8U)));
    }
}

[[nodiscard]] Core::Status buildStateFailure(Core::ErrorCode code, std::string_view message)
{
    return Core::failure(code, message);
}

} // namespace

Core::Status validateRenderSceneCapacity(const RenderSceneCapacity& capacity) noexcept
{
    if (capacity.spriteCapacity == 0 || capacity.spriteCapacity > RenderSceneCapacity::MaximumSpriteCapacity)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneCapacity,
                             "RenderScene sprite capacity is outside the supported range");
    }
    return Core::success();
}

Core::Result<RenderSceneBuilder> RenderSceneBuilder::Create(RenderSceneCapacity capacity,
                                                            std::pmr::memory_resource& storage)
{
    if (auto status = validateRenderSceneCapacity(capacity); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    const usize spriteCount = static_cast<usize>(capacity.spriteCapacity);
    if (spriteCount > (std::numeric_limits<usize>::max)() / sizeof(RenderSprite2DItem))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneCapacity,
                             "RenderScene sprite capacity exceeds addressable storage");
    }
    const usize spriteBytes = sizeof(RenderSprite2DItem) * spriteCount;
    RenderSprite2DItem* sprites = nullptr;
    try
    {
        sprites = static_cast<RenderSprite2DItem*>(storage.allocate(spriteBytes, alignof(RenderSprite2DItem)));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(RenderErrorCode::RenderSceneStorageAllocationFailed,
                             "RenderScene fixed storage allocation failed");
    }
    return RenderSceneBuilder{capacity, storage, sprites};
}

RenderSceneBuilder::RenderSceneBuilder(RenderSceneCapacity capacity, std::pmr::memory_resource& storage,
                                       RenderSprite2DItem* sprites) noexcept
    : m_capacity(capacity), m_storage(&storage), m_sprites(sprites)
{
}

RenderSceneBuilder::RenderSceneBuilder(RenderSceneBuilder&& other) noexcept
    : m_capacity(other.m_capacity), m_storage(std::exchange(other.m_storage, nullptr)),
      m_sprites(std::exchange(other.m_sprites, nullptr)),
      m_spriteCount(std::exchange(other.m_spriteCount, 0)), m_camera(std::move(other.m_camera)),
      m_candidateStatistics(other.m_candidateStatistics), m_publishedStatistics(other.m_publishedStatistics),
      m_statistics(other.m_statistics), m_stickyBuildError(std::move(other.m_stickyBuildError)),
      m_state(other.m_state)
{
    other.m_capacity = {};
    other.m_camera.reset();
    other.m_candidateStatistics = {};
    other.m_publishedStatistics = {};
    other.m_statistics = {};
    other.m_stickyBuildError.reset();
    other.m_state = State::Ready;
}

RenderSceneBuilder::~RenderSceneBuilder()
{
    releaseStorage();
}

Core::Status RenderSceneBuilder::beginFrame()
{
    if (m_state == State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildAlreadyOpen,
                                 "A RenderScene build is already open");
    }

    clearCandidate();
    m_publishedStatistics = {};
    m_stickyBuildError.reset();
    m_state = State::Building;
    ++m_statistics.begunBuildCount;
    return Core::success();
}

RenderSceneWriter RenderSceneBuilder::writer() noexcept
{
    return RenderSceneWriter{*this};
}

Core::Status RenderSceneWriter::setCamera2D(const RenderCamera2DInput& camera)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->setCamera2D(camera);
}

Core::Status RenderSceneWriter::addSprite2D(const RenderSprite2DInput& sprite)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->addSprite2D(sprite);
}

Core::Status RenderSceneBuilder::validateCamera(const RenderCamera2DInput& camera) const noexcept
{
    if (camera.stableCameraKey == 0 || !finite(camera.centerX) || !finite(camera.centerY) ||
        !finite(camera.rotationRadians) ||
        !finite(camera.worldWidth) || !finite(camera.worldHeight) || !finite(camera.actualPixelsPerMeter) ||
        camera.worldWidth <= 0.0F || camera.worldHeight <= 0.0F || camera.actualPixelsPerMeter <= 0.0F ||
        !finiteViewport(camera.normalizedViewport) || !validPixelSnapPolicy(camera.pixelSnap))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene Camera2D contains invalid projection or viewport values");
    }
    return Core::success();
}

Core::Status RenderSceneBuilder::validateSprite(const RenderSprite2DInput& sprite) const noexcept
{
    const float scaledWidth = sprite.widthMeters * std::abs(sprite.scaleX);
    const float scaledHeight = sprite.heightMeters * std::abs(sprite.scaleY);
    if (sprite.spriteKey == 0 || sprite.stableEntityKey == 0 || !finite(sprite.centerX) ||
        !finite(sprite.centerY) ||
        !finite(sprite.rotationRadians) || !finite(sprite.widthMeters) || !finite(sprite.heightMeters) ||
        !finite(sprite.scaleX) || !finite(sprite.scaleY) ||
        sprite.widthMeters <= 0.0F || sprite.heightMeters <= 0.0F || sprite.scaleX == 0.0F || sprite.scaleY == 0.0F ||
        !finite(scaledWidth) || !finite(scaledHeight) || scaledWidth <= 0.0F || scaledHeight <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene Sprite2D contains invalid geometry or resource values");
    }
    return Core::success();
}

Core::Status RenderSceneBuilder::failBuild(Core::ErrorCode code, const char* message)
{
    if (!m_stickyBuildError.has_value())
    {
        m_stickyBuildError.emplace(code, message);
        if (code == RenderErrorCode::RenderSceneCapacityExceeded)
        {
            ++m_statistics.capacityFailureCount;
        }
        else
        {
            ++m_statistics.invalidInputFailureCount;
        }
    }
    return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                     m_stickyBuildError->origin});
}

Core::Status RenderSceneBuilder::setCamera2D(const RenderCamera2DInput& camera)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before setting a camera");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (m_camera.has_value())
    {
        return failBuild(RenderErrorCode::RenderSceneCameraConflict,
                         "A RenderScene may contain only one active Camera2D");
    }
    if (auto status = validateCamera(camera); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    m_camera = RenderCamera2D{
        .stableCameraKey = camera.stableCameraKey,
        .centerX = camera.centerX,
        .centerY = camera.centerY,
        .rotationRadians = camera.rotationRadians,
        .worldWidth = camera.worldWidth,
        .worldHeight = camera.worldHeight,
        .actualPixelsPerMeter = camera.actualPixelsPerMeter,
        .normalizedViewport = camera.normalizedViewport,
        .pixelSnap = camera.pixelSnap,
    };
    if (camera.pixelSnap != RenderPixelSnapPolicy::Disabled)
    {
        m_camera->centerX = snapCoordinate(m_camera->centerX, m_camera->actualPixelsPerMeter);
        m_camera->centerY = snapCoordinate(m_camera->centerY, m_camera->actualPixelsPerMeter);
    }
    m_candidateStatistics.cameraCount = 1;
    return Core::success();
}

Core::Status RenderSceneBuilder::addSprite2D(const RenderSprite2DInput& sprite)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before adding a sprite");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (auto status = validateSprite(sprite); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    ++m_candidateStatistics.submittedSpriteCount;
    if (!sprite.visible)
    {
        ++m_candidateStatistics.prunedInvisibleCount;
        return Core::success();
    }
    if (sprite.alpha == 0)
    {
        ++m_candidateStatistics.prunedTransparentCount;
        return Core::success();
    }
    if (m_spriteCount >= m_capacity.spriteCapacity)
    {
        return failBuild(RenderErrorCode::RenderSceneCapacityExceeded,
                         "RenderScene sprite capacity was exceeded");
    }

    std::construct_at(&m_sprites[m_spriteCount], RenderSprite2DItem{
        .spriteKey = sprite.spriteKey,
        .stableEntityKey = sprite.stableEntityKey,
        .insertionOrder = m_spriteCount,
        .centerX = sprite.centerX,
        .centerY = sprite.centerY,
        .rotationRadians = sprite.rotationRadians,
        .widthMeters = sprite.widthMeters,
        .heightMeters = sprite.heightMeters,
        .scaleX = sprite.scaleX,
        .scaleY = sprite.scaleY,
        .sortingLayer = sprite.sortingLayer,
        .orderInLayer = sprite.orderInLayer,
        .red = sprite.red,
        .green = sprite.green,
        .blue = sprite.blue,
        .alpha = sprite.alpha,
        .flipX = sprite.flipX,
        .flipY = sprite.flipY,
    });
    ++m_spriteCount;
    return Core::success();
}

bool RenderSceneBuilder::intersectsCamera(const RenderSprite2DItem& sprite,
                                          const RenderCamera2D& camera) const noexcept
{
    const float relativeX = sprite.centerX - camera.centerX;
    const float relativeY = sprite.centerY - camera.centerY;
    const float cameraCosine = std::cos(camera.rotationRadians);
    const float cameraSine = std::sin(camera.rotationRadians);
    const float localX = relativeX * cameraCosine + relativeY * cameraSine;
    const float localY = -relativeX * cameraSine + relativeY * cameraCosine;

    const float relativeRotation = sprite.rotationRadians - camera.rotationRadians;
    const float spriteCosine = std::abs(std::cos(relativeRotation));
    const float spriteSine = std::abs(std::sin(relativeRotation));
    const float halfWidth = 0.5F * (sprite.widthMeters * std::abs(sprite.scaleX) * spriteCosine +
                                    sprite.heightMeters * std::abs(sprite.scaleY) * spriteSine);
    const float halfHeight = 0.5F * (sprite.widthMeters * std::abs(sprite.scaleX) * spriteSine +
                                     sprite.heightMeters * std::abs(sprite.scaleY) * spriteCosine);
    const float cameraHalfWidth = camera.worldWidth * 0.5F;
    const float cameraHalfHeight = camera.worldHeight * 0.5F;
    return finite(localX) && finite(localY) && finite(halfWidth) && finite(halfHeight) &&
           std::abs(localX) - halfWidth <= cameraHalfWidth &&
           std::abs(localY) - halfHeight <= cameraHalfHeight;
}

Core::Result<RenderSceneView> RenderSceneBuilder::commit()
{
    if (m_state != State::Building)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "A RenderScene build must be open before commit");
    }
    if (m_stickyBuildError.has_value())
    {
        Core::Error error{m_stickyBuildError->code, m_stickyBuildError->message, m_stickyBuildError->origin};
        rollbackBuilding();
        return Core::failure(std::move(error));
    }
    if (m_spriteCount != 0 && !m_camera.has_value())
    {
        Core::Status status = failBuild(RenderErrorCode::RenderSceneMissingCamera,
                                         "World sprites require exactly one active Camera2D");
        Core::Error error = std::move(status.error());
        rollbackBuilding();
        return Core::failure(std::move(error));
    }

    if (m_camera.has_value() && m_camera->pixelSnap == RenderPixelSnapPolicy::CameraAndSprites)
    {
        for (RenderSprite2DItem& sprite : std::span<RenderSprite2DItem>{m_sprites, m_spriteCount})
        {
            sprite.centerX = snapCoordinate(sprite.centerX, m_camera->actualPixelsPerMeter);
            sprite.centerY = snapCoordinate(sprite.centerY, m_camera->actualPixelsPerMeter);
        }
    }

    if (m_camera.has_value())
    {
        usize writeIndex = 0;
        for (usize readIndex = 0; readIndex < m_spriteCount; ++readIndex)
        {
            const RenderSprite2DItem& candidate = m_sprites[readIndex];
            if (!intersectsCamera(candidate, *m_camera))
            {
                ++m_candidateStatistics.culledSpriteCount;
                continue;
            }
            if (writeIndex != readIndex)
            {
                m_sprites[writeIndex] = candidate;
            }
            ++writeIndex;
        }
        if (writeIndex < m_spriteCount)
        {
            std::destroy_n(m_sprites + writeIndex, m_spriteCount - writeIndex);
            m_spriteCount = static_cast<u32>(writeIndex);
        }
    }

    std::sort(m_sprites, m_sprites + m_spriteCount, [](const RenderSprite2DItem& left,
                                                       const RenderSprite2DItem& right) noexcept {
        if (left.sortingLayer != right.sortingLayer)
        {
            return left.sortingLayer < right.sortingLayer;
        }
        if (left.orderInLayer != right.orderInLayer)
        {
            return left.orderInLayer < right.orderInLayer;
        }
        if (left.stableEntityKey != right.stableEntityKey)
        {
            return left.stableEntityKey < right.stableEntityKey;
        }
        return left.insertionOrder < right.insertionOrder;
    });

    m_candidateStatistics.visibleSpriteCount = m_spriteCount;
    u64 checksum = FnvOffset;
    for (const RenderSprite2DItem& sprite : std::span<const RenderSprite2DItem>{m_sprites, m_spriteCount})
    {
        hashUnsigned(checksum, sprite.sortingLayer);
        hashUnsigned(checksum, sprite.orderInLayer);
        hashUnsigned(checksum, sprite.stableEntityKey);
        hashUnsigned(checksum, sprite.insertionOrder);
    }
    m_candidateStatistics.sortOrderChecksum = checksum;
    m_publishedStatistics = m_candidateStatistics;
    m_state = State::Published;
    ++m_statistics.committedBuildCount;
    return makePublishedView();
}

void RenderSceneBuilder::rollback() noexcept
{
    if (m_state == State::Building)
    {
        rollbackBuilding();
    }
}

void RenderSceneBuilder::clearCandidate() noexcept
{
    if (m_spriteCount != 0)
    {
        std::destroy_n(m_sprites, m_spriteCount);
        m_spriteCount = 0;
    }
    m_camera.reset();
    m_candidateStatistics = {};
    m_stickyBuildError.reset();
}

void RenderSceneBuilder::releaseStorage() noexcept
{
    if (m_storage == nullptr)
    {
        return;
    }
    clearCandidate();
    m_storage->deallocate(m_sprites,
                          sizeof(RenderSprite2DItem) * static_cast<usize>(m_capacity.spriteCapacity),
                          alignof(RenderSprite2DItem));
    m_storage = nullptr;
    m_sprites = nullptr;
}

void RenderSceneBuilder::rollbackBuilding() noexcept
{
    clearCandidate();
    m_publishedStatistics = {};
    m_state = State::Ready;
    ++m_statistics.rolledBackBuildCount;
}

RenderSceneView RenderSceneBuilder::makePublishedView() const noexcept
{
    if (m_state != State::Published)
    {
        return {};
    }
    return RenderSceneView{m_camera, std::span<const RenderSprite2DItem>{m_sprites, m_spriteCount},
                           m_publishedStatistics};
}

RenderSceneView RenderSceneBuilder::publishedView() const noexcept
{
    return makePublishedView();
}

RenderSceneCapacity RenderSceneBuilder::capacity() const noexcept
{
    return m_capacity;
}

RenderSceneBuilderStatistics RenderSceneBuilder::statistics() const noexcept
{
    return m_statistics;
}

} // namespace Tina::Render
