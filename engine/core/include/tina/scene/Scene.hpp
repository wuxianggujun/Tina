#pragma once

#include "tina/core/Core.hpp"
#include <entt/entt.hpp>
#include <string>

namespace Tina
{
    class TINA_CORE_API Scene
    {
    public:
        Scene(const std::string& name = "Scene");
        ~Scene() = default;

        // 创建实体
        entt::entity createEntity(const std::string& name = "Entity");
        // 销毁实体
        void destroyEntity(entt::entity entity);
        // 获取实体名称
        std::string getEntityName(entt::entity entity) const;
        // 设置实体名称
        void setEntityName(entt::entity entity, const std::string& name);

        // 获取注册表
        entt::registry& getRegistry() { return m_registry; }
        const entt::registry& getRegistry() const { return m_registry; }

        // 获取场景名称
        const std::string& getName() const { return m_name; }
        void setName(const std::string& name) { m_name = name; }

    private:
        std::string m_name;
        entt::registry m_registry;
    };
}
