#pragma once

// DLL export/import macros
#ifdef _WIN32
    #ifdef TINA_SHARED_LIB
        #ifdef TINA_CORE_EXPORTS
            #define TINA_CORE_API __declspec(dllexport)
        #else
            #define TINA_CORE_API __declspec(dllimport)
        #endif
    #else
        #define TINA_CORE_API
    #endif
#else
    #define TINA_CORE_API
#endif

namespace Tina {
namespace Core {

class TINA_CORE_API Engine {
public:
    Engine();
    ~Engine();

    bool initialize();
    void shutdown();

    // Get engine version
    const char* getVersion() const;
};

} // namespace Core
} // namespace Tina 