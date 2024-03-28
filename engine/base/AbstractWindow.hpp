//
// Created by 33442 on 2024/3/16.
//

#ifndef ABSTRACTWINDOW_HPP
#define ABSTRACTWINDOW_HPP

#include <cstdint>

namespace Tina
{
    class AbstractWindow
    {
    public:
        virtual void create(const char* title, int width, int height) {
            this->title = title;
            this->width = width;
            this->height = height;

            initialize();
        };

        virtual void initialize() = 0;
        protected:
        int32_t width;
        int32_t height;
        const char* title;
    };
} // Tina

#endif //ABSTRACTWINDOW_HPP
