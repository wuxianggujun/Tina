#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <compare>
#include <memory_resource>
#include <optional>
#include <span>
#include <utility>

namespace Tina::Render {

struct RenderSceneCapacity final {
    static constexpr u32 DefaultSpriteCapacity = 16'384;
    static constexpr u32 MaximumSpriteCapacity = 1'048'576;
    static constexpr u32 DefaultMesh3DItemCapacity = 16'384;
    static constexpr u32 MaximumMesh3DItemCapacity = 1'048'576;
    static constexpr u32 DefaultMesh3DBatchCapacity = 4'096;
    static constexpr u32 MaximumMesh3DBatchCapacity = 262'144;

    u32 spriteCapacity = DefaultSpriteCapacity;
    u32 mesh3DItemCapacity = DefaultMesh3DItemCapacity;
    u32 mesh3DBatchCapacity = DefaultMesh3DBatchCapacity;
};

[[nodiscard]] Core::Status validateRenderSceneCapacity(const RenderSceneCapacity& capacity) noexcept;

// Runtime resolves the primary surface aspect once per frame. Camera
// components never retain Window/Surface state themselves.
struct RenderSceneFrameParameters final {
    std::optional<float> primarySurfaceAspectRatio;
};

struct RenderNormalizedViewport final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
};

enum class RenderPixelSnapPolicy : u8 {
    Disabled,
    CameraTranslation,
    CameraAndSprites,
};

// This is a resolved render input. Projection policy and surface metrics are
// resolved by the Scene/Runtime integration before it reaches Render.
struct RenderCamera2DInput final {
    u64 stableCameraKey = 0;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float rotationRadians = 0.0F;
    float worldWidth = 1.0F;
    float worldHeight = 1.0F;
    float actualPixelsPerMeter = 1.0F;
    RenderNormalizedViewport normalizedViewport{};
    RenderPixelSnapPolicy pixelSnap = RenderPixelSnapPolicy::Disabled;
};

// spriteKey is a backend-neutral bind table id (setSprite2DTextureBinding).
// UV rect defaults to full texture [0,1]; typed Sprite payload extraction may
// override it. The position is the resolved geometric center; Scene/Asset
// extraction applies any authored pivot before writing this render-facing value.
struct RenderSprite2DInput final {
    u32 spriteKey = 0;
    u64 stableEntityKey = 0;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float rotationRadians = 0.0F;
    float widthMeters = 1.0F;
    float heightMeters = 1.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;
    bool flipX = false;
    bool flipY = false;
    bool visible = true;
};

struct RenderPose3DInput final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float rotationX = 0.0F;
    float rotationY = 0.0F;
    float rotationZ = 0.0F;
    float rotationW = 1.0F;
};

struct RenderTransform3DInput final {
    RenderPose3DInput pose{};
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
};

struct RenderBoundingSphereInput final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    float radius = 1.0F;
};

struct RenderLinearColor final {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
};

// The camera pose is resolved from Scene WorldTransform. Local -Z is forward
// and local +Y is up. Runtime supplies the current surface aspect separately.
struct RenderPerspectiveCameraInput final {
    u64 stableCameraKey = 0;
    RenderPose3DInput worldPose{};
    float verticalFovDegrees = 60.0F;
    float nearPlaneMeters = 0.1F;
    float farPlaneMeters = 1000.0F;
    RenderNormalizedViewport normalizedViewport{};
};

// meshKey/materialKey are phase-local backend-neutral bind table ids produced by
// Scene extraction from weak AssetHandles; RenderScene never owns those handles.
struct RenderMesh3DInput final {
    u32 meshKey = 0;
    u32 materialKey = 0;
    u32 submeshIndex = 0;
    u64 stableEntityKey = 0;
    RenderTransform3DInput worldTransform{};
    RenderBoundingSphereInput localBounds{};
    RenderLinearColor baseColorFactor{};
    bool doubleSided = false;
    bool visible = true;
};

struct RenderCamera2D final {
    u64 stableCameraKey = 0;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float rotationRadians = 0.0F;
    float worldWidth = 1.0F;
    float worldHeight = 1.0F;
    float actualPixelsPerMeter = 1.0F;
    RenderNormalizedViewport normalizedViewport{};
    RenderPixelSnapPolicy pixelSnap = RenderPixelSnapPolicy::Disabled;
};

struct RenderSprite2DItem final {
    u32 spriteKey = 0;
    u64 stableEntityKey = 0;
    u32 insertionOrder = 0;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float rotationRadians = 0.0F;
    float widthMeters = 1.0F;
    float heightMeters = 1.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;
    bool flipX = false;
    bool flipY = false;
};

