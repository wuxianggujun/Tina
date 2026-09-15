#include "BgfxParticle3DGeometry.hpp"

#include <tina/render/RenderErrors.hpp>

#include <cmath>
#include <limits>
#include <type_traits>

namespace Tina::Render::Bgfx {
namespace {

inline constexpr u32 VerticesPerParticle = 4;
inline constexpr u32 IndicesPerParticle = 6;

static_assert(std::is_standard_layout_v<BgfxParticle3DVertex>);
static_assert(sizeof(BgfxParticle3DVertex) == sizeof(float) * 5U + sizeof(u32));

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] u32 packAbgr(const RenderParticle3DItem& particle) noexcept
{
    return (static_cast<u32>(particle.alpha) << 24U) | (static_cast<u32>(particle.blue) << 16U) |
           (static_cast<u32>(particle.green) << 8U) | static_cast<u32>(particle.red);
}

[[nodiscard]] bool finiteParticle(const RenderParticle3DItem& particle) noexcept
{
    return particle.stableParticleKey != 0 && static_cast<bool>(particle.texture) &&
           finite(particle.worldX) && finite(particle.worldY) && finite(particle.worldZ) &&
           finite(particle.widthMeters) && finite(particle.heightMeters) &&
           finite(particle.rotationRadians) && particle.widthMeters > 0.0F &&
           particle.heightMeters > 0.0F && Core::isSupportedBlendMode(particle.blendMode);
}

[[nodiscard]] Core::Result<BgfxParticle3DFrameRequirements> invalidFrame(const char* message)
{
    return Core::failure(RenderErrorCode::InvalidRenderSceneInput, message);
}

void writeParticle(const RenderParticle3DItem& particle, const BgfxParticle3DBillboardBasis& basis,
                   std::span<BgfxParticle3DVertex> vertices, std::span<u32> indices,
                   u32 firstVertex) noexcept
{
    // Spin the billboard in its own plane: rotating the basis rather than the
    // corners keeps the quad camera-facing at every rotation angle.
    const float cosine = std::cos(particle.rotationRadians);
    const float sine = std::sin(particle.rotationRadians);
    const float axisXX = basis.rightX * cosine + basis.upX * sine;
    const float axisXY = basis.rightY * cosine + basis.upY * sine;
    const float axisXZ = basis.rightZ * cosine + basis.upZ * sine;
    const float axisYX = basis.upX * cosine - basis.rightX * sine;
    const float axisYY = basis.upY * cosine - basis.rightY * sine;
    const float axisYZ = basis.upZ * cosine - basis.rightZ * sine;

    const float halfWidth = particle.widthMeters * 0.5F;
    const float halfHeight = particle.heightMeters * 0.5F;
    const float halfAxisXX = axisXX * halfWidth;
    const float halfAxisXY = axisXY * halfWidth;
    const float halfAxisXZ = axisXZ * halfWidth;
    const float halfAxisYX = axisYX * halfHeight;
    const float halfAxisYY = axisYY * halfHeight;
    const float halfAxisYZ = axisYZ * halfHeight;

    const u32 color = packAbgr(particle);

    // UV V is top=low / bottom=high, matching the Sprite2D convention so one
    // cooked texture reads the same way in both paths.
    vertices[0] = BgfxParticle3DVertex{
        .positionX = particle.worldX - halfAxisXX - halfAxisYX,
        .positionY = particle.worldY - halfAxisXY - halfAxisYY,
        .positionZ = particle.worldZ - halfAxisXZ - halfAxisYZ,
        .textureU = 0.0F,
        .textureV = 1.0F,
        .abgr = color,
    };
    vertices[1] = BgfxParticle3DVertex{
        .positionX = particle.worldX + halfAxisXX - halfAxisYX,
        .positionY = particle.worldY + halfAxisXY - halfAxisYY,
        .positionZ = particle.worldZ + halfAxisXZ - halfAxisYZ,
        .textureU = 1.0F,
        .textureV = 1.0F,
        .abgr = color,
    };
    vertices[2] = BgfxParticle3DVertex{
        .positionX = particle.worldX + halfAxisXX + halfAxisYX,
        .positionY = particle.worldY + halfAxisXY + halfAxisYY,
        .positionZ = particle.worldZ + halfAxisXZ + halfAxisYZ,
        .textureU = 1.0F,
        .textureV = 0.0F,
        .abgr = color,
    };
    vertices[3] = BgfxParticle3DVertex{
        .positionX = particle.worldX - halfAxisXX + halfAxisYX,
        .positionY = particle.worldY - halfAxisXY + halfAxisYY,
        .positionZ = particle.worldZ - halfAxisXZ + halfAxisYZ,
        .textureU = 0.0F,
        .textureV = 0.0F,
        .abgr = color,
    };

    indices[0] = firstVertex;
    indices[1] = firstVertex + 1U;
    indices[2] = firstVertex + 2U;
    indices[3] = firstVertex;
    indices[4] = firstVertex + 2U;
    indices[5] = firstVertex + 3U;
}

} // namespace

