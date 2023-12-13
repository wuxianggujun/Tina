//
// Created by WuXiangGuJun on 2023/12/13.
//

#ifndef RECTS_HPP
#define RECTS_HPP

#include <SDL3/SDL_rect.h>
#include <cmath>

typedef SDL_FPoint Vector2f;
typedef SDL_Point Vector2i;

typedef SDL_FRect Vector4f;
typedef SDL_Rect Vector4i;

static Vector4f substract(const Vector4f& v1, const Vector4f& v2) {
    return { v2.x - v1.x, v2.y - v1.y, v2.w - v1.w, v2.h - v1.h };
}

static float get_length(const Vector4f& v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

#endif //RECTS_HPP
