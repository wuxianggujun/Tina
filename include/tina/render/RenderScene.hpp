#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/FrameResource.hpp>

#include <array>
#include <cstddef>
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

// texture is a required packet-local Texture2D reference. normalTexture is an
// optional packet-local Texture2D reference; invalid means no normal map. The
// backend resolves both through RenderFrame::resources during synchronous submit.
// UV rect defaults to full texture [0,1]; typed Sprite payload extraction may
// override it. The position is the resolved geometric center; Scene/Asset
// extraction applies any authored pivot before writing this render-facing value.
struct RenderSprite2DInput final {
    FrameResourceRef texture{};
    FrameResourceRef normalTexture{};
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

struct Sprite2DPointLight final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float radiusMeters = 1.0F;
    // Zero preserves point-source hard shadows. A positive radius produces a
    // bounded penumbra and may not exceed radiusMeters.
    float sourceRadiusMeters = 0.0F;
    float colorR = 1.0F;
    float colorG = 1.0F;
    float colorB = 1.0F;
};

struct Sprite2DShadowSegment final {
    float startX = -0.5F;
    float startY = 0.0F;
    float endX = 0.5F;
    float endY = 0.0F;
};

struct Sprite2DLightingDesc final {
    // Committed snapshot capacities. Scene extraction may cull point lights before
    // supplying this descriptor; the writer validates the supplied list and never
    // silently truncates it.
    static constexpr std::size_t MaximumPointLightCount = 8;
    static constexpr std::size_t MaximumShadowSegmentCount = 32;

    // Consumed synchronously by the receiving writer; no span is retained.
    std::span<const Sprite2DPointLight> pointLights{};
    std::span<const Sprite2DShadowSegment> shadowSegments{};
    float ambientScale = 0.2F;
};

[[nodiscard]] Core::Status validateSprite2DLightingDesc(const Sprite2DLightingDesc& lighting) noexcept;

// Self-contained committed frame snapshot. Sprite ordering remains governed by
// RenderSprite2DItem sorting; lighting never reorders or splits transparent items.
class RenderSprite2DLighting final {
  public:
    [[nodiscard]] constexpr std::span<const Sprite2DPointLight> pointLights() const noexcept
    {
        return {m_pointLights.data(), m_pointLightCount};
    }

    [[nodiscard]] constexpr float ambientScale() const noexcept
    {
        return m_ambientScale;
    }

    [[nodiscard]] constexpr std::span<const Sprite2DShadowSegment> shadowSegments() const noexcept
    {
        return {m_shadowSegments.data(), m_shadowSegmentCount};
    }

    [[nodiscard]] constexpr Sprite2DLightingDesc descriptor() const noexcept
    {
        return {
            .pointLights = pointLights(),
            .shadowSegments = shadowSegments(),
            .ambientScale = m_ambientScale,
        };
    }

  private:
    friend class RenderSceneBuilder;

    std::array<Sprite2DPointLight, Sprite2DLightingDesc::MaximumPointLightCount> m_pointLights{};
    std::array<Sprite2DShadowSegment, Sprite2DLightingDesc::MaximumShadowSegmentCount>
        m_shadowSegments{};
    u32 m_pointLightCount = 0;
    u32 m_shadowSegmentCount = 0;
    float m_ambientScale = 0.2F;
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

    friend constexpr bool operator==(const RenderLinearColor&,
                                     const RenderLinearColor&) noexcept = default;
};

struct Mesh3DDirectionalLight final {
    float directionTowardLightX = 0.0F;
    float directionTowardLightY = 1.0F;
    float directionTowardLightZ = 0.0F;
    float colorR = 1.0F;
    float colorG = 1.0F;
    float colorB = 1.0F;
};

struct Mesh3DPointLight final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float influenceRadius = 1.0F;
    float colorR = 1.0F;
    float colorG = 1.0F;
    float colorB = 1.0F;
};

struct Mesh3DSpotLight final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float influenceRadius = 1.0F;
    float directionFromLightX = 0.0F;
    float directionFromLightY = 0.0F;
    float directionFromLightZ = -1.0F;
    float innerConeCosine = 0.9396926F;
    float outerConeCosine = 0.8660254F;
    float colorR = 1.0F;
    float colorG = 1.0F;
    float colorB = 1.0F;
};

struct Mesh3DLightingDesc final {
    static constexpr std::size_t MaximumDirectionalLightCount = 4;
    static constexpr std::size_t MaximumPointLightCount = 8;
    static constexpr std::size_t MaximumSpotLightCount = 8;

    // Consumed synchronously by the receiving writer/device; no span is retained.
    std::span<const Mesh3DDirectionalLight> directionalLights{};
    std::span<const Mesh3DPointLight> pointLights{};
    std::span<const Mesh3DSpotLight> spotLights{};
    float ambientScale = 0.18F;
};

[[nodiscard]] Core::Status validateMesh3DLightingDesc(const Mesh3DLightingDesc& lighting) noexcept;

// Self-contained committed frame snapshot. Unlike Mesh3DLightingDesc, this type
// owns its fixed-capacity light array and is safe to publish through RenderSceneView.
class RenderMesh3DLighting final {
  public:
    [[nodiscard]] constexpr std::span<const Mesh3DDirectionalLight> directionalLights() const noexcept
    {
        return {m_directionalLights.data(), m_directionalLightCount};
    }

    [[nodiscard]] constexpr std::span<const Mesh3DPointLight> pointLights() const noexcept
    {
        return {m_pointLights.data(), m_pointLightCount};
    }

    [[nodiscard]] constexpr std::span<const Mesh3DSpotLight> spotLights() const noexcept
    {
        return {m_spotLights.data(), m_spotLightCount};
    }

