//
// GameScene 实现 - 完整游戏逻辑
// 从 main.cpp 迁移而来
//

#include "GameScene.hpp"
#include "PauseScene.hpp"  // 暂停场景
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"  // 场景管理器
#include "../engine/InputSystem.hpp"  // 添加 InputSystem 头文件
// Camera2D由Scene基类提供
#include "../core/Log.hpp"
#include "../core/Time.hpp"
#include "../game/CoordinateMapper.hpp"
#include "../game/TerrainEditor.hpp"
#include "../game/GameConfig.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include "../ui/UIConstants.hpp"
#include <algorithm>
#include <cmath>
#include "../renderer/ShaderCatalog.hpp"

namespace Tina::Game {

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

// 视图配置（使用新架构）
Container::Vector<Engine::Scene::ViewSetup> GameScene::getViewSetup() {
    return {
        { UI::VIEW_WORLD_SOLID, Engine::Scene::ViewSetup::World3D, true, GameConfig::CLEAR_COLOR, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH },
        { UI::VIEW_WORLD_ALPHA, Engine::Scene::ViewSetup::World3D, false },
        { UI::VIEW_UI, Engine::Scene::ViewSetup::UI2D, false }
    };
}

void GameScene::onEnter()
{
    TINA_INFO("GameScene::onEnter - 进入游戏场景");

    // 获取窗口尺寸（Scene基类会在applyWindowResize中自动更新）

    initializeResources();
    createGameWorld();

    // 加载并准备音效（仅在游戏界面播放）
    if (auto* hub = &app()->resources()) {
        auto* sfx = hub->load<Tina::Engine::AudioResource>(Tina::Engine::Path("resources/audio/yingxiao.mp3"));
        if (sfx) {
            m_sfxYingxiao = Tina::Engine::ResourceRef<Tina::Engine::AudioResource>(hub, sfx);
            m_sfxYingxiao->setTag("sfx"); // 标记为音效分组
            m_sfxStarted = false; // 等待资源READY后在update里触发播放
        } else {
            TINA_WARN("GameScene: 加载音效失败 resources/audio/yingxiao.mp3");
        }

        // 加载并准备 BGM（循环播放，仅游戏界面），路径配置见 GameConfig
        auto* bgmRes = hub->load<Tina::Engine::AudioResource>(Tina::Engine::Path(Tina::GameConfig::BGM_PATH));
        if (bgmRes) {
            m_bgm = Tina::Engine::ResourceRef<Tina::Engine::AudioResource>(hub, bgmRes);
            m_bgm->setTag("music");
            m_bgmStarted = false;
        } else {
            TINA_WARN("GameScene: 加载 BGM 失败 {}", Tina::GameConfig::BGM_PATH);
        }
    }
}

void GameScene::onExit()
{
    TINA_INFO("GameScene::onExit - 退出游戏场景");

    // 事件订阅由 SubscriptionManager 自动管理，析构时会自动取消所有订阅
    // 不再需要 m_isExiting 标志

    // 清理 ECS 世界
    m_playerEntity = entt::null;  // 先标记玩家实体为无效
    if (m_ecsWorld) {
        m_ecsWorld.reset();  // 销毁 ECS 世界
    }

    // 停止并释放游戏界面音效
    if (m_sfxYingxiao) {
        m_sfxYingxiao->stop();
        m_sfxYingxiao.reset();
        m_sfxStarted = false;
    }

    // 停止并释放 BGM（淡出）
    if (m_bgm) {
        m_bgm->stop(Tina::GameConfig::BGM_FADEOUT_MS);
        m_bgm.reset();
        m_bgmStarted = false;
    }
    
    // 清理资源（按创建逆序）
    m_characterPanel.reset();
    m_toolbar.reset();
    // 相机由基类管理，不需要手动reset
    // m_ecsWorld 已在开头清理
    m_tileMap.reset();

    m_tileRenderer.reset();
    m_particleSystem.reset();
    

    // 渲染程序由各渲染器管理，无需手动销毁
}

void GameScene::onPause()
{
    TINA_INFO("GameScene::onPause - 游戏暂停");

    // 游戏暂停时，update() 不会被调用，但渲染资源保持有效
    if (m_sfxYingxiao && m_sfxStarted) {
        m_sfxYingxiao->pause();
    }
    if (m_bgm && m_bgmStarted) {
        m_bgm->pause();
    }
}

void GameScene::onResume()
{
    TINA_INFO("GameScene::onResume - 游戏恢复");

    // 窗口尺寸已由Scene基类自动管理
    TINA_INFO("GameScene::onResume - 当前窗口尺寸: {}x{}", getPixelWidth(), getPixelHeight());

    // 更新相机视口
    if (camera()) {
        camera()->setViewportPixels(getPixelWidth(), getPixelHeight());

        // 立即将相机定位到当前控制的角色
        // 这比保存/恢复相机位置更直接、更可靠
        if (m_ecsWorld) {
            auto controlled = m_ecsWorld->getControlledEntity();
            if (controlled != entt::null) {
                auto& reg = m_ecsWorld->registry();
                if (reg.any_of<ECS::Transform, ECS::PhysicsBody>(controlled)) {
                    auto& tr = reg.get<ECS::Transform>(controlled);
                    auto& pb = reg.get<ECS::PhysicsBody>(controlled);

                    // 计算角色中心点
                    float centerX = tr.x + pb.width * 0.5f;
                    float centerY = tr.y + pb.height * 0.5f;

                    // 将相机居中到角色
                    float viewW = camera()->viewW();
                    float viewH = camera()->viewH();
                    float camX = centerX - viewW * 0.5f;
                    float camY = centerY - viewH * 0.5f;

                    // 限制相机边界
                    if (m_tileMap) {
                        int mapW = m_tileMap->width();
                        int mapH = m_tileMap->height();
                        camX = std::clamp(camX, 0.0f, std::max(0.0f, (float)mapW - viewW));
                        camY = std::clamp(camY, 0.0f, std::max(0.0f, (float)mapH - viewH));
                    }

                    camera()->setPosition(camX, camY);
                    TINA_INFO("相机重新定位到角色位置: ({}, {})", centerX, centerY);
                }
            }
        }
    }

    // 更新 UI 组件
    if (m_toolbar) {
        // ✅ 确保工具栏可见（可能在切换角色控制时被隐藏了）
        if (m_toolbar->root()) {
            m_toolbar->root()->setVisible(true);
        }
        m_toolbar->onResize(getPixelWidth(), getPixelHeight());

        // 确保工具栏图标已加载（以防资源加载延迟）
        ensureToolbarIconsReady();
    }

    if (m_characterPanel) {
        m_characterPanel->centerOnScreen(getPixelWidth(), getPixelHeight());
    }

    // 重新设置 UI 视图（view 3）
    setupUIView(uiViewId(), getPixelWidth(), getPixelHeight());

    // 恢复音频
    if (m_sfxYingxiao && m_sfxStarted) {
        m_sfxYingxiao->resume();
    }
    if (m_bgm && m_bgmStarted) {
        m_bgm->resume();
    }
}

void GameScene::onWindowSizeChanged(int width, int height)
{
    TINA_INFO("GameScene::onWindowSizeChanged - 窗口尺寸变化: {}x{}", width, height);

    // 基类Scene已经更新了m_pixelWidth和m_pixelHeight
    // 但我们需要确保相关组件也立即更新

    // 更新UI视图
    setupUIView(uiViewId(), width, height);

    // 通知工具栏尺寸变化（虽然框架会自动调用，但确保立即生效）
    if (m_toolbar) {
        m_toolbar->onResize(width, height);
    }

    if (m_characterPanel) {
        m_characterPanel->centerOnScreen(width, height);
    }

    // 重要：窗口大小改变后，需要重新计算相机位置
    // 因为视野宽度改变了，相机需要重新定位以保持角色居中
    if (camera() && m_ecsWorld) {
        float viewW = camera()->viewW();
        float viewH = camera()->viewH();

        auto controlled = m_ecsWorld->getControlledEntity();
        if (controlled != entt::null) {
            auto& reg = m_ecsWorld->registry();
            if (reg.any_of<ECS::Transform, ECS::PhysicsBody>(controlled)) {
                auto& tr = reg.get<ECS::Transform>(controlled);
                auto& pb = reg.get<ECS::PhysicsBody>(controlled);

                // 计算角色中心点
                float centerX = tr.x + pb.width * 0.5f;
                float centerY = tr.y + pb.height * 0.5f;

                // 立即更新相机位置（不使用平滑）
                float camX = centerX - viewW * 0.5f;
                float camY = centerY - viewH * 0.5f;

                // 限制相机边界
                if (m_tileMap) {
                    int mapW = m_tileMap->width();
                    int mapH = m_tileMap->height();
                    camX = std::clamp(camX, 0.0f, std::max(0.0f, (float)mapW - viewW));
                    camY = std::clamp(camY, 0.0f, std::max(0.0f, (float)mapH - viewH));
                }

                camera()->setPosition(camX, camY);
                TINA_DEBUG("窗口大小改变后重新定位相机: ({:.1f}, {:.1f}), 视野: {:.1f}x{:.1f}",
                          camX, camY, viewW, viewH);
            }
        }
    }
}

void GameScene::update(float dt)
{
    // 处理输入
    handleInput();

    updateGameLogic(dt);
    updateCamera(dt);
    ensureToolbarIconsReady();

    // 昼夜推进
    m_dayNight.update(dt);

    // 若音效已加载完成且尚未开始，则立即播放一次
    if (!m_sfxStarted && m_sfxYingxiao) {
        auto state = m_sfxYingxiao->getState();
        if (state == Tina::Engine::Resource::State::READY) {
            // 非循环播放，音量可按需调整，带轻微淡入
            m_sfxYingxiao->setVolume(1.0f);
            if (m_sfxYingxiao->play(false, 150)) {
                m_sfxStarted = true;
                TINA_INFO("GameScene: 已播放音效 yingxiao.mp3");
            }
        }
    }

    // 若 BGM 已加载完成且尚未开始，则循环并淡入播放
    if (!m_bgmStarted && m_bgm) {
        auto state = m_bgm->getState();
        if (state == Tina::Engine::Resource::State::READY) {
            m_bgm->setVolume(Tina::GameConfig::BGM_VOLUME);
            if (m_bgm->play(true, Tina::GameConfig::BGM_FADEIN_MS)) {
                m_bgmStarted = true;
                TINA_INFO("GameScene: BGM 播放中 ({})", Tina::GameConfig::BGM_PATH);
            }
        }
    }
}

void GameScene::render()
{
    renderWorld();
    // 夜色叠加（绘制在 UI 视图，但早于 UI，确保不影响 UI 可见性）
    // 使用基类提供的 ui() 方法访问 UIRenderer
    {
        const float a = m_dayNight.overlayAlpha();
        if (a > 0.001f) {
            ui().drawRect(3, 0.0f, 0.0f, (float)getPixelWidth(), (float)getPixelHeight(),
                                   0.0f, 0.0f, 0.0f, a);
        }
    }
    renderUI();
}

// handleEvent 已删除，输入处理移至 update() 中使用 InputSystem

void GameScene::handleInput()
{
    // 使用 InputSystem 进行输入处理
    auto* appPtr = app();
    if (!appPtr) return;
    auto& input = appPtr->input();

    // 键盘输入处理
    if (input.isKeyPressed(Engine::KeyCode::Escape)) {
        // 进入暂停菜单
        TINA_INFO("GameScene: 按下 ESC，进入暂停菜单");
        appPtr->scenes().requestPush(Memory::MakeUnique<PauseScene>());
        return;
    }

    // 鼠标输入处理
    auto mousePos = input.getMousePosition();
    float mx = mousePos.x, my = mousePos.y;

    // 右键：查看角色信息
    if (input.isMouseButtonPressed(Engine::MouseButton::Right)) {
        handleRightClick(mx, my);
    }

    // 左键：工具栏或地形编辑
    if (input.isMouseButtonPressed(Engine::MouseButton::Left)) {
        m_isToolActive = true;

        // 检查是否点击工具栏
        if (m_toolbar && m_toolbar->hitTest(mx, my)) {
            // TINA_INFO("工具栏命中测试通过: 鼠标({}, {}), 窗口尺寸: {}x{}", mx, my, getPixelWidth(), getPixelHeight());
            if (m_toolbar->clickAt(mx, my)) {
                return;
            }
        } // else if (m_toolbar) {
            // TINA_INFO("工具栏命中测试失败: 鼠标({}, {}), 窗口尺寸: {}x{}", mx, my, getPixelWidth(), getPixelHeight());
        // }

        // 地形编辑工具
        handleLeftClick(mx, my);
    }

    // 当鼠标按钮释放时，停止工具活动
    if (input.isMouseButtonReleased(Engine::MouseButton::Left)) {
        m_isToolActive = false;
    }

    // 鼠标中键：拖动相机视图
    if (camera() && input.isMouseButtonDown(Engine::MouseButton::Middle)) {
        auto delta = input.getMouseDelta();
        // 反向移动相机（拖动感觉更自然）
        float worldScale = camera()->getZoom();
        camera()->moveBy(-delta.x / worldScale, -delta.y / worldScale);
    }

    // 鼠标滚轮：切换工具
    float wheelDelta = input.getMouseWheelDelta();
    if (wheelDelta != 0.0f && m_toolbar) {
        int currentIndex = m_toolbar->selectedIndex();
        int slotCount = m_toolbar->slotCount();
        if (slotCount > 0) {
            int newIndex = currentIndex;
            if (wheelDelta > 0) {
                // 向上滚动，切换到上一个工具
                newIndex = (currentIndex - 1 + slotCount) % slotCount;
            } else {
                // 向下滚动，切换到下一个工具
                newIndex = (currentIndex + 1) % slotCount;
            }
            m_toolbar->select(newIndex);
            TINA_INFO("GameScene: 滚轮切换工具 {} -> {}", currentIndex, newIndex);
        }
    }

    // 数字键1-8：快速切换工具
    for (int i = 0; i < 8; ++i) {
        auto key = static_cast<Engine::KeyCode>('1' + i);
        if (input.isKeyPressed(key) && m_toolbar) {
            if (i < m_toolbar->slotCount()) {
                m_toolbar->select(i);
                TINA_INFO("GameScene: 数字键切换工具 {}", i);
            }
        }
    }
}

void GameScene::initializeResources()
{
    TINA_INFO("GameScene: 初始化资源...");

    // 1. 着色器程序（来自全局 ShaderManager）
    // 世界基础管线由各渲染器内部持有（TileRenderer/CharacterRenderSystem）

    // 2. 文本渲染器
    // 改用全局 TextRenderer
    // 使用全局 TextRenderer（默认 32 号），避免在场景内切换字号
    #if 0
    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 24);
    }
    #endif

    // 3. 粒子系统
    m_particleSystem = Memory::MakeUnique<Particles::ParticleSystem2D>();
    if (!m_particleSystem->initialize(app()->shaders())) {
        TINA_WARN("ParticleSystem 初始化失败：无粒子效果");
    } else {
        m_particleSystem->setGlobalAcceleration(GameConfig::PARTICLE_GRAVITY_X,
                                                 GameConfig::PARTICLE_GRAVITY_Y);
        m_particleSystem->setDrag(GameConfig::PARTICLE_DRAG);
    }

    // 4. 瓦片渲染器
    m_tileRenderer = Memory::MakeUnique<Renderer::TileRenderer>();
    m_tileRenderer->initialize(app()->shaders());

    // 设置 UI 视图（view 3，像素坐标）
    setupUIView(uiViewId(), getPixelWidth(), getPixelHeight());

    TINA_INFO("GameScene: 资源初始化完成");
}

