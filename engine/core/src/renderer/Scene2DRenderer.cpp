//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/Scene2DRenderer.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/components/SpriteComponent.hpp"

namespace Tina
{
    void Scene2DRenderer::render(Scene* scene, Renderer2D& renderer)
    {
        if (!scene)
            return;

        std::vector<std::pair<int, entt::entity>> sortedEntities;
        sortedEntities.clear();

        // 收集并排序所有要渲染的实体
        sortRenderables(scene, sortedEntities);

        // 渲染排序后的实体，只渲染一次
        for (const auto& [layer, entity] : sortedEntities)
        {
            auto& registry = scene->getRegistry();
            if (!registry.valid(entity)) continue;

            auto& sprite = registry.get<SpriteComponent>(entity);
            if (!sprite.isVisible()) continue;

            renderEntity(registry, entity, renderer);
        }
    }

    void Scene2DRenderer::sortRenderables(Scene* scene, std::vector<std::pair<int, entt::entity>>& entitles)
    {
        auto& registry = scene->getRegistry();

        auto view = registry.view<Transform2DComponent, SpriteComponent>();

        entitles.clear();

        view.each([&entitles](entt::entity entity, const Transform2DComponent& transform, const SpriteComponent& sprite)
        {
            if (sprite.isVisible())
            {
                entitles.emplace_back(sprite.getLayer(), entity);
            }
        });

        std::sort(entitles.begin(), entitles.end());
    }

    void Scene2DRenderer::renderEntity(entt::registry& registry, entt::entity entity, Renderer2D& renderer)
    {
        auto& transform = registry.get<Transform2DComponent>(entity);
        auto& sprite = registry.get<SpriteComponent>(entity);

        // 只在必要时更新
        static glm::vec2 lastPos;
        static glm::vec2 lastSize;

        glm::vec2 currentPos = transform.getPosition();
        glm::vec2 currentSize = sprite.getSize() * transform.getScale();

        if (currentPos != lastPos || currentSize != lastSize)
        {
            sprite.setTransform(&transform);
            sprite.updateRenderData();
            lastPos = currentPos;
            lastSize = currentSize;
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
