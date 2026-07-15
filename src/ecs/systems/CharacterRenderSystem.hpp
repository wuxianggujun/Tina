//
// 角色渲染系统 - 渲染所有角色
//

#pragma once

#include "../Components.hpp"
#include <bgfx/bgfx.h>

namespace Tina { namespace Renderer { class ShaderManager; } }

namespace Tina::ECS {

// 颜色顶点结构（与TileRenderer一致）
struct ColorVertex {
    float x, y, z;
    float r, g, b, a;
};

class CharacterRenderSystem {
public:
    bool initialize(Tina::Renderer::ShaderManager& shaders);
    void render(entt::registry& registry, uint16_t viewId);

private:
    // 渲染单个AABB
    void renderAABB(float x, float y, float width, float height,
                    float r, float g, float b, float a,
                    uint16_t viewId) const;

    bgfx::ProgramHandle m_prog = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout{};
};

} // namespace Tina::ECS
