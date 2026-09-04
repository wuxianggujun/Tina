#include <tina/render/RenderScene.hpp>

#include <tina/math/Mat4.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>
#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Tina::Render {
namespace {

inline constexpr float Pi = 3.14159265358979323846F;
inline constexpr u64 FnvOffset = 14695981039346656037ULL;
inline constexpr u64 FnvPrime = 1099511628211ULL;

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

// Math::normalized reports failure by returning the zero quaternion; this boundary
// turns that into the structured Error the writer contract requires. The two
// rejection cases stay distinct because their messages name different causes.
[[nodiscard]] Core::Result<Math::Quaternion> normalizedQuaternion(const RenderPose3DInput& pose)
{
    const Math::Quaternion authored{
        pose.rotationX, pose.rotationY, pose.rotationZ, pose.rotationW};
    const double lengthSquared = static_cast<double>(authored.x) * authored.x +
                                 static_cast<double>(authored.y) * authored.y +
                                 static_cast<double>(authored.z) * authored.z +
                                 static_cast<double>(authored.w) * authored.w;
    if (!std::isfinite(lengthSquared) || lengthSquared <= Math::MinimumNormalizableLengthSquared)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene 3D rotation quaternion is invalid");
    }
    const Math::Quaternion normalized = Math::normalized(authored);
    if (!Math::isFinite(normalized))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene 3D rotation normalization overflowed");
    }
    return normalized;
}

// Column-major, and handed to the backend unchanged. Math::fromTrs produces the
// same element order this function built by hand before Tina::Math existed.
[[nodiscard]] std::array<float, 16> makeColumnMajorWorldTransform(
    const RenderTransform3DInput& transform,
    Math::Quaternion rotation) noexcept
{
    return Math::fromTrs(
               Math::Vec3{transform.pose.positionX, transform.pose.positionY,
                   transform.pose.positionZ},
               rotation,
               Math::Vec3{transform.scaleX, transform.scaleY, transform.scaleZ})
        .columns;
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
           color.blue >= 0.0F && color.blue <= 1.0F && color.alpha >= 0.0F && color.alpha <= 1.0F;
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
           left.submeshIndex == right.submeshIndex && left.alphaMode == right.alphaMode &&
           left.doubleSided == right.doubleSided && left.shader == right.shader &&
           left.shaderUniforms == right.shaderUniforms;
}

} // namespace

Core::Status validateRenderSceneCapacity(const RenderSceneCapacity& capacity) noexcept
{
    if (capacity.spriteCapacity == 0 || capacity.spriteCapacity > RenderSceneCapacity::MaximumSpriteCapacity ||
        capacity.mesh3DItemCapacity == 0 ||
        capacity.mesh3DItemCapacity > RenderSceneCapacity::MaximumMesh3DItemCapacity ||
        capacity.mesh3DBatchCapacity == 0 ||
        capacity.mesh3DBatchCapacity > RenderSceneCapacity::MaximumMesh3DBatchCapacity ||
        capacity.skinnedMesh3DItemCapacity == 0 ||
        capacity.skinnedMesh3DItemCapacity > RenderSceneCapacity::MaximumSkinnedMesh3DItemCapacity ||
        capacity.transparent3DDrawCapacity == 0 ||
        capacity.transparent3DDrawCapacity > RenderSceneCapacity::MaximumTransparent3DDrawCapacity ||
        capacity.skinnedMesh3DPaletteJointCapacity < MaxSkinnedMesh3DPaletteJointCount ||
        capacity.skinnedMesh3DPaletteJointCapacity >
            RenderSceneCapacity::MaximumSkinnedMesh3DPaletteJointCapacity)
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
    usize skinnedMesh3DBytes = 0;
    usize transparent3DDrawBytes = 0;
    usize skinnedPaletteFloatCount = 0;
    usize skinnedPaletteBytes = 0;
    if (!checkedStorageBytes<RenderSprite2DItem>(capacity.spriteCapacity, spriteBytes) ||
        !checkedStorageBytes<RenderMesh3DItem>(capacity.mesh3DItemCapacity, mesh3DBytes) ||
        !checkedStorageBytes<RenderMesh3DBatch>(capacity.mesh3DBatchCapacity, mesh3DBatchBytes) ||
        !checkedStorageBytes<RenderSkinnedMesh3DItem>(capacity.skinnedMesh3DItemCapacity, skinnedMesh3DBytes) ||
        !checkedStorageBytes<RenderTransparent3DDraw>(capacity.transparent3DDrawCapacity,
                                                     transparent3DDrawBytes))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneCapacity,
                             "RenderScene capacity exceeds addressable storage");
    }
    skinnedPaletteFloatCount = static_cast<usize>(capacity.skinnedMesh3DPaletteJointCapacity) *
                               SkinnedMesh3DPaletteFloatsPerJoint;
    if (skinnedPaletteFloatCount > (std::numeric_limits<usize>::max)() / sizeof(float))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneCapacity,
                             "RenderScene capacity exceeds addressable storage");
    }
    skinnedPaletteBytes = skinnedPaletteFloatCount * sizeof(float);

    RenderSprite2DItem* sprites = nullptr;
    RenderMesh3DItem* meshes3D = nullptr;
    RenderMesh3DBatch* mesh3DBatches = nullptr;
    RenderSkinnedMesh3DItem* skinnedMeshes3D = nullptr;
    RenderTransparent3DDraw* transparent3DDraws = nullptr;
    float* skinnedMesh3DPalette = nullptr;
    try
    {
        sprites = static_cast<RenderSprite2DItem*>(storage.allocate(spriteBytes, alignof(RenderSprite2DItem)));
        meshes3D = static_cast<RenderMesh3DItem*>(storage.allocate(mesh3DBytes, alignof(RenderMesh3DItem)));
        mesh3DBatches =
            static_cast<RenderMesh3DBatch*>(storage.allocate(mesh3DBatchBytes, alignof(RenderMesh3DBatch)));
        skinnedMeshes3D = static_cast<RenderSkinnedMesh3DItem*>(
            storage.allocate(skinnedMesh3DBytes, alignof(RenderSkinnedMesh3DItem)));
        transparent3DDraws = static_cast<RenderTransparent3DDraw*>(
            storage.allocate(transparent3DDrawBytes, alignof(RenderTransparent3DDraw)));
        skinnedMesh3DPalette = static_cast<float*>(storage.allocate(skinnedPaletteBytes, alignof(float)));
    }
    catch (const std::bad_alloc&)
    {
        if (transparent3DDraws != nullptr)
        {
            storage.deallocate(transparent3DDraws, transparent3DDrawBytes,
                               alignof(RenderTransparent3DDraw));
        }
        if (skinnedMeshes3D != nullptr)
        {
            storage.deallocate(skinnedMeshes3D, skinnedMesh3DBytes, alignof(RenderSkinnedMesh3DItem));
        }
        if (mesh3DBatches != nullptr)
        {
            storage.deallocate(mesh3DBatches, mesh3DBatchBytes, alignof(RenderMesh3DBatch));
        }
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
    return RenderSceneBuilder{capacity, storage, sprites, meshes3D, mesh3DBatches,
                              skinnedMeshes3D, transparent3DDraws, skinnedMesh3DPalette};
}

