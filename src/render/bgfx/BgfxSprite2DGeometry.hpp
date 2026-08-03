#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <span>

namespace Tina::Render::Bgfx {

struct BgfxSprite2DVertex final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float textureU = 0.0F;
    float textureV = 0.0F;
    u32 abgr = 0;
};

struct BgfxSprite2DFrameRequirements final {
    u32 spriteCount = 0;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u32 batchCount = 0;
};

// Validates every required base texture and non-empty optional normal texture ref
// without touching geometry output or backend state. This also applies to
// suspended (non-drawing) frames.
[[nodiscard]] Core::Status validateSprite2DFrameResources(
    RenderSceneView scene, FrameResourceTableView resources) noexcept;

// Validates the Sprite2D contract, including every packet-local texture ref and
// contiguous (base texture, normal texture) batch identity, before the backend
// commits surface state or allocates transient geometry.
[[nodiscard]] Core::Result<BgfxSprite2DFrameRequirements>
checkedSprite2DFrame(RenderSceneView scene, FrameResourceTableView resources);

// Allocation-free and transactional: the scene and exact output extents are
// validated before the first write. Indices are always absolute u32 indices.
[[nodiscard]] Core::Result<BgfxSprite2DFrameRequirements>
writeSprite2DGeometry(RenderSceneView scene, FrameResourceTableView resources,
                      std::span<BgfxSprite2DVertex> vertices, std::span<u32> indices);

} // namespace Tina::Render::Bgfx