void GameScene::createGameWorld()
{
    TINA_INFO("GameScene: 创建游戏世界...");

    createTileMap();
    createECS();
    createCamera();
    createUI();

    // 查找出生点并生成角色
    int spawnX = 0, spawnY = 0;
    if (!findSpawnPoint(spawnX, spawnY)) {
        TINA_WARN("未找到合适出生点，使用地图中心");
        spawnX = m_tileMap->width() / 2;
        spawnY = m_tileMap->height() / 2;
    }
    spawnCharacters(spawnX, spawnY);

    TINA_INFO("GameScene: 游戏世界创建完成");
}

void GameScene::createTileMap()
{
    TileMapConfig mapCfg;
    mapCfg.width = GameConfig::DEFAULT_MAP_WIDTH;
    mapCfg.height = GameConfig::DEFAULT_MAP_HEIGHT;
    mapCfg.seed = GameConfig::DEFAULT_MAP_SEED;

    m_tileMap = Memory::MakeUnique<TileMap>(mapCfg);
    m_tileMap->generate();
    TINA_INFO("地图创建完成: {}x{}, 种子={}", mapCfg.width, mapCfg.height, mapCfg.seed);
}

void GameScene::createECS()
{
    m_ecsWorld = Memory::MakeUnique<ECS::World>();
    m_ecsWorld->initializeRenderers(app()->shaders());
    TINA_INFO("ECS 世界创建完成");
}

