//
// Created by wuxianggujun on 2025/2/14.
//

#pragma once

#include "tina/core/Core.hpp"
#include "tina/scene/Scene.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include <vector>

namespace Tina
{
    class TINA_CORE_API Scene2DRenderer
    {
    public:
        void render(Scene* scene, Renderer2D& renderer);
        void sortRenderables(Scene* scene, std::vector<std::pair<int, entt::entity>>& entities);
        void renderEntity(entt::registry& registry, entt::entity entity, Renderer2D& renderer);
    };
} // Tina
