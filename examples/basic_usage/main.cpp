#include "tina/core/Core.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"

using namespace Tina;

int main()
{
    fmt::print("Starting application...\n");

    auto& logger = Tina::Logger::instance();
    
    // 使用绝对路径
    auto logPath = ghc::filesystem::current_path() / "logs" / "app.log";
    fmt::print("Log file path: {}\n", logPath.string());
    
    // 确保日志目录存在
    ghc::filesystem::create_directories(logPath.parent_path());
    fmt::print("Log directory created: {}\n", logPath.parent_path().string());

    // 设置日志文件
    logger.setOutputFile(String(logPath.string()));
    logger.start();
    fmt::print("Logger started\n");
    
    // 设置日志级别
    logger.setMinLevel(LogLevel::Info);

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

    if (logger.isRunning())
        logger.stop();
    return 0;
}
