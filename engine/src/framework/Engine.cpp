//
// Created by wuxianggujun on 2024/4/27.
//

#include <cassert>
#include "Engine.hpp"
#include "Application.hpp"
#include "Configuration.hpp"

namespace Tina {

    Engine::Engine(Scope<Application> application) : engineApplication(std::move(application)) {
    }

/*
    Engine::~Engine() {
        if (isRunning) {
            shutdown();
        }
    }
*/

    void Engine::init(Configuration config) {
        isRunning = engineApplication->initialize();
    }

    int Engine::run(Configuration config) {
        init(config);

        while (isRunning) {
            if (!engineApplication->isRunning()) {
                stop();
                break;
            }
            engineApplication->run();
        }
        //shutdown();
        return EXIT_SUCCESS;
    }

    void Engine::shutdown() {
        stop();
        engineApplication->close();
    }

    static Scope<Engine> scopeEngine;

    Engine *Engine::create(Scope<Application> application) {
        Application *app = application.get();
        scopeEngine = createScope<Engine>(std::move(application));
        app->setEngine(scopeEngine.get());
        return scopeEngine.get();
    }

    void Engine::destroy(Engine *engine) {
        assert(scopeEngine.get() == engine);
        if (scopeEngine) {
            scopeEngine->shutdown();
            scopeEngine.reset();
            scopeEngine = nullptr;
        }
    }

    void Engine::stop() {
        isRunning = false;
    }


} // Tina
