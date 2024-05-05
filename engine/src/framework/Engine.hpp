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
    private:
        static Engine* instance_;
        Engine(){};
    public:
        Engine(const Engine&) =delete;
        Engine& operator=(const Engine&) = delete;

    public:
        static  Engine* getSingleton();

        virtual ~Engine() =default;

        int run(std::unique_ptr<Application> application);
    private:
        bool isRunning = false;
    };

} // Tina

#endif //TINA_ENGINE_HPP