RenderSceneBuilder::RenderSceneBuilder(RenderSceneCapacity capacity, std::pmr::memory_resource& storage,
                                       RenderSprite2DItem* sprites, RenderMesh3DItem* meshes3D,
                                       RenderMesh3DBatch* mesh3DBatches,
                                       RenderSkinnedMesh3DItem* skinnedMeshes3D,
                                       RenderTransparent3DDraw* transparent3DDraws,
                                       float* skinnedMesh3DPalette) noexcept
    : m_capacity(capacity), m_storage(&storage), m_sprites(sprites), m_meshes3D(meshes3D),
      m_mesh3DBatches(mesh3DBatches), m_skinnedMeshes3D(skinnedMeshes3D),
      m_transparent3DDraws(transparent3DDraws),
      m_skinnedMesh3DPalette(skinnedMesh3DPalette)
{
}

RenderSceneBuilder::RenderSceneBuilder(RenderSceneBuilder&& other) noexcept
    : m_capacity(other.m_capacity), m_storage(std::exchange(other.m_storage, nullptr)),
      m_sprites(std::exchange(other.m_sprites, nullptr)), m_meshes3D(std::exchange(other.m_meshes3D, nullptr)),
      m_mesh3DBatches(std::exchange(other.m_mesh3DBatches, nullptr)),
      m_skinnedMeshes3D(std::exchange(other.m_skinnedMeshes3D, nullptr)),
      m_transparent3DDraws(std::exchange(other.m_transparent3DDraws, nullptr)),
      m_skinnedMesh3DPalette(std::exchange(other.m_skinnedMesh3DPalette, nullptr)),
      m_spriteCount(std::exchange(other.m_spriteCount, 0)), m_mesh3DCount(std::exchange(other.m_mesh3DCount, 0)),
      m_mesh3DBatchCount(std::exchange(other.m_mesh3DBatchCount, 0)),
      m_skinnedMesh3DCount(std::exchange(other.m_skinnedMesh3DCount, 0)),
      m_opaqueMesh3DCount(std::exchange(other.m_opaqueMesh3DCount, 0)),
      m_opaqueSkinnedMesh3DCount(std::exchange(other.m_opaqueSkinnedMesh3DCount, 0)),
      m_transparent3DDrawCount(std::exchange(other.m_transparent3DDrawCount, 0)),
      m_skinnedMesh3DPaletteJointCount(std::exchange(other.m_skinnedMesh3DPaletteJointCount, 0)),
      m_camera(std::move(other.m_camera)),
      m_sprite2DLighting(std::move(other.m_sprite2DLighting)),
      m_perspectiveCamera(std::move(other.m_perspectiveCamera)),
      m_mesh3DLighting(std::move(other.m_mesh3DLighting)), m_clearColor(other.m_clearColor),
      m_frameParameters(other.m_frameParameters),
      m_candidateStatistics(other.m_candidateStatistics), m_publishedStatistics(other.m_publishedStatistics),
      m_statistics(other.m_statistics), m_stickyBuildError(std::move(other.m_stickyBuildError)), m_state(other.m_state)
{
    other.m_capacity = {};
    other.m_camera.reset();
    other.m_sprite2DLighting.reset();
    other.m_perspectiveCamera.reset();
    other.m_mesh3DLighting.reset();
    other.m_clearColor = DefaultSceneClearColor;
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

Core::Status RenderSceneWriter::setSprite2DLighting(const Sprite2DLightingDesc& lighting)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->setSprite2DLighting(lighting);
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

Core::Status RenderSceneWriter::addSkinnedMesh3D(const RenderSkinnedMesh3DInput& mesh)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->addSkinnedMesh3D(mesh);
}

