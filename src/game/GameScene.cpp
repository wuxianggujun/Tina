//
// GameScene 实现 - 完整游戏逻辑
// 从 main.cpp 迁移而来
//

#include "GameScene.hpp"
#include "PauseScene.hpp"  // 暂停场景
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"  // 场景管理器
#include "../engine/EventBus.hpp"  // 包含 EventBus 完整定义
#include "../core/Log.hpp"
#include "../core/Time.hpp"
#include "../game/CoordinateMapper.hpp"
#include "../game/TerrainEditor.hpp"
#include "../game/GameConfig.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <algorithm>
#include <cmath>

namespace Tina::Game {

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::onEnter()
{
    TINA_INFO("GameScene::onEnter - 进入游戏场景");

    // 获取窗口尺寸
    app()->getPixelSize(m_pixelWidth, m_pixelHeight);

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

    // Signal 连接会自动断开（Connection 析构函数）
    // 但显式重置更清晰
    m_switchControlConnection.disconnect();
    m_keyPressedConnection.disconnect();
    m_mouseWheelConnection.disconnect();
    m_playerJumpedConnection.disconnect();
    m_playerMovedConnection.disconnect();
    m_setDayNightConnection.disconnect();
    m_adjustDayNightConnection.disconnect();

    // 清理资源（按创建逆序）
    m_characterPanel.reset();
    m_toolbar.reset();
    m_camera.reset();
    m_ecsWorld.reset();
    m_tileMap.reset();

    m_tileRenderer.reset();
    m_particleSystem.reset();
    

    // 程序句柄由全局 ShaderManager 管理，无需手动销毁
    m_progColor = BGFX_INVALID_HANDLE;
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

    // 重新设置 UI 视图（view 3）
    setupUIView();

    if (m_sfxYingxiao && m_sfxStarted) {
        m_sfxYingxiao->resume();
    }
    if (m_bgm && m_bgmStarted) {
        m_bgm->resume();
    }
}

void GameScene::update(float dt)
{
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
    if (m_uiRenderer) {
        const float a = m_dayNight.overlayAlpha();
        if (a > 0.001f) {
            m_uiRenderer->drawRect(3, 0.0f, 0.0f, (float)m_pixelWidth, (float)m_pixelHeight,
                                   0.0f, 0.0f, 0.0f, a);
        }
    }
    renderUI();
}

void GameScene::handleEvent(const Tina::os::Event& event)
{
    using E = Tina::os::Event;

    switch (event.type) {
        case E::Type::WINDOW_SIZE:
            // 窗口调整大小
            m_pixelWidth = event.win_size.w;
            m_pixelHeight = event.win_size.h;

            // 更新相机视口
            if (m_camera) {
                m_camera->setViewportPixels(m_pixelWidth, m_pixelHeight);
            }

            // 更新 UI
            if (m_toolbar) {
                m_toolbar->onResize(m_pixelWidth, m_pixelHeight);
            }
            if (m_characterPanel) {
                m_characterPanel->centerOnScreen(m_pixelWidth, m_pixelHeight);
            }

            // 更新 UI 视图的正交矩阵
            setupUIView();
            break;

        case E::Type::MOUSE_WHEEL:
            // 滚轮事件已通过 EventBus Signal 订阅处理
            break;

        case E::Type::KEY:
            handleKeyboard(event);
            break;

        case E::Type::MOUSE_BUTTON:
            handleMouse(event);
            break;

        default:
            break;
    }
}

void GameScene::initializeResources()
{
    TINA_INFO("GameScene: 初始化资源...");

    // 1. 着色器程序（来自全局 ShaderManager）
    m_progColor = app()->shaders().loadProgram("color", "color");

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
    m_tileRenderer->initialize();

    // 设置 UI 视图（view 3，像素坐标）
    setupUIView();

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
    TINA_INFO("ECS 世界创建完成");
}

void GameScene::createCamera()
{
    float viewH = std::min(GameConfig::DEFAULT_VIEW_HEIGHT, (float)m_tileMap->height());
    m_camera = Memory::MakeUnique<Camera2D>();
    m_camera->setViewportPixels(m_pixelWidth, m_pixelHeight);
    m_camera->setViewHeightWorld(viewH);
    TINA_INFO("相机创建完成: 视图高度={}", viewH);
}

void GameScene::createUI()
{
    // UI 渲染器
    m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    m_uiRenderer->initialize(app()->shaders(), &app()->textRenderer());

    // 工具栏
    m_toolbar = Memory::MakeUnique<UI::UIToolbar>();
    m_toolbar->initialize(m_pixelWidth, m_pixelHeight, *m_uiRenderer, &app()->textRenderer());
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
    m_characterPanel->centerOnScreen(m_pixelWidth, m_pixelHeight);

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

    // 5. 订阅角色面板的 Signal 事件（切换控制）
    m_switchControlConnection = m_characterPanel->onSwitchControl.connect([this]() {
        if (m_clickedEntity != entt::null) {
            m_ecsWorld->switchControl(m_clickedEntity);
            m_toolbar->root()->setVisible(false);
            TINA_INFO("切换控制到角色");
        }
    });

    // 6. 订阅 EventBus 事件
    subscribeToEvents();

    TINA_INFO("角色生成完成: 1 玩家 + 3 NPC");
}

void GameScene::updateGameLogic(float dt)
{
    // 1. 获取键盘输入（SDL 键盘状态适合连续输入，如移动）
    SDL_PumpEvents();
    const bool* ks = SDL_GetKeyboardState(nullptr);

    // 2. ECS 输入与更新（物理、碰撞、AI 等）
    ECS::InputState input{};
    input.moveLeft = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT];
    input.moveRight = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
    input.jump = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_SPACE];

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
        float mx = 0.0f, my = 0.0f;
        uint32_t btnMask = (uint32_t)SDL_GetMouseState(&mx, &my);
