#include <tina/core/Engine.hpp>
#include <tina/layer/Render2DLayer.hpp>
#include <glm/glm.hpp>
#include "tina/scene/Scene.hpp"
#include "tina/log/Logger.hpp"

using namespace Tina;

 int main() {
    // 初始化日志系统
    auto& logger = Tina::Logger::getInstance();
    logger.init("app.log", true, true);  // 文件名，是否清空文件，是否输出到控制台

    // 设置日志级别
    logger.setLogLevel(Tina::Logger::Level::Debug);

    try {
        // 创建引擎实例
        UniquePtr<Core::Engine> engine = MakeUnique<Core::Engine>();
        
        // 初始化引擎
        if (!engine->initialize()) {
            TINA_LOG_ERROR("Failed to initialize engine");
            return -1;
        }

        // 创建场景
        Scene* scene = engine->createScene("Main Scene");
        if (!scene) {
            TINA_LOG_ERROR("Failed to create scene");
            return -1;
        }
        
        // 添加Render2DLayer
        const auto render2DLayer = MakeShared<Render2DLayer>();
        scene->pushLayer(render2DLayer.get());

        // 运行引擎
        if (!engine->run()) {
            TINA_LOG_ERROR("Engine run failed");
            return -1;
        }

        // 引擎将在这里自动销毁
        return 0;
    } catch (const std::exception& e) {
        TINA_LOG_ERROR("Application error: {}", e.what());
        return 1;
    }
}
