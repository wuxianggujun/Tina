#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <compare>
#include <memory_resource>
#include <optional>
#include <span>
#include <utility>

namespace Tina::Render {

struct RenderSceneCapacity final {
    static constexpr u32 DefaultSpriteCapacity = 16'384;
    static constexpr u32 MaximumSpriteCapacity = 1'048'576;

    u32 spriteCapacity = DefaultSpriteCapacity;
};

[[nodiscard]] Core::Status validateRenderSceneCapacity(const RenderSceneCapacity& capacity) noexcept;

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

// spriteKey is an M8 fixture resource key. M10 replaces it with a resolved
// FrameResourceRef without changing the render item ownership boundary. The
// position is the resolved geometric center; Scene/Asset extraction applies
// any authored pivot before writing this render-facing value.
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
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;
    bool flipX = false;
    bool flipY = false;
};

struct RenderSceneStatistics final {
    u32 cameraCount = 0;
    u32 submittedSpriteCount = 0;
    u32 visibleSpriteCount = 0;
    u32 culledSpriteCount = 0;
    u32 prunedInvisibleCount = 0;
    u32 prunedTransparentCount = 0;
    u64 sortOrderChecksum = 0;
};

struct RenderSceneBuilderStatistics final {
    u64 begunBuildCount = 0;
    u64 committedBuildCount = 0;
    u64 rolledBackBuildCount = 0;
    u64 capacityFailureCount = 0;
    u64 invalidInputFailureCount = 0;
};

// Borrowed view. beginFrame(), rollback(), a successful replacement commit,
// move, or destruction invalidates the previous view.
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

    [[nodiscard]] constexpr const RenderSceneStatistics& statistics() const noexcept
    {
        return m_statistics;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return !m_camera.has_value() && m_sprites.empty();
    }

  private:
    friend class RenderSceneBuilder;

    constexpr RenderSceneView(std::optional<RenderCamera2D> camera,
                              std::span<const RenderSprite2DItem> sprites,
                              RenderSceneStatistics statistics) noexcept
        : m_camera(std::move(camera)), m_sprites(sprites), m_statistics(statistics)
    {
    }

    std::optional<RenderCamera2D> m_camera{};
    std::span<const RenderSprite2DItem> m_sprites{};
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

    [[nodiscard]] Core::Status beginFrame();
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
                       RenderSprite2DItem* sprites) noexcept;

    [[nodiscard]] Core::Status setCamera2D(const RenderCamera2DInput& camera);
    [[nodiscard]] Core::Status addSprite2D(const RenderSprite2DInput& sprite);
    [[nodiscard]] Core::Status failBuild(Core::ErrorCode code, const char* message);
    [[nodiscard]] Core::Status validateCamera(const RenderCamera2DInput& camera) const noexcept;
    [[nodiscard]] Core::Status validateSprite(const RenderSprite2DInput& sprite) const noexcept;
    [[nodiscard]] bool intersectsCamera(const RenderSprite2DItem& sprite,
                                         const RenderCamera2D& camera) const noexcept;
    void clearCandidate() noexcept;
    void releaseStorage() noexcept;
    void rollbackBuilding() noexcept;
    [[nodiscard]] RenderSceneView makePublishedView() const noexcept;

    RenderSceneCapacity m_capacity{};
    std::pmr::memory_resource* m_storage = nullptr;
    RenderSprite2DItem* m_sprites = nullptr;
    u32 m_spriteCount = 0;
    std::optional<RenderCamera2D> m_camera{};
    RenderSceneStatistics m_candidateStatistics{};
    RenderSceneStatistics m_publishedStatistics{};
    RenderSceneBuilderStatistics m_statistics{};
    std::optional<Core::Error> m_stickyBuildError{};
    State m_state = State::Ready;
};

} // namespace Tina::Render
