//
// 角色渲染系统 - 渲染所有角色
//

#pragma once

#include "../Components.hpp"
#include <bgfx/bgfx.h>

namespace Tina::ECS {

// 颜色顶点结构（与TileRenderer一致）
struct ColorVertex {
    float x, y, z;
    float r, g, b, a;
};

class CharacterRenderSystem {
public:
    void render(entt::registry& registry, uint16_t viewId,
                bgfx::ProgramHandle program, const bgfx::VertexLayout& layout);

private:
    // 渲染单个AABB
    void renderAABB(float x, float y, float width, float height,
                    float r, float g, float b, float a,
                    uint16_t viewId, bgfx::ProgramHandle program,
                    const bgfx::VertexLayout& layout) const;
};

} // namespace Tina::ECS
