#include "tina/scene/Scene.hpp"
#include "tina/log/Logger.hpp"

namespace Tina
{
    Scene::Scene(const std::string& name)
        : m_name(name)
    {
        TINA_LOG_INFO("Created scene: {}", name);
    }

    entt::entity Scene::createEntity(const std::string& name)
    {
        entt::entity entity = m_registry.create();
        
        // 添加名称组件
        m_registry.emplace<std::string>(entity, name);
        
        TINA_LOG_INFO("Created entity: {} in scene: {}", name, m_name);
        return entity;
    }

    void Scene::destroyEntity(entt::entity entity)
    {
        if (m_registry.valid(entity))
        {
            std::string name = getEntityName(entity);
            m_registry.destroy(entity);
            TINA_LOG_INFO("Destroyed entity: {} in scene: {}", name, m_name);
        }
    }

    std::string Scene::getEntityName(entt::entity entity) const
    {
        if (m_registry.valid(entity))
        {
            if (auto* name = m_registry.try_get<std::string>(entity))
            {
                return *name;
            }
        }
        return "Invalid Entity";
    }

    void Scene::setEntityName(entt::entity entity, const std::string& name)
    {
        if (m_registry.valid(entity))
        {
            m_registry.replace<std::string>(entity, name);
            TINA_LOG_INFO("Renamed entity to: {} in scene: {}", name, m_name);
        }
    }
}
