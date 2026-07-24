#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <array>
#include <span>

namespace Tina::Render::Bgfx {

inline constexpr u32 Opaque3DmeshKey = 1;
inline constexpr u32 Opaque3DmaterialKey = 1;
inline constexpr u32 Opaque3DFixtureSubmeshIndex = 0;

struct BgfxOpaque3DVertex final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float normalX = 0.0F;
    float normalY = 0.0F;
    float normalZ = 0.0F;
    float textureU = 0.0F;
    float textureV = 0.0F;
};

struct BgfxOpaque3DInstanceData final {
    std::array<float, 16> columnMajorWorldTransform{};
    std::array<float, 4> baseColorFactor{};
};

struct BgfxOpaque3DFrameRequirements final {
    u32 instanceCount = 0;
    u32 batchCount = 0;
};

[[nodiscard]] std::span<const BgfxOpaque3DVertex> canonicalCubeVertices() noexcept;
[[nodiscard]] std::span<const u16> canonicalCubeIndices() noexcept;

// Validates the private M9-B procedural fixture contract before the backend
// commits surface state or allocates transient instance data.
[[nodiscard]] Core::Result<BgfxOpaque3DFrameRequirements>
checkedOpaque3DFrame(RenderSceneView scene);

// Allocation-free and transactional: all input and output extents are checked
// before the first write.
[[nodiscard]] Core::Result<BgfxOpaque3DFrameRequirements>
writeOpaque3DInstanceData(RenderSceneView scene,
                          std::span<BgfxOpaque3DInstanceData> instances);

} // namespace Tina::Render::Bgfx
