//
// Created by wuxianggujun on 2024/4/28.
//

#ifndef TINA_APPLICATION_GAMEAPPLICATION_HPP
#define TINA_APPLICATION_GAMEAPPLICATION_HPP

#include "framework/Application.hpp"

namespace Tina {

    class Configuration;

    class GameApplication : public Application{
    public:
        explicit GameApplication(Configuration config);
        explicit GameApplication(const char *name, int width, int height);
        bool initialize() override;

        void close() override;

    };

} // Tina

#endif //TINA_APPLICATION_GAMEAPPLICATION_HPP