struct RenderPerspectiveCamera final {
    u64 stableCameraKey = 0;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float forwardX = 0.0F;
    float forwardY = 0.0F;
    float forwardZ = -1.0F;
    float upX = 0.0F;
    float upY = 1.0F;
    float upZ = 0.0F;
    float verticalFovDegrees = 60.0F;
    float nearPlaneMeters = 0.1F;
    float farPlaneMeters = 1000.0F;
    float aspectRatio = 1.0F;
    RenderNormalizedViewport normalizedViewport{};
};

struct RenderMesh3DItem final {
    u32 meshKey = 0;
    u32 materialKey = 0;
    u32 submeshIndex = 0;
    u64 stableEntityKey = 0;
    u32 insertionOrder = 0;
    u32 depthBucket = 0;
    float cameraDepth = 0.0F;
    float worldBoundsCenterX = 0.0F;
    float worldBoundsCenterY = 0.0F;
    float worldBoundsCenterZ = 0.0F;
    float worldBoundsRadius = 1.0F;
    std::array<float, 16> columnMajorWorldTransform{};
    RenderLinearColor baseColorFactor{};
    bool doubleSided = false;
};

struct RenderMesh3DBatch final {
    u32 firstItem = 0;
    u32 itemCount = 0;
    u32 meshKey = 0;
    u32 materialKey = 0;
    u32 submeshIndex = 0;
    bool doubleSided = false;
};

struct RenderSceneStatistics final {
    u32 cameraCount = 0;
    u32 camera2DCount = 0;
    u32 perspectiveCameraCount = 0;
    u32 submittedSpriteCount = 0;
    u32 visibleSpriteCount = 0;
    u32 culledSpriteCount = 0;
    u32 prunedInvisibleCount = 0;
    u32 prunedTransparentCount = 0;
    u64 sortOrderChecksum = 0;
    u32 submittedMesh3DCount = 0;
    u32 visibleMesh3DCount = 0;
    u32 culledMesh3DCount = 0;
    u32 prunedInvisibleMesh3DCount = 0;
    u32 mesh3DBatchCount = 0;
    u64 mesh3DSortOrderChecksum = 0;
};

struct RenderSceneBuilderStatistics final {
    u64 begunBuildCount = 0;
    u64 committedBuildCount = 0;
    u64 rolledBackBuildCount = 0;
    u64 capacityFailureCount = 0;
    u64 invalidInputFailureCount = 0;
};

// Borrowed view. A successful beginFrame(), rollback(), a successful
// replacement commit, move, or destruction invalidates the previous view.
// A beginFrame() preflight failure preserves the last published view.
class RenderSceneView final {
  public:
    constexpr RenderSceneView() noexcept = default;

    [[nodiscard]] constexpr const std::optional<RenderCamera2D>& camera2D() const noexcept
    {
        return m_camera;
    }

    [[nodiscard]] constexpr std::span<const RenderSprite2DItem> sprites2D() const noexcept
    {
        return m_sprites;
    }

    [[nodiscard]] constexpr const std::optional<RenderPerspectiveCamera>& perspectiveCamera() const noexcept
    {
        return m_perspectiveCamera;
    }

    [[nodiscard]] constexpr std::span<const RenderMesh3DItem> meshes3D() const noexcept
    {
        return m_meshes3D;
    }

    [[nodiscard]] constexpr std::span<const RenderMesh3DBatch> mesh3DBatches() const noexcept
    {
        return m_mesh3DBatches;
    }

    [[nodiscard]] constexpr const RenderSceneStatistics& statistics() const noexcept
    {
        return m_statistics;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return !m_camera.has_value() && m_sprites.empty() && !m_perspectiveCamera.has_value() &&
               m_meshes3D.empty();
    }

  private:
    friend class RenderSceneBuilder;

    constexpr RenderSceneView(std::optional<RenderCamera2D> camera,
                              std::span<const RenderSprite2DItem> sprites,
                              std::optional<RenderPerspectiveCamera> perspectiveCamera,
                              std::span<const RenderMesh3DItem> meshes3D,
                              std::span<const RenderMesh3DBatch> mesh3DBatches,
                              RenderSceneStatistics statistics) noexcept
        : m_camera(std::move(camera)), m_sprites(sprites),
          m_perspectiveCamera(std::move(perspectiveCamera)), m_meshes3D(meshes3D),
          m_mesh3DBatches(mesh3DBatches), m_statistics(statistics)
    {
    }

    std::optional<RenderCamera2D> m_camera{};
    std::span<const RenderSprite2DItem> m_sprites{};
    std::optional<RenderPerspectiveCamera> m_perspectiveCamera{};
    std::span<const RenderMesh3DItem> m_meshes3D{};
    std::span<const RenderMesh3DBatch> m_mesh3DBatches{};
    RenderSceneStatistics m_statistics{};
};

class RenderSceneBuilder;

// Game-facing, phase-local writer. It cannot publish or resize the builder.
class RenderSceneWriter final {
  public:
    RenderSceneWriter(const RenderSceneWriter&) = delete;
    RenderSceneWriter& operator=(const RenderSceneWriter&) = delete;

