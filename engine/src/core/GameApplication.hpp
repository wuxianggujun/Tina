//
// Created by wuxianggujun on 2024/7/12.
//

#pragma once

#include <memory>
#include "window/IWindow.hpp"
#include "graphics/Renderer2D.hpp"
#include "graphics/Camera.hpp"
#include "filesystem/Path.hpp"
#include"window/IWindowResizeListener.hpp"

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

        std::unique_ptr<IWindow> m_window;
        // std::unique_ptr<GuiSystem> m_guiSystem;
        std::unique_ptr<Renderer2D> m_renderer2D;
        std::unique_ptr<OrthographicCamera> m_camera;  // 默认使用正交相机
        float m_lastFrameTime;
        Path m_configPath;
    };
} // Tina