Core::Status RenderSceneWriter::setMesh3DLighting(const Mesh3DLightingDesc& lighting)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->setMesh3DLighting(lighting);
}

Core::Status RenderSceneWriter::setClearColor(const RenderLinearColor& color)
{
    if (m_builder == nullptr)
    {
        return Core::failure(RenderErrorCode::RenderSceneBuildNotOpen,
                             "The RenderScene writer is no longer attached to a builder");
    }
    return m_builder->setClearColor(color);
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
        bounds.radius <= 0.0F || !finiteColor(mesh.baseColorFactor) ||
        !isSupportedMesh3DAlphaMode(mesh.alphaMode))
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
        .normalTexture = sprite.normalTexture,
        .shader = sprite.shader,
        .shaderUniforms = sprite.shaderUniforms,
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
    const Math::Quaternion rotation = *rotationResult;
    const Math::Vec3 forward = Math::rotate(rotation, Math::Vec3{0.0F, 0.0F, -1.0F});
    const Math::Vec3 up = Math::rotate(rotation, Math::Vec3{0.0F, 1.0F, 0.0F});
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
    const Math::Quaternion rotation = *rotationResult;
    const Math::Vec3 scaledLocalCenter =
        Math::Vec3{mesh.localBounds.centerX, mesh.localBounds.centerY, mesh.localBounds.centerZ} *
        Math::Vec3{mesh.worldTransform.scaleX, mesh.worldTransform.scaleY, mesh.worldTransform.scaleZ};
    const Math::Vec3 worldCenter =
        Math::Vec3{mesh.worldTransform.pose.positionX, mesh.worldTransform.pose.positionY,
            mesh.worldTransform.pose.positionZ} +
        Math::rotate(rotation, scaledLocalCenter);
    const float worldRadius = mesh.localBounds.radius *
                              std::max({mesh.worldTransform.scaleX, mesh.worldTransform.scaleY,
                                        mesh.worldTransform.scaleZ});
    const std::array<float, 16> worldTransform = makeColumnMajorWorldTransform(mesh.worldTransform, rotation);
    if (!Math::isFinite(worldCenter) || !finite(worldRadius) || worldRadius <= 0.0F ||
        !std::ranges::all_of(worldTransform, [](float value) noexcept { return finite(value); }))
    {
        return failBuild(RenderErrorCode::InvalidRenderSceneInput,
                         "RenderScene Mesh3D world transform or bounds overflowed");
    }

    std::construct_at(&m_meshes3D[m_mesh3DCount], RenderMesh3DItem{
        .mesh = mesh.mesh,
        .material = mesh.material,
        .shader = mesh.shader,
        .shaderUniforms = mesh.shaderUniforms,
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
        .alphaMode = mesh.alphaMode,
        .doubleSided = mesh.doubleSided,
    });
    ++m_mesh3DCount;
    return Core::success();
}

Core::Status RenderSceneBuilder::validateSkinnedMesh3D(const RenderSkinnedMesh3DInput& mesh) const noexcept
{
    const RenderPose3DInput& pose = mesh.worldTransform.pose;
    const RenderBoundingSphereInput& bounds = mesh.localBounds;
    const usize paletteFloatCount = mesh.paletteColumnMajorJointMatrices.size();
    if (!mesh.mesh.hasValue() || !mesh.material.hasValue() || mesh.stableEntityKey == 0 ||
        !finite(pose.positionX) || !finite(pose.positionY) || !finite(pose.positionZ) ||
        !finite(pose.rotationX) || !finite(pose.rotationY) || !finite(pose.rotationZ) ||
        !finite(pose.rotationW) || !finite(mesh.worldTransform.scaleX) ||
        !finite(mesh.worldTransform.scaleY) || !finite(mesh.worldTransform.scaleZ) ||
        mesh.worldTransform.scaleX <= 0.0F || mesh.worldTransform.scaleY <= 0.0F ||
        mesh.worldTransform.scaleZ <= 0.0F || !finite(bounds.centerX) || !finite(bounds.centerY) ||
        !finite(bounds.centerZ) || !finite(bounds.radius) || bounds.radius <= 0.0F ||
        !finiteColor(mesh.baseColorFactor) || !isSupportedMesh3DAlphaMode(mesh.alphaMode))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene SkinnedMesh3D contains invalid geometry, transform, material, or "
                             "resource values");
    }
    if (paletteFloatCount == 0 || (paletteFloatCount % SkinnedMesh3DPaletteFloatsPerJoint) != 0 ||
        paletteFloatCount / SkinnedMesh3DPaletteFloatsPerJoint > MaxSkinnedMesh3DPaletteJointCount)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "RenderScene SkinnedMesh3D palette must contain 1..256 column-major joint matrices");
    }
    for (const float value : mesh.paletteColumnMajorJointMatrices)
    {
        if (!finite(value))
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "RenderScene SkinnedMesh3D palette values must be finite");
        }
    }
    if (auto rotation = normalizedQuaternion(pose); !rotation)
    {
        return Core::failure(std::move(rotation.error()));
    }
    return Core::success();
}

