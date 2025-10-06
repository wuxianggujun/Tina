//
// GameScene 实现 - 完整游戏逻辑
// 从 main.cpp 迁移而来
//

#include "GameScene.hpp"
#include "../engine/Application.hpp"
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
}

void GameScene::onExit()
{
    TINA_INFO("GameScene::onExit - 退出游戏场景");

    // 清理资源（按创建逆序）
    m_characterPanel.reset();
    m_toolbar.reset();
    m_camera.reset();
    m_ecsWorld.reset();
    m_tileMap.reset();

    m_tileRenderer.reset();
    m_particleSystem.reset();
    m_textRenderer.reset();

    // 销毁 bgfx 程序句柄
    if (bgfx::isValid(m_progColor)) {
        bgfx::destroy(m_progColor);
        m_progColor = BGFX_INVALID_HANDLE;
    }

    m_shaderMgr.reset();
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
            // 滚轮切换工具
            if (m_toolbar) {
                int n = m_toolbar->slotCount();
                if (n > 0) {
                    int cur = m_toolbar->selectedIndex();
                    if (cur < 0) cur = 0;
                    if (event.mouse_wheel.amount > 0.0f) {
                        cur = (cur + 1) % n;
                    } else if (event.mouse_wheel.amount < 0.0f) {
                        cur = (cur - 1 + n) % n;
                    }
                    m_toolbar->select(cur);
                }
            }
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

    // 1. 着色器管理器
    m_shaderMgr = Memory::MakeUnique<renderer::ShaderManager>();
    m_shaderMgr->initialize();
    m_progColor = m_shaderMgr->loadProgram("color", "color");

    // 2. 文本渲染器
    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize()) {
        TINA_ERROR("TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 24);
    }

    // 3. 粒子系统
    m_particleSystem = Memory::MakeUnique<Particles::ParticleSystem2D>();
    if (!m_particleSystem->initialize(*m_shaderMgr)) {
        TINA_WARN("ParticleSystem 初始化失败：无粒子效果");
    } else {
        m_particleSystem->setGlobalAcceleration(0.0f, -12.0f);
        m_particleSystem->setDrag(0.10f);
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

    // 1. 创建地图
    TileMapConfig mapCfg;
    mapCfg.width = 160;
    mapCfg.height = 90;
    mapCfg.seed = 1337;

    m_tileMap = Memory::MakeUnique<TileMap>(mapCfg);
    m_tileMap->generate();

    // 2. 创建 ECS 世界
    m_ecsWorld = Memory::MakeUnique<ECS::World>();

    // 3. 创建相机
    float viewH = std::min(60.0f, (float)mapCfg.height);
    m_camera = Memory::MakeUnique<Camera2D>();
    m_camera->setViewportPixels(m_pixelWidth, m_pixelHeight);
    m_camera->setViewHeightWorld(viewH);

    // 4. 创建 UI
    // 4.1 UI 渲染器（作为成员变量）
    m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    m_uiRenderer->initialize(*m_shaderMgr, m_textRenderer.get());

    // 4.2 工具栏
    m_toolbar = Memory::MakeUnique<UI::UIToolbar>();
    m_toolbar->initialize(m_pixelWidth, m_pixelHeight, *m_uiRenderer, m_textRenderer.get());

    // 4.3 角色面板
    m_characterPanel = Memory::MakeUnique<UI::UICharacterPanel>();
    m_characterPanel->centerOnScreen(m_pixelWidth, m_pixelHeight);

    // 5. 查找玩家出生点
    int spawnX = mapCfg.width / 2;
    int spawnY = mapCfg.height / 2;

    auto isNaturalGround = [&](TileType t) { return m_tileMap->isNaturalGround(t); };

    auto findSpawnInColumn = [&](int cx, int& outY) -> bool {
        if (cx < 0 || cx >= mapCfg.width) return false;
        for (int y = mapCfg.height - 3; y >= 0; --y) {
            auto base = m_tileMap->get(cx, y);
            if (!isNaturalGround(base)) continue;
            if (y + 2 >= mapCfg.height) continue;

            auto up1 = m_tileMap->get(cx, y + 1);
            auto up2 = m_tileMap->get(cx, y + 2);

            if (up1 == TileType::Air && up2 == TileType::Air &&
                !m_tileMap->isWater(cx, y + 1) && !m_tileMap->isWater(cx, y + 2) &&
                !m_tileMap->isLava(cx, y + 1) && !m_tileMap->isLava(cx, y + 2)) {
                outY = y + 1;
                return true;
            }
        }
        return false;
    };

    bool foundSpawn = findSpawnInColumn(spawnX, spawnY);

    if (!foundSpawn) {
        int maxRadius = std::min(40, mapCfg.width / 2);
        for (int dx = 1; dx <= maxRadius && !foundSpawn; ++dx) {
            int left = spawnX - dx;
            int right = spawnX + dx;
            if (findSpawnInColumn(left, spawnY)) {
                spawnX = left;
                foundSpawn = true;
                break;
            }
            if (findSpawnInColumn(right, spawnY)) {
                spawnX = right;
                foundSpawn = true;
                break;
            }
        }
    }

    if (!foundSpawn) {
        spawnX = std::clamp(spawnX, 0, mapCfg.width - 1);
        spawnY = std::clamp(mapCfg.height / 2, 2, mapCfg.height - 3);
    }

    // 6. 创建角色
    // 6.1 玩家
    m_playerEntity = m_ecsWorld->createCharacter((float)spawnX, (float)spawnY, true);

    // 6.2 NPC
    auto npc1 = m_ecsWorld->createCharacter((float)spawnX + 5.0f, (float)spawnY, false);
    m_ecsWorld->registry().get<ECS::Renderable>(npc1).r = 0.2f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc1).g = 1.0f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc1).b = 0.2f;

    auto npc2 = m_ecsWorld->createCharacter((float)spawnX - 5.0f, (float)spawnY, false);
    m_ecsWorld->registry().get<ECS::Renderable>(npc2).r = 0.2f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc2).g = 0.5f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc2).b = 1.0f;

    auto npc3 = m_ecsWorld->createCharacter((float)spawnX + 10.0f, (float)spawnY, false);
    m_ecsWorld->registry().get<ECS::Renderable>(npc3).r = 1.0f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc3).g = 1.0f;
    m_ecsWorld->registry().get<ECS::Renderable>(npc3).b = 0.2f;

    // 7. 设置角色面板回调
    entt::entity* pClickedEntity = &m_clickedEntity;  // 捕获指针
    m_characterPanel->setSwitchControlCallback([this, pClickedEntity]() {
        if (*pClickedEntity != entt::null) {
            m_ecsWorld->switchControl(*pClickedEntity);
            m_toolbar->root()->setVisible(false);
            TINA_INFO("切换控制到角色");
        }
    });

    TINA_INFO("GameScene: 游戏世界创建完成");
}