    [[nodiscard]] constexpr float ambientScale() const noexcept
    {
        return m_ambientScale;
    }

    [[nodiscard]] constexpr Mesh3DLightingDesc descriptor() const noexcept
    {
        return {
            .directionalLights = directionalLights(),
            .pointLights = pointLights(),
            .spotLights = spotLights(),
            .ambientScale = m_ambientScale,
        };
    }

  private:
    friend class RenderSceneBuilder;

    std::array<Mesh3DDirectionalLight, Mesh3DLightingDesc::MaximumDirectionalLightCount>
        m_directionalLights{};
    u32 m_directionalLightCount = 0;
    std::array<Mesh3DPointLight, Mesh3DLightingDesc::MaximumPointLightCount> m_pointLights{};
    u32 m_pointLightCount = 0;
    std::array<Mesh3DSpotLight, Mesh3DLightingDesc::MaximumSpotLightCount> m_spotLights{};
    u32 m_spotLightCount = 0;
    float m_ambientScale = 0.18F;
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

// mesh/material are packet-local refs produced by Scene extraction from weak
// AssetHandles. The backend resolves them synchronously through RenderFrame::resources.
struct RenderMesh3DInput final {
    FrameResourceRef mesh{};
    FrameResourceRef material{};
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
    FrameResourceRef texture{};
    FrameResourceRef normalTexture{};
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
    FrameResourceRef mesh{};
    FrameResourceRef material{};
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
    FrameResourceRef mesh{};
    FrameResourceRef material{};
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
    bool sprite2DLightingConfigured = false;
    u32 pointLight2DCount = 0;
    u32 shadowOccluder2DCount = 0;
    u32 submittedMesh3DCount = 0;
    u32 visibleMesh3DCount = 0;
    u32 culledMesh3DCount = 0;
    u32 prunedInvisibleMesh3DCount = 0;
    u32 mesh3DBatchCount = 0;
    u64 mesh3DSortOrderChecksum = 0;
    bool mesh3DLightingConfigured = false;
    u32 directionalLightCount = 0;
    u32 pointLight3DCount = 0;
    u32 spotLight3DCount = 0;
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

    [[nodiscard]] constexpr const std::optional<RenderSprite2DLighting>& sprite2DLighting() const noexcept
    {
        return m_sprite2DLighting;
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

    [[nodiscard]] constexpr const std::optional<RenderMesh3DLighting>& mesh3DLighting() const noexcept
    {
        return m_mesh3DLighting;
    }

    [[nodiscard]] constexpr const RenderSceneStatistics& statistics() const noexcept
    {
        return m_statistics;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return !m_camera.has_value() && m_sprites.empty() && !m_sprite2DLighting.has_value() &&
               !m_perspectiveCamera.has_value() && m_meshes3D.empty() && !m_mesh3DLighting.has_value();
    }

  private:
    friend class RenderSceneBuilder;

    constexpr RenderSceneView(std::optional<RenderCamera2D> camera,
                              std::span<const RenderSprite2DItem> sprites,
                              std::optional<RenderSprite2DLighting> sprite2DLighting,
                              std::optional<RenderPerspectiveCamera> perspectiveCamera,
                              std::span<const RenderMesh3DItem> meshes3D,
                              std::span<const RenderMesh3DBatch> mesh3DBatches,
                              std::optional<RenderMesh3DLighting> mesh3DLighting,
                              RenderSceneStatistics statistics) noexcept
        : m_camera(std::move(camera)), m_sprites(sprites), m_sprite2DLighting(std::move(sprite2DLighting)),
          m_perspectiveCamera(std::move(perspectiveCamera)), m_meshes3D(meshes3D),
          m_mesh3DBatches(mesh3DBatches), m_mesh3DLighting(std::move(mesh3DLighting)),
          m_statistics(statistics)
    {
    }

    std::optional<RenderCamera2D> m_camera{};
    std::span<const RenderSprite2DItem> m_sprites{};
    std::optional<RenderSprite2DLighting> m_sprite2DLighting{};
    std::optional<RenderPerspectiveCamera> m_perspectiveCamera{};
    std::span<const RenderMesh3DItem> m_meshes3D{};
    std::span<const RenderMesh3DBatch> m_mesh3DBatches{};
    std::optional<RenderMesh3DLighting> m_mesh3DLighting{};
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
    [[nodiscard]] Core::Status setSprite2DLighting(const Sprite2DLightingDesc& lighting);
    [[nodiscard]] Core::Status setPerspectiveCamera(const RenderPerspectiveCameraInput& camera);
    [[nodiscard]] Core::Status addMesh3D(const RenderMesh3DInput& mesh);
    [[nodiscard]] Core::Status setMesh3DLighting(const Mesh3DLightingDesc& lighting);

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
    [[nodiscard]] Core::Status setSprite2DLighting(const Sprite2DLightingDesc& lighting);
    [[nodiscard]] Core::Status setPerspectiveCamera(const RenderPerspectiveCameraInput& camera);
    [[nodiscard]] Core::Status addMesh3D(const RenderMesh3DInput& mesh);
    [[nodiscard]] Core::Status setMesh3DLighting(const Mesh3DLightingDesc& lighting);
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
    std::optional<RenderSprite2DLighting> m_sprite2DLighting{};
    std::optional<RenderPerspectiveCamera> m_perspectiveCamera{};
    std::optional<RenderMesh3DLighting> m_mesh3DLighting{};
    RenderSceneFrameParameters m_frameParameters{};
    RenderSceneStatistics m_candidateStatistics{};
    RenderSceneStatistics m_publishedStatistics{};
    RenderSceneBuilderStatistics m_statistics{};
    std::optional<Core::Error> m_stickyBuildError{};
    State m_state = State::Ready;
};

} // namespace Tina::Render
