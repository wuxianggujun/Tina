//
// Created by wuxianggujun on 2024/4/28.
//

#ifndef TINA_APPLICATION_GAMEAPPLICATION_HPP
#define TINA_APPLICATION_GAMEAPPLICATION_HPP

#include <memory>

#include "framework/Core.hpp"
#include "framework/Window.hpp"
#include "framework/render/RenderContext.hpp"
#include "framework/Application.hpp"
#include "framework/render/Renderer.hpp"
#include "framework/render/WorldRenderer.hpp"
#include "framework/time/StopWatch.hpp"
#include "flatphysics/FlatVector.hpp"

namespace Tina {
    class Configuration;

    class Window;

    class GameApplication final : public Application {
    public:
        bool initialize(const Configuration& config) override;

        bool isRunning() override;

        void run(float deltaTime) override;

        void shutdown() override;

    private:
        Scope<Window> window;
        Scope<RenderContext> renderContext;
    };
} // Tina

#endif //TINA_APPLICATION_GAMEAPPLICATION_HPP
