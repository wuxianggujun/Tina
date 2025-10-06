// 完整游戏入口（最小可玩：地图 + 水流 + 相机 + 文本 HUD）

#include <cstdint>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <cmath>
#include <algorithm>
#include "core/Log.hpp"
#include "core/Time.hpp"
#include "os/OS.hpp"
#include "renderer/ShaderManager.hpp"
#include "game/TileMap.hpp"
#include "ecs/World.hpp"
#include "game/CoordinateMapper.hpp"
#include "game/TerrainEditor.hpp"
#include "game/GameConfig.hpp"
#include "game/Camera2D.hpp"
#include "renderer/TileRenderer.hpp"
#include "ui/TextRenderer.hpp"
#include "ui/UIToolbar.hpp"
#include "ui/UICharacterPanel.hpp"
#include "particles/ParticleSystem.hpp"

using Tina::os::Event;

static void ResetBgfxWithSize(int w, int h, uint32_t resetFlags)
{
    if (w <= 0 || h <= 0) { w = 1280; h = 720; }
    bgfx::reset((uint32_t)w, (uint32_t)h, resetFlags);
}

static void SetupOrtho(uint16_t viewId, float l, float r, float t, float b)
{
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    // 注意：bx::mtxOrtho 参数顺序为 (left, right, bottom, top, ...)
    // 这里将 bottom=t, top=b，以便调用方传入想要的 bottom/top
    bx::mtxOrtho(ortho, l, r, t, b, -1.0f, 1.0f, 0.0f, caps ? caps->homogeneousDepth : false);
    bgfx::setViewTransform(viewId, nullptr, ortho);
}

// 颜色由 TileRenderer 管理

