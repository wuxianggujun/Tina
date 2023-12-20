//
// Created by WuXiangGuJun on 2023/12/13.
//

#ifndef WORLD_HPP
#define WORLD_HPP

#include <SDL3/SDL.h>
#include <deque>
#include <memory>
#include "GameObject.hpp"


class World
{

public:
    World();

    void load_textures(SDL_Renderer* renderer);
    void clear();

    void update(float dt);
    void render(SDL_Renderer* renderer);

    template<class T>
    T* addGameOject()
    {
        m_deque_objects.push_back(std::make_unique<T>(this));
        return dynamic_cast<T*>(m_deque_objects.back().get());
    }

private:
    std::deque<std::unique_ptr<GameObject>> m_deque_objects;
    bool m_game_over;

};

#endif //WORLD_HPP
