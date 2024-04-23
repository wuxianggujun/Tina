#ifndef TINA_FRAMEWORK_APPLICATION_HPP
#define TINA_FRAMEWORK_APPLICATION_HPP

#include "InitArgs.hpp"
#include <boost/utility.hpp>

namespace Tina{

    class InitArgs;
    class Engine;

    class Application : public boost::noncopyable{

    public:

        Application() = default;
        Application(Application&&) = default;
        Application& operator=(Application&&) = default;
        virtual ~Application() {}

        virtual void init(InitArgs initArgs) = 0;
        virtual bool update(float deltaTime) = 0;
        virtual void shutdown() = 0;


        void setEngine(Engine* engine) {
            this->engine = engine;
        }

        Engine* getEngine() {
            return engine;
        }
    protected:
        Engine* engine;
    };

}


#endif
