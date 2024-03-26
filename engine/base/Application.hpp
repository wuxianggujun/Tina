//
// Created by 33442 on 2024/3/16.
//

#ifndef APPLICATION_HPP
#define APPLICATION_HPP
#include <cstdint>

namespace Tina
{
    class Application
    {
    private:
        virtual void initialize(int _argc, char** _argv);
        virtual void shutDown();

    public:
        explicit Application(const char* title = "Tina", uint32_t width = 1280, uint32_t height = 768);
        ~Application() { Application::shutDown(); }
        virtual int run(int argc, char** argv);

    private:
        uint32_t mWidth;
        uint32_t mHeight;
        const char* mTitle;
    };
} // Tina

#endif //APPLICATION_HPP