void GameScene::createCamera()
{
    // 使用基类提供的相机，只需要配置它
    float viewH = std::min(GameConfig::DEFAULT_VIEW_HEIGHT, (float)m_tileMap->height());
    if (camera()) {
        camera()->setViewportPixels(getPixelWidth(), getPixelHeight());
        camera()->setViewHeightWorld(viewH);
        TINA_INFO("相机配置完成: 视图高度={}", viewH);
    }
}

void GameScene::createUI()
{
    // UI 渲染器由基类 Scene 管理，通过 ui() 方法访问

    // 工具栏
    m_toolbar = Memory::MakeUnique<UI::UIToolbar>();
    m_toolbar->initialize(getPixelWidth(), getPixelHeight(), ui());
    // 注册到框架，自动处理窗口resize
    addUIRoot(m_toolbar.get());

    // 通过 TextureManager 加载工具图标（示例使用现有纹理资源）
    {
        auto* hub = &app()->resources();
        if (auto* t0 = hub->load<Tina::Engine::Texture2DResource>(Tina::Engine::Path("resources/textures/player.png"))) {
            m_iconWater = Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource>(hub, t0);
            m_toolbar->setSlotIcon(0, t0->handle());
        }
        if (auto* t1 = hub->load<Tina::Engine::Texture2DResource>(Tina::Engine::Path("resources/textures/grassland.png"))) {
            m_iconClean = Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource>(hub, t1);
            m_toolbar->setSlotIcon(1, t1->handle());
        }
        if (auto* t2 = hub->load<Tina::Engine::Texture2DResource>(Tina::Engine::Path("resources/textures/dirt_block.png"))) {
            m_iconBomb = Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource>(hub, t2);
            m_toolbar->setSlotIcon(2, t2->handle());
        }
    }

    // 角色面板
    m_characterPanel = Memory::MakeUnique<UI::UICharacterPanel>();
    m_characterPanel->centerOnScreen(getPixelWidth(), getPixelHeight());
    // 注册到框架，自动处理窗口resize
    addUIRoot(m_characterPanel.get());

    TINA_INFO("UI 创建完成");
}

