#include <tina/render/RenderScene.hpp>

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina::Render {
namespace {

struct Vector3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quaternion final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

inline constexpr float Pi = 3.14159265358979323846F;
inline constexpr u64 FnvOffset = 14695981039346656037ULL;
inline constexpr u64 FnvPrime = 1099511628211ULL;

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(Vector3 value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] float dot(Vector3 left, Vector3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vector3 cross(Vector3 left, Vector3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vector3 subtract(Vector3 left, Vector3 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vector3 add(Vector3 left, Vector3 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vector3 multiply(Vector3 left, Vector3 right) noexcept
{
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] Core::Result<Quaternion> normalizedQuaternion(const RenderPose3DInput& pose)
{
    const double lengthSquared = static_cast<double>(pose.rotationX) * pose.rotationX +
                                 static_cast<double>(pose.rotationY) * pose.rotationY +
                                 static_cast<double>(pose.rotationZ) * pose.rotationZ +
                                 static_cast<double>(pose.rotationW) * pose.rotationW;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-24)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene 3D rotation quaternion is invalid");
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
    const Quaternion normalized{
        .x = pose.rotationX * inverseLength,
        .y = pose.rotationY * inverseLength,
        .z = pose.rotationZ * inverseLength,
        .w = pose.rotationW * inverseLength,
    };
    if (!finite(normalized.x) || !finite(normalized.y) || !finite(normalized.z) || !finite(normalized.w))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene 3D rotation normalization overflowed");
    }
    return normalized;
}

[[nodiscard]] Vector3 rotate(Quaternion rotation, Vector3 value) noexcept
{
    const Vector3 quaternionVector{rotation.x, rotation.y, rotation.z};
    const Vector3 twiceCross{
        2.0F * (quaternionVector.y * value.z - quaternionVector.z * value.y),
        2.0F * (quaternionVector.z * value.x - quaternionVector.x * value.z),
        2.0F * (quaternionVector.x * value.y - quaternionVector.y * value.x),
    };
    return add(value, add(
                          {rotation.w * twiceCross.x, rotation.w * twiceCross.y, rotation.w * twiceCross.z},
                          cross(quaternionVector, twiceCross)));
}

[[nodiscard]] std::array<float, 16> makeColumnMajorWorldTransform(const RenderTransform3DInput& transform,
                                                                  Quaternion rotation) noexcept
{
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    return {
        (1.0F - 2.0F * (yy + zz)) * transform.scaleX,
        (2.0F * (xy + wz)) * transform.scaleX,
        (2.0F * (xz - wy)) * transform.scaleX,
        0.0F,
        (2.0F * (xy - wz)) * transform.scaleY,
        (1.0F - 2.0F * (xx + zz)) * transform.scaleY,
        (2.0F * (yz + wx)) * transform.scaleY,
        0.0F,
        (2.0F * (xz + wy)) * transform.scaleZ,
        (2.0F * (yz - wx)) * transform.scaleZ,
        (1.0F - 2.0F * (xx + yy)) * transform.scaleZ,
        0.0F,
        transform.pose.positionX,
        transform.pose.positionY,
        transform.pose.positionZ,
        1.0F,
    };
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

[[nodiscard]] bool finiteColor(RenderLinearColor color) noexcept
{
    return finite(color.red) && finite(color.green) && finite(color.blue) && finite(color.alpha) &&
           color.red >= 0.0F && color.red <= 1.0F && color.green >= 0.0F && color.green <= 1.0F &&
           color.blue >= 0.0F && color.blue <= 1.0F && color.alpha == 1.0F;
}

template <typename Value>
[[nodiscard]] bool checkedStorageBytes(u32 count, usize& bytes) noexcept
{
    const usize elementCount = static_cast<usize>(count);
    if (elementCount > (std::numeric_limits<usize>::max)() / sizeof(Value))
    {
        return false;
    }
    bytes = elementCount * sizeof(Value);
    return true;
}

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

[[nodiscard]] u32 depthBucket(float cameraDepth, const RenderPerspectiveCamera& camera) noexcept
{
    const double normalized = std::clamp(
        (static_cast<double>(cameraDepth) - camera.nearPlaneMeters) /
            (static_cast<double>(camera.farPlaneMeters) - camera.nearPlaneMeters),
        0.0, 1.0);
    return static_cast<u32>(std::llround(normalized * 65'535.0));
}

[[nodiscard]] bool sameMeshBatch(const RenderMesh3DItem& left, const RenderMesh3DItem& right) noexcept
{
    return left.mesh == right.mesh && left.material == right.material &&
           left.submeshIndex == right.submeshIndex && left.doubleSided == right.doubleSided;
}

} // namespace

Core::Status validateRenderSceneCapacity(const RenderSceneCapacity& capacity) noexcept
{
    if (capacity.spriteCapacity == 0 || capacity.spriteCapacity > RenderSceneCapacity::MaximumSpriteCapacity ||
        capacity.mesh3DItemCapacity == 0 ||
        capacity.mesh3DItemCapacity > RenderSceneCapacity::MaximumMesh3DItemCapacity ||
        capacity.mesh3DBatchCapacity == 0 ||
        capacity.mesh3DBatchCapacity > RenderSceneCapacity::MaximumMesh3DBatchCapacity)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneCapacity,
                             "RenderScene capacity is outside the supported range");
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

    usize spriteBytes = 0;
    usize mesh3DBytes = 0;
    usize mesh3DBatchBytes = 0;
    if (!checkedStorageBytes<RenderSprite2DItem>(capacity.spriteCapacity, spriteBytes) ||
        !checkedStorageBytes<RenderMesh3DItem>(capacity.mesh3DItemCapacity, mesh3DBytes) ||
        !checkedStorageBytes<RenderMesh3DBatch>(capacity.mesh3DBatchCapacity, mesh3DBatchBytes))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneCapacity,
                             "RenderScene capacity exceeds addressable storage");
    }

    RenderSprite2DItem* sprites = nullptr;
    RenderMesh3DItem* meshes3D = nullptr;
    RenderMesh3DBatch* mesh3DBatches = nullptr;
    try
    {
        sprites = static_cast<RenderSprite2DItem*>(storage.allocate(spriteBytes, alignof(RenderSprite2DItem)));
        meshes3D = static_cast<RenderMesh3DItem*>(storage.allocate(mesh3DBytes, alignof(RenderMesh3DItem)));
        mesh3DBatches =
            static_cast<RenderMesh3DBatch*>(storage.allocate(mesh3DBatchBytes, alignof(RenderMesh3DBatch)));
    }
    catch (const std::bad_alloc&)
    {
        if (meshes3D != nullptr)
        {
            storage.deallocate(meshes3D, mesh3DBytes, alignof(RenderMesh3DItem));
        }
        if (sprites != nullptr)
        {
            storage.deallocate(sprites, spriteBytes, alignof(RenderSprite2DItem));
        }
        return Core::failure(RenderErrorCode::RenderSceneStorageAllocationFailed,
                             "RenderScene fixed storage allocation failed");
    }
    return RenderSceneBuilder{capacity, storage, sprites, meshes3D, mesh3DBatches};
}

RenderSceneBuilder::RenderSceneBuilder(RenderSceneCapacity capacity, std::pmr::memory_resource& storage,
                                       RenderSprite2DItem* sprites, RenderMesh3DItem* meshes3D,
                                       RenderMesh3DBatch* mesh3DBatches) noexcept
    : m_capacity(capacity), m_storage(&storage), m_sprites(sprites), m_meshes3D(meshes3D),
      m_mesh3DBatches(mesh3DBatches)
{
}

RenderSceneBuilder::RenderSceneBuilder(RenderSceneBuilder&& other) noexcept
    : m_capacity(other.m_capacity), m_storage(std::exchange(other.m_storage, nullptr)),
      m_sprites(std::exchange(other.m_sprites, nullptr)), m_meshes3D(std::exchange(other.m_meshes3D, nullptr)),
      m_mesh3DBatches(std::exchange(other.m_mesh3DBatches, nullptr)),
      m_spriteCount(std::exchange(other.m_spriteCount, 0)), m_mesh3DCount(std::exchange(other.m_mesh3DCount, 0)),
      m_mesh3DBatchCount(std::exchange(other.m_mesh3DBatchCount, 0)), m_camera(std::move(other.m_camera)),
      m_perspectiveCamera(std::move(other.m_perspectiveCamera)), m_frameParameters(other.m_frameParameters),
      m_candidateStatistics(other.m_candidateStatistics), m_publishedStatistics(other.m_publishedStatistics),
      m_statistics(other.m_statistics), m_stickyBuildError(std::move(other.m_stickyBuildError)), m_state(other.m_state)
{
    other.m_capacity = {};
    other.m_camera.reset();
    other.m_perspectiveCamera.reset();
    other.m_frameParameters = {};
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

Core::Status RenderSceneBuilder::beginFrame(RenderSceneFrameParameters parameters)
{
    if (m_state == State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildAlreadyOpen,
                                 "A RenderScene build is already open");
    }
    if (parameters.primarySurfaceAspectRatio.has_value() &&
        (!finite(*parameters.primarySurfaceAspectRatio) || *parameters.primarySurfaceAspectRatio <= 0.0F))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene primary surface aspect ratio must be finite and positive");
    }

    clearCandidate();
    m_publishedStatistics = {};
    m_stickyBuildError.reset();
    m_frameParameters = parameters;
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

Core::Status RenderSceneWriter::setPerspectiveCamera(const RenderPerspectiveCameraInput& camera)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->setPerspectiveCamera(camera);
}

Core::Status RenderSceneWriter::addMesh3D(const RenderMesh3DInput& mesh)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->addMesh3D(mesh);
}

Core::Status RenderSceneBuilder::validateCamera(const RenderCamera2DInput& camera) const noexcept
{
    if (camera.stableCameraKey == 0 || !finite(camera.centerX) || !finite(camera.centerY) ||
        !finite(camera.rotationRadians) || !finite(camera.worldWidth) || !finite(camera.worldHeight) ||
        !finite(camera.actualPixelsPerMeter) || camera.worldWidth <= 0.0F || camera.worldHeight <= 0.0F ||
        camera.actualPixelsPerMeter <= 0.0F || !finiteViewport(camera.normalizedViewport) ||
        !validPixelSnapPolicy(camera.pixelSnap))
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
    if (!sprite.texture || sprite.stableEntityKey == 0 || !finite(sprite.centerX) ||
        !finite(sprite.centerY) || !finite(sprite.rotationRadians) || !finite(sprite.widthMeters) ||
        !finite(sprite.heightMeters) || !finite(sprite.scaleX) || !finite(sprite.scaleY) ||
        sprite.widthMeters <= 0.0F || sprite.heightMeters <= 0.0F || sprite.scaleX == 0.0F ||
        sprite.scaleY == 0.0F || !finite(scaledWidth) || !finite(scaledHeight) || scaledWidth <= 0.0F ||
        scaledHeight <= 0.0F || !finite(sprite.u0) || !finite(sprite.v0) || !finite(sprite.u1) ||
        !finite(sprite.v1) || sprite.u0 < 0.0F || sprite.v0 < 0.0F || sprite.u1 > 1.0F || sprite.v1 > 1.0F ||
        !(sprite.u0 < sprite.u1) || !(sprite.v0 < sprite.v1))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene Sprite2D contains invalid geometry or resource values");
    }
    return Core::success();
}

