#include <utility>

#include "tina/scene/Scene.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/event/Event.hpp"
#include "tina/layer/Layer.hpp"
#include "tina/utils/Profiler.hpp"

namespace Tina
{
    Scene::Scene(std::string name)
        : m_name(std::move(name)) // 最先初始化
    {
        TINA_PROFILE_FUNCTION();
        TINA_LOG_INFO("Created scene: {}", m_name);
    }

    Scene::~Scene()
    {
        TINA_PROFILE_FUNCTION();
        try
        {
            // 在析构函数开始时记录场景名称
            std::string sceneName = m_name;
            TINA_LOG_INFO("Destroying scene: {}", sceneName);

            // 1. 先清理LayerStack
            m_layerStack.clear();

            // 只在真正需要时执行渲染同步
            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                bgfx::frame(false); // 使用 false 参数避免等待 VSync
            }

            // 清理实体注册表
            TINA_LOG_DEBUG("Clearing entity registry");
            m_registry.clear();
            TINA_LOG_INFO("Scene destroyed: {}", sceneName);
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error in scene destructor: {}", e.what());
        }
    }

    entt::entity Scene::createEntity(const std::string& name)
    {
        TINA_PROFILE_FUNCTION();
        entt::entity entity = m_registry.create();
        m_registry.emplace<std::string>(entity, name);
        TINA_LOG_INFO("Created entity: {} in scene: {}", name, m_name);
        return entity;
    }

    void Scene::destroyEntity(entt::entity entity)
    {
        TINA_PROFILE_FUNCTION();
        if (m_registry.valid(entity))
        {
            std::string name = getEntityName(entity);
            m_registry.destroy(entity);
            TINA_LOG_INFO("Destroyed entity: {} in scene: {}", name, m_name);
        }
    }

    std::string Scene::getEntityName(entt::entity entity) const
    {
        TINA_PROFILE_FUNCTION();
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
        TINA_PROFILE_FUNCTION();
        if (m_registry.valid(entity))
        {
            m_registry.replace<std::string>(entity, name);
            TINA_LOG_INFO("Renamed entity to: {} in scene: {}", name, m_name);
        }
    }

    void Scene::onUpdate(float deltaTime)
    {
        TINA_PROFILE_FUNCTION();
        TINA_PROFILE_PLOT("Scene Delta Time", deltaTime);
        
        for (Layer* layer : m_layerStack)
        {
            TINA_PROFILE_SCOPE("Layer Update");
            layer->onUpdate(deltaTime);
        }
    }

    void Scene::onRender()
    {
        TINA_PROFILE_FUNCTION();
        
        for (Layer* layer : m_layerStack)
        {
            TINA_PROFILE_SCOPE("Layer Render");
            layer->onRender();
        }
        
        TINA_PROFILE_FRAME();
    }

    void Scene::onImGuiRender()
    {
        TINA_PROFILE_FUNCTION();
        
        for (Layer* layer : m_layerStack)
        {
            TINA_PROFILE_SCOPE("Layer ImGui Render");
            layer->onImGuiRender();
        }
    }

    void Scene::onEvent(Event& event)
    {
        TINA_PROFILE_FUNCTION();
        
        // 从后向前遍历，使最上层的Layer先处理事件
        for (auto it = m_layerStack.end(); it != m_layerStack.begin();)
        {
            TINA_PROFILE_SCOPE("Layer Event");
            (*--it)->onEvent(event);
            if (event.handled)
            {
                break;
            }
        }
    }

    void Scene::pushLayer(Layer* layer)
    {
        TINA_PROFILE_FUNCTION();
        layer->setScene(this);
        m_layerStack.pushLayer(layer);
    }

    void Scene::pushOverlay(Layer* overlay)
    {
        TINA_PROFILE_FUNCTION();
        overlay->setScene(this);
        m_layerStack.pushOverlay(overlay);
    }
} // namespace Tina
