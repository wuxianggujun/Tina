#pragma once

#include "../engine/Scene.hpp"
#include "../core/Memory.hpp"
#include "../renderer/ShaderManager.hpp"
#include "../game/TileMap.hpp"
#include "../ecs/World.hpp"
#include "../game/Camera2D.hpp"
#include "../renderer/TileRenderer.hpp"
#include "../ui/TextRenderer.hpp"
#include "../ui/UICore.hpp"  // UIRenderer
#include "../ui/UIToolbar.hpp"
#include "../ui/UICharacterPanel.hpp"
#include "../particles/ParticleSystem.hpp"
#include "../game/TerrainEditor.hpp"

namespace Tina::Game {

/**
 * GameScene - 主游戏场景
 *
 * 职责：
 * - 管理游戏世界（TileMap、ECS、Camera）
 * - 处理游戏输入（移动、跳跃、工具使用）
 * - 更新游戏逻辑（液体模拟、物理、粒子）
 * - 渲染游戏画面（瓦片、角色、粒子、UI）
 */
class GameScene : public Engine::Scene {
public:
    GameScene();
    ~GameScene() override;

    // 生命周期
    void onEnter() override;
    void onExit() override;

    // 主循环
    void update(float dt) override;
    void render() override;
    void handleEvent(const Tina::os::Event& event) override;

private:
    void initializeResources();
    void createGameWorld();
    void updateGameLogic(float dt);
    void updateCamera(float dt);
    void renderWorld();
    void renderUI();

    // 世界创建辅助函数
    void createTileMap();
    void createECS();
    void createCamera();
    void createUI();
    void spawnCharacters(int spawnX, int spawnY);
    bool findSpawnPoint(int& outX, int& outY);

    // 输入处理
    void handleKeyboard(const Tina::os::Event& event);
    void handleMouse(const Tina::os::Event& event);
    void handleRightClick(float mx, float my);
    void handleLeftClick(float mx, float my);

    // 工具逻辑
    void useWaterTool(int worldX, int worldY);
    void useDiggerTool(int worldX, int worldY);
    void useExplodeTool(int worldX, int worldY);

    // UI 设置
    void setupUIView();

private:
    // 渲染资源
    Memory::UniquePtr<renderer::ShaderManager> m_shaderMgr;
    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;

    Memory::UniquePtr<UI::TextRenderer> m_textRenderer;
    Memory::UniquePtr<UI::UIRenderer> m_uiRenderer;
    Memory::UniquePtr<Particles::ParticleSystem2D> m_particleSystem;
    Memory::UniquePtr<Renderer::TileRenderer> m_tileRenderer;

    // 游戏世界
    Memory::UniquePtr<TileMap> m_tileMap;
    Memory::UniquePtr<ECS::World> m_ecsWorld;
    Memory::UniquePtr<Camera2D> m_camera;

    // UI
    Memory::UniquePtr<UI::UIToolbar> m_toolbar;
    Memory::UniquePtr<UI::UICharacterPanel> m_characterPanel;

    // 游戏状态
    entt::entity m_playerEntity = entt::null;
    entt::entity m_clickedEntity = entt::null;  // 右键点击的角色
    bool m_isToolActive = false;  // 标记当前帧是否有工具激活（用于 UI 事件处理）

    // 视口尺寸
    int m_pixelWidth = 1280;
    int m_pixelHeight = 720;
};

} // namespace Tina::Game
