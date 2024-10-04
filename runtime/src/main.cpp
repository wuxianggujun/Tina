#include "TinaEngine.hpp"

int main(int argc, char* argv[]) {
    Tina::ParserYamlConfig config("../resources/config/textures.yaml");
    Tina::GameApplication application;
    application.run();
    return 0;
}
