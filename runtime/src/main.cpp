#include "TinaEngine.hpp"


int main(int argc, char* argv[]) {
  /*   Tina::Configuration config("Tina",1280,720);
     auto engine =  Tina::Engine::getSingleton();
     return engine->run(std::make_unique<Tina::GameApplication>(config));*/
     Tina::GlfwWindowApplication application;
     application.run();
    return 0;
}