Core::Status RenderSceneBuilder::validatePerspectiveCamera(const RenderPerspectiveCameraInput& camera) const noexcept
{
    const RenderPose3DInput& pose = camera.worldPose;
    if (camera.stableCameraKey == 0 || !finite(pose.positionX) || !finite(pose.positionY) ||
        !finite(pose.positionZ) || !finite(pose.rotationX) || !finite(pose.rotationY) ||
        !finite(pose.rotationZ) || !finite(pose.rotationW) || !finite(camera.verticalFovDegrees) ||
        !finite(camera.nearPlaneMeters) || !finite(camera.farPlaneMeters) || camera.verticalFovDegrees < 1.0F ||
        camera.verticalFovDegrees > 179.0F || camera.nearPlaneMeters <= 0.0F ||
        camera.farPlaneMeters <= camera.nearPlaneMeters || !finiteViewport(camera.normalizedViewport) ||
        !m_frameParameters.primarySurfaceAspectRatio.has_value())
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene PerspectiveCamera contains invalid pose, projection, or surface values");
    }
    if (auto rotation = normalizedQuaternion(pose); !rotation)
    {
        return Core::failure(std::move(rotation.error()));
    }
    const double viewportAspect = static_cast<double>(*m_frameParameters.primarySurfaceAspectRatio) *
                                  static_cast<double>(camera.normalizedViewport.width) /
                                  camera.normalizedViewport.height;
    if (!std::isfinite(viewportAspect) || viewportAspect <= 0.0 ||
        viewportAspect > (std::numeric_limits<float>::max)())
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene PerspectiveCamera viewport aspect is invalid");
    }
    return Core::success();
}

