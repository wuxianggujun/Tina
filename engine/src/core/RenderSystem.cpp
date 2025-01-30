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


    // 渲染所有带有 SpriteComponent 的实体
    auto spriteView = registry.view<const TransformComponent, const SpriteComponent>();

    // 按深度排序
    std::vector<entt::entity> sortedEntities;
    for (auto entity : spriteView) {
        sortedEntities.push_back(entity);
    }

    std::sort(sortedEntities.begin(), sortedEntities.end(),
        [&registry](entt::entity a, entt::entity b) {
            return registry.get<const SpriteComponent>(a).depth <
                   registry.get<const SpriteComponent>(b).depth;
        });

    // 渲染精灵
    for (auto entity : sortedEntities) {
        const auto& transform = registry.get<const TransformComponent>(entity);
        const auto& spriteComp = registry.get<const SpriteComponent>(entity);

        if (!spriteComp.visible) continue;

        // 更新精灵的变换
        Sprite& sprite = const_cast<Sprite&>(spriteComp.sprite);
        sprite.setPosition(transform.position);
        sprite.setRotation(transform.rotation);
        sprite.setScale(transform.scale);

        // 绘制精灵
        m_renderer2D->drawSprite(sprite);
    }

    // 按组件类型分别渲染
    renderSprites(registry);
    renderQuads(registry);
    renderCustom(registry);

    // 结束2D渲染器的批处理
    m_renderer2D->end();
}

template<typename Component>
std::vector<entt::entity> RenderSystem::sortEntitiesByDepth(entt::registry& registry) {
    auto view = registry.view<TransformComponent, Component>();
    std::vector<entt::entity> sortedEntities;
    
    // 使用迭代器获取实体数量
    size_t entityCount = 0;
    for (auto entity : view) {
        entityCount++;
    }
    sortedEntities.reserve(entityCount);

    // 收集实体
    for (auto entity : view) {
        sortedEntities.push_back(entity);
    }

    // 按深度排序
    std::sort(sortedEntities.begin(), sortedEntities.end(),
        [&registry](const entt::entity& a, const entt::entity& b) {
            const auto& compA = registry.get<Component>(a);
            const auto& compB = registry.get<Component>(b);
            return compA.depth < compB.depth;
        });

    return sortedEntities;
}

void RenderSystem::renderSprites(entt::registry& registry) {
    auto sortedEntities = sortEntitiesByDepth<SpriteRendererComponent>(registry);
    
    for (auto entity : sortedEntities) {
        const auto& transform = registry.get<TransformComponent>(entity);
        const auto& sprite = registry.get<SpriteRendererComponent>(entity);
        
        if (sprite.visible && m_renderer2D) {
            m_renderer2D->drawTexturedRect(
                transform.position,
                sprite.size * transform.scale,
                sprite.texture,
                sprite.color
            );
        }
    }
}

void RenderSystem::renderQuads(entt::registry& registry) {
    auto sortedEntities = sortEntitiesByDepth<QuadRendererComponent>(registry);
    
    for (auto entity : sortedEntities) {
        const auto& transform = registry.get<TransformComponent>(entity);
        const auto& quad = registry.get<QuadRendererComponent>(entity);
        
        if (quad.visible && m_renderer2D) {
            m_renderer2D->drawRect(
                transform.position,
                quad.size * transform.scale,
                quad.color
            );
        }
    }
}

void RenderSystem::renderCustom(entt::registry& registry) {
    auto sortedEntities = sortEntitiesByDepth<CustomRendererComponent>(registry);
    
    for (auto entity : sortedEntities) {
        const auto& custom = registry.get<CustomRendererComponent>(entity);
        
        if (custom.visible && custom.renderFunction) {
            custom.renderFunction();
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