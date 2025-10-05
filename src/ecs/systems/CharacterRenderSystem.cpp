//
// 角色渲染系统实现
//

#include "CharacterRenderSystem.hpp"
#include <bgfx/bgfx.h>

namespace Tina::ECS {

void CharacterRenderSystem::renderAABB(float x, float y, float width, float height,
                                       float r, float g, float b, float a,
                                       uint16_t viewId, bgfx::ProgramHandle program,
                                       const bgfx::VertexLayout& layout) const {
    // 创建AABB的顶点数据（矩形）
    ColorVertex vertices[4] = {
        { x,         y,          0.0f, r, g, b, a },  // 左下
        { x + width, y,          0.0f, r, g, b, a },  // 右下
        { x + width, y + height, 0.0f, r, g, b, a },  // 右上
        { x,         y + height, 0.0f, r, g, b, a }   // 左上
    };

    uint16_t indices[6] = {
        0, 1, 2,  // 第一个三角形
        0, 2, 3   // 第二个三角形
    };

    // 创建临时顶点和索引缓冲区
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer  tib;

    if (bgfx::allocTransientBuffers(&tvb, layout, 4, &tib, 6)) {
        // 复制数据
        memcpy(tvb.data, vertices, sizeof(vertices));
        memcpy(tib.data, indices, sizeof(indices));

        // 设置状态和渲染
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(viewId, program);
    }
}

void CharacterRenderSystem::render(entt::registry& registry, uint16_t viewId,
                                   bgfx::ProgramHandle program, const bgfx::VertexLayout& layout) {
    // 遍历所有可渲染的角色
    auto view = registry.view<Transform, PhysicsBody, Renderable>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& physics = view.get<PhysicsBody>(entity);
        auto& renderable = view.get<Renderable>(entity);

        if (!renderable.visible) continue;

        // 渲染角色AABB
        renderAABB(transform.x, transform.y, physics.width, physics.height,
                   renderable.r, renderable.g, renderable.b, renderable.a,
                   viewId, program, layout);
    }
}

} // namespace Tina::ECS
