//
// Created by wuxianggujun on 2024/7/12.
//

#pragma once

#include <memory>
#include "window/IWindow.hpp"
#include "graphics/Renderer2D.hpp"
#include "graphics/Texture.hpp"
#include "math/Vector.hpp"
#include "filesystem/Path.hpp"

namespace Tina
{
    class GameApplication
    {
    public:
        GameApplication();
        explicit GameApplication(const Path& configPath);
        virtual ~GameApplication();

        void initialize();
        void run();
        void mainLoop();
        void update(float deltaTime);
        void render();
        void shutdown();

    protected:
        std::unique_ptr<IWindow> m_window;
        // std::unique_ptr<GuiSystem> m_guiSystem;
        std::unique_ptr<Renderer2D> m_renderer2D;
        TextureHandle m_logoTexture;
        float m_lastFrameTime{0.0f};
        Path m_configPath;
    };
} // Tina