//
// Created by WuXiangGuJun on 2023/12/15.
//

#ifndef COMPONENT_HPP
#define COMPONENT_HPP

#include "rects.hpp"
#include <unordered_map>
#include <string>
#include <SDL3/SDL_render.h>

#include "config.hpp"

class Component
{
public:
    Component() = default;
    virtual ~Component() = default;
};

class TransformComponent : public Component
{
public:
    TransformComponent()
    {
        transf = {0.f, 0.f, 0.f, 0.f};
        origin = {0.f, 0.f};
        flip = SDL_FLIP_NONE;
        rotation = 0.f;
    }

    void debug_draw(SDL_Renderer* renderer)
    {
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_RenderRect(renderer, &transf);
    }

private:
    Vector4f transf{};
    Vector2f origin{};
    SDL_RendererFlip flip;
    float rotation;
};

class AnimComponent : public Component
{
public:
    struct Anim
    {
        Vector2i size;
        Vector2i start;
        int32_t frame_count;
        float duration;
        int32_t offset;
        bool loop;
    };

    AnimComponent(): m_id("none"), m_time(0), m_finished(false)
    {
    }

    void addAnimation(const std::string& id, const Anim& anim)
    {
        m_animations[id] = anim;
    }

    void setAnimation(const std::string& id)
    {
        if (m_animations.find(id) == m_animations.end())
        {
            BREAK();
        }

        if (m_animations[id].loop == false)
        {
            m_time = 0.f;
        }

        this->m_id = id;
    }

    void update(float dt, SDL_Rect* rect)
    {
        m_time += dt;

        const Anim anim = m_animations[m_id];
        float scaled_time = (m_time / anim.duration);
        int32_t frame = static_cast<int>(scaled_time * anim.frame_count);

        if (anim.loop)
        {
            frame %= anim.frame_count;
        }
        else if (frame >= anim.frame_count)
        {
            frame = anim.frame_count - 1;
            m_finished = true;
        }

        *rect = {
            anim.start.x + frame * (anim.size.x + anim.offset),
            anim.start.y, anim.size.x, anim.size.y
        };
    }

    const std::string& getCurrentAnimation() const
    {
        return m_id;
    }

    bool isFinished() const
    {
        return m_finished;
    }

public:
    std::unordered_map<std::string, Anim> m_animations;
    std::string m_id;

    float m_time;
    bool m_finished;
};


#endif //COMPONENT_HPP