bool GameScene::findSpawnPoint(int& outX, int& outY)
{
    if (!m_tileMap) return false;

    int mapW = m_tileMap->width();
    int mapH = m_tileMap->height();
    int centerX = mapW / 2;

    // Lambda：判断是否为自然地面
    auto isNaturalGround = [&](TileType t) { return m_tileMap->isNaturalGround(t); };

    // Lambda：在指定列查找出生点（向下扫描，寻找地面 + 两格空气）
    auto findSpawnInColumn = [&](int cx, int& outYLocal) -> bool {
        if (cx < 0 || cx >= mapW) return false;
        for (int y = mapH - GameConfig::SPAWN_OFFSET_FROM_BOTTOM; y >= 0; --y) {
            auto base = m_tileMap->get(cx, y);
            if (!isNaturalGround(base)) continue;
            if (y + 2 >= mapH) continue;

            auto up1 = m_tileMap->get(cx, y + 1);
            auto up2 = m_tileMap->get(cx, y + 2);

            // 确保上方两格为空气，且无液体
            if (up1 == TileType::Air && up2 == TileType::Air &&
                !m_tileMap->isWater(cx, y + 1) && !m_tileMap->isWater(cx, y + 2) &&
                !m_tileMap->isLava(cx, y + 1) && !m_tileMap->isLava(cx, y + 2)) {
                outYLocal = y + 1;
                return true;
            }
        }
        return false;
    };

    // 1. 优先尝试地图中心
    bool found = findSpawnInColumn(centerX, outY);
    if (found) {
        outX = centerX;
        return true;
    }

    // 2. 螺旋搜索中心附近
    int maxRadius = std::min(GameConfig::SPAWN_SEARCH_MAX_RADIUS, mapW / 2);
    for (int dx = 1; dx <= maxRadius && !found; ++dx) {
        int left = centerX - dx;
        int right = centerX + dx;
        if (findSpawnInColumn(left, outY)) {
            outX = left;
            return true;
        }
        if (findSpawnInColumn(right, outY)) {
            outX = right;
            return true;
        }
    }

    return false;
}

