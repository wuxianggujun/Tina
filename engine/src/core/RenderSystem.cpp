#include "RenderSystem.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <bgfx/bgfx.h>
#include <bx/uint32_t.h>

#include "components/SpriteComponent.hpp"

namespace Tina {

void RenderSystem::initialize() {
    if (m_initialized) {
        fmt::print("RenderSystem already initialized\n");
        return;
    }

    // 初始化2D渲染器
    m_renderer2D = std::make_unique<Renderer2D>(0);
    m_renderer2D->initialize();

    m_initialized = true;
    fmt::print("RenderSystem initialized successfully\n");
}

void RenderSystem::shutdown() {
    if (!m_initialized) {
        return;
    }

    // 释放渲染器
    if (m_renderer2D) {
        m_renderer2D.reset();
    }

    m_initialized = false;
    fmt::print("RenderSystem shutdown completed\n");
}

void RenderSystem::beginFrame() {
    if (!m_initialized) {
        fmt::print("RenderSystem not initialized\n");
        return;
    }

    // 设置默认视图
    bgfx::setViewClear(0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        m_clearColor.toABGR(),
        1.0f,
        0
    );

    // 如果有活动相机，更新视图和投影矩阵
    if (m_activeCamera) {
        bgfx::setViewTransform(0, 
            &m_activeCamera->getViewMatrix()[0][0],
            &m_activeCamera->getProjectionMatrix()[0][0]);
            
        // 设置Renderer2D的相机
        if (m_renderer2D) {
            m_renderer2D->setCamera(m_activeCamera);
        }
    }
}

void RenderSystem::render(entt::registry& registry) {
    if (!m_initialized || !m_renderer2D) {
        return;
    }

    // 开始2D渲染器的批处理
    m_renderer2D->begin();

    // 按层级顺序渲染
    renderLayer(registry, RenderLayer::Background);
    renderLayer(registry, RenderLayer::Default);
    renderLayer(registry, RenderLayer::EntityLow);
    renderLayer(registry, RenderLayer::EntityMid);
    renderLayer(registry, RenderLayer::EntityHigh);
    renderLayer(registry, RenderLayer::UI);
    renderLayer(registry, RenderLayer::UIHigh);
    renderLayer(registry, RenderLayer::Overlay);
    renderLayer(registry, RenderLayer::Debug);

    // 结束2D渲染器的批处理
    m_renderer2D->end();
}

void RenderSystem::renderLayer(entt::registry& registry, RenderLayer layer) {
    // 获取当前层的所有实体
    const auto& entities = m_layerManager.getEntitiesInLayer(layer);

    // 渲染精灵
    for (auto entity : entities) {
        if (registry.all_of<TransformComponent, SpriteComponent>(entity)) {
            const auto& transform = registry.get<TransformComponent>(entity);
            const auto& spriteComp = registry.get<SpriteComponent>(entity);

            if (!spriteComp.visible) continue;

            // 更新精灵的变换
            Sprite& sprite = const_cast<Sprite&>(spriteComp.sprite);
            sprite.setPosition(transform.position);
            sprite.setRotation(transform.rotation);
            sprite.setScale(transform.scale);

            // 绘制精灵
            m_renderer2D->drawSprite(sprite);
        }
        else if (registry.all_of<TransformComponent, QuadRendererComponent>(entity)) {
            const auto& transform = registry.get<TransformComponent>(entity);
            const auto& quad = registry.get<QuadRendererComponent>(entity);

            if (!quad.visible) continue;

            m_renderer2D->drawRect(
                transform.position,
                quad.size * transform.scale,
                quad.color
            );
        }
    }
}

void RenderSystem::endFrame() {
    if (!m_initialized) {
        return;
    }

    // 提交帧
    bgfx::frame();
}

void RenderSystem::setViewport(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    if (!m_initialized) {
        return;
    }
    bgfx::setViewRect(0, x, y, width, height);
}

void RenderSystem::setClearColor(const Color& color) {
    m_clearColor = color;
}

} 