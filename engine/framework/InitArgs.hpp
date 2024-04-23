#ifndef TINA_FRAMEWORK_INITARGS_HPP
#define TINA_FRAMEWORK_INITARGS_HPP

#include <cstdint>

namespace Tina {

    struct InitArgs
    {
        const char* title = "TinaApplication";
        const char* iconFilePath = nullptr;
        uint16_t width = 800;
        uint16_t height = 600;
        bool useFullScreen = false;
    };

}

#endif
