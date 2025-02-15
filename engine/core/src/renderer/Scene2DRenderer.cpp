//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/Scene2DRenderer.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/components/SpriteComponent.hpp"

namespace Tina
{
    Scene2DRenderer::Scene2DRenderer()
    {
        // 预分配排序实体数组
        m_SortedEntities.reserve(1000);
        
        // 初始化实体状态缓存
        m_EntityStates.reserve(1000);
        
        TINA_LOG_DEBUG("Scene2DRenderer initialized with capacity for 1000 entities");
    }

    void Scene2DRenderer::render(Scene* scene, Renderer2D& renderer)
    {
        if (!scene)
            return;

        TINA_PROFILE_FUNCTION();
        
        // 清理并预分配向量
        m_SortedEntities.clear();
        
        // 收集并排序所有要渲染的实体
        sortRenderables(scene, m_SortedEntities);
        
        // 输出渲染统计
        TINA_LOG_DEBUG("Rendering {} entities, Cache size: {} entries, Memory: {}KB",
            m_SortedEntities.size(),
            m_EntityStates.size(),
            (m_EntityStates.size() * sizeof(EntityState) + 
             m_SortedEntities.capacity() * sizeof(std::pair<int, entt::entity>)) / 1024);

        // 渲染排序后的实体
        for (const auto& [layer, entity] : m_SortedEntities)
        {
            auto& registry = scene->getRegistry();
            if (!registry.valid(entity)) 
            {
                // 如果实体无效,清理其缓存
                m_EntityStates.erase(entity);
                continue;
            }

            auto& sprite = registry.get<SpriteComponent>(entity);
            if (!sprite.isVisible()) continue;

            renderEntity(registry, entity, renderer);
        }
        
        // 清理已经不存在的实体的缓存
        for (auto it = m_EntityStates.begin(); it != m_EntityStates.end();)
        {
            if (!scene->getRegistry().valid(it->first))
            {
                it = m_EntityStates.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Scene2DRenderer::sortRenderables(Scene* scene, std::vector<std::pair<int, entt::entity>>& entities)
    {
        auto& registry = scene->getRegistry();
        auto view = registry.view<Transform2DComponent, SpriteComponent>();

        view.each([&entities](entt::entity entity, const Transform2DComponent& transform, const SpriteComponent& sprite)
        {
            if (sprite.isVisible())
            {
                entities.emplace_back(sprite.getLayer(), entity);
            }
        });

        std::sort(entities.begin(), entities.end());
    }

    void Scene2DRenderer::renderEntity(entt::registry& registry, entt::entity entity, Renderer2D& renderer)
    {
        auto& transform = registry.get<Transform2DComponent>(entity);
        auto& sprite = registry.get<SpriteComponent>(entity);

        // 获取或创建实体状态
        auto& state = m_EntityStates[entity];
        
        // 检查是否需要更新
        glm::vec2 currentPos = transform.getPosition();
        glm::vec2 currentSize = sprite.getSize() * transform.getScale();

        if (currentPos != state.lastPosition || currentSize != state.lastSize)
        {
            sprite.setTransform(&transform);
            sprite.updateRenderData();
            
            state.lastPosition = currentPos;
            state.lastSize = currentSize;
            
            TINA_LOG_TRACE("Updated entity {} transform - Pos: ({}, {}), Size: ({}, {})",
                static_cast<uint32_t>(entity),
                currentPos.x, currentPos.y,
                currentSize.x, currentSize.y);
        }

        if (sprite.getTexture() && sprite.getTexture()->isValid())
        {
            renderer.drawSprite(transform.getPosition(),
                              sprite.getSize() * transform.getScale(),
                              sprite.getTexture(),
                              sprite.getTextureRect(),
                              sprite.getColor());
        }
    }
} // Tina
