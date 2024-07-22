
#include <SDL3/SDL.h>
#include "TinaEngine.hpp"

#define CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>
#include <iostream>
#include <typeinfo>


int main(int argc, char* argv[]) { 
    Tina::GameApplication application;
    application.run();
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtDumpMemoryLeaks();

    const std::type_info& info = typeid(application);
    std::cout << "Type of application: " << info.hash_code() << " " << info.name() << std::endl;
    return 0;
}
