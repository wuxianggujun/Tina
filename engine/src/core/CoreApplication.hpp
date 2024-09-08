#ifndef TINA_CORE_APPLICATION_HPP
#define TINA_CORE_APPLICATION_HPP
#include <cstdint>
#include <string>

namespace Tina {
    class CoreApplication {
    public:
        CoreApplication(int argc,char** argv);
        virtual ~CoreApplication() = default;

        int run(bool isProtected = false);

        static bool hasBeenInterrupted;

    protected:
        virtual bool init();
        void exit();

        void createWindow(uint16_t width,uint16_t height,const std::string &title);
    };
}


#endif //TINA_CORE_APPLICATION_HPP