#include "Engine.hpp"
#include "log/Log.hpp"

#include "GlfwWindow.hpp"
#include "timer/Clock.hpp"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScoped
#define FrameMark
#endif

namespace Tina {



    Engine::Engine(std::unique_ptr<Application> application):engineApplication(std::move(application)){

    }

    Engine::~Engine() {
        engineApplication->shutdown();
        shutdown();
    }

    void Engine::init(InitArgs args) {
        LOG_LOGGER_INIT("logs/Engine.log",Tina::STDOUT |Tina::FILEOUT|Tina::ASYNC);
        LOG_INFO("Init engine");
        GlfwWindow::init();

        engineApplication->init(args);

    }

    void Engine::run() {
        Clock clock;

        while (true)
        {
            ZoneScoped;

            if (!engineApplication->update(clock.getDeltaTime()))
            {
                break;
            }

            FrameMark;
        }
    }

    void Engine::shutdown() {
        GlfwWindow::shutdown();
    }

    static std::unique_ptr<Engine> s_engine;


    Engine* Engine::create(std::unique_ptr<Application> application) {
        LOG_INFO("Init log");

        Application* app = application.get();
        s_engine = std::make_unique<Engine>(std::move(application));
        app->setEngine(s_engine.get());

        return s_engine.get();
    }

    void Engine::destroy(Engine* engine) {

        if (s_engine)
        {
            s_engine->shutdown();
            s_engine.reset();
            s_engine = nullptr;
        }
    }
}
