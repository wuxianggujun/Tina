#pragma once

#include "tina/core/Core.hpp"
#include <entt/entt.hpp>
#include <string>
#include "tina/layer/LayerStack.hpp"
#include "tina/log/Logger.hpp"

namespace Tina
{
    class TINA_CORE_API Scene
    {
    public:
        explicit Scene(const std::string& name = "Scene");
        ~Scene();

        // 禁止拷贝和赋值
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

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
        // 成员变量按照销毁顺序排列（从下到上销毁）
        std::string m_name;           // 最先被初始化，最后被销毁
        entt::registry m_registry;    // 第二个被初始化
        LayerStack m_LayerStack;      // 第三个被初始化
    };
}
