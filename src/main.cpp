#include "game/GameApplication.hpp"
#include <boost/timer/timer.hpp>
#include "log/Log.hpp"
int main(int argc, char** argv)
{
    Tina::Engine* engine = Tina::Engine::create(std::make_unique<Tina::GameApplication>());
    boost::timer::auto_cpu_timer time("%w seconds\n");
    engine->init({ "Tina",nullptr,1280,720,false });
    LOG_STREAM_INFO()<<"Engine Init Timer : " << time.format();
    engine->run();

    Tina::Engine::destroy(engine);
    return 0;
}