Core::Status RenderSceneBuilder::addSkinnedMesh3D(const RenderSkinnedMesh3DInput& mesh)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before adding a skinned 3D mesh");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (auto status = validateSkinnedMesh3D(mesh); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    ++m_candidateStatistics.submittedSkinnedMesh3DCount;
    if (!mesh.visible)
    {
        ++m_candidateStatistics.prunedInvisibleSkinnedMesh3DCount;
        return Core::success();
    }
    if (m_skinnedMesh3DCount >= m_capacity.skinnedMesh3DItemCapacity)
    {
        return failBuild(RenderErrorCode::RenderSceneCapacityExceeded,
                         "RenderScene SkinnedMesh3D item capacity was exceeded");
    }
    const u32 paletteJointCount = static_cast<u32>(
        mesh.paletteColumnMajorJointMatrices.size() / SkinnedMesh3DPaletteFloatsPerJoint);
    if (paletteJointCount > m_capacity.skinnedMesh3DPaletteJointCapacity - m_skinnedMesh3DPaletteJointCount)
    {
        return failBuild(RenderErrorCode::RenderSceneCapacityExceeded,
                         "RenderScene SkinnedMesh3D palette pool capacity was exceeded");
    }

    auto rotationResult = normalizedQuaternion(mesh.worldTransform.pose);
    if (!rotationResult)
    {
        return failBuild(rotationResult.error().code, rotationResult.error().message.c_str());
    }
    const Math::Quaternion rotation = *rotationResult;
    const Math::Vec3 scaledLocalCenter =
        Math::Vec3{mesh.localBounds.centerX, mesh.localBounds.centerY, mesh.localBounds.centerZ} *
        Math::Vec3{mesh.worldTransform.scaleX, mesh.worldTransform.scaleY, mesh.worldTransform.scaleZ};
    const Math::Vec3 worldCenter =
        Math::Vec3{mesh.worldTransform.pose.positionX, mesh.worldTransform.pose.positionY,
            mesh.worldTransform.pose.positionZ} +
        Math::rotate(rotation, scaledLocalCenter);
    const float worldRadius = mesh.localBounds.radius *
                              std::max({mesh.worldTransform.scaleX, mesh.worldTransform.scaleY,
                                        mesh.worldTransform.scaleZ});
    const std::array<float, 16> worldTransform = makeColumnMajorWorldTransform(mesh.worldTransform, rotation);
    if (!Math::isFinite(worldCenter) || !finite(worldRadius) || worldRadius <= 0.0F ||
        !std::ranges::all_of(worldTransform, [](float value) noexcept { return finite(value); }))
    {
        return failBuild(RenderErrorCode::InvalidRenderSceneInput,
                         "RenderScene SkinnedMesh3D world transform or bounds overflowed");
    }

    const u32 paletteJointOffset = m_skinnedMesh3DPaletteJointCount;
    std::ranges::copy(mesh.paletteColumnMajorJointMatrices,
                      m_skinnedMesh3DPalette +
                          static_cast<usize>(paletteJointOffset) * SkinnedMesh3DPaletteFloatsPerJoint);
    m_skinnedMesh3DPaletteJointCount += paletteJointCount;

    std::construct_at(&m_skinnedMeshes3D[m_skinnedMesh3DCount], RenderSkinnedMesh3DItem{
        .mesh = mesh.mesh,
        .material = mesh.material,
        .shader = mesh.shader,
        .shaderUniforms = mesh.shaderUniforms,
        .submeshIndex = mesh.submeshIndex,
        .stableEntityKey = mesh.stableEntityKey,
        .insertionOrder = m_skinnedMesh3DCount,
        .depthBucket = 0,
        .cameraDepth = 0.0F,
        .worldBoundsCenterX = worldCenter.x,
        .worldBoundsCenterY = worldCenter.y,
        .worldBoundsCenterZ = worldCenter.z,
        .worldBoundsRadius = worldRadius,
        .columnMajorWorldTransform = worldTransform,
        .baseColorFactor = mesh.baseColorFactor,
        .paletteJointOffset = paletteJointOffset,
        .paletteJointCount = paletteJointCount,
        .alphaMode = mesh.alphaMode,
        .doubleSided = mesh.doubleSided,
    });
    ++m_skinnedMesh3DCount;
    return Core::success();
}

