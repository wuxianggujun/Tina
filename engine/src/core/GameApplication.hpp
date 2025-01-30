//
// Created by wuxianggujun on 2024/7/12.
//

#pragma once

#include <memory>
#include "window/IWindow.hpp"
#include "graphics/Camera.hpp"
#include "filesystem/Path.hpp"
#include "window/IWindowResizeListener.hpp"
#include <entt/entt.hpp>

namespace Tina
{
    class GameApplication : public IWindowResizeListener
    {
    public:
        GameApplication();
        explicit GameApplication(const Path& configPath);
        virtual ~GameApplication();

        void run();

        void onWindowResize(int width, int height) override;

    protected:
        virtual void initialize();
        virtual void update(float deltaTime);
        virtual void render();
        virtual void shutdown();

        void mainLoop();

        // 创建示例实体
        void createExampleEntities();

        std::unique_ptr<IWindow> m_window;
        std::unique_ptr<OrthographicCamera> m_camera;
        float m_lastFrameTime;
        Path m_configPath;
        
        // ECS注册表
        entt::registry m_registry;
    };
} // Tina