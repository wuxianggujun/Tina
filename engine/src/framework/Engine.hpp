//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_ENGINE_HPP
#define TINA_ENGINE_HPP

#include <memory>

namespace Tina {
    class Application;
    class Configuration;

    class Engine {
    public:
        static Engine* singleton;
    public:
        static  Engine* getSingleton();
        Engine();
        virtual ~Engine() =default;

        int run(std::unique_ptr<Application> application);
    private:
        bool isRunning = false;
    };

} // Tina

#endif //TINA_ENGINE_HPP