Core::Status RenderSceneBuilder::setMesh3DLighting(const Mesh3DLightingDesc& lighting)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before setting 3D lighting");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (m_mesh3DLighting.has_value())
    {
        return failBuild(RenderErrorCode::RenderSceneLightingConflict,
                         "A RenderScene may contain only one Mesh3D lighting snapshot");
    }
    if (auto status = validateMesh3DLightingDesc(lighting); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    RenderMesh3DLighting snapshot;
    snapshot.m_directionalLightCount = static_cast<u32>(lighting.directionalLights.size());
    snapshot.m_pointLightCount = static_cast<u32>(lighting.pointLights.size());
    snapshot.m_spotLightCount = static_cast<u32>(lighting.spotLights.size());
    snapshot.m_cascadedDirectionalShadow = lighting.cascadedDirectionalShadow;
    snapshot.m_pointLightShadow = lighting.pointLightShadow;
    snapshot.m_spotLightShadow = lighting.spotLightShadow;
    snapshot.m_ambientScale = lighting.ambientScale;
    std::ranges::copy(lighting.directionalLights, snapshot.m_directionalLights.begin());
    std::ranges::copy(lighting.pointLights, snapshot.m_pointLights.begin());
    std::ranges::copy(lighting.spotLights, snapshot.m_spotLights.begin());
    m_mesh3DLighting = snapshot;
    m_candidateStatistics.mesh3DLightingConfigured = true;
    m_candidateStatistics.directionalLightCount = snapshot.m_directionalLightCount;
    m_candidateStatistics.pointLight3DCount = snapshot.m_pointLightCount;
    m_candidateStatistics.spotLight3DCount = snapshot.m_spotLightCount;
    return Core::success();
}

Core::Status RenderSceneBuilder::setClearColor(const RenderLinearColor& color)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before setting the clear color");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (m_candidateStatistics.clearColorConfigured)
    {
        return failBuild(RenderErrorCode::InvalidSceneClearColor,
                         "A RenderScene may contain only one clear color");
    }
    // Linear radiance, so values above 1 are meaningful for lights but not for a
    // background: the surface cannot display more than full white, and letting an
    // over-range value through would silently clamp in the backend instead of here.
    if (!finite(color.red) || !finite(color.green) || !finite(color.blue) || !finite(color.alpha) ||
        color.red < 0.0F || color.green < 0.0F || color.blue < 0.0F || color.alpha < 0.0F ||
        color.red > 1.0F || color.green > 1.0F || color.blue > 1.0F || color.alpha > 1.0F)
    {
        return failBuild(RenderErrorCode::InvalidSceneClearColor,
                         "RenderScene clear color components must be finite and within [0, 1]");
    }

    m_clearColor = color;
    m_candidateStatistics.clearColorConfigured = true;
    return Core::success();
}

