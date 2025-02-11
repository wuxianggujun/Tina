#include "tina/scene/Scene.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/event/Event.hpp"

namespace Tina
{
    Scene::Scene(const std::string& name)
        : m_name(name)  // 最先初始化
        , m_registry()  // 第二个初始化
        , m_LayerStack() // 第三个初始化
    {
        TINA_LOG_INFO("Created scene: {}", m_name);
    }

    Scene::~Scene() {
        try {
            // 保存场景名称的副本，并立即清空原始名称以防止后续访问
            std::string sceneName = std::move(m_name);
            TINA_LOG_INFO("Destroying scene: {}", sceneName);
            
            // 确保完成所有渲染操作
            if (bgfx::getInternalData()->context) {
                bgfx::frame();
                bgfx::renderFrame();
            }
            
            // 清理LayerStack
            try {
                TINA_LOG_DEBUG("Clearing LayerStack");
                // 先调用所有层的onDetach
                for (auto& layer : m_LayerStack) {
                    if (layer) {
                        try {
                            TINA_LOG_DEBUG("Detaching layer: {}", layer->getName());
                            layer->onDetach();
                        }
                        catch (const std::exception& e) {
                            TINA_LOG_ERROR("Error detaching layer: {}", e.what());
                        }
                    }
                }
                
                // 等待GPU完成所有操作
                if (bgfx::getInternalData()->context) {
                    bgfx::frame();
                    bgfx::renderFrame();
                }

                // 然后清空LayerStack（不会触发onDetach）
                m_LayerStack.clear();
            }
            catch (const std::exception& e) {
                TINA_LOG_ERROR("Error clearing LayerStack: {}", e.what());
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

    void Scene::pushLayer(std::shared_ptr<Layer> layer)
    {
        m_LayerStack.pushLayer(layer);
        layer->onAttach();
    }

    void Scene::pushOverlay(std::shared_ptr<Layer> overlay)
    {
        m_LayerStack.pushOverlay(overlay);
        overlay->onAttach();
    }

    void Scene::popLayer(std::shared_ptr<Layer> layer)
    {
        layer->onDetach();
        m_LayerStack.popLayer(layer);
    }

    void Scene::popOverlay(std::shared_ptr<Layer> overlay)
    {
        overlay->onDetach();
        m_LayerStack.popOverlay(overlay);
    }

    void Scene::onUpdate(float deltaTime)
    {
        // 更新所有层
        for (auto& layer : m_LayerStack)
        {
            layer->onUpdate(deltaTime);
        }
    }

    void Scene::onRender()
    {
        // 渲染所有层
        for (auto& layer : m_LayerStack)
        {
            layer->onRender();
        }
    }

    void Scene::onImGuiRender()
    {
        // ImGui渲染所有层
        for (auto& layer : m_LayerStack)
        {
            layer->onImGuiRender();
        }
    }

    void Scene::onEvent(Event& event)
    {
        // 从上到下处理事件
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            (*it)->onEvent(event);
            if (event.handled)
                break;
        }
    }

} // namespace Tina
