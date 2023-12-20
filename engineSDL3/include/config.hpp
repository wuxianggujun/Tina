//
// Created by WuXiangGuJun on 2023/12/13.
//

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <cstdint>

#define SDL_CHECK(code) if (code < 0) __debugbreak();
#define BREAK() __debugbreak();

constexpr auto FRAME_TIME_144 = 1.F/144.F;
constexpr auto FRAME_TIME_60= 1.F/60.F;

#endif //CONFIG_HPP