Core::Status RenderSceneBuilder::setSprite2DLighting(const Sprite2DLightingDesc& lighting)
{
    if (m_state != State::Building)
    {
        return buildStateFailure(RenderErrorCode::RenderSceneBuildNotOpen,
                                 "A RenderScene build must be open before setting 2D lighting");
    }
    if (m_stickyBuildError.has_value())
    {
        return Core::failure(Core::Error{m_stickyBuildError->code, m_stickyBuildError->message,
                                         m_stickyBuildError->origin});
    }
    if (m_sprite2DLighting.has_value())
    {
        return failBuild(RenderErrorCode::RenderSceneLightingConflict,
                         "A RenderScene may contain only one Sprite2D lighting snapshot");
    }
    if (auto status = validateSprite2DLightingDesc(lighting); !status)
    {
        return failBuild(status.error().code, status.error().message.c_str());
    }

    RenderSprite2DLighting snapshot;
    snapshot.m_pointLightCount = static_cast<u32>(lighting.pointLights.size());
    snapshot.m_shadowSegmentCount = static_cast<u32>(lighting.shadowSegments.size());
    snapshot.m_ambientScale = lighting.ambientScale;
    std::ranges::copy(lighting.pointLights, snapshot.m_pointLights.begin());
    std::ranges::copy(lighting.shadowSegments, snapshot.m_shadowSegments.begin());
    m_sprite2DLighting = snapshot;
    m_candidateStatistics.sprite2DLightingConfigured = true;
    m_candidateStatistics.pointLight2DCount = snapshot.m_pointLightCount;
    m_candidateStatistics.shadowOccluder2DCount = snapshot.m_shadowSegmentCount;
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

namespace {

// Deliberately NOT Math::sphereIntersectsPerspectiveFrustum, for two reasons.
//
// It also returns the view depth, which the caller needs for depth bucketing and
// transparent sorting — the shared helper only answers the visibility question.
//
// And it accumulates in float where the shared helper uses double. Switching this
// one to double would reclassify spheres sitting within a float epsilon of a
// frustum plane, changing published draw counts; that is a re-baselining exercise,
// not a refactor. The two must stay as they are until it is done deliberately.
[[nodiscard]] bool sphereIntersectsPerspectiveCamera(Math::Vec3 worldCenter, float radius,
                                                     const RenderPerspectiveCamera& camera,
                                                     float& cameraDepth) noexcept
{
    const Math::Vec3 cameraPosition{camera.positionX, camera.positionY, camera.positionZ};
    const Math::Vec3 forward{camera.forwardX, camera.forwardY, camera.forwardZ};
    const Math::Vec3 up{camera.upX, camera.upY, camera.upZ};
    const Math::Vec3 right = Math::cross(forward, up);
    const Math::Vec3 relative = worldCenter - cameraPosition;
    const float x = Math::dot(relative, right);
    const float y = Math::dot(relative, up);
    const float depth = Math::dot(relative, forward);
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

} // namespace

bool RenderSceneBuilder::intersectsPerspectiveCamera(const RenderMesh3DItem& mesh,
                                                     const RenderPerspectiveCamera& camera,
                                                     float& cameraDepth) const noexcept
{
    return sphereIntersectsPerspectiveCamera(
        {mesh.worldBoundsCenterX, mesh.worldBoundsCenterY, mesh.worldBoundsCenterZ},
        mesh.worldBoundsRadius, camera, cameraDepth);
}

Core::Status RenderSceneBuilder::finalizeMesh3DBatches()
{
    if (m_opaqueMesh3DCount == 0)
    {
        return Core::success();
    }

    u32 firstItem = 0;
    while (firstItem < m_opaqueMesh3DCount)
    {
        u32 nextItem = firstItem + 1;
        while (nextItem < m_opaqueMesh3DCount &&
               sameMeshBatch(m_meshes3D[firstItem], m_meshes3D[nextItem]))
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
            .shader = item.shader,
            .shaderUniforms = item.shaderUniforms,
            .submeshIndex = item.submeshIndex,
            .doubleSided = item.doubleSided,
        });
        ++m_mesh3DBatchCount;
        firstItem = nextItem;
    }
    m_candidateStatistics.mesh3DBatchCount = m_mesh3DBatchCount;
    return Core::success();
}

