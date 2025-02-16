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
        // 获取当前窗口大小
        uint32_t width, height;
        Core::Engine::get().getWindowSize(width, height);
        m_rootView->setViewPort(Rect(0, 0, width, height));
        m_rootView->onAttach();
    }

    Scene::~Scene()
    {
        TINA_PROFILE_FUNCTION();
        try
        {
            // 在析构函数开始时记录场景名称
            std::string sceneName = m_name;
            TINA_LOG_INFO("Destroying scene: {}", sceneName);

            if (m_rootView)
            {
                m_rootView->onDetach();
                delete m_rootView;
            }

            // 只在真正需要时执行渲染同步
            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                bgfx::frame(false); // 使用 false 参数避免等待 VSync
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
            m_rootView->onUpdate(deltaTime);
        }
    }

    void Scene::addView(View* view)
    {
        if (view)
        {
            m_rootView->addChild(view);
        }
    }

    void Scene::removeView(View* view)
    {
        if (view)
        {
            m_rootView->removeChild(view);
        }
    }

    void Scene::onEvent(Event& event)
    {
        TINA_LOG_DEBUG("Scene: Received event type {}", static_cast<int>(event.type));

        // 直接传递事件给根视图
        if (m_rootView)
        {
            m_rootView->onEvent(event);
        }
    }

    void Scene::onRender()
    {
        if (m_rootView)
        {
            // 渲染视图树
            renderView(m_rootView);
            // 在所有View渲染完成后,提交这一帧
            bgfx::frame();
        }
    }

    void Scene::renderView(View* view)
    {
        if (!view || !view->isVisible()) return;
        // 开始渲染
        view->beginRender();

        // 如果是GameView,渲染场景实体
        if (auto* gameView = dynamic_cast<GameView*>(view))
        {
            gameView->render(this);
        }

        // 渲染子视图
        for (auto* child : view->getChildren())
        {
            renderView(dynamic_cast<View*>(child));
        }

        // 结束渲染
        view->endRender();
    }
} // namespace Tina
