#include "framework/Engine.hpp"
#include "game/GameApplication.hpp"

int main(int argc, char** argv)
{
    using namespace Tina;

    Engine* engine = Engine::create(std::make_unique<Tina::GameApplication>());


    return 0;
}
