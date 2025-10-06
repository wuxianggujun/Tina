//
// GameScene 实现（场景骨架）
//

#include "GameScene.hpp"
#include "../engine/Application.hpp"
#include "../core/Log.hpp"

namespace Tina::Game {

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::onEnter()
{
    TINA_INFO("GameScene::onEnter - 进入游戏场景");
    initializeResources();
    createGameWorld();
}

void GameScene::onExit()
{
    TINA_INFO("GameScene::onExit - 退出游戏场景");
}

void GameScene::update(float dt)
{
    updateGameLogic(dt);
    updateCamera(dt);
}

void GameScene::render()
{
    renderWorld();
    renderUI();
}

void GameScene::handleEvent(const Tina::os::Event& event)
{
    // 键鼠事件分发
    if (event.type == Tina::os::Event::Type::KEY) {
        handleKeyboard(event);
    } else if (event.type == Tina::os::Event::Type::MOUSE_BUTTON ||
               event.type == Tina::os::Event::Type::MOUSE_MOVE) {
        handleMouse(event);
    }
}

void GameScene::initializeResources()
{
    TINA_INFO("GameScene: 初始化资源...");
    // TODO: 初始化 ShaderManager / TextRenderer / TileRenderer 等
}

void GameScene::createGameWorld()
{
    TINA_INFO("GameScene: 创建游戏世界...");
    // TODO: 创建 TileMap / ECS World / Camera 等
}

void GameScene::updateGameLogic(float dt)
{
    // TODO: 推进 ECS / 液体模拟 / 粒子等
}

void GameScene::updateCamera(float dt)
{
    // TODO: 相机逻辑
}

void GameScene::renderWorld()
{
    // TODO: 提交地表/液体/角色等
}

void GameScene::renderUI()
{
    // TODO: 提交 UI（工具栏/角色面板等）
}

void GameScene::handleKeyboard(const Tina::os::Event& /*event*/)
{
    // TODO: 处理键盘
}

void GameScene::handleMouse(const Tina::os::Event& /*event*/)
{
    // TODO: 处理鼠标
}

void GameScene::useWaterTool(int /*worldX*/, int /*worldY*/)
{
}

void GameScene::useDiggerTool(int /*worldX*/, int /*worldY*/)
{
}

void GameScene::useExplodeTool(int /*worldX*/, int /*worldY*/)
{
}

} // namespace Tina::Game