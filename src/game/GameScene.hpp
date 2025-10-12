#pragma once

#include "../engine/Scene.hpp"
#include "../core/Memory.hpp"
#include "../renderer/ShaderManager.hpp"
#include "../game/TileMap.hpp"
#include "../ecs/World.hpp"
// Camera2D现在由Scene基类提供
#include "../renderer/TileRenderer.hpp"
#include "../ui/UICore.hpp"  // UIRenderer
#include "../ui/UIToolbar.hpp"
#include "../ui/UICharacterPanel.hpp"
#include "../particles/ParticleSystem.hpp"
#include "../game/TerrainEditor.hpp"
#include "DayNight.hpp"
#include "GameEvents.hpp"
#include "../engine/Resource.hpp"
#include "../engine/Texture.hpp"
#include "../engine/AudioResource.hpp"
#include "../engine/EventSystem.hpp"
#include "../engine/SubscriptionToken.hpp"  // 添加订阅令牌管理
#include "../engine/EventHelpers.hpp"  // 添加事件辅助工具

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
    void onPause() override;
    void onResume() override;
    // 注意：不再需要覆盖onWindowSizeChanged，UI组件已通过registerResizable注册到框架

    // 主循环
    void update(float dt) override;
    void render() override;
    // 事件处理已迁移到 InputSystem

protected:
    // 视图配置（使用新架构）
    Container::Vector<ViewSetup> getViewSetup() override;

private:
    void initializeResources();
    void createGameWorld();
    void updateGameLogic(float dt);
    void updateCamera(float dt);
    void renderWorld();
    void renderUI();
    void ensureToolbarIconsReady();

    // 世界创建辅助函数
    void createTileMap();
    void createECS();
    void createCamera();
    void createUI();
    void spawnCharacters(int spawnX, int spawnY);
    bool findSpawnPoint(int& outX, int& outY);

    // 输入处理
    void handleInput();
    void handleRightClick(float mx, float my);
    void handleLeftClick(float mx, float my);

    // 工具逻辑
    void useWaterTool(int worldX, int worldY);
    void useDiggerTool(int worldX, int worldY);
    void useExplodeTool(int worldX, int worldY);

    // UI 设置
    // 事件订阅管理（OS 输入 + 强类型事件）
    void subscribeToEvents();
    void triggerPlayerEvents(float prevX, float prevY, bool wasOnGround);

private:
    // 渲染资源（着色器来自全局 ShaderManager）

    // Memory::UniquePtr<UI::UIRenderer> m_uiRenderer;  //  已由 Scene 基类管理
    Memory::UniquePtr<Particles::ParticleSystem2D> m_particleSystem;
    Memory::UniquePtr<Renderer::TileRenderer> m_tileRenderer;

    // 游戏世界
    Memory::UniquePtr<TileMap> m_tileMap;
    Memory::UniquePtr<ECS::World> m_ecsWorld;
    // Camera2D现在由Scene基类提供，不需要自己的了

    // UI
    Memory::UniquePtr<UI::UIToolbar> m_toolbar;
    Memory::UniquePtr<UI::UICharacterPanel> m_characterPanel;
    Container::Vector<Memory::UniquePtr<UI::UINode>> m_ownedNodes;  // 统一管理UI节点生命周期

    // Signal 连接管理（RAII 自动断开）
    Core::Signal<>::Connection m_switchControlConnection;
    Core::Signal<int, bool>::Connection m_keyPressedConnection;
    Core::Signal<float>::Connection m_mouseWheelConnection;

    // 事件订阅管理器（RAII 自动取消订阅）
    Engine::SubscriptionManager m_eventSubscriptions;

    // 游戏状态
    entt::entity m_playerEntity = entt::null;
    entt::entity m_clickedEntity = entt::null;  // 右键点击的角色
    bool m_isToolActive = false;  // 标记当前帧是否有工具激活（用于 UI 事件处理）

    // 视口尺寸
    int m_pixelWidth = 1280;
    int m_pixelHeight = 720;

    // 工具栏图标资源（通过 TextureManager 加载）
    Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource> m_iconWater;
    Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource> m_iconClean;
    Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource> m_iconBomb;

    // 音效资源：游戏界面专用（非菜单/暂停）
    Tina::Engine::ResourceRef<Tina::Engine::AudioResource> m_sfxYingxiao;
    bool m_sfxStarted = false;

    // 背景音乐（BGM）：游戏界面循环播放
    Tina::Engine::ResourceRef<Tina::Engine::AudioResource> m_bgm;
    bool m_bgmStarted = false;

    // 昼夜系统（始终推进，不支持暂停）
    DayNight m_dayNight;
    // 强类型事件处理
    void onSetDayNight(const Tina::Game::Events::SetDayNight& e);
    void onAdjustDayNight(const Tina::Game::Events::AdjustDayNight& e);
    void onPlayerJumpedEvt(const Tina::Game::Events::PlayerJumped& e);
    void onPlayerMovedEvt(const Tina::Game::Events::PlayerMoved& e);
};

} // namespace Tina::Game
