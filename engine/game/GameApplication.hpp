#ifndef TINA_GAME_APPLICATION_HPP
#define TINA_GAME_APPLICATION_HPP

#include <memory>
#include <boost/noncopyable.hpp>
#include "framework/Application.hpp"

namespace Tina {

    class GameApplication : public Application, boost::noncopyable{

    public:
        GameApplication();
        GameApplication(GameApplication&&) = default;
        GameApplication& operator=(GameApplication&&) = default;
        virtual ~GameApplication();

        virtual void init(InitArgs initArgs) override;
        virtual bool update(float deltaTime) override;
        virtual void shutdown() override;

        virtual Excution run() override;
    };



}


#endif
