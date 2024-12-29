#include "TinaEngine.hpp"

using namespace Tina;

int main(int argc, char *argv[]) {
    try {
        Path configFilePath("../resources/config/textures.yaml");
        ScopePtr<GameApplication> app = createScopePtr<GameApplication>(configFilePath);
        app->run();
    } catch (const std::exception &e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
