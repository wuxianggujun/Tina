#ifndef TINA_GAME_APPLICATION_HPP
#define TINA_GAME_APPLICATION_HPP

#include <memory>
#include "framework/Engine.hpp"
#include "framework/Application.hpp"

namespace Tina {

    class GlfwWindow;
    class Renderer;
    class RenderTarget;
    class Screen;


    class GameApplication : public Application{

    public:
        GameApplication();
        GameApplication(GameApplication&&) = default;
        GameApplication& operator=(GameApplication&&) = default;
        virtual ~GameApplication();

        virtual void init(InitArgs initArgs) override;
        virtual bool update(float deltaTime) override;
        virtual void shutdown() override;
    };



}


#endif
