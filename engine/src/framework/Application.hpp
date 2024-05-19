//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_APPLICATION_HPP
#define TINA_APPLICATION_HPP

#include "Engine.hpp"

namespace Tina {

    class Application {

    protected:
        Engine *engine{};
    public:
        virtual void run(float deltaTime) = 0;

    public:
        virtual bool initialize() = 0;

        virtual bool isRunning() = 0;

        virtual void close() {};

        void setEngine([[maybe_unused]] Engine *currentEngine) {
            if (currentEngine) {
                engine = currentEngine;
            }
        }

        [[maybe_unused]] [[nodiscard]] Engine *getEngine() const { return engine; }
    };

} // Tina

#endif //TINA_APPLICATION_HPP
