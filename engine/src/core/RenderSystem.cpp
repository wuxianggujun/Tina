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

    // 重置视图矩形
    const uint32_t width = bgfx::getStats()->width;
    const uint32_t height = bgfx::getStats()->height;
    bgfx::setViewRect(0, 0, 0, width, height);

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

    // 设置默认渲染状态
    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_CULL_CW
        | BGFX_STATE_MSAA
        | BGFX_STATE_BLEND_SRC_ALPHA;

    bgfx::setState(state);
}

void RenderSystem::render(entt::registry& registry)
{
    if (!m_initialized || !m_renderer2D)
    {
        return;
    }

    // 按层级顺序渲染
    renderLayer(registry, RenderLayer::Background);
    renderLayer(registry, RenderLayer::EntityLow);
    renderLayer(registry, RenderLayer::EntityMid);
    renderLayer(registry, RenderLayer::EntityHigh);
    renderLayer(registry, RenderLayer::UI);
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

float RenderSystem::getDepthFromLayer(RenderLayer layer)
{
    // 将层级映射到深度值范围 [0.0, 1.0]
    // Background 最远 (1.0)，UI 最近 (0.0)
    switch (layer)
    {
        case RenderLayer::Background: return 0.99f;  // 接近远平面但不完全是1.0
        case RenderLayer::EntityLow:  return 0.75f;
        case RenderLayer::EntityMid:  return 0.5f;
        case RenderLayer::EntityHigh: return 0.25f;
        case RenderLayer::UI:         return 0.01f;  // 接近近平面但不完全是0.0
        default:                      return 0.5f;
    }
}

void RenderSystem::renderLayer(entt::registry& registry, RenderLayer layer)
{
    fmt::print("\n=== Rendering Layer: {} ===\n", static_cast<int>(layer));
    
    // 开始新的批次
    m_renderer2D->begin();

    // 获取该层级的所有实体
    std::vector<entt::entity> entities;
    
    // 处理矩形实体
    auto quadView = registry.view<TransformComponent, QuadRendererComponent>();
    for (auto entity : quadView)
    {
        const auto& quadComp = quadView.get<QuadRendererComponent>(entity);
        if (quadComp.layer == layer)
        {
            entities.push_back(entity);
            fmt::print("Found quad entity {} in layer {}, visible: {}\n",
                static_cast<uint32_t>(entity),
                static_cast<int>(layer),
                quadComp.visible);
        }
    }

    // 处理精灵实体
    auto spriteView = registry.view<TransformComponent, SpriteComponent>();
    for (auto entity : spriteView)
    {
        const auto& spriteComp = spriteView.get<SpriteComponent>(entity);
        if (spriteComp.layer == layer)
        {
            entities.push_back(entity);
            fmt::print("Found sprite entity {} in layer {}, visible: {}, has texture: {}\n",
                static_cast<uint32_t>(entity),
                static_cast<int>(layer),
                spriteComp.visible,
                spriteComp.sprite.getTexture().isValid());
        }
    }

    // 如果这个层级有实体需要渲染
    if (!entities.empty())
    {
        fmt::print("Rendering {} entities in layer {}\n", entities.size(), static_cast<int>(layer));

        // 按深度排序
        std::sort(entities.begin(), entities.end(),
            [&registry](entt::entity a, entt::entity b) {
                float depthA = 0.0f;
                float depthB = 0.0f;
                
                if (registry.all_of<SpriteComponent>(a))
                    depthA = registry.get<SpriteComponent>(a).depth;
                if (registry.all_of<QuadRendererComponent>(a))
                    depthA = registry.get<QuadRendererComponent>(a).depth;
                    
                if (registry.all_of<SpriteComponent>(b))
                    depthB = registry.get<SpriteComponent>(b).depth;
                if (registry.all_of<QuadRendererComponent>(b))
                    depthB = registry.get<QuadRendererComponent>(b).depth;
                    
                return depthA < depthB;
            });

        // 获取当前层级的基础深度值
        float baseDepth = getDepthFromLayer(layer);

        // 渲染该层级的所有实体
        for (auto entity : entities)
        {
            const auto& transform = registry.get<TransformComponent>(entity);

            // 渲染矩形
            if (registry.all_of<QuadRendererComponent>(entity))
            {
                const auto& quadComp = registry.get<QuadRendererComponent>(entity);
                if (!quadComp.visible) continue;

                fmt::print("Drawing quad entity {} at position ({}, {})\n",
                    static_cast<uint32_t>(entity),
                    transform.position.x,
                    transform.position.y);

                // 计算最终深度值
                float finalDepth = baseDepth + quadComp.depth * 0.01f;  // 在层内微调深度

                m_renderer2D->drawRect(
                    transform.position,
                    quadComp.size * transform.scale,
                    quadComp.color,
                    finalDepth
                );
            }
            // 渲染精灵
            else if (registry.all_of<SpriteComponent>(entity))
            {
                const auto& spriteComp = registry.get<SpriteComponent>(entity);
                if (!spriteComp.visible) continue;

                fmt::print("Drawing sprite entity {} at position ({}, {}), rotation: {}, has texture: {}\n",
                    static_cast<uint32_t>(entity),
                    transform.position.x,
                    transform.position.y,
                    transform.rotation,
                    spriteComp.sprite.getTexture().isValid());

                // 更新精灵的变换
                Sprite& sprite = const_cast<Sprite&>(spriteComp.sprite);
                sprite.setPosition(transform.position);
                sprite.setRotation(transform.rotation);
                sprite.setScale(transform.scale);

                // 计算最终深度值
                float finalDepth = baseDepth + spriteComp.depth * 0.01f;  // 在层内微调深度

                // 绘制精灵，使用计算出的深度值
                m_renderer2D->drawSprite(
                    sprite.getPosition(),
                    sprite.getSize(),
                    sprite.getColor(),
                    sprite.getTexture(),
                    finalDepth
                );
            }
        }
    }
    else
    {
        fmt::print("No entities to render in layer {}\n", static_cast<int>(layer));
    }

    // 结束这个层级的批次
    m_renderer2D->end();
    
    fmt::print("=== Layer {} Rendering Complete ===\n\n", static_cast<int>(layer));
}

} 