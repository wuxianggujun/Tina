#include "tina/log/Logger.hpp"
#include <thread>
#include <chrono>

int main()
{
    try {
        // 初始化日志系统
        auto& logger = Tina::Logger::getInstance();
        logger.init("app.log", true, true);  // 文件名，是否清空文件，是否输出到控制台

        // 设置日志级别
        logger.setLogLevel(Tina::Logger::Level::Debug);
        logger.setFlushLevel(Tina::Logger::Level::Debug);

        // 记录一些测试日志
        TINA_LOG_DEBUG("启动应用程序");
        TINA_LOG_INFO("系统初始化完成");
        
        // 模拟一些操作
        for (int i = 0; i < 3; ++i) {
            TINA_LOG_DEBUG("处理任务 {}", i);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            TINA_LOG_INFO("任务 {} 完成", i);
        }
        
        TINA_LOG_WARNING("发现潜在问题：系统负载较高");
        TINA_LOG_ERROR("错误：无法连接到数据库");

        return 0;
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        return 1;
    }
}