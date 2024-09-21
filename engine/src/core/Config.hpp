#ifndef TINA_CORE_CONFIG_HPP
#define TINA_CORE_CONFIG_HPP

#include <cstring>

namespace Tina::Config {

    extern bool isBgfxDebugLogEnabled;
    
    void loadConfigFromFile(const char *fileName);

    void saveConfigToFile(const char *fileName);
}


#endif //TINA_CORE_CONFIG_HPP
