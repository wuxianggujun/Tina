#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"
#include <fmt/format.h>

using namespace Tina;

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

    // 运行引擎主循环
    if (!engine.run())
    {
        TINA_LOG_ERROR("Application run failed");
        return 1;
    }

    TINA_LOG_INFO("Application finished successfully");
    return 0;
}
