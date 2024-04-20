#include "game/GameApplication.hpp"

int main(int argc, char** argv)
{
    using namespace Tina;

    Engine* engine = Engine::create(std::make_unique<Tina::GameApplication>());

    engine->init({"Tina",nullptr,1280,720,false});
    engine->run();

    Engine::destroy(engine);
    return 0;
}
