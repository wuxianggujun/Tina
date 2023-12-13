//
// Created by WuXiangGuJun on 2023/12/13.
//

#ifndef HUD_HPP
#define HUD_HPP

#include "config.hpp"
#include "rects.hpp"
#include <SDL3/SDL_ttf.h>

class HUD
{
public:
    static HUD& getInstance();

    void init();
    void drawText(SDL_Renderer* renderer, const std::string* text, const Vector2f& pos, float charSize);

private:
    static HUD* s_instance;
    HUD();

    TTF_Font* m_font;
};

#endif //HUD_HPP
