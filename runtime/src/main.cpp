#include "TinaEngine.hpp"


int main(int argc, char *argv[]) {
    Tina::Configuration config("Tina", 1920, 1080);
    auto engine = Tina::Engine::getSingleton();
    auto result = engine->run(new Tina::GameApplication(config));
    delete engine;
    return result;
}