int main(int /*argc*/, char* /*argv*/[])
{
    Tina::Core::Log::InitWithFile("Tina", Tina::Core::Log::Level::Info,
                                  "logs/tina.log", 10ull*1024ull*1024ull, 5, false);
    TINA_INFO("启动 Tina（完整游戏最小恢复）");

    // 创建窗口
    const int winW = 1280, winH = 720;
    Tina::os::InitWindowArgs args{}; args.width = winW; args.height = winH; args.name = "Tina (Game)";
    Tina::os::WindowHandle window = Tina::os::createWindow(args);
    if (window == Tina::os::INVALID_WINDOW_HANDLE) { TINA_ERROR("创建窗口失败"); return -1; }

    // 获取原生窗口句柄
    void* nwh = nullptr;
    {
        SDL_Window* sdl_win = (SDL_Window*)window;
        SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
#ifdef SDL_PROP_WINDOW_WIN32_HWND_POINTER
        nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
        nwh = SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr);
#endif
    }
    if (!nwh) { TINA_ERROR("无法获取原生窗口句柄"); Tina::os::destroyWindow(window); return -1; }

    // 初始化 bgfx
    bgfx::PlatformData pd{}; pd.nwh = nwh;
    bgfx::Init init{}; init.type = bgfx::RendererType::Count; init.platformData = pd;
    int pxW = winW, pxH = winH; SDL_GetWindowSizeInPixels((SDL_Window*)window, &pxW, &pxH);
    init.resolution.width = (uint32_t)pxW; init.resolution.height = (uint32_t)pxH; init.resolution.reset = (uint32_t)(BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8);
    if (!bgfx::init(init)) { TINA_ERROR("bgfx 初始化失败"); Tina::os::destroyWindow(window); return -1; }

    // 视图：1=世界-固体，2=世界-水，3=UI像素
    bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    ResetBgfxWithSize(pxW, pxH, init.resolution.reset);
    // UI 像素坐标（view=3，y 向下）：bottom=pxH, top=0
    bgfx::setViewRect(3, 0, 0, (uint16_t)pxW, (uint16_t)pxH);
    SetupOrtho(3, 0.0f, (float)pxW, (float)pxH, 0.0f);

    // 加载着色器与文本渲染（显式控制 ShaderManager 生命周期）
    std::unique_ptr<Tina::renderer::ShaderManager> shaderMgr = std::make_unique<Tina::renderer::ShaderManager>();
    shaderMgr->initialize();
    bgfx::ProgramHandle progColor = shaderMgr->loadProgram("color", "color");
    Tina::UI::TextRenderer text; if (!text.initialize()) { TINA_ERROR("TextRenderer 初始化失败"); }
    else { (void)text.loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 24); }

    // 粒子系统（纯视觉，不使用物理）
    Tina::Particles::ParticleSystem2D particles;
    if (!particles.initialize(*shaderMgr)) {
        TINA_WARN("ParticleSystem ��ʼ��ʧ��: ����Ч���ڵ");
    } else {
        particles.setGlobalAcceleration(0.0f, -12.0f);
        particles.setDrag(0.10f);
    }

    // 颜色顶点布局
    bgfx::VertexLayout colorLayout; colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
        .end();

    // 生成地图
    Tina::Game::TileMapConfig mapCfg; mapCfg.width = 160; mapCfg.height = 90; mapCfg.seed = 1337;
    Tina::Game::TileMap tilemap(mapCfg);
    tilemap.generate();

    // UI 渲染器 + 顶部工具栏
    Tina::UI::UIRenderer uiRenderer;
    uiRenderer.initialize(*shaderMgr, &text);
    Tina::UI::UIToolbar toolbar;
    toolbar.initialize(pxW, pxH, uiRenderer, &text);

    // 角色信息面板
    Tina::UI::UICharacterPanel characterPanel;
    characterPanel.centerOnScreen(pxW, pxH);

    // 渲染器
    Tina::Renderer::TileRenderer tileRenderer; tileRenderer.initialize();

    // 使用 ECS 世界管理玩家/角色
    Tina::ECS::World ecsWorld;
    // 选择出生列（地图中央）
    int spawnX = mapCfg.width / 2;
    int spawnY = mapCfg.height / 2; // 兜底值（若未找到合法地表，将被修正）

    // 判定“自然地表”：草/土/石/沙/雪/冰/粘土（排除树木、树叶、装饰、矿石等）
    auto isNaturalGround = [&](Tina::Game::TileType t){ return tilemap.isNaturalGround(t); };

    // 按列搜索函数：在列 cx 中寻找出生 y（2格净空，非液体）
    auto findSpawnInColumn = [&](int cx, int& outY)->bool {
        if (cx < 0 || cx >= mapCfg.width) return false;
        for (int y = mapCfg.height - 3; y >= 0; --y) { // 预留头顶2格空间
            auto base = tilemap.get(cx, y);
            if (!isNaturalGround(base)) continue;
            if (y + 2 >= mapCfg.height) continue;
            auto up1 = tilemap.get(cx, y + 1);
            auto up2 = tilemap.get(cx, y + 2);
            if (up1 == Tina::Game::TileType::Air && up2 == Tina::Game::TileType::Air &&
                !tilemap.isWater(cx, y + 1) && !tilemap.isWater(cx, y + 2) &&
                !tilemap.isLava(cx, y + 1) && !tilemap.isLava(cx, y + 2)) {
                outY = y + 1; return true;
            }
        }
        return false;
    };

    // 先尝试中央列
    bool foundSpawn = findSpawnInColumn(spawnX, spawnY);

    // 如中央列不可用，向左右扩散搜索最近可用列
    if (!foundSpawn) {
        int maxRadius = std::min(40, mapCfg.width / 2);
        for (int dx = 1; dx <= maxRadius && !foundSpawn; ++dx) {
            int left  = spawnX - dx;
            int right = spawnX + dx;
            if (findSpawnInColumn(left, spawnY))  { spawnX = left;  foundSpawn = true; break; }
            if (findSpawnInColumn(right, spawnY)) { spawnX = right; foundSpawn = true; break; }
        }
    }

    // 兜底：仍未找到就固定到地图中部上方，避免出界
    if (!foundSpawn) {
        spawnX = std::clamp(spawnX, 0, mapCfg.width - 1);
        spawnY = std::clamp(mapCfg.height / 2, 2, mapCfg.height - 3);
    }

    // 创建玩家角色（玩家控制）
    auto playerEntity = ecsWorld.createCharacter((float)spawnX, (float)spawnY, /*isPlayerControlled*/ true);

    // 创建额外的AI角色（在玩家附近）
    // 角色1：玩家右侧5格
    auto npc1 = ecsWorld.createCharacter((float)spawnX + 5.0f, (float)spawnY, false);
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc1).r = 0.2f;
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc1).g = 1.0f;
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc1).b = 0.2f;  // 绿色

    // 角色2：玩家左侧5格
    auto npc2 = ecsWorld.createCharacter((float)spawnX - 5.0f, (float)spawnY, false);
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc2).r = 0.2f;
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc2).g = 0.5f;
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc2).b = 1.0f;  // 蓝色

    // 角色3：玩家右侧10格
    auto npc3 = ecsWorld.createCharacter((float)spawnX + 10.0f, (float)spawnY, false);
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc3).r = 1.0f;
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc3).g = 1.0f;
    ecsWorld.registry().get<Tina::ECS::Renderable>(npc3).b = 0.2f;  // 黄色

    // 相机（世界单位：1=1格）- 将改为跟随玩家
    float camX = 0.0f, camY = 0.0f;
    float viewW = std::min(80.0f, (float)mapCfg.width), viewH = std::min(60.0f, (float)mapCfg.height);
    Tina::Game::Camera2D camera; camera.setViewportPixels(pxW, pxH); camera.setViewHeightWorld(viewH);

    // 主循环
    Tina::Core::FrameTimer frameTimer; frameTimer.reset();
    bool running = true;
    bool mouseLeftDown = false;
    while (running) {
        frameTimer.beginFrame();
        Event ev;
        while (Tina::os::getEvent(ev)) {
            switch (ev.type) {
                case Event::Type::QUIT:
                case Event::Type::WINDOW_CLOSE: running = false; break;
                case Event::Type::MOUSE_WHEEL: {
                    // 滚轮切换选中工具（向上下一项，向下上一项）
                    int n = toolbar.slotCount();
                    if (n > 0) {
                        int cur = toolbar.selectedIndex();
                        if (cur < 0) cur = 0;
                        if (ev.mouse_wheel.amount > 0.0f) cur = (cur + 1) % n;
                        else if (ev.mouse_wheel.amount < 0.0f) cur = (cur - 1 + n) % n;
                        toolbar.select(cur);
                    }
                } break;
                case Event::Type::WINDOW_SIZE:
                    pxW = ev.win_size.w; pxH = ev.win_size.h;
                    ResetBgfxWithSize(pxW, pxH, init.resolution.reset);
                    bgfx::setViewRect(3, 0, 0, (uint16_t)pxW, (uint16_t)pxH);
                    SetupOrtho(3, 0.0f, (float)pxW, (float)pxH, 0.0f);
                    camera.setViewportPixels(pxW, pxH);
                    toolbar.onResize(pxW, pxH);
                    characterPanel.centerOnScreen(pxW, pxH);
                    break;
                case Event::Type::MOUSE_BUTTON:
                {
                    bool __toolbarHandled = false;
                    if (ev.mouse_button.down) {
                        // 右键点击：查看角色信息（不切换控制）
                        if (ev.mouse_button.button == Tina::os::MouseButton::RIGHT) {
                            float mx=0.0f, my=0.0f;
                            SDL_GetMouseState(&mx, &my);

                            // 转换到世界坐标
                            float wx=0.0f, wy=0.0f;
                            Tina::Game::screenToWorld(mx, my, pxW, pxH, camera, wx, wy);

                            // 检测所有角色
                            auto& reg = ecsWorld.registry();
                            auto view = reg.view<Tina::ECS::Transform, Tina::ECS::PhysicsBody,
                                                 Tina::ECS::Name, Tina::ECS::Health>();

                            bool clickedCharacter = false;
                            for (auto entity : view) {
                                auto& transform = view.get<Tina::ECS::Transform>(entity);
                                auto& body = view.get<Tina::ECS::PhysicsBody>(entity);
                                auto& name = view.get<Tina::ECS::Name>(entity);
                                auto& health = view.get<Tina::ECS::Health>(entity);

                                // AABB碰撞检测
                                if (wx >= transform.x && wx <= transform.x + body.width &&
                                    wy >= transform.y && wy <= transform.y + body.height) {
                                    // 点击了角色！更新面板数据
                                    characterPanel.updateData(name.name, health.percentage());
                                    characterPanel.setVisible(true);
                                    clickedCharacter = true;
                                    TINA_INFO("查看角色信息: {}, 血量: {:.0f}/{:.0f}", name.name, health.current, health.max);
                                    break;
                                }
                            }

                            // 如果没点击角色，隐藏面板
                            if (!clickedCharacter) {
                                characterPanel.setVisible(false);
                            }

                            __toolbarHandled = true;
                        }

                        // 左键点击：检测角色并切换控制（点击世界则显示工具栏）
                        if (ev.mouse_button.button == Tina::os::MouseButton::LEFT) {
                            mouseLeftDown = true;
                            float mx=0.0f, my=0.0f;
                            SDL_GetMouseState(&mx, &my);

                            // 如果点击了工具栏，处理工具栏事件
                            if (toolbar.hitTest(mx, my)) {
                                __toolbarHandled = true;
                            } else {
                                // 转换到世界坐标
                                float wx=0.0f, wy=0.0f;
                                Tina::Game::screenToWorld(mx, my, pxW, pxH, camera, wx, wy);
                                int tx = (int)std::floor(wx);
                                int ty = (int)std::floor(wy);

                                // 检测所有角色
                                auto& reg = ecsWorld.registry();
                                auto view = reg.view<Tina::ECS::Transform, Tina::ECS::PhysicsBody,
                                                     Tina::ECS::Name, Tina::ECS::Health>();

                                bool clickedCharacter = false;
                                for (auto entity : view) {
                                    auto& transform = view.get<Tina::ECS::Transform>(entity);
                                    auto& body = view.get<Tina::ECS::PhysicsBody>(entity);
                                    auto& name = view.get<Tina::ECS::Name>(entity);

                                    // AABB碰撞检测
                                    if (wx >= transform.x && wx <= transform.x + body.width &&
                                        wy >= transform.y && wy <= transform.y + body.height) {
                                        // 点击了角色！切换控制权
                                        ecsWorld.switchControl(entity);
                                        // 隐藏工具栏（控制角色时不需要编辑地形）
                                        toolbar.root()->setVisible(false);
                                        clickedCharacter = true;
                                        TINA_INFO("切换控制到角色: {}", name.name);
                                        break;
                                    }
                                }

                                // 如果没点击角色，说明点击了世界
                                if (!clickedCharacter) {
                                    // 显示工具栏，允许编辑地形
                                    toolbar.root()->setVisible(true);

                                    // 执行地形编辑工具
                                    if (tx>=0 && ty>=0 && tx<mapCfg.width && ty<mapCfg.height) {
                                        int tool = toolbar.selectedIndex();
                                        if (tool < 0) tool = 0;
                                        switch (tool) {
                                            case 0: // 注水
                                                Tina::Game::placeWater(tilemap, tx, ty, 255);
                                                break;
                                            case 1: // 挖空（圆形）
                                            case 2: { // 爆炸：挖空 + 粒子
                                                Tina::Game::excavateCircle(tilemap, wx, wy, Tina::GameConfig::EXCAVATE_RADIUS);
                                                if (tool == 2) {
                                                    particles.explode(wx, wy,
                                                                      /*count*/ 260,
                                                                      /*speed*/ 6.0f, 14.0f,
                                                                      /*size*/ 0.30f, 0.90f,
                                                                      /*life*/ 0.6f, 1.6f,
                                                                      /*color*/ 0.78f, 0.70f, 0.58f);
                                                }
                                            } break;
                                            default: break;
                                        }
                                    }
                                }
                                __toolbarHandled = true;
                            }
                        }
                    }
                    if (__toolbarHandled) { break; }
                }
                    break;
#if 0 // legacy mouse branch (replaced by toolbar-driven tools)
                    if (ev.mouse_button.down) { if (ev.mouse_button.button == Tina::os::MouseButton::LEFT) mouseLeftDown = true;
                        float mx=0.0f, my=0.0f; SDL_GetMouseState(&mx, &my);
                        if (toolbar.hitTest(mx, my)) break;
                        float u = (pxW>0)? mx / (float)pxW : 0.0f;
                        float v = (pxH>0)? 1.0f - my / (float)pxH : 0.0f; // 映射到世界坐标（y 向上）
                        float wx = camX + u * viewW;
                        float wy = camY + v * viewH;
                        int tx = (int)std::floor(wx);
                        int ty = (int)std::floor(wy);
                        if (tx>=0 && ty>=0 && tx<mapCfg.width && ty<mapCfg.height) {
                            // 支持：右键爆破；或按住 Shift + 左键爆破；否则左键注水
                            const bool* ksNow = SDL_GetKeyboardState(nullptr);
                            bool shiftDown = ksNow[SDL_SCANCODE_LSHIFT] || ksNow[SDL_SCANCODE_RSHIFT];
                            if (ev.mouse_button.button == Tina::os::MouseButton::LEFT && !shiftDown) {
                                tilemap.setWater(tx, ty, 255);
                            } else if (ev.mouse_button.button == Tina::os::MouseButton::RIGHT || (ev.mouse_button.button == Tina::os::MouseButton::LEFT && shiftDown)) {
                                // 简易爆破：半径 3.5 格，清空为空气
                                const float radius = 3.5f, r2 = radius*radius;
                                int x0 = std::max(0, (int)std::floor(wx - radius));
                                int y0 = std::max(0, (int)std::floor(wy - radius));
                                int x1 = std::min(mapCfg.width-1,  (int)std::ceil(wx + radius));
                                int y1 = std::min(mapCfg.height-1, (int)std::ceil(wy + radius));
                                for (int y=y0; y<=y1; ++y) for (int x=x0; x<=x1; ++x) {
                                    float cx = x+0.5f, cy = y+0.5f;
                                    float dx = cx-wx, dy = cy-wy;
                                    if (dx*dx+dy*dy <= r2) tilemap.setSafe(x,y, Tina::Game::TileType::Air);
                                }
                                // 右键爆炸：纯视觉“尘雾”粒子（不启用物理碎块）
                                if (ev.mouse_button.button == Tina::os::MouseButton::RIGHT) {
                                    particles.explode(wx, wy,
                                                      /*count*/ 260,
                                                      /*speed*/ 6.0f, 14.0f,
                                                      /*size*/ 0.30f, 0.90f,
                                                      /*life*/ 0.6f, 1.6f,
                                                      /*color*/ 0.78f, 0.70f, 0.58f);
                                }
                            }
                        }
                    }
                    break;
#endif
                default: break;
            }
        }

        // 简单 WASD 相机（使用 SDL 键盘状态）- 改为玩家控制
        // 确保键盘状态已更新（如果这一帧没有事件到来，也要刷新）
        SDL_PumpEvents();
        const bool* ks = SDL_GetKeyboardState(nullptr);

        // 玩家移动控制
        // ECS 输入与更新
        Tina::ECS::InputState input{};
        input.moveLeft  = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT];
        input.moveRight = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
        input.jump      = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_SPACE];
        ecsWorld.update((float)frameTimer.deltaSeconds(), tilemap, input);

        // 相机平滑跟随玩家
        viewW = (float)camera.viewW(); // 与 Camera2D 保持一致的视区宽度
        float targetCamX = camX;
        float targetCamY = camY;
        {
            auto e = ecsWorld.getControlledEntity();
            if (e != entt::null) {
                auto& reg = ecsWorld.registry();
                if (reg.any_of<Tina::ECS::Transform, Tina::ECS::PhysicsBody>(e)) {
                    auto& tr = reg.get<Tina::ECS::Transform>(e);
                    auto& pb = reg.get<Tina::ECS::PhysicsBody>(e);
                    float centerX = tr.x + pb.width * 0.5f;
                    float centerY = tr.y + pb.height * 0.5f;
                    targetCamX = centerX - viewW * 0.5f;
                    targetCamY = centerY - viewH * 0.5f;
                }
            }
        }
        camX += (targetCamX - camX) * 0.1f;
        camY += (targetCamY - camY) * 0.1f;
        camX = std::clamp(camX, 0.0f, std::max(0.0f, (float)mapCfg.width - viewW));
        camY = std::clamp(camY, 0.0f, std::max(0.0f, (float)mapCfg.height - viewH));

        // 工具栏快捷键（1-8 选择），重复选择幂等
        if (ks[SDL_SCANCODE_1]) toolbar.select(0);
        if (ks[SDL_SCANCODE_2]) toolbar.select(1);
        if (ks[SDL_SCANCODE_3]) toolbar.select(2);
        if (ks[SDL_SCANCODE_4]) toolbar.select(3);
        if (ks[SDL_SCANCODE_5]) toolbar.select(4);
        if (ks[SDL_SCANCODE_6]) toolbar.select(5);
        if (ks[SDL_SCANCODE_7]) toolbar.select(6);
        if (ks[SDL_SCANCODE_8]) toolbar.select(7);

        // 世界视图（1=固体，2=水）
        bgfx::setViewRect(1, 0, 0, (uint16_t)pxW, (uint16_t)pxH);
        bgfx::setViewRect(2, 0, 0, (uint16_t)pxW, (uint16_t)pxH);
        camera.setViewHeightWorld(viewH);
        camera.setPosition(camX, camY);
        float viewM[16], projM[16];
        camera.buildViewProj(viewM, projM);
        bgfx::setViewTransform(1, viewM, projM);
        bgfx::setViewTransform(2, viewM, projM);

        // 背景清屏（放在世界视图 1 上）
        bgfx::touch(1);
        bgfx::touch(2);

        // 水模拟（两次迭代）
        int a=0,b=0,c=0,d=0; tilemap.stepWaterAdvanced(2, a,b,c,d);

        // 粒子更新
        particles.update((float)frameTimer.deltaSeconds());

        // 连续清除：左键按住 + 工具2=清除（避免与工具栏点击冲突，命中 UI 则不执行）
        {
            float mx=0.0f, my=0.0f;
            uint32_t btnMask = (uint32_t)SDL_GetMouseState(&mx, &my);
#ifdef SDL_BUTTON_MASK
            bool leftHeld = (btnMask & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
#else
            bool leftHeld = (btnMask & SDL_BUTTON_LMASK) != 0;
#endif
            if (leftHeld && !toolbar.hitTest(mx, my)) {
                int tool = toolbar.selectedIndex();
                if (tool == 1) {
                    // 将鼠标像素坐标映射到世界坐标，并圆形清除固体
                    float wx=0.0f, wy=0.0f;
                    Tina::Game::screenToWorld(mx, my, pxW, pxH, camera, wx, wy);
                    Tina::Game::excavateCircle(tilemap, wx, wy, Tina::GameConfig::EXCAVATE_RADIUS);
                }
            }
        }

        // 渲染（固体 / 水 / 玩家）
        tileRenderer.renderSolid(tilemap, 1, progColor, colorLayout);
        tileRenderer.renderWater(tilemap, 2, progColor, colorLayout);
        ecsWorld.render(2, progColor, colorLayout);





        // UI 文本（视图3，像素坐标，最后绘制）
        // 如果工具栏可见，文本在工具栏下方；否则在屏幕顶部
        float hudY = toolbar.root()->isVisible() ? (float)toolbar.barHeight() + 12.0f : 12.0f;
        uiRenderer.drawTextEx(3,
                              16.0f, hudY,
                              (float)pxW - 32.0f, 28.0f,
                              1,1,1,1,
                              "A/D 移动 | W/空格 跳跃 | 左键点击角色切换控制/点击地形编辑 | 右键查看角色信息 | 滚轮/数字键切换工具",
                              Tina::UI::UIRenderer::AlignH::Left,
                              Tina::UI::UIRenderer::AlignV::Top,
                              0.0f, 0.0f);

        particles.render(2);

        // UI 事件与渲染
        {
            float mx=0.0f, my=0.0f; SDL_GetMouseState(&mx, &my);
            toolbar.setMousePos(mx, my);
            toolbar.events().updateMouse(mx, my, mouseLeftDown);
            toolbar.events().processEvents();
            toolbar.update((float)frameTimer.deltaSeconds());
            toolbar.render(3);

            // 角色信息面板更新与渲染
            characterPanel.update((float)frameTimer.deltaSeconds());
            characterPanel.render(3, uiRenderer);

            // 将 mouseLeftDown 退回到 false，实现一次性点击沿用事件边沿
            mouseLeftDown = false;
        }
        bgfx::frame();
    }

    text.shutdown();
    // 在 bgfx::shutdown 之前销毁 ShaderManager，避免句柄在关闭后被销毁导致崩溃
    shaderMgr.reset();
    bgfx::shutdown();
    Tina::os::destroyWindow(window);
    TINA_INFO("退出 Tina");
    Tina::Core::Log::Shutdown();
    return 0;
}












