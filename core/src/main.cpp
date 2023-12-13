#include <iostream>
#include <SDL3/SDL.h>
#include "Game.hpp"

int main()
{

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        SDL_Log("Failed at SDL_Init(%s)", SDL_GetError());
    }

    std::cout << "Hello, World!" << std::endl;
    return 0;
}
