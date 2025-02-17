#include <utility>

#include "tina/scene/Scene.hpp"

#include "bgfx/platform.h"
#include "tina/log/Logger.hpp"
#include "tina/event/Event.hpp"
#include "tina/utils/Profiler.hpp"

namespace Tina
{
    Scene::Scene(std::string name)
        : m_name(std::move(name)),
          m_rootView(new View("RootView"))
    {
        TINA_PROFILE_FUNCTION();
        TINA_LOG_INFO("Created scene: {}", m_name);
        
        // 初始化根视图
        initializeView(m_rootView);
    }

    Scene::~Scene()
    {
        TINA_PROFILE_FUNCTION();
        try
        {
            std::string sceneName = m_name;
            TINA_LOG_INFO("Destroying scene: {}", sceneName);

            if (m_rootView)
            {
                cleanupView(m_rootView);
                delete m_rootView;
            }

            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                bgfx::frame(false);
            }
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error in scene destructor: {}", e.what());
        }
    }

    void Scene::onUpdate(float deltaTime)
    {
        TINA_PROFILE_FUNCTION();
        TINA_PROFILE_PLOT("Scene Delta Time", deltaTime);

        if (m_rootView)
        {
            updateViewHierarchy(m_rootView, deltaTime);
        }
    }

    void Scene::addView(View* view)
    {
        if (!view) return;

        try 
        {
            TINA_LOG_DEBUG("Adding view '{}' to scene", view->getName());
            
            // 1. 建立节点关系
            m_rootView->addChild(view);
            
            // 2. 初始化视图
            initializeView(view);
            
            TINA_LOG_DEBUG("View '{}' added successfully", view->getName());
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Failed to add view '{}': {}", view->getName(), e.what());
            m_rootView->removeChild(view);
            throw;
        }
    }

    void Scene::removeView(View* view)
    {
        if (!view) return;

        try 
        {
            TINA_LOG_DEBUG("Removing view '{}' from scene", view->getName());
            
            // 1. 清理视图
            cleanupView(view);
            
            // 2. 移除节点关系
            m_rootView->removeChild(view);
            
            TINA_LOG_DEBUG("View '{}' removed successfully", view->getName());
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error removing view '{}': {}", view->getName(), e.what());
            throw;
        }
    }

    void Scene::onEvent(Event& event)
    {
        TINA_LOG_DEBUG("Scene: Received event type {}", static_cast<int>(event.type));

        if (m_rootView)
        {
            propagateEventToView(m_rootView, event);
        }
    }

    void Scene::onRender()
    {
        if (m_rootView)
        {
            renderViewHierarchy(m_rootView);
            bgfx::frame();
        }
    }

    // 私有方法 - View 生命周期管理
    
    void Scene::initializeView(View* view)
    {
        if (!view) return;
        
        TINA_LOG_DEBUG("Initializing view '{}'", view->getName());
        
        // 1. 设置视口
        uint32_t width, height;
        Core::Engine::get().getWindowSize(width, height);
        view->setViewPort(Rect(0, 0, width, height));
        
        // 2. 调用生命周期方法
        view->onAttach();
        
        // 3. 递归初始化子视图
        for (auto* child : view->getChildren())
        {
            if (auto* childView = dynamic_cast<View*>(child))
            {
                initializeView(childView);
            }
        }
    }

    void Scene::cleanupView(View* view)
    {
        if (!view) return;
        
        TINA_LOG_DEBUG("Cleaning up view '{}'", view->getName());
        
        // 1. 递归清理子视图
        for (auto* child : view->getChildren())
        {
            if (auto* childView = dynamic_cast<View*>(child))
            {
                cleanupView(childView);
            }
        }
        
        // 2. 调用生命周期方法
        view->onDetach();
    }

    void Scene::updateViewHierarchy(View* view, float deltaTime)
    {
        if (!view) return;
        
        // 1. 更新当前视图
        view->onUpdate(deltaTime);
        
        // 2. 递归更新子视图
        for (auto* child : view->getChildren())
        {
            if (auto* childView = dynamic_cast<View*>(child))
            {
                updateViewHierarchy(childView, deltaTime);
            }
        }
    }

    void Scene::renderViewHierarchy(View* view)
    {
        if (!view || !view->isVisible()) return;

        // 1. 开始渲染
        view->beginRender();
        
        // 2. 渲染当前视图
        if (auto* gameView = dynamic_cast<GameView*>(view))
        {
            gameView->render(this);
        }
        
        // 3. 递归渲染子视图
        for (auto* child : view->getChildren())
        {
            if (auto* childView = dynamic_cast<View*>(child))
            {
                renderViewHierarchy(childView);
            }
        }
        
        // 4. 结束渲染
        view->endRender();
    }

    void Scene::propagateEventToView(View* view, Event& event)
    {
        if (!view) return;
        
        // 1. 处理当前视图的事件
        view->onEvent(event);
        
        // 2. 如果事件未被处理,递归传递给子视图
        if (!event.handled)
        {
            for (auto* child : view->getChildren())
            {
                if (auto* childView = dynamic_cast<View*>(child))
                {
                    propagateEventToView(childView, event);
                    if (event.handled) break;
                }
            }
        }
    }

} // namespace Tina
