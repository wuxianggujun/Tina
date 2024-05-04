#include "TinaEngine.hpp"


int main(int argc, char* argv[]) {
    Tina::Configuration config("Tina",1920,1080);
    auto engine = Tina::Engine::getSingleton();
    return engine->run(std::make_unique<Tina::GameApplication>(config));
}
