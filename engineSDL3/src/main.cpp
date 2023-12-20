#include <iostream>
#include "Game.hpp"

int main(int argv, char** argc)
{
    Game::getInstance().run();
    return 0;
}
