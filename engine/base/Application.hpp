//
// Created by 33442 on 2024/3/16.
//

#ifndef APPLICATION_HPP
#define APPLICATION_HPP
#include <cstdint>
#include "BaseApplication.hpp"
#include <boost/timer/timer.hpp>

namespace Tina
{
    template<class WindowType>
    class Application : public BaseApplication
    {
    public:
        explicit Application(const char* title = "Tina", uint32_t width = 1280, uint32_t height = 768) :BaseApplication(title, width, height) {

        }
        void initialize(int argc, char** argv) override {

        }
       int run(int argc, char** argv) override {
		   //计算程序运行时间
		   boost::timer::auto_cpu_timer startTime;
           initialize(argc, argv);
           window.create(mTitle, mWidth, mHeight);
           std::cout << "Application Initialize Time: " << startTime.format() << std::endl;
           int result = window.run();
           shutdown();
           return result;
       }
       void shutdown() override {
           window.shutdown();
       }
    private:
        WindowType window;
    };
} // Tina

#endif //APPLICATION_HPP