Core::Status RenderSceneBuilder::validateMesh3D(const RenderMesh3DInput& mesh) const noexcept
{
    const RenderPose3DInput& pose = mesh.worldTransform.pose;
    const RenderBoundingSphereInput& bounds = mesh.localBounds;
    if (!mesh.mesh.hasValue() || !mesh.material.hasValue() || mesh.stableEntityKey == 0 || !finite(pose.positionX) ||
        !finite(pose.positionY) || !finite(pose.positionZ) || !finite(pose.rotationX) ||
        !finite(pose.rotationY) || !finite(pose.rotationZ) || !finite(pose.rotationW) ||
        !finite(mesh.worldTransform.scaleX) || !finite(mesh.worldTransform.scaleY) ||
        !finite(mesh.worldTransform.scaleZ) || mesh.worldTransform.scaleX <= 0.0F ||
        mesh.worldTransform.scaleY <= 0.0F || mesh.worldTransform.scaleZ <= 0.0F ||
        !finite(bounds.centerX) || !finite(bounds.centerY) || !finite(bounds.centerZ) || !finite(bounds.radius) ||
        bounds.radius <= 0.0F || !finiteColor(mesh.baseColorFactor))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene Mesh3D contains invalid geometry, transform, material, or resource values");
    }
    if (auto rotation = normalizedQuaternion(pose); !rotation)
    {
        return Core::failure(std::move(rotation.error()));
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
    ++m_candidateStatistics.cameraCount;
    m_candidateStatistics.camera2DCount = 1;
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
        .texture = sprite.texture,
        .stableEntityKey = sprite.stableEntityKey,
        .insertionOrder = m_spriteCount,
        .centerX = sprite.centerX,
        .centerY = sprite.centerY,
        .rotationRadians = sprite.rotationRadians,
        .widthMeters = sprite.widthMeters,
        .heightMeters = sprite.heightMeters,
        .scaleX = sprite.scaleX,
        .scaleY = sprite.scaleY,
        .u0 = sprite.u0,
        .v0 = sprite.v0,
        .u1 = sprite.u1,
        .v1 = sprite.v1,
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

Core::Status RenderSceneBuilder::setPerspectiveCamera(const RenderPerspectiveCameraInput& camera)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before setting a perspective camera");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (m_perspectiveCamera.has_value())
    {
        return failBuild(RenderErrorCode::RenderSceneCameraConflict,
                         "A RenderScene may contain only one active PerspectiveCamera");
    }
    if (auto status = validatePerspectiveCamera(camera); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    auto rotationResult = normalizedQuaternion(camera.worldPose);
    if (!rotationResult)
    {
        return failBuild(rotationResult.error().code, rotationResult.error().message.c_str());
    }
    const Quaternion rotation = *rotationResult;
    const Vector3 forward = rotate(rotation, {0.0F, 0.0F, -1.0F});
    const Vector3 up = rotate(rotation, {0.0F, 1.0F, 0.0F});
    const float aspectRatio = *m_frameParameters.primarySurfaceAspectRatio *
                              camera.normalizedViewport.width / camera.normalizedViewport.height;
    m_perspectiveCamera = RenderPerspectiveCamera{
        .stableCameraKey = camera.stableCameraKey,
        .positionX = camera.worldPose.positionX,
        .positionY = camera.worldPose.positionY,
        .positionZ = camera.worldPose.positionZ,
        .forwardX = forward.x,
        .forwardY = forward.y,
        .forwardZ = forward.z,
        .upX = up.x,
        .upY = up.y,
        .upZ = up.z,
        .verticalFovDegrees = camera.verticalFovDegrees,
        .nearPlaneMeters = camera.nearPlaneMeters,
        .farPlaneMeters = camera.farPlaneMeters,
        .aspectRatio = aspectRatio,
        .normalizedViewport = camera.normalizedViewport,
    };
    ++m_candidateStatistics.cameraCount;
    m_candidateStatistics.perspectiveCameraCount = 1;
    return Core::success();
}

Core::Status RenderSceneBuilder::addMesh3D(const RenderMesh3DInput& mesh)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before adding a 3D mesh");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (auto status = validateMesh3D(mesh); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    ++m_candidateStatistics.submittedMesh3DCount;
    if (!mesh.visible)
    {
        ++m_candidateStatistics.prunedInvisibleMesh3DCount;
        return Core::success();
    }
    if (m_mesh3DCount >= m_capacity.mesh3DItemCapacity)
    {
        return failBuild(RenderErrorCode::RenderSceneCapacityExceeded,
                         "RenderScene Mesh3D item capacity was exceeded");
    }

    auto rotationResult = normalizedQuaternion(mesh.worldTransform.pose);
    if (!rotationResult)
    {
        return failBuild(rotationResult.error().code, rotationResult.error().message.c_str());
    }
    const Quaternion rotation = *rotationResult;
    const Vector3 scaledLocalCenter = multiply(
        {mesh.localBounds.centerX, mesh.localBounds.centerY, mesh.localBounds.centerZ},
        {mesh.worldTransform.scaleX, mesh.worldTransform.scaleY, mesh.worldTransform.scaleZ});
    const Vector3 worldCenter = add(
        {mesh.worldTransform.pose.positionX, mesh.worldTransform.pose.positionY, mesh.worldTransform.pose.positionZ},
        rotate(rotation, scaledLocalCenter));
    const float worldRadius = mesh.localBounds.radius *
                              std::max({mesh.worldTransform.scaleX, mesh.worldTransform.scaleY,
                                        mesh.worldTransform.scaleZ});
    const std::array<float, 16> worldTransform = makeColumnMajorWorldTransform(mesh.worldTransform, rotation);
    if (!finite(worldCenter) || !finite(worldRadius) || worldRadius <= 0.0F ||
        !std::ranges::all_of(worldTransform, [](float value) noexcept { return finite(value); }))
    {
        return failBuild(RenderErrorCode::InvalidRenderSceneInput,
                         "RenderScene Mesh3D world transform or bounds overflowed");
    }

    std::construct_at(&m_meshes3D[m_mesh3DCount], RenderMesh3DItem{
        .mesh = mesh.mesh,
        .material = mesh.material,
        .submeshIndex = mesh.submeshIndex,
        .stableEntityKey = mesh.stableEntityKey,
        .insertionOrder = m_mesh3DCount,
        .depthBucket = 0,
        .cameraDepth = 0.0F,
        .worldBoundsCenterX = worldCenter.x,
        .worldBoundsCenterY = worldCenter.y,
        .worldBoundsCenterZ = worldCenter.z,
        .worldBoundsRadius = worldRadius,
        .columnMajorWorldTransform = worldTransform,
        .baseColorFactor = mesh.baseColorFactor,
        .doubleSided = mesh.doubleSided,
    });
    ++m_mesh3DCount;
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

bool RenderSceneBuilder::intersectsPerspectiveCamera(const RenderMesh3DItem& mesh,
                                                     const RenderPerspectiveCamera& camera,
                                                     float& cameraDepth) const noexcept
{
    const Vector3 cameraPosition{camera.positionX, camera.positionY, camera.positionZ};
    const Vector3 forward{camera.forwardX, camera.forwardY, camera.forwardZ};
    const Vector3 up{camera.upX, camera.upY, camera.upZ};
    const Vector3 right = cross(forward, up);
    const Vector3 relative = subtract(
        {mesh.worldBoundsCenterX, mesh.worldBoundsCenterY, mesh.worldBoundsCenterZ}, cameraPosition);
    const float x = dot(relative, right);
    const float y = dot(relative, up);
    const float depth = dot(relative, forward);
    const float radius = mesh.worldBoundsRadius;
    cameraDepth = depth;

    if (!finite(x) || !finite(y) || !finite(depth) || !finite(radius) ||
        depth + radius < camera.nearPlaneMeters || depth - radius > camera.farPlaneMeters)
    {
        return false;
    }

    const float tangentY = std::tan(camera.verticalFovDegrees * Pi / 360.0F);
    const float tangentX = tangentY * camera.aspectRatio;
    const float horizontalRadiusScale = std::sqrt(tangentX * tangentX + 1.0F);
    const float verticalRadiusScale = std::sqrt(tangentY * tangentY + 1.0F);
    return depth * tangentX + x >= -radius * horizontalRadiusScale &&
           depth * tangentX - x >= -radius * horizontalRadiusScale &&
           depth * tangentY + y >= -radius * verticalRadiusScale &&
           depth * tangentY - y >= -radius * verticalRadiusScale;
}

Core::Status RenderSceneBuilder::finalizeMesh3DBatches()
{
    if (m_mesh3DCount == 0)
    {
        return Core::success();
    }

    u32 firstItem = 0;
    while (firstItem < m_mesh3DCount)
    {
        u32 nextItem = firstItem + 1;
        while (nextItem < m_mesh3DCount && sameMeshBatch(m_meshes3D[firstItem], m_meshes3D[nextItem]))
        {
            ++nextItem;
        }
        if (m_mesh3DBatchCount >= m_capacity.mesh3DBatchCapacity)
        {
            return failBuild(RenderErrorCode::RenderSceneCapacityExceeded,
                             "RenderScene Mesh3D batch capacity was exceeded");
        }

        const RenderMesh3DItem& item = m_meshes3D[firstItem];
        std::construct_at(&m_mesh3DBatches[m_mesh3DBatchCount], RenderMesh3DBatch{
            .firstItem = firstItem,
            .itemCount = nextItem - firstItem,
            .mesh = item.mesh,
            .material = item.material,
            .submeshIndex = item.submeshIndex,
            .doubleSided = item.doubleSided,
        });
        ++m_mesh3DBatchCount;
        firstItem = nextItem;
    }
    m_candidateStatistics.mesh3DBatchCount = m_mesh3DBatchCount;
    return Core::success();
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
    if (m_mesh3DCount != 0 && !m_perspectiveCamera.has_value())
    {
        Core::Status status = failBuild(RenderErrorCode::RenderSceneMissingCamera,
                                        "World meshes require exactly one active PerspectiveCamera");
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

    if (m_perspectiveCamera.has_value())
    {
        usize writeIndex = 0;
        for (usize readIndex = 0; readIndex < m_mesh3DCount; ++readIndex)
        {
            RenderMesh3DItem candidate = m_meshes3D[readIndex];
            float cameraDepth = 0.0F;
            if (!intersectsPerspectiveCamera(candidate, *m_perspectiveCamera, cameraDepth))
            {
                ++m_candidateStatistics.culledMesh3DCount;
                continue;
            }
            candidate.cameraDepth = cameraDepth;
            candidate.depthBucket = depthBucket(cameraDepth, *m_perspectiveCamera);
            if (writeIndex != readIndex)
            {
                m_meshes3D[writeIndex] = candidate;
            }
            else
            {
                m_meshes3D[readIndex] = candidate;
            }
            ++writeIndex;
        }
        if (writeIndex < m_mesh3DCount)
        {
            std::destroy_n(m_meshes3D + writeIndex, m_mesh3DCount - writeIndex);
            m_mesh3DCount = static_cast<u32>(writeIndex);
        }
    }

    std::sort(m_meshes3D, m_meshes3D + m_mesh3DCount, [](const RenderMesh3DItem& left,
                                                         const RenderMesh3DItem& right) noexcept {
        if (left.material != right.material)
        {
            return left.material < right.material;
        }
        if (left.mesh != right.mesh)
        {
            return left.mesh < right.mesh;
        }
        if (left.submeshIndex != right.submeshIndex)
        {
            return left.submeshIndex < right.submeshIndex;
        }
        if (left.doubleSided != right.doubleSided)
        {
            return left.doubleSided < right.doubleSided;
        }
        if (left.depthBucket != right.depthBucket)
        {
            return left.depthBucket < right.depthBucket;
        }
        if (left.stableEntityKey != right.stableEntityKey)
        {
            return left.stableEntityKey < right.stableEntityKey;
        }
        return left.insertionOrder < right.insertionOrder;
    });

    if (auto batchStatus = finalizeMesh3DBatches(); !batchStatus)
    {
        Core::Error error = std::move(batchStatus.error());
        rollbackBuilding();
        return Core::failure(std::move(error));
    }

    m_candidateStatistics.visibleSpriteCount = m_spriteCount;
    u64 spriteChecksum = FnvOffset;
    for (const RenderSprite2DItem& sprite : std::span<const RenderSprite2DItem>{m_sprites, m_spriteCount})
    {
        hashUnsigned(spriteChecksum, sprite.sortingLayer);
        hashUnsigned(spriteChecksum, sprite.orderInLayer);
        hashUnsigned(spriteChecksum, sprite.stableEntityKey);
        hashUnsigned(spriteChecksum, sprite.insertionOrder);
    }
    m_candidateStatistics.sortOrderChecksum = spriteChecksum;

    m_candidateStatistics.visibleMesh3DCount = m_mesh3DCount;
    u64 meshChecksum = FnvOffset;
    for (const RenderMesh3DItem& mesh : std::span<const RenderMesh3DItem>{m_meshes3D, m_mesh3DCount})
    {
        hashUnsigned(meshChecksum, mesh.material.index());
        hashUnsigned(meshChecksum, mesh.mesh.index());
        hashUnsigned(meshChecksum, mesh.submeshIndex);
        hashUnsigned(meshChecksum, mesh.depthBucket);
        hashUnsigned(meshChecksum, mesh.stableEntityKey);
        hashUnsigned(meshChecksum, mesh.insertionOrder);
    }
    m_candidateStatistics.mesh3DSortOrderChecksum = meshChecksum;

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
    if (m_mesh3DBatchCount != 0)
    {
        std::destroy_n(m_mesh3DBatches, m_mesh3DBatchCount);
        m_mesh3DBatchCount = 0;
    }
    if (m_mesh3DCount != 0)
    {
        std::destroy_n(m_meshes3D, m_mesh3DCount);
        m_mesh3DCount = 0;
    }
    if (m_spriteCount != 0)
    {
        std::destroy_n(m_sprites, m_spriteCount);
        m_spriteCount = 0;
    }
    m_camera.reset();
    m_perspectiveCamera.reset();
    m_frameParameters = {};
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
    m_storage->deallocate(m_mesh3DBatches,
                          sizeof(RenderMesh3DBatch) * static_cast<usize>(m_capacity.mesh3DBatchCapacity),
                          alignof(RenderMesh3DBatch));
    m_storage->deallocate(m_meshes3D,
                          sizeof(RenderMesh3DItem) * static_cast<usize>(m_capacity.mesh3DItemCapacity),
                          alignof(RenderMesh3DItem));
    m_storage->deallocate(m_sprites,
                          sizeof(RenderSprite2DItem) * static_cast<usize>(m_capacity.spriteCapacity),
                          alignof(RenderSprite2DItem));
    m_storage = nullptr;
    m_sprites = nullptr;
    m_meshes3D = nullptr;
    m_mesh3DBatches = nullptr;
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
    return RenderSceneView{
        m_camera,
        std::span<const RenderSprite2DItem>{m_sprites, m_spriteCount},
        m_perspectiveCamera,
        std::span<const RenderMesh3DItem>{m_meshes3D, m_mesh3DCount},
        std::span<const RenderMesh3DBatch>{m_mesh3DBatches, m_mesh3DBatchCount},
        m_publishedStatistics,
    };
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
