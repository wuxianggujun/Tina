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
        Scene(const std::string& name = "Scene");
        ~Scene() = default;

        // Entity management
        entt::entity createEntity(const std::string& name = std::string());
        void destroyEntity(entt::entity entity);
        // 获取实体名称
        std::string getEntityName(entt::entity entity) const;
        // 设置实体名称
        void setEntityName(entt::entity entity, const std::string& name);

        // Layer management
        void pushLayer(std::shared_ptr<Layer> layer);
        void pushOverlay(std::shared_ptr<Layer> overlay);
        void popLayer(std::shared_ptr<Layer> layer);
        void popOverlay(std::shared_ptr<Layer> overlay);

        // Scene lifecycle
        void onUpdate(float deltaTime);
        void onRender();
        void onImGuiRender();
        void onEvent(class Event& event);

        // Getters
        entt::registry& getRegistry() { return m_registry; }
        const entt::registry& getRegistry() const { return m_registry; }
        const std::string& getName() const { return m_name; }
        void setName(const std::string& name) { m_name = name; }
        LayerStack& getLayers() { return m_LayerStack; }

    private:
        std::string m_name;
        entt::registry m_registry;
        LayerStack m_LayerStack;
    };
}
