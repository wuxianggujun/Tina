#ifndef TINA_FRAMEWORK_ENGINE_HPP
#define TINA_FRAMEWORK_ENGINE_HPP

#include "EngineDefines.hpp"
#include "Application.hpp"

#include <vector>
#include <memory>
#include <boost/noncopyable.hpp>

namespace Tina {

    class RenderContext;
    class Renderer;
    class World;

    class Engine : boost::noncopyable{
    public:
        Engine() = delete;
        explicit Engine(std::unique_ptr<Application> pApplication);
        Engine(Engine&&) = delete;
        Engine& operator=(Engine&&) = delete;
        virtual ~Engine();

        static ENGINE_API Engine* create(std::unique_ptr<Application> application);
        static ENGINE_API void destroy(Engine* engine);

        ENGINE_API void init(InitArgs args);

        ENGINE_API void run();

        ENGINE_API void shutdown();

    private:

        std::unique_ptr<Application> engineApplication;

    };

}







#endif
