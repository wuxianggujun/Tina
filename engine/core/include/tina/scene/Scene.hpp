#pragma once

#include "tina/core/Core.hpp"
#include <entt/entt.hpp>
#include <string>
#include "tina/layer/LayerStack.hpp"

namespace Tina
{
    class TINA_CORE_API Scene
    {
    public:
        explicit Scene(std::string name = "Scene");
        ~Scene();

        // 禁止拷贝和赋值
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        // Entity management
        entt::entity createEntity(const std::string& name = std::string());
        void destroyEntity(entt::entity entity);
        // 获取实体名称
        [[nodiscard]] std::string getEntityName(entt::entity entity) const;
        // 设置实体名称
        void setEntityName(entt::entity entity, const std::string& name);

        // Component management
        template<typename T, typename... Args>
        T& addComponent(entt::entity entity, Args&&... args) {
            return m_registry.emplace<T>(entity, std::forward<Args>(args)...);
        }

        template<typename T>
        T& getComponent(entt::entity entity) {
            return m_registry.get<T>(entity);
        }

        template<typename T>
        bool hasComponent(entt::entity entity) {
            return m_registry.all_of<T>(entity);
        }

        template<typename T>
        void removeComponent(entt::entity entity) {
            m_registry.remove<T>(entity);
        }

        // Scene lifecycle
        void onUpdate(float deltaTime);
        void onRender();
        void onImGuiRender();
        void onEvent(class Event& event);

        // Getters
        entt::registry& getRegistry() { return m_registry; }
        [[nodiscard]] const entt::registry& getRegistry() const { return m_registry; }
        [[nodiscard]] const std::string& getName() const { return m_name; }
        void setName(const std::string& name) { m_name = name; }

        void pushLayer(Layer* layer) { m_layerStack.pushLayer(layer); }
        void pushOverlay(Layer* overlay) { m_layerStack.pushOverlay(overlay); }
        void popLayer(Layer* layer) { m_layerStack.popLayer(layer); }
        void popOverlay(Layer* overlay) { m_layerStack.popOverlay(overlay); }

        // Layer access
        const LayerStack& getLayerStack() const { return m_layerStack; }
        LayerStack& getLayerStack() { return m_layerStack; }

    private:
        // 成员变量按照销毁顺序排列（从下到上销毁）
        std::string m_name; // 最先被初始化，最后被销毁
        entt::registry m_registry; // 第二个被初始化
        LayerStack m_layerStack;
    };
}
