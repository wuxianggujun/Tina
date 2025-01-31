#pragma once

#include <entt/entt.hpp>
#include "graphics/Color.hpp"
#include "math/Vector.hpp"
#include "components/RenderComponents.hpp"
#include "components/SpriteComponent.hpp"

namespace Tina {

class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    // 创建2D图形的便捷方法
    entt::entity createQuad(const Vector2f& position, const Vector2f& size, const Color& color, RenderLayer layer = RenderLayer::Default) {
        auto entity = m_registry.create();
        
        // 自动创建必要的组件
        auto& transform = m_registry.emplace<TransformComponent>(entity);
        transform.position = position;
        
        auto& quad = m_registry.emplace<QuadRendererComponent>(entity);
        quad.color = color;
        quad.size = size;
        quad.layer = layer;  // 设置渲染层级
        quad.depth = getDepthFromLayer(layer);  // 设置深度值
        quad.visible = true;
        
        return entity;
    }

    // 创建精灵的便捷方法
    entt::entity createSprite(const Vector2f& position, const Vector2f& size, bgfx::TextureHandle texture, const Color& tint = Color::White, RenderLayer layer = RenderLayer::Default) {
        auto entity = m_registry.create();
        
        auto& transform = m_registry.emplace<TransformComponent>(entity);
        transform.position = position;
        
        auto& sprite = m_registry.emplace<SpriteRendererComponent>(entity);
        sprite.texture = texture;
        sprite.size = size;
        sprite.color = tint;
        sprite.layer = layer;  // 设置渲染层级
        
        return entity;
    }

    // 创建精灵实体
    entt::entity createSprite(const Texture& texture, const Vector2f& position,
                             const Vector2f& size = Vector2f(1.0f, 1.0f),
                             const Color& color = Color::White,
                             RenderLayer layer = RenderLayer::Default)
    {
        auto entity = m_registry.create();

        // 添加变换组件
        auto& transform = m_registry.emplace<TransformComponent>(entity);
        transform.position = position;
        transform.scale = size;

        // 添加精灵组件
        auto& spriteComp = m_registry.emplace<SpriteComponent>(entity);
        spriteComp.sprite.setTexture(texture);
        spriteComp.sprite.setColor(color);
        spriteComp.layer = layer;  // 设置层级
        spriteComp.depth = getDepthFromLayer(layer);  // 设置深度值
        spriteComp.visible = true;

        return entity;
    }

    // 创建动画精灵
    entt::entity createAnimatedSprite(const Texture& spriteSheet,
                                    const Vector2f& position,
                                    const Rectf& initialFrame)
    {
        auto entity = m_registry.create();

        auto& transform = m_registry.emplace<TransformComponent>(entity);
        transform.position = position;

        auto& spriteComp = m_registry.emplace<SpriteComponent>(entity);
        spriteComp.sprite.setTexture(spriteSheet);
        spriteComp.sprite.setTextureRect(initialFrame);

        // 可以添加动画组件
        // auto& animComp = m_registry.emplace<AnimationComponent>(entity);

        return entity;
    }

    // 实体操作方法
    void destroyEntity(entt::entity entity) {
        m_registry.destroy(entity);
    }

    // 清理所有实体
    void clear() {
        m_registry.clear();
    }

    // 获取组件
    template<typename T>
    T& getComponent(entt::entity entity) {
        return m_registry.get<T>(entity);
    }

    // 设置实体位置
    void setPosition(entt::entity entity, const Vector2f& position) {
        auto& transform = m_registry.get<TransformComponent>(entity);
        transform.position = position;
    }

    // 设置实体缩放
    void setScale(entt::entity entity, const Vector2f& scale) {
        auto& transform = m_registry.get<TransformComponent>(entity);
        transform.scale = scale;
    }

    // 设置实体旋转
    void setRotation(entt::entity entity, float rotation) {
        auto& transform = m_registry.get<TransformComponent>(entity);
        transform.rotation = rotation;
    }

    // 设置颜色
    void setColor(entt::entity entity, const Color& color) {
        if (m_registry.all_of<QuadRendererComponent>(entity)) {
            auto& quad = m_registry.get<QuadRendererComponent>(entity);
            quad.color = color;
        }
        else if (m_registry.all_of<SpriteRendererComponent>(entity)) {
            auto& sprite = m_registry.get<SpriteRendererComponent>(entity);
            sprite.color = color;
        }
    }

    // 获取注册表（供渲染系统使用）
    entt::registry& getRegistry() { return m_registry; }
    const entt::registry& getRegistry() const { return m_registry; }

private:
    entt::registry m_registry;
};

} // namespace Tina 