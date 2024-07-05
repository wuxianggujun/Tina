//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_APPLICATION_HPP
#define TINA_APPLICATION_HPP

#include "Engine.hpp"
#include "framework/Configuration.hpp"

namespace Tina {
    class Application {
    protected:
        Engine *engine{};

    public:
        Application() = default;
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        virtual ~Application() = default;

        virtual void run(float deltaTime) = 0;

        virtual bool initialize(const Configuration &config) = 0;

        virtual bool isRunning() = 0;

        virtual void shutdown() = 0;

        void setEngine([[maybe_unused]] Engine *currentEngine) {
            if (currentEngine) {
                engine = currentEngine;
            }
        }

        [[maybe_unused]] [[nodiscard]] Engine *getEngine() const { return engine; }
    };
} // Tina

#endif //TINA_APPLICATION_HPP