void GameScene::updateGameLogic(float dt)
{
    // 1. 获取键盘输入
    SDL_PumpEvents();
    const bool* ks = SDL_GetKeyboardState(nullptr);

    // 2. ECS 输入与更新
    ECS::InputState input{};
    input.moveLeft = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT];
    input.moveRight = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
    input.jump = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_SPACE];

    m_ecsWorld->update(dt, *m_tileMap, input);

    // 3. 水模拟
    int a, b, c, d;
    m_tileMap->stepWaterAdvanced(2, a, b, c, d);

    // 4. 粒子更新
    if (m_particleSystem) {
        m_particleSystem->update(dt);
    }

    // 5. 工具栏快捷键
    if (m_toolbar) {
        if (ks[SDL_SCANCODE_1]) m_toolbar->select(0);
        if (ks[SDL_SCANCODE_2]) m_toolbar->select(1);
        if (ks[SDL_SCANCODE_3]) m_toolbar->select(2);
        if (ks[SDL_SCANCODE_4]) m_toolbar->select(3);
        if (ks[SDL_SCANCODE_5]) m_toolbar->select(4);
        if (ks[SDL_SCANCODE_6]) m_toolbar->select(5);
        if (ks[SDL_SCANCODE_7]) m_toolbar->select(6);
        if (ks[SDL_SCANCODE_8]) m_toolbar->select(7);
    }

    // 6. 连续清除工具（左键按住 + 工具1=挖掘）
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
            if (tool == 1) {
                float wx = 0.0f, wy = 0.0f;
                screenToWorld(mx, my, m_pixelWidth, m_pixelHeight, *m_camera, wx, wy);
                excavateCircle(*m_tileMap, wx, wy, GameConfig::EXCAVATE_RADIUS);
            }
        }
    }

    // 7. UI 更新
    if (m_toolbar) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        m_toolbar->setMousePos(mx, my);
        m_toolbar->events().updateMouse(mx, my, m_isToolActive);
        m_toolbar->events().processEvents();
        m_toolbar->update(dt);
    }

    if (m_characterPanel) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        m_characterPanel->events().updateMouse(mx, my, m_isToolActive);
        m_characterPanel->events().processEvents();
        m_characterPanel->update(dt);
    }

    // 重置工具激活状态（实现一次性点击）
    m_isToolActive = false;
}