Core::Result<BgfxParticle3DBillboardBasis>
particle3DBillboardBasis(const RenderPerspectiveCamera& camera) noexcept
{
    // Same handedness as RenderScene's own culling basis: right = forward x up.
    const float rightX = camera.forwardY * camera.upZ - camera.forwardZ * camera.upY;
    const float rightY = camera.forwardZ * camera.upX - camera.forwardX * camera.upZ;
    const float rightZ = camera.forwardX * camera.upY - camera.forwardY * camera.upX;
    const double rightLengthSquared = static_cast<double>(rightX) * rightX +
                                      static_cast<double>(rightY) * rightY +
                                      static_cast<double>(rightZ) * rightZ;
    if (!std::isfinite(rightLengthSquared) || rightLengthSquared <= 0.0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Particle3D billboards require a camera whose forward and up are not parallel");
    }
    const float rightScale = static_cast<float>(1.0 / std::sqrt(rightLengthSquared));
    const float unitRightX = rightX * rightScale;
    const float unitRightY = rightY * rightScale;
    const float unitRightZ = rightZ * rightScale;

    // Re-derive up from the orthonormalised right so a camera whose authored up is
    // merely near-perpendicular still yields a square, non-sheared billboard.
    const float upX = unitRightY * camera.forwardZ - unitRightZ * camera.forwardY;
    const float upY = unitRightZ * camera.forwardX - unitRightX * camera.forwardZ;
    const float upZ = unitRightX * camera.forwardY - unitRightY * camera.forwardX;
    const double upLengthSquared = static_cast<double>(upX) * upX + static_cast<double>(upY) * upY +
                                   static_cast<double>(upZ) * upZ;
    if (!std::isfinite(upLengthSquared) || upLengthSquared <= 0.0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Particle3D billboards require a camera with a non-degenerate forward direction");
    }
    const float upScale = static_cast<float>(1.0 / std::sqrt(upLengthSquared));
    return BgfxParticle3DBillboardBasis{
        .rightX = unitRightX,
        .rightY = unitRightY,
        .rightZ = unitRightZ,
        .upX = upX * upScale,
        .upY = upY * upScale,
        .upZ = upZ * upScale,
    };
}

Core::Status validateParticle3DFrameResources(RenderSceneView scene,
                                              FrameResourceTableView resources) noexcept
{
    for (const RenderParticle3DItem& particle : scene.particles3D())
    {
        if (resources.resolve(particle.texture, FrameResourceKind::Texture2D) == nullptr)
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "Particle3D references a texture that is not in the frame resource table");
        }
    }
    return Core::success();
}

