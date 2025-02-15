//
// Created by wuxianggujun on 2025/2/13.
//

#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/scene/Scene.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/components/RectangleComponent.hpp"
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
                // 设置viewId
                // m_render2DLayer->setViewId(Core::Engine::get().allocateViewId());
                m_scene.pushLayer(m_render2DLayer);
            }
        }

        SpriteComponent& createSprite(const std::string& name, const std::string& texturePath)
        {
            // 检查Render2DLayer是否有效
            if (!m_render2DLayer)
            {
                TINA_LOG_ERROR("Render2DLayer is not initialized!");
                throw std::runtime_error("Render2DLayer is not initialized");
            }

            // 加载纹理
            std::shared_ptr<Texture2D> texture = m_render2DLayer->loadTexture(name, texturePath);
            if (!texture)
            {
                TINA_LOG_ERROR("Failed to load texture for sprite: {}", name);
                throw std::runtime_error("Failed to load texture");
            }

            // 创建实体和组件
            auto entity = m_scene.createEntity(name);

            // 添加Transform组件
            auto& transform = m_scene.addComponent<Transform2DComponent>(entity);

            // 添加Sprite组件,将texture作为构造参数传入
            auto& sprite = m_scene.addComponent<SpriteComponent>(entity, texture);

            // 设置Transform引用
            sprite.setTransform(&transform);

            return sprite;
        }

        // 创建矩形
        RectangleComponent& createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
        {
            auto entity = m_scene.createEntity("Rectangle");

            // 添加Transform组件
            auto& transform = m_scene.addComponent<Transform2DComponent>(entity);
            transform.setPosition(position);

            // 添加Rectangle组件
            auto& rectangle = m_scene.addComponent<RectangleComponent>(entity);
            rectangle.setSize(size);
            rectangle.setColor(color);
            rectangle.setTransform(&transform); // 设置Transform引用

            return rectangle;
        }

        // 在GameLayer中管理精灵
        void setSpritePriority(const std::string& name, int layer)
        {
            auto view = m_scene.getRegistry().view<SpriteComponent>();
            for (auto entity : view)
            {
                if (m_scene.getEntityName(entity) == name)
                {
                    auto& sprite = view.get<SpriteComponent>(entity);
                    sprite.setLayer(layer);
                    break;
                }
            }
        }

        // 动态改变渲染顺序
        void bringToFront(const std::string& name)
        {
            auto view = m_scene.getRegistry().view<SpriteComponent>();
            int maxLayer = 0;

            // 找到当前最高层
            for (auto entity : view)
            {
                auto& sprite = view.get<SpriteComponent>(entity);
                maxLayer = std::max(maxLayer, sprite.getLayer());
            }

            // 将指定精灵移到最上层
            setSpritePriority(name, maxLayer + 1);
        }

        Scene& getScene() { return m_scene; }
        const Scene& getScene() const { return m_scene; }

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
