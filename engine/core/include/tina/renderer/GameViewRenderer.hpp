#pragma once

#include "tina/core/Core.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include <vector>
#include <unordered_map>
#include <entt/entity/registry.hpp>

namespace Tina
{
    class Scene;

    class TINA_CORE_API GameViewRenderer
    {
    public:
        GameViewRenderer();
        ~GameViewRenderer() = default;

        void render(Scene* scene, Renderer2D& renderer);
        
    private:
        // 实体状态缓存
        struct EntityState {
            glm::vec2 lastPosition{0.0f};
            glm::vec2 lastSize{0.0f};
            bool needsUpdate{true};
        };

        // 渲染单个实体
        void renderEntity(entt::registry& registry, entt::entity entity, Renderer2D& renderer);
        
        // 收集和排序可渲染实体
        void collectRenderables(Scene* scene);
        
        // 更新实体状态
        void updateEntityState(entt::entity entity, const glm::vec2& position, const glm::vec2& size);
        
        // 清理无效实体的缓存
        void cleanupEntityStates(entt::registry& registry);

        // 缓存的排序实体数组
        std::vector<std::pair<int, entt::entity>> m_SortedEntities;
        
        // 实体状态缓存
        std::unordered_map<entt::entity, EntityState> m_EntityStates;
    };
} // namespace Tina 