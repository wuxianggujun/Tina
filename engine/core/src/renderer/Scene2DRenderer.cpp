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

        auto& registry = scene->getRegistry();
        std::vector<std::pair<int, entt::entity>> sortedEntities;

        // 收集并排序所有要渲染的实体
        sortRenderables(scene, sortedEntities);

        // 渲染排序后的实体
        for (const auto& [layer, entity] : sortedEntities)
        {
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

    void Scene2DRenderer::renderEntity(const entt::registry& registry, entt::entity entity, Renderer2D& renderer)
    {
        const auto& transform = registry.get<Transform2DComponent>(entity);
        const auto& sprite = registry.get<SpriteComponent>(entity);

        if (sprite.getTexture() && sprite.getTexture()->isValid())
        {
            renderer.drawSprite(transform.getPosition(), sprite.getSize() * transform.getScale(),
                                sprite.getTexture(), sprite.getTextureRect(), sprite.getColor());
        }
        else
        {
            renderer.drawQuad(transform.getPosition(), sprite.getSize() * transform.getScale(), sprite.getColor());
        }
    }
} // Tina
