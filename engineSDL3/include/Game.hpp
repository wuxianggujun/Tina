//
// Created by WuXiangGuJun on 2023/12/13.
//

#ifndef GAME_HPP
#define GAME_HPP

#include "config.hpp"
#include "rects.hpp"
#include "mario.hpp"
#include "world.hpp"

struct SDL_Window;
struct SDL_Renderer;

class Game
{
public:
    void run();

    const Vector2i getWindowSize();
    const std::string& getBasePath();

    const uint32_t getFrameRate()
    {
        return 144;
    }

    static Game& getInstance()
    {
        return s_instance;
    }

private:
    static Game s_instance;

    Game();

    void init();
    void destroy();

    void updateEvents();
    void update(float dt);
    void render();

    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    bool m_running;
    float m_deltaTime;
    std::string m_basePath;

    uint64_t t1;
    World *m_world;
    uint64_t m_timePassed;
};

#endif //GAME_HPP