    [[nodiscard]] Core::Status setCamera2D(const RenderCamera2DInput& camera);
    [[nodiscard]] Core::Status addSprite2D(const RenderSprite2DInput& sprite);
    [[nodiscard]] Core::Status setPerspectiveCamera(const RenderPerspectiveCameraInput& camera);
    [[nodiscard]] Core::Status addMesh3D(const RenderMesh3DInput& mesh);

  private:
    friend class RenderSceneBuilder;

    explicit RenderSceneWriter(RenderSceneBuilder& builder) noexcept : m_builder(&builder)
    {
    }

    RenderSceneBuilder* m_builder = nullptr;
};

class RenderSceneBuilder final {
  public:
    [[nodiscard]] static Core::Result<RenderSceneBuilder> Create(
        RenderSceneCapacity capacity = {},
        std::pmr::memory_resource& storage = *std::pmr::get_default_resource());

    RenderSceneBuilder(const RenderSceneBuilder&) = delete;
    RenderSceneBuilder& operator=(const RenderSceneBuilder&) = delete;
    RenderSceneBuilder(RenderSceneBuilder&&) noexcept;
    RenderSceneBuilder& operator=(RenderSceneBuilder&&) = delete;
    ~RenderSceneBuilder();

    [[nodiscard]] Core::Status beginFrame(RenderSceneFrameParameters parameters = {});
    [[nodiscard]] RenderSceneWriter writer() noexcept;
    [[nodiscard]] Core::Result<RenderSceneView> commit();
    void rollback() noexcept;

    [[nodiscard]] RenderSceneView publishedView() const noexcept;
    [[nodiscard]] RenderSceneCapacity capacity() const noexcept;
    [[nodiscard]] RenderSceneBuilderStatistics statistics() const noexcept;

  private:
    friend class RenderSceneWriter;

    enum class State : u8 {
        Ready,
        Building,
        Published,
    };

    RenderSceneBuilder(RenderSceneCapacity capacity, std::pmr::memory_resource& storage,
                       RenderSprite2DItem* sprites, RenderMesh3DItem* meshes3D,
                       RenderMesh3DBatch* mesh3DBatches) noexcept;

    [[nodiscard]] Core::Status setCamera2D(const RenderCamera2DInput& camera);
    [[nodiscard]] Core::Status addSprite2D(const RenderSprite2DInput& sprite);
    [[nodiscard]] Core::Status setPerspectiveCamera(const RenderPerspectiveCameraInput& camera);
    [[nodiscard]] Core::Status addMesh3D(const RenderMesh3DInput& mesh);
    [[nodiscard]] Core::Status failBuild(Core::ErrorCode code, const char* message);
    [[nodiscard]] Core::Status validateCamera(const RenderCamera2DInput& camera) const noexcept;
    [[nodiscard]] Core::Status validateSprite(const RenderSprite2DInput& sprite) const noexcept;
    [[nodiscard]] Core::Status validatePerspectiveCamera(const RenderPerspectiveCameraInput& camera) const noexcept;
    [[nodiscard]] Core::Status validateMesh3D(const RenderMesh3DInput& mesh) const noexcept;
    [[nodiscard]] bool intersectsCamera(const RenderSprite2DItem& sprite,
                                         const RenderCamera2D& camera) const noexcept;
    [[nodiscard]] bool intersectsPerspectiveCamera(const RenderMesh3DItem& mesh,
                                                   const RenderPerspectiveCamera& camera,
                                                   float& cameraDepth) const noexcept;
    [[nodiscard]] Core::Status finalizeMesh3DBatches();
    void clearCandidate() noexcept;
    void releaseStorage() noexcept;
    void rollbackBuilding() noexcept;
    [[nodiscard]] RenderSceneView makePublishedView() const noexcept;

    RenderSceneCapacity m_capacity{};
    std::pmr::memory_resource* m_storage = nullptr;
    RenderSprite2DItem* m_sprites = nullptr;
    RenderMesh3DItem* m_meshes3D = nullptr;
    RenderMesh3DBatch* m_mesh3DBatches = nullptr;
    u32 m_spriteCount = 0;
    u32 m_mesh3DCount = 0;
    u32 m_mesh3DBatchCount = 0;
    std::optional<RenderCamera2D> m_camera{};
    std::optional<RenderPerspectiveCamera> m_perspectiveCamera{};
    RenderSceneFrameParameters m_frameParameters{};
    RenderSceneStatistics m_candidateStatistics{};
    RenderSceneStatistics m_publishedStatistics{};
    RenderSceneBuilderStatistics m_statistics{};
    std::optional<Core::Error> m_stickyBuildError{};
    State m_state = State::Ready;
};

} // namespace Tina::Render
