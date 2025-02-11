#include <utility>

#include "tina/scene/Scene.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/event/Event.hpp"

namespace Tina
{
    Scene::Scene(std::string  name)
        : m_name(std::move(name))  // 最先初始化
    {
        TINA_LOG_INFO("Created scene: {}", m_name);
    }

    Scene::~Scene() {
        try {
            // 在析构函数开始时记录场景名称
            std::string sceneName = m_name;
            TINA_LOG_INFO("Destroying scene: {}", sceneName);
            
            // 只在真正需要时执行渲染同步
            if (bgfx::getInternalData() && bgfx::getInternalData()->context) {
                bgfx::frame(false); // 使用 false 参数避免等待 VSync
            }

            // 清理实体注册表
            try {
                TINA_LOG_DEBUG("Clearing entity registry");
                m_registry.clear();
            }
            catch (const std::exception& e) {
                TINA_LOG_ERROR("Error clearing registry: {}", e.what());
            }

            TINA_LOG_INFO("Scene destroyed: {}", sceneName);
        }
        catch (const std::exception& e) {
            TINA_LOG_ERROR("Error in scene destructor: {}", e.what());
        }
        catch (...) {
            TINA_LOG_ERROR("Unknown error in scene destructor");
        }
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

    void Scene::onUpdate(float deltaTime)
    {
    }

    void Scene::onRender()
    {
    }

    void Scene::onImGuiRender()
    {
    }

    void Scene::onEvent(struct Event& event)
    {
    }
} // namespace Tina