void GameScene::spawnCharacters(int spawnX, int spawnY)
{
    // 1. 玩家
    m_playerEntity = m_ecsWorld->createCharacter((float)spawnX, (float)spawnY, true);
    TINA_INFO("玩家生成: ({}, {})", spawnX, spawnY);

    // 2. NPC（绿色）
    auto npc1 = m_ecsWorld->createCharacter(
        (float)spawnX + GameConfig::NPC_SPAWN_OFFSET_1, (float)spawnY, false);
    m_ecsWorld->registry().get<ECS::Renderable>(npc1).r = 0.2f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc1).g = 1.0f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc1).b = 0.2f;

    // 3. NPC（蓝色）
    auto npc2 = m_ecsWorld->createCharacter(
        (float)spawnX - GameConfig::NPC_SPAWN_OFFSET_2, (float)spawnY, false);
    m_ecsWorld->registry().get<ECS::Renderable>(npc2).r = 0.2f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc2).g = 0.5f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc2).b = 1.0f;

    // 4. NPC（黄色）
    auto npc3 = m_ecsWorld->createCharacter(
        (float)spawnX + GameConfig::NPC_SPAWN_OFFSET_3, (float)spawnY, false);
    m_ecsWorld->registry().get<ECS::Renderable>(npc3).r = 1.0f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc3).g = 1.0f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc3).b = 0.2f;

    // 5. 角色面板切换控制处理
    // TODO: 使用事件系统替代 Signal

    // 6. 订阅事件（OS 输入 + 强类型玩法事件）
    subscribeToEvents();

    // 7. 初始化相机位置到玩家位置（避免从地图左下角开始）
    if (camera() && m_ecsWorld && m_playerEntity != entt::null) {
        auto& registry = m_ecsWorld->registry();
        if (registry.valid(m_playerEntity) && registry.all_of<ECS::Transform>(m_playerEntity)) {
            auto& playerTransform = registry.get<ECS::Transform>(m_playerEntity);
            // 将相机中心设置到玩家位置
            float viewW = camera()->viewW();
            float viewH = camera()->viewH();
            float initialCamX = playerTransform.x - viewW * 0.5f;
            float initialCamY = playerTransform.y - viewH * 0.5f;
            camera()->setPosition(initialCamX, initialCamY);
            TINA_INFO("相机初始化到玩家位置: ({}, {})", playerTransform.x, playerTransform.y);
        }
    }

    TINA_INFO("角色生成完成: 1 玩家 + 3 NPC");
}

void GameScene::updateGameLogic(float dt)
{
    // 1. 获取键盘输入（使用InputSystem）
    auto* appPtr = app();
    if (!appPtr) return;
    auto& inputSys = appPtr->input();

    // 2. ECS 输入与更新（物理、碰撞、AI 等）
    ECS::InputState input{};
    input.moveLeft = inputSys.isKeyDown(Engine::KeyCode::A) ||
                    inputSys.isKeyDown(Engine::KeyCode::Left);
    input.moveRight = inputSys.isKeyDown(Engine::KeyCode::D) ||
                     inputSys.isKeyDown(Engine::KeyCode::Right);
    input.jump = inputSys.isKeyDown(Engine::KeyCode::W) ||
                inputSys.isKeyDown(Engine::KeyCode::Up) ||
                inputSys.isKeyDown(Engine::KeyCode::Space);

    // 记录更新前的玩家状态（用于触发事件）
    float prevPlayerX = 0.0f, prevPlayerY = 0.0f;
    bool wasOnGround = false;
    if (m_playerEntity != entt::null) {
        auto& reg = m_ecsWorld->registry();
        if (reg.any_of<ECS::Transform, ECS::PhysicsBody>(m_playerEntity)) {
            auto& transform = reg.get<ECS::Transform>(m_playerEntity);
            auto& body = reg.get<ECS::PhysicsBody>(m_playerEntity);
            prevPlayerX = transform.x;
            prevPlayerY = transform.y;
            wasOnGround = body.onGround;  // 驼峰命名
        }
    }

    m_ecsWorld->update(dt, *m_tileMap, input);

    // 触发玩家事件
    triggerPlayerEvents(prevPlayerX, prevPlayerY, wasOnGround);

    // 3. 水模拟（每帧执行多步流体模拟）
    int a, b, c, d;
    m_tileMap->stepWaterAdvanced(GameConfig::WATER_STEP_COUNT, a, b, c, d);

    // 4. 粒子更新（爆炸、碎片等视觉效果）
    if (m_particleSystem) {
        m_particleSystem->update(dt);
    }

    // 5. 连续清除工具（左键按住 + 工具1=挖掘）
    // 注：SDL 鼠标状态适合检测按住状态，而非单击
    {
        auto mousePos = inputSys.getMousePosition();
        float mx = mousePos.x, my = mousePos.y;
        bool leftHeld = inputSys.isMouseButtonDown(Engine::MouseButton::Left);

        if (leftHeld && m_toolbar && !m_toolbar->hitTest(mx, my)) {
            int tool = m_toolbar->selectedIndex();
            if (tool == 1) {  // 工具1=挖掘器
                float wx = 0.0f, wy = 0.0f;

                // 详细调试：检查窗口尺寸一致性
                int sceneW = getPixelWidth();
                int sceneH = getPixelHeight();
                int camVpW = camera()->vpW();
                int camVpH = camera()->vpH();

                // 如果尺寸不一致，强制更新相机视口
                if (sceneW != camVpW || sceneH != camVpH) {
                    TINA_WARN("检测到窗口尺寸不一致！Scene:{}x{} vs Camera:{}x{}, 强制同步",
                             sceneW, sceneH, camVpW, camVpH);
                    camera()->setViewportPixels(sceneW, sceneH);
                    // 重新获取更新后的值
                    camVpW = camera()->vpW();
                    camVpH = camera()->vpH();
                }

                TINA_DEBUG("工具使用 - 鼠标:({}, {}) 窗口:{}x{} 相机:({}, {}) 视野:{}x{}",
                          mx, my, sceneW, sceneH,
                          camera()->x(), camera()->y(), camera()->viewW(), camera()->viewH());

                screenToWorld(mx, my, sceneW, sceneH, *camera(), wx, wy);
                TINA_DEBUG("转换后世界坐标:({}, {})", wx, wy);
                excavateCircle(*m_tileMap, wx, wy, GameConfig::EXCAVATE_RADIUS);
            }
        }
    }

    // 6. UI 更新（工具栏、角色面板）
    if (m_toolbar) {
        auto mousePos = inputSys.getMousePosition();
        float mx = mousePos.x, my = mousePos.y;
        bool leftHeld = inputSys.isMouseButtonDown(Engine::MouseButton::Left);

        // 先更新布局（计算各子节点世界坐标），再做命中测试与事件处理
        m_toolbar->update(dt);
        m_toolbar->setMousePos(mx, my);
        m_toolbar->events().updateMouse(mx, my, leftHeld);
        m_toolbar->events().processEvents();
    }

    if (m_characterPanel) {
        auto mousePos = inputSys.getMousePosition();
        float mx = mousePos.x, my = mousePos.y;
        bool leftHeld = inputSys.isMouseButtonDown(Engine::MouseButton::Left);

        // 先更新布局，再分发事件
        m_characterPanel->update(dt);
        m_characterPanel->events().updateMouse(mx, my, leftHeld);
        m_characterPanel->events().processEvents();
    }

    // 重置工具激活状态（实现一次性点击，防止 UI 事件连续触发）
    m_isToolActive = false;
}