Core::Status RenderSceneBuilder::finalizeTransparent3DDraws()
{
    const u32 transparentStaticCount = m_mesh3DCount - m_opaqueMesh3DCount;
    const u32 transparentSkinnedCount = m_skinnedMesh3DCount - m_opaqueSkinnedMesh3DCount;
    const u64 transparentCount = static_cast<u64>(transparentStaticCount) + transparentSkinnedCount;
    if (transparentCount > m_capacity.transparent3DDrawCapacity)
    {
        return failBuild(RenderErrorCode::RenderSceneCapacityExceeded,
                         "RenderScene Transparent3D draw capacity was exceeded");
    }
    if (transparentCount == 0)
    {
        return Core::success();
    }
    if (!m_perspectiveCamera.has_value())
    {
        return failBuild(RenderErrorCode::RenderSceneMissingCamera,
                         "Transparent3D draws require an active PerspectiveCamera");
    }

    const RenderPerspectiveCamera& camera = *m_perspectiveCamera;
    const auto distanceSquared = [&camera](float x, float y, float z) noexcept {
        const double deltaX = static_cast<double>(x) - camera.positionX;
        const double deltaY = static_cast<double>(y) - camera.positionY;
        const double deltaZ = static_cast<double>(z) - camera.positionZ;
        return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
    };
    const auto append = [&](RenderTransparent3DDrawKind kind, u32 itemIndex,
                            u64 stableEntityKey, double itemDistanceSquared) {
        std::construct_at(&m_transparent3DDraws[m_transparent3DDrawCount],
                          RenderTransparent3DDraw{
                              .kind = kind,
                              .itemIndex = itemIndex,
                              .stableEntityKey = stableEntityKey,
                              .cameraDistanceSquared = itemDistanceSquared,
                          });
        ++m_transparent3DDrawCount;
    };

    for (u32 index = m_opaqueMesh3DCount; index < m_mesh3DCount; ++index)
    {
        const RenderMesh3DItem& item = m_meshes3D[index];
        append(RenderTransparent3DDrawKind::StaticMesh, index, item.stableEntityKey,
               distanceSquared(item.worldBoundsCenterX, item.worldBoundsCenterY,
                               item.worldBoundsCenterZ));
    }
    for (u32 index = m_opaqueSkinnedMesh3DCount; index < m_skinnedMesh3DCount; ++index)
    {
        const RenderSkinnedMesh3DItem& item = m_skinnedMeshes3D[index];
        append(RenderTransparent3DDrawKind::SkinnedMesh, index, item.stableEntityKey,
               distanceSquared(item.worldBoundsCenterX, item.worldBoundsCenterY,
                               item.worldBoundsCenterZ));
    }

    std::sort(m_transparent3DDraws, m_transparent3DDraws + m_transparent3DDrawCount,
              [](const RenderTransparent3DDraw& left,
                 const RenderTransparent3DDraw& right) noexcept {
        if (left.cameraDistanceSquared != right.cameraDistanceSquared)
        {
            return left.cameraDistanceSquared > right.cameraDistanceSquared;
        }
        if (left.stableEntityKey != right.stableEntityKey)
        {
            return left.stableEntityKey < right.stableEntityKey;
        }
        if (left.kind != right.kind)
        {
            return left.kind < right.kind;
        }
        return left.itemIndex < right.itemIndex;
    });

    m_candidateStatistics.transparentMesh3DCount = transparentStaticCount;
    m_candidateStatistics.transparentSkinnedMesh3DCount = transparentSkinnedCount;
    m_candidateStatistics.transparent3DDrawCount = m_transparent3DDrawCount;
    u64 checksum = FnvOffset;
    for (const RenderTransparent3DDraw& draw :
         std::span<const RenderTransparent3DDraw>{m_transparent3DDraws,
                                                  m_transparent3DDrawCount})
    {
        hashUnsigned(checksum, static_cast<u8>(draw.kind));
        hashUnsigned(checksum, draw.stableEntityKey);
        hashUnsigned(checksum, draw.itemIndex);
    }
    m_candidateStatistics.transparent3DSortOrderChecksum = checksum;
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
    if ((m_mesh3DCount != 0 || m_skinnedMesh3DCount != 0) && !m_perspectiveCamera.has_value())
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
        if (left.alphaMode != right.alphaMode)
        {
            return left.alphaMode < right.alphaMode;
        }
        if (left.alphaMode == Mesh3DAlphaMode::Blend)
        {
            if (left.stableEntityKey != right.stableEntityKey)
            {
                return left.stableEntityKey < right.stableEntityKey;
            }
            return left.insertionOrder < right.insertionOrder;
        }
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

    m_opaqueMesh3DCount = static_cast<u32>(std::distance(
        m_meshes3D,
        std::find_if(m_meshes3D, m_meshes3D + m_mesh3DCount,
                     [](const RenderMesh3DItem& item) noexcept {
                         return item.alphaMode != Mesh3DAlphaMode::Opaque;
                     })));
    m_candidateStatistics.opaqueMesh3DCount = m_opaqueMesh3DCount;
    m_candidateStatistics.transparentMesh3DCount = m_mesh3DCount - m_opaqueMesh3DCount;

    if (auto batchStatus = finalizeMesh3DBatches(); !batchStatus)
    {
        Core::Error error = std::move(batchStatus.error());
        rollbackBuilding();
        return Core::failure(std::move(error));
    }

    if (m_perspectiveCamera.has_value())
    {
        // Palette ranges are not compacted: culled items simply leave their
        // pool slots unused for the remainder of this frame.
        usize writeIndex = 0;
        for (usize readIndex = 0; readIndex < m_skinnedMesh3DCount; ++readIndex)
        {
            RenderSkinnedMesh3DItem candidate = m_skinnedMeshes3D[readIndex];
            float cameraDepth = 0.0F;
            if (!sphereIntersectsPerspectiveCamera(
                    {candidate.worldBoundsCenterX, candidate.worldBoundsCenterY, candidate.worldBoundsCenterZ},
                    candidate.worldBoundsRadius, *m_perspectiveCamera, cameraDepth))
            {
                ++m_candidateStatistics.culledSkinnedMesh3DCount;
                continue;
            }
            candidate.cameraDepth = cameraDepth;
            candidate.depthBucket = depthBucket(cameraDepth, *m_perspectiveCamera);
            m_skinnedMeshes3D[writeIndex] = candidate;
            ++writeIndex;
        }
        if (writeIndex < m_skinnedMesh3DCount)
        {
            std::destroy_n(m_skinnedMeshes3D + writeIndex, m_skinnedMesh3DCount - writeIndex);
            m_skinnedMesh3DCount = static_cast<u32>(writeIndex);
        }
    }

    std::sort(m_skinnedMeshes3D, m_skinnedMeshes3D + m_skinnedMesh3DCount,
              [](const RenderSkinnedMesh3DItem& left, const RenderSkinnedMesh3DItem& right) noexcept {
        if (left.alphaMode != right.alphaMode)
        {
            return left.alphaMode < right.alphaMode;
        }
        if (left.alphaMode == Mesh3DAlphaMode::Blend)
        {
            if (left.stableEntityKey != right.stableEntityKey)
            {
                return left.stableEntityKey < right.stableEntityKey;
            }
            return left.insertionOrder < right.insertionOrder;
        }
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

    m_opaqueSkinnedMesh3DCount = static_cast<u32>(std::distance(
        m_skinnedMeshes3D,
        std::find_if(m_skinnedMeshes3D, m_skinnedMeshes3D + m_skinnedMesh3DCount,
                     [](const RenderSkinnedMesh3DItem& item) noexcept {
                         return item.alphaMode != Mesh3DAlphaMode::Opaque;
                     })));
    m_candidateStatistics.opaqueSkinnedMesh3DCount = m_opaqueSkinnedMesh3DCount;
    m_candidateStatistics.transparentSkinnedMesh3DCount =
        m_skinnedMesh3DCount - m_opaqueSkinnedMesh3DCount;

    if (auto transparentStatus = finalizeTransparent3DDraws(); !transparentStatus)
    {
        Core::Error error = std::move(transparentStatus.error());
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
        hashUnsigned(meshChecksum, static_cast<u8>(mesh.alphaMode));
        hashUnsigned(meshChecksum, mesh.depthBucket);
        hashUnsigned(meshChecksum, mesh.stableEntityKey);
        hashUnsigned(meshChecksum, mesh.insertionOrder);
    }
    m_candidateStatistics.mesh3DSortOrderChecksum = meshChecksum;

    m_candidateStatistics.visibleSkinnedMesh3DCount = m_skinnedMesh3DCount;
    m_candidateStatistics.skinnedMesh3DPaletteJointCount = m_skinnedMesh3DPaletteJointCount;
    u64 skinnedChecksum = FnvOffset;
    for (const RenderSkinnedMesh3DItem& mesh :
         std::span<const RenderSkinnedMesh3DItem>{m_skinnedMeshes3D, m_skinnedMesh3DCount})
    {
        hashUnsigned(skinnedChecksum, mesh.material.index());
        hashUnsigned(skinnedChecksum, mesh.mesh.index());
        hashUnsigned(skinnedChecksum, mesh.submeshIndex);
        hashUnsigned(skinnedChecksum, static_cast<u8>(mesh.alphaMode));
        hashUnsigned(skinnedChecksum, mesh.depthBucket);
        hashUnsigned(skinnedChecksum, mesh.stableEntityKey);
        hashUnsigned(skinnedChecksum, mesh.insertionOrder);
        hashUnsigned(skinnedChecksum, mesh.paletteJointCount);
    }
    m_candidateStatistics.skinnedMesh3DSortOrderChecksum = skinnedChecksum;

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
    if (m_transparent3DDrawCount != 0)
    {
        std::destroy_n(m_transparent3DDraws, m_transparent3DDrawCount);
        m_transparent3DDrawCount = 0;
    }
    m_opaqueMesh3DCount = 0;
    m_opaqueSkinnedMesh3DCount = 0;
    if (m_skinnedMesh3DCount != 0)
    {
        std::destroy_n(m_skinnedMeshes3D, m_skinnedMesh3DCount);
        m_skinnedMesh3DCount = 0;
    }
    m_skinnedMesh3DPaletteJointCount = 0;
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
    m_sprite2DLighting.reset();
    m_perspectiveCamera.reset();
    m_mesh3DLighting.reset();
    // Back to the engine default, not to the previous frame's colour: a frame that stops
    // calling setClearColor must go back to the documented background rather than latch
    // whatever the last frame happened to ask for.
    m_clearColor = DefaultSceneClearColor;
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
    m_storage->deallocate(m_transparent3DDraws,
                          sizeof(RenderTransparent3DDraw) *
                              static_cast<usize>(m_capacity.transparent3DDrawCapacity),
                          alignof(RenderTransparent3DDraw));
    m_storage->deallocate(m_skinnedMesh3DPalette,
                          sizeof(float) * static_cast<usize>(m_capacity.skinnedMesh3DPaletteJointCapacity) *
                              SkinnedMesh3DPaletteFloatsPerJoint,
                          alignof(float));
    m_storage->deallocate(m_skinnedMeshes3D,
                          sizeof(RenderSkinnedMesh3DItem) *
                              static_cast<usize>(m_capacity.skinnedMesh3DItemCapacity),
                          alignof(RenderSkinnedMesh3DItem));
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
    m_skinnedMeshes3D = nullptr;
    m_transparent3DDraws = nullptr;
    m_skinnedMesh3DPalette = nullptr;
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
        m_sprite2DLighting,
        m_perspectiveCamera,
        std::span<const RenderMesh3DItem>{m_meshes3D, m_mesh3DCount},
        m_opaqueMesh3DCount,
        std::span<const RenderMesh3DBatch>{m_mesh3DBatches, m_mesh3DBatchCount},
        std::span<const RenderSkinnedMesh3DItem>{m_skinnedMeshes3D, m_skinnedMesh3DCount},
        m_opaqueSkinnedMesh3DCount,
        std::span<const RenderTransparent3DDraw>{m_transparent3DDraws,
                                                 m_transparent3DDrawCount},
        std::span<const float>{m_skinnedMesh3DPalette,
                               static_cast<usize>(m_skinnedMesh3DPaletteJointCount) *
                                   SkinnedMesh3DPaletteFloatsPerJoint},
        m_mesh3DLighting,
        m_clearColor,
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
