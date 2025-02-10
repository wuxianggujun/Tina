#include <memory>
#include <tina/core/Engine.hpp>
#include <tina/layer/Render2DLayer.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
        auto engine = std::make_shared<Core::Engine>();
        
        // 初始化引擎
        if (!engine->initialize()) {
            return -1;
        }

        // 创建场景
        const auto scene = std::make_shared<Scene>("Main Scene");
        
        // 添加Render2DLayer
        const auto render2DLayer = std::make_shared<Render2DLayer>();
        scene->pushLayer(render2DLayer);

        // 设置场景
        engine->setActiveScene(scene.get());

        // 运行引擎
        engine->run();

        return 0;
    } catch (const std::exception& e) {
        TINA_LOG_ERROR("Application error: {}", e.what());
        return 1;
    }
}
