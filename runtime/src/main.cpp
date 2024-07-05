#include "TinaEngine.hpp"

int main(int argc, char *argv[]) {
    Tina::Configuration config("Tina","", 1920, 1080);
    Scope<Tina::Application> app = createScope<Tina::GameApplication>();
    auto engine = Tina::Engine::create(std::move(app));
    int result = engine->run(config);
    Tina::Engine::destroy(engine);
    return result;
}
