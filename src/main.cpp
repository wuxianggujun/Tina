#include "game/GameApplication.hpp"
#include "framework/Application.hpp"

int main(int argc, char** argv)
{
    Tina::Engine* engine = Tina::Engine::create(std::make_unique<Tina::GameApplication>());
    engine->init(
           { "Tina",
             nullptr,
             1280, 720,
             false});
    engine->run();

    Tina::Engine::destroy(engine);

    return 0;
}
