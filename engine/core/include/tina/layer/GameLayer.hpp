//
// Created by wuxianggujun on 2025/2/13.
//

#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/scene/Scene.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/layer/Render2DLayer.hpp"
#include "tina/core/Engine.hpp"

namespace Tina
{
    class GameLayer : public Layer
    {
    public:
        GameLayer(Scene& scene)
            : Layer("GameLayer")
            , m_scene(scene)
        {
            m_render2DLayer = findRender2DLayer();
            if (!m_render2DLayer)
            {
                m_render2DLayer = new Render2DLayer();
                m_scene.pushLayer(m_render2DLayer);
            }
        }

        SpriteComponent& createSprite(const std::string& name, const std::string& texturePath)
        {
            auto entity = m_scene.createEntity(name);

            auto& transform = m_scene.addComponent<Transform2DComponent>(entity);
            auto& sprite = m_scene.addComponent<SpriteComponent>(entity);

            if (m_render2DLayer)
            {
                auto texture = m_render2DLayer->loadTexture(name, texturePath);
                if (texture)
                {
                    sprite.setTexture(texture);
                }
            }
            return sprite;
        }

        // 在GameLayer中管理精灵
        void setSpritePriority(const std::string& name, int layer) {
            auto view = m_scene.getRegistry().view<SpriteComponent>();
            for (auto entity : view) {
                if (m_scene.getEntityName(entity) == name) {
                    auto& sprite = view.get<SpriteComponent>(entity);
                    sprite.setLayer(layer);
                    break;
                }
            }
        }

        // 动态改变渲染顺序
        void bringToFront(const std::string& name) {
            auto view = m_scene.getRegistry().view<SpriteComponent>();
            int maxLayer = 0;

            // 找到当前最高层
            for (auto entity : view) {
                auto& sprite = view.get<SpriteComponent>(entity);
                maxLayer = std::max(maxLayer, sprite.getLayer());
            }

            // 将指定精灵移到最上层
            setSpritePriority(name, maxLayer + 1);
        }

    protected:
        Scene& m_scene;
        Render2DLayer* m_render2DLayer = nullptr;

    private:
        Render2DLayer* findRender2DLayer()
        {
            for (auto* layer : m_scene.getLayerStack())
            {
                if (auto* render2D = dynamic_cast<Render2DLayer*>(layer))
                {
                    return render2D;
                }
            }
            return nullptr;
        }
    };
} // Tina
