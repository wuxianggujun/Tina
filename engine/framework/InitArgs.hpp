#ifndef TINA_FRAMEWORK_INITARGS_HPP
#define TINA_FRAMEWORK_INITARGS_HPP

#include <cstdint>

namespace Tina {

    struct InitArgs
    {
        const char* title = "TinaApplication";
        const char* iconFilePath = nullptr;
        uint32_t width = 800;
        uint32_t height = 600;
        bool useFullScreen = false;
    };

}

#endif
