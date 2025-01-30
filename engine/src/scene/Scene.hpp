#pragma once

#include <entt/entt.hpp>
#include "graphics/Color.hpp"
#include "math/Vector.hpp"
#include "core/components/RenderComponents.hpp"

namespace Tina {

class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    // 创建2D图形的便捷方法
    entt::entity createQuad(const Vector2f& position, const Vector2f& size, const Color& color) {
        auto entity = m_registry.create();
        
        // 自动创建必要的组件
        auto& transform = m_registry.emplace<TransformComponent>(entity);
        transform.position = position;
        
        auto& quad = m_registry.emplace<QuadRendererComponent>(entity);
        quad.color = color;
        quad.size = size;
        
        return entity;
    }

    // 创建精灵的便捷方法
    entt::entity createSprite(const Vector2f& position, const Vector2f& size, bgfx::TextureHandle texture, const Color& tint = Color::White) {
        auto entity = m_registry.create();
        
        auto& transform = m_registry.emplace<TransformComponent>(entity);
        transform.position = position;
        
        auto& sprite = m_registry.emplace<SpriteRendererComponent>(entity);
        sprite.texture = texture;
        sprite.size = size;
        sprite.color = tint;
        
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