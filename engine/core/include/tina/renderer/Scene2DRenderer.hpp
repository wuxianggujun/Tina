//
// Created by wuxianggujun on 2025/2/14.
//

#pragma once

#include "tina/core/Core.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include <vector>
#include <unordered_map>
#include <entt/entity/registry.hpp>

/*
namespace Tina
{
    class Scene;

    class TINA_CORE_API Scene2DRenderer
    {
    public:
        Scene2DRenderer();
        
        void render(Scene* scene, Renderer2D& renderer);
        void sortRenderables(Scene* scene, std::vector<std::pair<int, entt::entity>>& entities);
        void renderEntity(entt::registry& registry, entt::entity entity, Renderer2D& renderer);

    private:
        // 实体状态缓存
        struct EntityState {
            glm::vec2 lastPosition{0.0f};
            glm::vec2 lastSize{0.0f};
        };
        
        // 缓存的排序实体数组
        std::vector<std::pair<int, entt::entity>> m_SortedEntities;
        
        // 实体状态缓存
        std::unordered_map<entt::entity, EntityState> m_EntityStates;
    };
} // namespace Tina
*/
