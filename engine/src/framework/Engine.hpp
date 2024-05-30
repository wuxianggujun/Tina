//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_ENGINE_HPP
#define TINA_ENGINE_HPP

#include <memory>
#include "Core.hpp"
#include "tool/Tracy.hpp"


namespace Tina {
    class Application;

    class Configuration;

    class Engine {
    public:
        Engine() = default;

        explicit Engine(Scope<Application> application);

        Engine(const Engine &) = delete;

        Engine &operator=(const Engine &) = delete;

        Engine(Engine &&other) = delete;

        Engine &operator=(Engine &&) = delete;

        virtual ~Engine() = default;

    public:
        static ENGINE_API Engine *create(Scope<Application> application);

        static ENGINE_API void destroy(Engine *engine);

    protected:
        ENGINE_API void init(Configuration config);
        ENGINE_API void stop();

    public:
        ENGINE_API int run(Configuration config);

        ENGINE_API void shutdown();

    private:
        Scope<Application> engineApplication;
        bool isRunning = false;
    };

} // Tina

#endif //TINA_ENGINE_HPP
