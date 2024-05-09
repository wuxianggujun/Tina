//
// Created by wuxianggujun on 2024/4/28.
//

#ifndef TINA_APPLICATION_GAMEAPPLICATION_HPP
#define TINA_APPLICATION_GAMEAPPLICATION_HPP

#include "framework/Application.hpp"

namespace Tina {

    class Configuration;
    class Window;

    class GameApplication : public Application{
    public:
        explicit GameApplication(const Configuration &configuration);

        bool initialize() override;
        bool isRunning() override;
        void run() override;
        void close() override;
    private:
        Window * window = nullptr;
    };

} // Tina

#endif //TINA_APPLICATION_GAMEAPPLICATION_HPP