void GameScene::updateCamera(float dt)
{
    if (!camera() || !m_ecsWorld) return;

    // 获取视区尺寸（世界单位）
    float viewW = (float)camera()->viewW();
    float viewH = (float)camera()->viewH();

    // 获取当前控制的角色位置
    float targetCamX = camera()->x();
    float targetCamY = camera()->y();

    auto controlled = m_ecsWorld->getControlledEntity();
    if (controlled != entt::null) {
        auto& reg = m_ecsWorld->registry();
        if (reg.any_of<ECS::Transform, ECS::PhysicsBody>(controlled)) {
            auto& tr = reg.get<ECS::Transform>(controlled);
            auto& pb = reg.get<ECS::PhysicsBody>(controlled);

            // 计算角色中心点
            float centerX = tr.x + pb.width * 0.5f;
            float centerY = tr.y + pb.height * 0.5f;

            // 目标相机位置（角色居中）
            targetCamX = centerX - viewW * 0.5f;
            targetCamY = centerY - viewH * 0.5f;
        }
    }

    // 平滑跟随（插值，避免相机抖动）
    float camX = camera()->x();
    float camY = camera()->y();
    camX += (targetCamX - camX) * GameConfig::CAMERA_SMOOTH_FACTOR;
    camY += (targetCamY - camY) * GameConfig::CAMERA_SMOOTH_FACTOR;

    // 限制相机边界（防止相机超出地图范围）
    if (m_tileMap) {
        int mapW = m_tileMap->width();
        int mapH = m_tileMap->height();
        camX = std::clamp(camX, 0.0f, std::max(0.0f, (float)mapW - viewW));
        camY = std::clamp(camY, 0.0f, std::max(0.0f, (float)mapH - viewH));
    }

    camera()->setPosition(camX, camY);
}

void GameScene::renderWorld()
{
    if (!m_tileRenderer || !m_tileMap || !camera()) return;

    // 框架已经自动设置了视图和相机矩阵，不需要手动调用bgfx了！

    // 渲染地形（固体 -> view 1，液体 -> view 2）
    m_tileRenderer->renderSolid(*m_tileMap, UI::VIEW_WORLD_SOLID);
    m_tileRenderer->renderWater(*m_tileMap, UI::VIEW_WORLD_ALPHA);

    // 渲染角色（ECS 实体，view 2 确保在液体层之上）
    if (m_ecsWorld) {
        m_ecsWorld->render(UI::VIEW_WORLD_ALPHA);
    }

    // 渲染粒子（爆炸、碎片等特效，view 2）
    if (m_particleSystem) {
        m_particleSystem->render(UI::VIEW_WORLD_ALPHA);
    }
}

