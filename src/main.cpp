// Tina 游戏入口 - 使用 Application + Scene 架构
//
// 架构说明：
// - Application：管理窗口、bgfx、主循环
// - MenuScene：主菜单场景
// - GameScene：游戏场景（地图、ECS、相机、UI）
// - PauseScene：暂停菜单场景
//

#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "engine/Application.hpp"
#include "engine/SceneManager.hpp"  // 需要完整定义才能调用 scenes()
#include "game/MenuScene.hpp"

using namespace Tina;

int main(int /*argc*/, char* /*argv*/[])
{
    // 初始化日志系统
    Core::Log::InitWithFile("Tina", Core::Log::Level::Info,
                            "logs/tina.log", 10ull * 1024ull * 1024ull, 5, false);
    TINA_INFO("启动 Tina 游戏引擎");

    try {
        // 配置应用
        Engine::Application::Config config;
        config.windowWidth = 1280;
        config.windowHeight = 720;
        config.windowTitle = "Tina - 2D Sandbox Game";
        config.vsync = true;
        config.msaa = 8;

        // 创建应用
        Engine::Application app(config);

        // 创建并推入主菜单场景
        auto menuScene = Memory::MakeUnique<Game::MenuScene>();
        app.scenes().push(std::move(menuScene));

        // 运行主循环（阻塞直到退出）
        app.run();

        TINA_INFO("游戏正常退出");
        return 0;

    } catch (const std::exception& e) {
        TINA_ERROR("游戏崩溃: {}", e.what());
        return -1;
    }
}
