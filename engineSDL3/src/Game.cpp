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
    SDL_GetWindowSize(m_window, &size.x, &size.y);
    return size;
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

    //HUD::getInstance().init();
}

void Game::init()
{
    SDL_CHECK(SDL_CreateWindowAndRenderer(1920,1080,SDL_EVENT_WINDOW_SHOWN, &m_window, &m_renderer));
    SDL_SetWindowTitle(m_window, "GameTest");
    SDL_SetWindowBordered(m_window,SDL_TRUE);

    std::string path = m_basePath + "assets\\img\\icon.jpg";

    SDL_Surface* surface = IMG_Load(path.c_str());
    SDL_SetWindowIcon(m_window, surface);
    SDL_DestroySurface(surface);

    m_running = true;

    //m_world = new World();
    //m_world->load_textures(m_renderer);
}

void Game::destroy()
{
    delete m_world;
    TextureManager::getInstance().clear();

    TTF_Quit();
    IMG_Quit();

    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Game::updateEvents()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            m_running = false;
        }
    }
}

void Game::update(float dt)
{
    //m_world->update(dt);
}

void Game::render()
{
    SDL_CHECK(SDL_SetRenderDrawColor(m_renderer,57,102,0,255));
    SDL_CHECK(SDL_RenderClear(m_renderer));

    std::string text = std::to_string(int(1.f / m_deltaTime));
    //HUD::getInstance().drawText(m_renderer, text + " FPS",{0.f,0.f},48);
    //m_world->render(m_renderer);

    SDL_RenderPresent(m_renderer);
}