void GameScene::renderUI()
{
    if (!m_toolbar) {
        TINA_WARN("renderUI: m_toolbar 不存在！");
        return;
    }

    // 添加调试日志
    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {  // 每60帧打印一次
        TINA_INFO("renderUI: m_toolbar={}, root visible={}, slotCount={}",
                  (void*)m_toolbar.get(),
                  m_toolbar->root() ? m_toolbar->root()->isVisible() : false,
                  m_toolbar->slotCount());
    }

    // UI 视图已在 initializeResources() 中设置


    auto __ui_scope = ui().beginRender(UI::VIEW_UI);
    // 渲染提示文本
    float hudY = m_toolbar->root()->isVisible()
        ? (float)m_toolbar->barHeight() + GameConfig::UI_HUD_PADDING_Y
        : GameConfig::UI_HUD_PADDING_Y;

    // HUD 提示使用较小字号，避免过大占屏
    {
        UI::UIRenderer::TextOptions to{};
        to.r = 1; to.g = 1; to.b = 1; to.a = 1; to.fontPx = 24;
        to.hAlign = UI::UIRenderer::AlignH::Left; to.vAlign = UI::UIRenderer::AlignV::Top;
        ui().drawTextBox(uiViewId(),
                                  GameConfig::UI_HUD_PADDING_X, hudY,
                                  (float)getPixelWidth() - GameConfig::UI_HUD_WIDTH_MARGIN,
                                  GameConfig::UI_HUD_HEIGHT,
                                  "A/D 移动 | W/空格 跳跃 | 左键地形编辑 | 右键查看角色并切换控制 | 滚轮/数字键切换工具",
                                  to);
    }

    // 渲染工具栏
    m_toolbar->render(UI::VIEW_UI);

    // 渲染角色面板
    if (m_characterPanel) {
        m_characterPanel->render(UI::VIEW_UI, ui());
    }
}

void GameScene::ensureToolbarIconsReady()
{
    if (!m_toolbar) return;
    auto trySet = [&](int slot, Tina::Engine::ResourceRef<Tina::Engine::Texture2DResource>& ref){
        auto* r = ref.get();
        if (!r) return;
        if (r->getState() == Tina::Engine::Resource::State::READY) {
            if (bgfx::isValid(r->handle())) {
                m_toolbar->setSlotIcon(slot, r->handle());
            }
        }
    };
    trySet(0, m_iconWater);
    trySet(1, m_iconClean);
    trySet(2, m_iconBomb);
}

// handleKeyboard 和 handleMouse 已删除，功能合并到 handleInput

void GameScene::handleRightClick(float mx, float my)
{
    if (!m_ecsWorld || !camera() || !m_characterPanel) return;

    // 确保相机视口与窗口尺寸同步
    int pixelW = getPixelWidth();
    int pixelH = getPixelHeight();
    if (pixelW != camera()->vpW() || pixelH != camera()->vpH()) {
        camera()->setViewportPixels(pixelW, pixelH);
    }

    // 转换到世界坐标
    float wx = 0.0f, wy = 0.0f;
    screenToWorld(mx, my, pixelW, pixelH, *camera(), wx, wy);

    // 检测所有角色
    auto& reg = m_ecsWorld->registry();
    auto view = reg.view<ECS::Transform, ECS::PhysicsBody, ECS::Name, ECS::Health>();

    bool clickedCharacter = false;
    for (auto entity : view) {
        auto& transform = view.get<ECS::Transform>(entity);
        auto& body = view.get<ECS::PhysicsBody>(entity);
        auto& name = view.get<ECS::Name>(entity);
        auto& health = view.get<ECS::Health>(entity);

        // AABB 碰撞检测
        if (wx >= transform.x && wx <= transform.x + body.width &&
            wy >= transform.y && wy <= transform.y + body.height) {
            // 点击了角色
            m_clickedEntity = entity;
            bool isControlled = (entity == m_ecsWorld->getControlledEntity());
            m_characterPanel->updateData(name.name, health.percentage(), isControlled);
            m_characterPanel->setVisible(true);
            clickedCharacter = true;

            TINA_INFO("查看角色信息: {}, 血量: {:.0f}/{:.0f}, 控制状态: {}",
                      name.name, health.current, health.max, isControlled ? "是" : "否");
            break;
        }
    }

    // 如果没点击角色，隐藏面板
    if (!clickedCharacter) {
        m_characterPanel->setVisible(false);
        m_clickedEntity = entt::null;
    }
}

void GameScene::handleLeftClick(float mx, float my)
{
    if (!m_tileMap || !camera() || !m_toolbar) return;

    // 获取窗口和相机状态
    int pixelW = getPixelWidth();
    int pixelH = getPixelHeight();
    int camVpW = camera()->vpW();
    int camVpH = camera()->vpH();

    // 确保相机视口与窗口尺寸同步
    if (pixelW != camVpW || pixelH != camVpH) {
        camera()->setViewportPixels(pixelW, pixelH);
    }

    // 转换到世界坐标
    float wx = 0.0f, wy = 0.0f;
    screenToWorld(mx, my, pixelW, pixelH, *camera(), wx, wy);

    // 调试：输出转换细节
    float camX = camera()->x();
    float camY = camera()->y();
    float viewW = camera()->viewW();
    float viewH = camera()->viewH();
    TINA_DEBUG("点击调试 - 鼠标:({:.0f},{:.0f}) 相机:({:.1f},{:.1f}) 视野:{:.1f}x{:.1f} → 世界:({:.2f},{:.2f})",
              mx, my, camX, camY, viewW, viewH, wx, wy);

    int tx = (int)std::floor(wx);
    int ty = (int)std::floor(wy);

    // 检查边界
    if (tx < 0 || ty < 0 || tx >= m_tileMap->width() || ty >= m_tileMap->height()) {
        return;
    }

    // 执行工具
    int tool = m_toolbar->selectedIndex();
    if (tool < 0) tool = 0;

    switch (tool) {
        case 0:  // 注水器
            useWaterTool(tx, ty);
            break;
        case 1:  // 挖掘器
            useDiggerTool(tx, ty);
            break;
        case 2:  // 爆炸器
            useExplodeTool(tx, ty);
            break;
        default:
            break;
    }
}

