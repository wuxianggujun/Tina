//
// Created by wuxianggujun on 2025/1/30.
//
#pragma once

namespace Tina
{
    class IWindowResizeListener
    {
    public:
        virtual ~IWindowResizeListener() = default;
        virtual void onWindowResize(int width, int height) = 0;
    };
}