void GameScene::updateCamera(float dt)
{
    if (!m_camera || !m_ecsWorld) return;

    // 获取视区尺寸
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

            float centerX = tr.x + pb.width * 0.5f;
            float centerY = tr.y + pb.height * 0.5f;

            targetCamX = centerX - viewW * 0.5f;
            targetCamY = centerY - viewH * 0.5f;
        }
    }

    // 平滑跟随
    float camX = m_camera->x();
    float camY = m_camera->y();
    camX += (targetCamX - camX) * 0.1f;
    camY += (targetCamY - camY) * 0.1f;

    // 限制相机边界
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

    // 构建相机矩阵
    float viewM[16], projM[16];
    m_camera->buildViewProj(viewM, projM);

    // 设置世界视图（view 1 = 固体，view 2 = 液体/角色）
    bgfx::setViewRect(1, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    bgfx::setViewRect(2, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    bgfx::setViewTransform(1, viewM, projM);
    bgfx::setViewTransform(2, viewM, projM);

    // 设置清屏（view 1 负责清理颜色和深度缓冲）
    bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

    // 触摸视图（确保视图被渲染）
    bgfx::touch(1);
    bgfx::touch(2);

    // 渲染地形
    bgfx::VertexLayout colorLayout;
    colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();

    m_tileRenderer->renderSolid(*m_tileMap, 1, m_progColor, colorLayout);
    m_tileRenderer->renderWater(*m_tileMap, 2, m_progColor, colorLayout);

    // 渲染角色
    if (m_ecsWorld) {
        m_ecsWorld->render(2, m_progColor, colorLayout);
    }

    // 渲染粒子
    if (m_particleSystem) {
        m_particleSystem->render(2);
    }
}

void GameScene::renderUI()
{
    if (!m_toolbar || !m_textRenderer || !m_uiRenderer) return;

    // UI 视图已在 initializeResources() 中设置

    // 渲染提示文本
    float hudY = m_toolbar->root()->isVisible() ? (float)m_toolbar->barHeight() + 12.0f : 12.0f;
    m_uiRenderer->drawTextEx(3,
                             16.0f, hudY,
                             (float)m_pixelWidth - 32.0f, 28.0f,
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
}

void GameScene::handleKeyboard(const Tina::os::Event& /*event*/)
{
    // 键盘输入在 updateGameLogic() 中处理（SDL 键盘状态）
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

        // 检查是否点击工具栏
        if (m_toolbar && m_toolbar->hitTest(mx, my)) {
            // 工具栏内部处理点击
            return;
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
    placeWater(*m_tileMap, worldX, worldY, 255);
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
                               /*count*/ 260,
                               /*speed*/ 6.0f, 14.0f,
                               /*size*/ 0.30f, 0.90f,
                               /*life*/ 0.6f, 1.6f,
                               /*color*/ Core::Color(0.78f, 0.70f, 0.58f, 1.0f));
}

void GameScene::setupUIView()
{
    // 设置 UI 视图（view 3，像素坐标，y 向下）
    bgfx::setViewRect(3, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);

    // 构建正交矩阵
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho,
                 0.0f, (float)m_pixelWidth,   // left, right
                 (float)m_pixelHeight, 0.0f,  // bottom, top（y 向下）
                 -1.0f, 1.0f,                 // near, far
                 0.0f,                         // offset
                 caps ? caps->homogeneousDepth : false);

    bgfx::setViewTransform(3, nullptr, ortho);
}

} // namespace Tina::Game