Core::Result<BgfxParticle3DFrameRequirements>
checkedParticle3DFrame(RenderSceneView scene, FrameResourceTableView resources)
{
    const std::span<const RenderParticle3DItem> particles = scene.particles3D();
    if (particles.empty())
    {
        return BgfxParticle3DFrameRequirements{};
    }
    if (!scene.perspectiveCamera().has_value())
    {
        return invalidFrame("Particle3D items require a valid perspective camera");
    }
    if (auto basis = particle3DBillboardBasis(*scene.perspectiveCamera()); !basis)
    {
        return Core::failure(std::move(basis.error()));
    }
    if (auto status = validateParticle3DFrameResources(scene, resources); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    for (const RenderParticle3DItem& particle : particles)
    {
        if (!finiteParticle(particle))
        {
            return invalidFrame("Particle3D contains invalid geometry or resource values");
        }
    }

    // Must break on exactly what the submit loop breaks on, including a mesh draw
    // between two particles: the device terminates when the counted batch total
    // disagrees with the submitted one.
    u32 particleCount = 0;
    u32 batchCount = 0;
    bool previousWasParticle = false;
    FrameResourceRef previousTexture{};
    Core::BlendMode previousBlendMode = Core::BlendMode::PremultipliedAlpha;
    for (const RenderTransparent3DDraw& draw : scene.transparent3DDraws())
    {
        if (draw.kind != RenderTransparent3DDrawKind::Particle)
        {
            previousWasParticle = false;
            continue;
        }
        if (draw.itemIndex >= particles.size())
        {
            return invalidFrame("Particle3D transparent draw addresses an out-of-range particle item");
        }
        const RenderParticle3DItem& particle = particles[draw.itemIndex];
        if (!previousWasParticle || particle.texture != previousTexture ||
            particle.blendMode != previousBlendMode)
        {
            ++batchCount;
        }
        previousWasParticle = true;
        previousTexture = particle.texture;
        previousBlendMode = particle.blendMode;
        ++particleCount;
    }
    if (particleCount != particles.size())
    {
        return invalidFrame("Particle3D transparent draws do not cover every committed particle exactly once");
    }
    if (particleCount > (std::numeric_limits<u32>::max)() / VerticesPerParticle ||
        particleCount > (std::numeric_limits<u32>::max)() / IndicesPerParticle)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Particle3D frame exceeds backend geometry count limits");
    }

    return BgfxParticle3DFrameRequirements{
        .particleCount = particleCount,
        .vertexCount = particleCount * VerticesPerParticle,
        .indexCount = particleCount * IndicesPerParticle,
        .batchCount = batchCount,
    };
}

Core::Result<BgfxParticle3DFrameRequirements>
writeParticle3DGeometry(RenderSceneView scene, FrameResourceTableView resources,
                        std::span<BgfxParticle3DVertex> vertices, std::span<u32> indices)
{
    auto requirements = checkedParticle3DFrame(scene, resources);
    if (!requirements)
    {
        return Core::failure(std::move(requirements.error()));
    }
    if (vertices.size() < requirements->vertexCount || indices.size() < requirements->indexCount)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Particle3D output buffers do not have enough capacity");
    }
    if (requirements->particleCount == 0)
    {
        return *requirements;
    }

    auto basis = particle3DBillboardBasis(*scene.perspectiveCamera());
    if (!basis)
    {
        return Core::failure(std::move(basis.error()));
    }

    const std::span<const RenderParticle3DItem> particles = scene.particles3D();
    u32 slot = 0;
    for (const RenderTransparent3DDraw& draw : scene.transparent3DDraws())
    {
        if (draw.kind != RenderTransparent3DDrawKind::Particle)
        {
            continue;
        }
        const usize vertexOffset = static_cast<usize>(slot) * VerticesPerParticle;
        const usize indexOffset = static_cast<usize>(slot) * IndicesPerParticle;
        writeParticle(particles[draw.itemIndex], *basis,
                      vertices.subspan(vertexOffset, VerticesPerParticle),
                      indices.subspan(indexOffset, IndicesPerParticle),
                      static_cast<u32>(vertexOffset));
        ++slot;
    }
    if (slot != requirements->particleCount)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Particle3D geometry write covered a different particle count than validation");
    }
    return *requirements;
}

} // namespace Tina::Render::Bgfx
