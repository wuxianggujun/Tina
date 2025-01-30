//
// Created by wuxianggujun on 2024/7/12.
//

#pragma once

#include <memory>
#include "window/IWindow.hpp"
#include "graphics/Camera.hpp"
#include "filesystem/Path.hpp"
#include "window/IWindowResizeListener.hpp"
#include "scene/Scene.hpp"

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

        // 获取当前场景
        Scene& getCurrentScene() { return m_scene; }

        std::unique_ptr<IWindow> m_window;
        std::unique_ptr<OrthographicCamera> m_camera;
        float m_lastFrameTime;
        Path m_configPath;
        
        // 场景
        Scene m_scene;
    };
} // Tina