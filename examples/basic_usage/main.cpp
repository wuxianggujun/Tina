#include "tina/core/Core.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"

using namespace Tina;

int main()
{
    fmt::print("Starting application...\n");

    auto& logger = Tina::Logger::getInstance();
    logger.init("app.log", true, true);  // 文件名，是否清空文件，是否输出到控制台
    logger.setLogLevel(Tina::Logger::Level::Debug);

    TINA_LOG_INFO("main", "Application starting...");

    // Create engine instance
    Core::Engine engine;

    // Initialize engine
    if (!engine.initialize())
    {
        TINA_LOG_ERROR("main", "Engine initialization failed.");
        return -1;
    }

    if (!engine.run())
    {
        TINA_LOG_ERROR("main", "Application run failed.");
        engine.shutdown();
        return 1;
    }

    // Shutdown engine
    engine.shutdown();
    TINA_LOG_INFO("main", "Application finished successfully.");

    return 0;
}