void GameScene::useWaterTool(int worldX, int worldY)
{
    if (!m_tileMap) return;
    placeWater(*m_tileMap, worldX, worldY, GameConfig::WATER_MAX_LEVEL);
}

void GameScene::useDiggerTool(int worldX, int worldY)
{
    if (!m_tileMap || !camera()) return;
    float wx = (float)worldX + 0.5f;
    float wy = (float)worldY + 0.5f;
    excavateCircle(*m_tileMap, wx, wy, GameConfig::EXCAVATE_RADIUS);
}

void GameScene::useExplodeTool(int worldX, int worldY)
{
    if (!m_tileMap || !camera() || !m_particleSystem) return;

    float wx = (float)worldX + 0.5f;
    float wy = (float)worldY + 0.5f;

    // 挖掘
    excavateCircle(*m_tileMap, wx, wy, GameConfig::EXCAVATE_RADIUS);

    // 粒子效果
    m_particleSystem->explode(wx, wy,
                               GameConfig::EXPLODE_PARTICLE_COUNT,
                               GameConfig::EXPLODE_SPEED_MIN, GameConfig::EXPLODE_SPEED_MAX,
                               GameConfig::EXPLODE_SIZE_MIN, GameConfig::EXPLODE_SIZE_MAX,
                               GameConfig::EXPLODE_LIFE_MIN, GameConfig::EXPLODE_LIFE_MAX,
                               Core::Color(0.78f, 0.70f, 0.58f, 1.0f));
}

// setupUIView 方法已移至基类 Scene 中实现

void GameScene::subscribeToEvents()
{
    if (!app()) return;

    // 使用便捷宏批量订阅事件（更简洁）
    TINA_SUBSCRIBE_EVENTS(
        TINA_SUBSCRIBE_EVENT(m_eventSubscriptions, Tina::Game::Events::PlayerJumped, GameScene::onPlayerJumpedEvt);
        TINA_SUBSCRIBE_EVENT(m_eventSubscriptions, Tina::Game::Events::PlayerMoved, GameScene::onPlayerMovedEvt);
        TINA_SUBSCRIBE_EVENT(m_eventSubscriptions, Tina::Game::Events::SetDayNight, GameScene::onSetDayNight);
        TINA_SUBSCRIBE_EVENT(m_eventSubscriptions, Tina::Game::Events::AdjustDayNight, GameScene::onAdjustDayNight);
    );

    TINA_INFO("GameScene: 事件订阅完成（使用 RAII 自动管理）");

    // 调试模式下打印事件统计
    PRINT_EVENT_STATS();
}

void GameScene::triggerPlayerEvents(float prevX, float prevY, bool wasOnGround)
{
    if (!app() || m_playerEntity == entt::null) return;

    auto& reg = m_ecsWorld->registry();
    if (!reg.any_of<ECS::Transform, ECS::PhysicsBody, ECS::Velocity>(m_playerEntity)) return;

    auto& transform = reg.get<ECS::Transform>(m_playerEntity);
    auto& body = reg.get<ECS::PhysicsBody>(m_playerEntity);
    auto& velocity = reg.get<ECS::Velocity>(m_playerEntity);

    // 检测跳跃（离地 + 垂直速度向上）
    if (wasOnGround && !body.onGround && velocity.vy < 0.0f) {
        Tina::Game::Events::PlayerJumped jumpEvent;
        app()->events().trigger(jumpEvent);
    }

    // 检测移动（位置变化）
    float dx = transform.x - prevX;
    float dy = transform.y - prevY;
    if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f) {
        Tina::Game::Events::PlayerMoved moveEvent;
        moveEvent.x = transform.x;
        moveEvent.y = transform.y;
        app()->events().trigger(moveEvent);
    }
}

// === 强类型事件处理 ===
void GameScene::onSetDayNight(const Tina::Game::Events::SetDayNight& e)
{
    m_dayNight.setNormalizedTime(e.normalized);
}

void GameScene::onAdjustDayNight(const Tina::Game::Events::AdjustDayNight& e)
{
    m_dayNight.setNormalizedTime(m_dayNight.normalizedTime() + e.delta);
}

void GameScene::onPlayerJumpedEvt(const Tina::Game::Events::PlayerJumped&)
{
    // 基本的资源有效性检查
    if (!m_ecsWorld || !m_particleSystem || m_playerEntity == entt::null) return;

    auto& reg = m_ecsWorld->registry();
    if (reg.any_of<ECS::Transform>(m_playerEntity)) {
        auto& transform = reg.get<ECS::Transform>(m_playerEntity);
        m_particleSystem->explode(
            transform.x + 0.5f, transform.y,
            5,
            0.5f, 1.0f,
            0.05f, 0.1f,
            0.3f, 0.5f,
            Core::Color(0.9f, 0.9f, 0.9f, 0.8f)
        );
    }
}

void GameScene::onPlayerMovedEvt(const Tina::Game::Events::PlayerMoved& e)
{
    (void)e; // 目前仅作示例订阅，可加入调试逻辑
}
} // namespace Tina::Game
