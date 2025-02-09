#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"
#include "tina/scene/Components.hpp"
#include <fmt/format.h>

using namespace Tina;

void createTestScene(Scene* scene)
{
    // 创建一个相机实体
    auto camera = scene->createEntity("Main Camera");
    scene->getRegistry().emplace<TransformComponent>(camera, glm::vec3(0.0f, 0.0f, -5.0f));
    scene->getRegistry().emplace<CameraComponent>(camera);

    // 创建一个立方体实体
    auto cube = scene->createEntity("Cube");
    scene->getRegistry().emplace<TransformComponent>(cube, glm::vec3(0.0f));
    scene->getRegistry().emplace<MeshRendererComponent>(cube);

    TINA_LOG_INFO("Created test scene with camera and cube");
}

int main()
{
    fmt::print("Starting application...\n");

    auto& logger = Tina::Logger::getInstance();
    logger.init("app.log", true, true);  // 文件名，是否清空文件，是否输出到控制台
    logger.setLogLevel(Tina::Logger::Level::Debug);

    TINA_LOG_INFO("Application starting");

    // 创建引擎实例
    Core::Engine engine;

    // 初始化引擎
    if (!engine.initialize())
    {
        TINA_LOG_ERROR("Engine initialization failed");
        return -1;
    }

    // 创建测试场景
    auto* scene = engine.getActiveScene();
    createTestScene(scene);

    // 运行引擎主循环
    if (!engine.run())
    {
        TINA_LOG_ERROR("Application run failed");
        return 1;
    }

    TINA_LOG_INFO("Application finished successfully");
    return 0;
}
