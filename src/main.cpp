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
#include "game/GameScene.hpp"
#include "game/Smoke3DScene.hpp"
#include "game/WorldSelectScene.hpp"

#include <charconv>
#include <string_view>

using namespace Tina;

int main(int argc, char* argv[])
{
    // 初始化日志系统
    Core::Log::InitWithFile("Tina", Core::Log::Level::Trace,
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

        enum class StartupScene { Menu, Game2D, UI, Smoke3D };
        StartupScene startupScene = StartupScene::Menu;
        constexpr std::string_view framePrefix = "--smoke-frames=";
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i] ? argv[i] : "";
            if (argument == "--smoke-game") {
                startupScene = StartupScene::Game2D;
                continue;
            }
            if (argument == "--smoke-3d") {
                startupScene = StartupScene::Smoke3D;
                continue;
            }
            if (argument == "--smoke-ui") {
                startupScene = StartupScene::UI;
                continue;
            }
            if (argument.starts_with(framePrefix)) {
                const std::string_view value = argument.substr(framePrefix.size());
                uint64_t frames = 0;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), frames);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || frames == 0) {
                    TINA_ERROR("无效参数: {}", argument);
                    return 2;
                }
                config.maxFrames = frames;
            }
        }

        switch (startupScene) {
            case StartupScene::UI:
                config.windowTitle = "Tina - UI Smoke";
                break;
            case StartupScene::Smoke3D:
                config.windowTitle = "Tina - 3D Smoke";
                break;
            case StartupScene::Menu:
            case StartupScene::Game2D:
                break;
        }

        // 创建应用（不使用IApplication扩展，传nullptr）
        Engine::Application app(nullptr, config);
        if (!app.isInitialized()) {
            TINA_ERROR("Tina 初始化失败");
            return 1;
        }

        // 冒烟模式可直接进入完整2D世界和自研UI；默认仍进入主菜单。
        if (startupScene == StartupScene::Game2D) {
            app.scenes().push(Memory::MakeUnique<Game::GameScene>(12345u));
        } else if (startupScene == StartupScene::UI) {
            app.scenes().push(Memory::MakeUnique<Game::WorldSelectScene>(true));
        } else if (startupScene == StartupScene::Smoke3D) {
            app.scenes().push(Memory::MakeUnique<Game::Smoke3DScene>());
        } else {
            app.scenes().push(Memory::MakeUnique<Game::MenuScene>());
        }

        // 运行主循环（阻塞直到退出）
        app.run();

        TINA_INFO("游戏正常退出");
        return 0;

    } catch (const std::exception& e) {
        TINA_ERROR("游戏崩溃: {}", e.what());
        return -1;
    }
}
