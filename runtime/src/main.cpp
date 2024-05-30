#include "TinaEngine.hpp"

int main(int argc, char *argv[]) {
    Tina::Configuration config("Tina", 1920, 1080);
    auto engine = Tina::Engine::create(createScope<Tina::GameApplication>(config));
    int result = engine->run(config);
    Tina::Engine::destroy(engine);
    return result;
}