#ifdef SDL_BUTTON_MASK
        bool leftHeld = (btnMask & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
#else
        bool leftHeld = (btnMask & SDL_BUTTON_LMASK) != 0;
#endif
        if (leftHeld && m_toolbar && !m_toolbar->hitTest(mx, my)) {
            int tool = m_toolbar->selectedIndex();
            if (tool == 1) {  // 工具1=挖掘器
                float wx = 0.0f, wy = 0.0f;
                screenToWorld(mx, my, m_pixelWidth, m_pixelHeight, *m_camera, wx, wy);
                excavateCircle(*m_tileMap, wx, wy, GameConfig::EXCAVATE_RADIUS);
            }
        }
    }

    // 6. UI 更新（工具栏、角色面板）
    if (m_toolbar) {
        float mx = 0.0f, my = 0.0f;
        uint32_t btnMask = (uint32_t)SDL_GetMouseState(&mx, &my);
#ifdef SDL_BUTTON_MASK
        bool leftHeld = (btnMask & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
#else
        bool leftHeld = (btnMask & SDL_BUTTON_LMASK) != 0;
#endif
        // 先更新布局（计算各子节点世界坐标），再做命中测试与事件处理
        m_toolbar->update(dt);
        m_toolbar->setMousePos(mx, my);
        m_toolbar->events().updateMouse(mx, my, leftHeld);
        m_toolbar->events().processEvents();
    }

    if (m_characterPanel) {
        float mx = 0.0f, my = 0.0f;
        uint32_t btnMask = (uint32_t)SDL_GetMouseState(&mx, &my);
#ifdef SDL_BUTTON_MASK
        bool leftHeld = (btnMask & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
#else
        bool leftHeld = (btnMask & SDL_BUTTON_LMASK) != 0;
#endif
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
    if (!m_camera || !m_ecsWorld) return;

    // 获取视区尺寸（世界单位）
    float viewW = (float)m_camera->viewW();
    float viewH = (float)m_camera->viewH();

    // 获取当前控制的角色位置
    float targetCamX = m_camera->x();
    float targetCamY = m_camera->y();

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
    float camX = m_camera->x();
    float camY = m_camera->y();
    camX += (targetCamX - camX) * GameConfig::CAMERA_SMOOTH_FACTOR;
    camY += (targetCamY - camY) * GameConfig::CAMERA_SMOOTH_FACTOR;

    // 限制相机边界（防止相机超出地图范围）
    if (m_tileMap) {
        int mapW = m_tileMap->width();
        int mapH = m_tileMap->height();
        camX = std::clamp(camX, 0.0f, std::max(0.0f, (float)mapW - viewW));
        camY = std::clamp(camY, 0.0f, std::max(0.0f, (float)mapH - viewH));
    }

    m_camera->setPosition(camX, camY);
}

void GameScene::renderWorld()
{
    if (!m_tileRenderer || !m_tileMap || !m_camera) return;
    if (!bgfx::isValid(m_progColor)) {
        TINA_WARN("GameScene::renderWorld - 程序句柄无效，跳过本帧渲染");
        return;
    }

    // 构建相机矩阵（视图矩阵 + 投影矩阵）
    float viewM[16], projM[16];
    m_camera->buildViewProj(viewM, projM);

    // 设置世界视图（view 1 = 固体地形，view 2 = 液体/角色）
    // 注：分离固体和液体渲染，实现半透明水体效果
    bgfx::setViewRect(1, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    bgfx::setViewRect(2, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    bgfx::setViewTransform(1, viewM, projM);
    bgfx::setViewTransform(2, viewM, projM);

    // 设置清屏（view 1 负责清理颜色和深度缓冲，防止残影）
    // 关键：必须调用 setViewClear，touch() 不会清理帧缓冲
    bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, GameConfig::CLEAR_COLOR, 1.0f, 0);

    // 触摸视图（确保视图被渲染，即使没有几何体）
    bgfx::touch(1);
    bgfx::touch(2);

    // 渲染地形（固体 -> view 1，液体 -> view 2）
    bgfx::VertexLayout colorLayout;
    colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();

    m_tileRenderer->renderSolid(*m_tileMap, 1, m_progColor, colorLayout);
    m_tileRenderer->renderWater(*m_tileMap, 2, m_progColor, colorLayout);

    // 渲染角色（ECS 实体，view 2 确保在液体层之上）
    if (m_ecsWorld) {
        m_ecsWorld->render(2, m_progColor, colorLayout);
    }

    // 渲染粒子（爆炸、碎片等特效，view 2）
    if (m_particleSystem) {
        m_particleSystem->render(2);
    }
}

void GameScene::renderUI()
{
    if (!m_toolbar || !m_uiRenderer) return;

    // UI 视图已在 initializeResources() 中设置


    m_uiRenderer->beginFrame(3);
    // 渲染提示文本
    float hudY = m_toolbar->root()->isVisible()
        ? (float)m_toolbar->barHeight() + GameConfig::UI_HUD_PADDING_Y
        : GameConfig::UI_HUD_PADDING_Y;

    m_uiRenderer->drawTextEx(3,
                             GameConfig::UI_HUD_PADDING_X, hudY,
                             (float)m_pixelWidth - GameConfig::UI_HUD_WIDTH_MARGIN,
                             GameConfig::UI_HUD_HEIGHT,
                             1, 1, 1, 1,
                             "A/D 移动 | W/空格 跳跃 | 左键地形编辑 | 右键查看角色并切换控制 | 滚轮/数字键切换工具",
                             UI::UIRenderer::AlignH::Left,
                             UI::UIRenderer::AlignV::Top,
                             0.0f, 0.0f);

    // 渲染工具栏
    m_toolbar->render(3);

    // 渲染角色面板
    if (m_characterPanel) {
        m_characterPanel->render(3, *m_uiRenderer);
    }
    
    m_uiRenderer->flush();
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

void GameScene::handleKeyboard(const Tina::os::Event& event)
{
    if (event.type != os::Event::Type::KEY || !event.key.down) return;

    // ESC 键：暂停游戏
    if (event.key.key_code == os::KeyCode::ESCAPE) {
        TINA_INFO("GameScene: 按下 ESC，进入暂停菜单");
        app()->scenes().requestPush(Memory::MakeUnique<PauseScene>());
        return;
    }

    // 其他键盘事件已通过 EventBus Signal 订阅处理
}

void GameScene::handleMouse(const Tina::os::Event& event)
{
    if (event.type != os::Event::Type::MOUSE_BUTTON) return;
    if (!event.mouse_button.down) return;  // 只处理按下事件

    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);

    // 右键：查看角色信息
    if (event.mouse_button.button == os::MouseButton::RIGHT) {
        handleRightClick(mx, my);
        return;
    }

    // 左键：工具栏或地形编辑
    if (event.mouse_button.button == os::MouseButton::LEFT) {
        m_isToolActive = true;

        // 检查是否点击工具栏（直接命中并切换选中，避免依赖事件系统的边沿判定）
        if (m_toolbar && m_toolbar->hitTest(mx, my)) {
            if (m_toolbar->clickAt(mx, my)) {
                return;
            }
        }

        // 地形编辑工具
        handleLeftClick(mx, my);
    }
}

void GameScene::handleRightClick(float mx, float my)
{
    if (!m_ecsWorld || !m_camera || !m_characterPanel) return;

    // 转换到世界坐标
    float wx = 0.0f, wy = 0.0f;
    screenToWorld(mx, my, m_pixelWidth, m_pixelHeight, *m_camera, wx, wy);

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
    if (!m_tileMap || !m_camera || !m_toolbar) return;

    // 转换到世界坐标
    float wx = 0.0f, wy = 0.0f;
    screenToWorld(mx, my, m_pixelWidth, m_pixelHeight, *m_camera, wx, wy);

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
    if (!m_tileMap || !m_camera) return;
    float wx = (float)worldX + 0.5f;
    float wy = (float)worldY + 0.5f;
    excavateCircle(*m_tileMap, wx, wy, GameConfig::EXCAVATE_RADIUS);
}

void GameScene::useExplodeTool(int worldX, int worldY)
{
    if (!m_tileMap || !m_camera || !m_particleSystem) return;

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

void GameScene::setupUIView()
{
    // 设置 UI 视图（view 3，像素坐标，y 向下）
    // 注：UI 使用屏幕坐标系统，原点在左上角，与世界坐标不同
    bgfx::setViewRect(3, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);

    // 构建正交矩阵（2D 投影，无透视）
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho,
                 0.0f, (float)m_pixelWidth,   // left, right
                 (float)m_pixelHeight, 0.0f,  // bottom, top（y 向下）
                 -1.0f, 1.0f,                 // near, far
                 0.0f,                         // offset
                 caps ? caps->homogeneousDepth : false);

    bgfx::setViewTransform(3, nullptr, ortho);
    // UI 必须按提交顺序绘制，防止默认排序打乱前后关系
    bgfx::setViewMode(3, bgfx::ViewMode::Sequential);
}

void GameScene::subscribeToEvents()
{
    if (!app()) return;

    // 订阅键盘事件（用于工具栏快捷键）
    m_keyPressedConnection = app()->events().onKeyPressed.connect(
        [this](int keycode, bool isRepeat) {
            if (!m_toolbar || isRepeat) return;

            // 工具栏快捷键（数字键 1-8）
            switch (keycode) {
                case (int)os::KeyCode::KEY_1: m_toolbar->select(0); break;
                case (int)os::KeyCode::KEY_2: m_toolbar->select(1); break;
                case (int)os::KeyCode::KEY_3: m_toolbar->select(2); break;
                case (int)os::KeyCode::KEY_4: m_toolbar->select(3); break;
                case (int)os::KeyCode::KEY_5: m_toolbar->select(4); break;
                case (int)os::KeyCode::KEY_6: m_toolbar->select(5); break;
                case (int)os::KeyCode::KEY_7: m_toolbar->select(6); break;
                case (int)os::KeyCode::KEY_8: m_toolbar->select(7); break;
                default: break;
            }
        }
    );

    // 订阅鼠标滚轮事件（切换工具）
    m_mouseWheelConnection = app()->events().onMouseWheel.connect(
        [this](float amount) {
            if (!m_toolbar) return;

            int n = m_toolbar->slotCount();
            if (n > 0) {
                int cur = m_toolbar->selectedIndex();
                if (cur < 0) cur = 0;
                if (amount > 0.0f) {
                    cur = (cur + 1) % n;
                } else if (amount < 0.0f) {
                    cur = (cur - 1 + n) % n;
                }
                m_toolbar->select(cur);
            }
        }
    );

    // 订阅玩家跳跃事件（添加粒子效果）
    m_playerJumpedConnection = app()->events().onPlayerJumped.connect(
        [this]() {
            if (!m_particleSystem || m_playerEntity == entt::null) return;

            auto& reg = m_ecsWorld->registry();
            if (reg.any_of<ECS::Transform>(m_playerEntity)) {
                auto& transform = reg.get<ECS::Transform>(m_playerEntity);
                // 在玩家脚下生成跳跃粒子
                m_particleSystem->explode(
                    transform.x + 0.5f, transform.y,
                    5,  // 少量粒子
                    0.5f, 1.0f,  // 速度范围
                    0.05f, 0.1f,  // 大小范围
                    0.3f, 0.5f,  // 生命周期
                    Core::Color(0.9f, 0.9f, 0.9f, 0.8f)  // 白色粉尘
                );
            }
        }
    );

    // 订阅玩家移动事件（可用于调试或其他逻辑）
    m_playerMovedConnection = app()->events().onPlayerMoved.connect(
        [](float x, float y) {
            // 未来可以在这里添加脚步声、拖尾效果等
            // TINA_TRACE("玩家移动到: ({:.2f}, {:.2f})", x, y);
            (void)x; (void)y;  // 避免未使用参数警告
        }
    );

    // === 昼夜系统调试信号 ===
    m_setDayNightConnection = app()->events().onSetDayNightNormalized.connect([this](float n){
        m_dayNight.setNormalizedTime(n);
    });
    m_adjustDayNightConnection = app()->events().onAdjustDayNightNormalized.connect([this](float dn){
        m_dayNight.setNormalizedTime(m_dayNight.normalizedTime() + dn);
    });
    // 已移除暂停/恢复昼夜功能

    TINA_INFO("GameScene: EventBus 订阅完成");
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
        app()->events().onPlayerJumped.emit();
    }

    // 检测移动（位置变化）
    float dx = transform.x - prevX;
    float dy = transform.y - prevY;
    if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f) {
        app()->events().onPlayerMoved.emit(transform.x, transform.y);
    }
}

} // namespace Tina::Game
