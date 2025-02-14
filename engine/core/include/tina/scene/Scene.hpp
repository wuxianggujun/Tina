#pragma once

#include "tina/core/Core.hpp"
#include "tina/components/ComponentDependencyManager.hpp"
#include <entt/entt.hpp>
#include <string>
#include "tina/layer/LayerStack.hpp"
#include <memory>

namespace Tina
{
    class Layer;
    class Event;

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

        // 获取注册表
        entt::registry& getRegistry() { return m_registry; }
        const entt::registry& getRegistry() const { return m_registry; }

        // Scene lifecycle
        void onUpdate(float deltaTime);
        void onRender();
        void onImGuiRender();
        void onEvent(Event& event);

        // Getters
        [[nodiscard]] const std::string& getName() const { return m_name; }
        void setName(const std::string& name) { m_name = name; }
        [[nodiscard]] const LayerStack& getLayerStack() const { return m_layerStack; }
        LayerStack& getLayerStack() { return m_layerStack; }

        // Layer management
        void pushLayer(Layer* layer);
        void pushOverlay(Layer* overlay);

        // Component dependency management
        template<typename T, typename Dependency>
        void registerComponentDependency() {
            ComponentDependencyManager::getInstance().registerDependency<T, Dependency>();
        }

        template<typename T, typename... Dependencies>
        void registerComponentDependencies() {
            ComponentDependencyManager::getInstance().registerDependencies<T, Dependencies...>();
        }

    private:
        std::string m_name;
        LayerStack m_layerStack;
        entt::registry m_registry;
    };
} // namespace Tina
