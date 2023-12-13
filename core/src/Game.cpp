//
// Created by WuXiangGuJun on 2023/12/13.
//
#include "Game.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_image.h>
#include "TextureManager.hpp"
#include "Hud.hpp"

Game Game::s_instance;

#include <iostream>

void Game::run()
{
    init();
    std::cout << "Launch time : " << static_cast<float>(SDL_GetTicks() - t1) / 1000.f << std::endl;

    while (s_instance.m_running)
    {
        if (SDL_GetTicks() > m_timePassed)
        {
            updateEvents();
            update(m_deltaTime);
            render();

            m_deltaTime = static_cast<float>(SDL_GetTicks() - m_timePassed) / 1000.f;
            m_timePassed = SDL_GetTicks();
        }
    }

    destroy();
}

const Vector2i Game::getWindowSize()
{
    Vector2i size{};
    SDL_GetWindowSize(m_window,&size.x, &size.y);
}

const std::string& Game::getBasePath()
{
    return m_basePath;
}

Game::Game()
{
    t1 = SDL_GetTicks();

    m_window = nullptr;
    m_renderer = nullptr;
    m_running = false;
    m_timePassed = 1000;
    m_deltaTime = FRAME_TIME_144;

    SDL_CHECK(SDL_Init(SDL_INIT_EVERYTHING));
    int32_t flag = IMG_Init(IMG_INIT_PNG);

    if ((flag & IMG_INIT_PNG) == false)
        BREAK();

    m_basePath = SDL_GetBasePath();

    HUD::getInstance().init();
}

