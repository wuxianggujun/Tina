#include "tina/renderer/GameViewRenderer.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/components/RectangleComponent.hpp"
#include "tina/log/Logger.hpp"
#include "tina/utils/Profiler.hpp"
#include "tina/scene/Scene.hpp"

namespace Tina
{
    GameViewRenderer::GameViewRenderer()
    {
        // 预分配排序实体数组
        m_SortedEntities.reserve(1000);
        
        // 初始化实体状态缓存
        m_EntityStates.reserve(1000);
        
        TINA_LOG_DEBUG("GameViewRenderer initialized with capacity for 1000 entities");
    }

    void GameViewRenderer::render(Scene* scene, Renderer2D& renderer)
    {
        if (!scene) return;

        TINA_PROFILE_FUNCTION();
        
        // 收集和排序可渲染实体
        collectRenderables(scene);
        
        // 输出渲染统计
        TINA_LOG_DEBUG("Rendering {} entities, Cache size: {} entries",
            m_SortedEntities.size(),
            m_EntityStates.size());

        auto& registry = scene->getRegistry();

        // 按层渲染实体
        for (const auto& [layer, entity] : m_SortedEntities)
        {
            const auto& transform = registry.get<Transform2DComponent>(entity);

            // 处理精灵组件
            if (auto* sprite = registry.try_get<SpriteComponent>(entity))
            {
                if (sprite->isVisible() && sprite->getTexture() && sprite->getTexture()->isValid())
                {
                    glm::vec2 position = transform.getPosition();
                    glm::vec2 size = sprite->getSize() * transform.getScale();
                    
                    // 更新实体状态
                    updateEntityState(entity, position, size);
                    
                    // 如果需要更新
                    if (m_EntityStates[entity].needsUpdate)
                    {
                        sprite->updateRenderData();
                        m_EntityStates[entity].needsUpdate = false;
                    }

                    // 渲染精灵
                    renderer.drawSprite(
                        position,
                        size,
                        sprite->getTexture(),
                        sprite->getTextureRect(),
                        sprite->getColor()
                    );
                }
            }
            // 处理矩形组件
            else if (auto* rect = registry.try_get<RectangleComponent>(entity))
            {
                if (rect->isVisible())
                {
                    glm::vec2 position = transform.getPosition();
                    glm::vec2 size = rect->getSize() * transform.getScale();
                    
                    // 更新实体状态
                    updateEntityState(entity, position, size);
                    
                    // 如果需要更新
                    if (m_EntityStates[entity].needsUpdate)
                    {
                        rect->updateRenderData();
                        m_EntityStates[entity].needsUpdate = false;
                    }

                    // 渲染矩形
                    renderer.drawQuad(position, size, rect->getColor());
                }
            }
        }
        
        // 清理无效实体的状态
        cleanupEntityStates(registry);
    }

    void GameViewRenderer::collectRenderables(Scene* scene)
    {
        m_SortedEntities.clear();
        
        auto& registry = scene->getRegistry();
        
        // 收集精灵组件
        auto spriteView = registry.view<Transform2DComponent, SpriteComponent>();
        spriteView.each([this](entt::entity entity, const Transform2DComponent&, const SpriteComponent& sprite)
        {
            if (sprite.isVisible())
            {
                m_SortedEntities.emplace_back(sprite.getLayer(), entity);
            }
        });

        // 收集矩形组件
        auto rectView = registry.view<Transform2DComponent, RectangleComponent>();
        rectView.each([this](entt::entity entity, const Transform2DComponent&, const RectangleComponent& rect)
        {
            if (rect.isVisible())
            {
                m_SortedEntities.emplace_back(rect.getLayer(), entity);
            }
        });

        // 按层排序
        std::sort(m_SortedEntities.begin(), m_SortedEntities.end());
    }

    void GameViewRenderer::updateEntityState(entt::entity entity, const glm::vec2& position, const glm::vec2& size)
    {
        auto& state = m_EntityStates[entity];
        
        // 检查位置或大小是否改变
        if (position != state.lastPosition || size != state.lastSize)
        {
            state.lastPosition = position;
            state.lastSize = size;
            state.needsUpdate = true;
            
            TINA_LOG_TRACE("Entity {} transform updated - Pos: ({}, {}), Size: ({}, {})",
                static_cast<uint32_t>(entity),
                position.x, position.y,
                size.x, size.y);
        }
    }

    void GameViewRenderer::cleanupEntityStates(entt::registry& registry)
    {
        for (auto it = m_EntityStates.begin(); it != m_EntityStates.end();)
        {
            if (!registry.valid(it->first))
            {
                TINA_LOG_TRACE("Removing invalid entity {} from cache", static_cast<uint32_t>(it->first));
                it = m_EntityStates.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
} // namespace Tina 