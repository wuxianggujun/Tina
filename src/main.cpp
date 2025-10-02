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
#include "ui/TextRenderer.hpp"
#include "ui/UIToolbar.hpp"
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

struct ColorVertex { float x, y, z; float r, g, b, a; };

static Tina::Container::Array<float,4> tileColor(Tina::Game::TileType t)
{
    using Tina::Game::TileType;
    switch (t) {
        case TileType::Grass:    return {{0.18f, 0.72f, 0.28f, 1.0f}};
        case TileType::Dirt:     return {{0.55f, 0.38f, 0.22f, 1.0f}};
        case TileType::Stone:    return {{0.55f, 0.55f, 0.58f, 1.0f}};
        case TileType::Sand:     return {{0.94f, 0.86f, 0.51f, 1.0f}};
        case TileType::Snow:     return {{0.95f, 0.95f, 0.98f, 1.0f}};
        case TileType::Ice:      return {{0.68f, 0.85f, 0.90f, 1.0f}};
        case TileType::Water:    return {{0.15f, 0.35f, 0.90f, 0.95f}};
        case TileType::Lava:     return {{0.90f, 0.25f, 0.10f, 1.0f}};
        case TileType::Coal:     return {{0.20f, 0.20f, 0.20f, 1.0f}};
        case TileType::Iron:     return {{0.60f, 0.55f, 0.50f, 1.0f}};
        case TileType::Gold:     return {{0.90f, 0.75f, 0.20f, 1.0f}};
        case TileType::Diamond:  return {{0.85f, 0.95f, 0.95f, 1.0f}};
        case TileType::Clay:     return {{0.72f, 0.45f, 0.30f, 1.0f}};
        case TileType::Bedrock:  return {{0.15f, 0.15f, 0.15f, 1.0f}};
        case TileType::Obsidian: return {{0.25f, 0.15f, 0.25f, 1.0f}};
        case TileType::Wood:     return {{0.45f, 0.35f, 0.25f, 1.0f}};
        case TileType::Leaves:   return {{0.25f, 0.60f, 0.30f, 1.0f}};
        default:                 return {{0.0f,  0.0f,  0.0f,  0.0f}};
    }
}

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
    // 相机（世界单位：1=1格）
    float camX = 0.0f, camY = 0.0f;
    float viewW = std::min(80.0f, (float)mapCfg.width), viewH = std::min(60.0f, (float)mapCfg.height);

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
                    toolbar.onResize(pxW, pxH);
                    break;
                case Event::Type::MOUSE_BUTTON:
                {
                    bool __toolbarHandled = false;
                    if (ev.mouse_button.down) {
                        // 禁用旧右键分支：统一由左键 + 工具选择驱动
                        if (ev.mouse_button.button == Tina::os::MouseButton::RIGHT) {
                            __toolbarHandled = true;
                        }
                        if (ev.mouse_button.button == Tina::os::MouseButton::LEFT) mouseLeftDown = true;
                        float mx=0.0f, my=0.0f; SDL_GetMouseState(&mx, &my);
                        // 若命中 UI，交给 UI 系统处理
                        if (toolbar.hitTest(mx, my)) {
                            __toolbarHandled = true;
                        } else {
                            float u = (pxW>0)? mx / (float)pxW : 0.0f;
                            float v = (pxH>0)? 1.0f - my / (float)pxH : 0.0f; // 世界坐标（y 向上）
                            float wx = camX + u * viewW;
                            float wy = camY + v * viewH;
                            int tx = (int)std::floor(wx);
                            int ty = (int)std::floor(wy);
                            if (ev.mouse_button.button == Tina::os::MouseButton::LEFT &&
                                tx>=0 && ty>=0 && tx<mapCfg.width && ty<mapCfg.height) {
                                int tool = toolbar.selectedIndex(); if (tool < 0) tool = 0;
                                switch (tool) {
                                    case 0: // 注水
                                        tilemap.setWater(tx, ty, 255);
                                        break;
                                    case 1: // 挖空（圆形）
                                    case 2: { // 爆炸：挖空 + 粒子
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

        // 简单 WASD 相机（使用 SDL 键盘状态）
        // 确保键盘状态已更新（如果这一帧没有事件到来，也要刷新）
        SDL_PumpEvents();
        const bool* ks = SDL_GetKeyboardState(nullptr);
        float move = 60.0f * (float)frameTimer.deltaSeconds();
        if (ks[SDL_SCANCODE_W]) camY -= move;
        if (ks[SDL_SCANCODE_S]) camY += move;
        if (ks[SDL_SCANCODE_A]) camX -= move;
        if (ks[SDL_SCANCODE_D]) camX += move;
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
        SetupOrtho(1, camX, camX + viewW, camY, camY + viewH);
        SetupOrtho(2, camX, camX + viewW, camY, camY + viewH);

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
                    // 将鼠标像素坐标映射到世界坐标
                    float u = (pxW>0)? mx / (float)pxW : 0.0f;
                    float v = (pxH>0)? 1.0f - my / (float)pxH : 0.0f;
                    float wx = camX + u * viewW;
                    float wy = camY + v * viewH;
                    // 圆形范围清除固体
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
                }
            }
        }

        // 构建固体网格（即时）
        const int W = mapCfg.width, H = mapCfg.height;
        const int maxTiles = W*H;
        const uint32_t maxV = (uint32_t)maxTiles * 4;
        const uint32_t maxI = (uint32_t)maxTiles * 6;

        bgfx::TransientVertexBuffer tvbSolid; bgfx::TransientIndexBuffer tibSolid;
        if (bgfx::getAvailTransientVertexBuffer(maxV, colorLayout) >= maxV &&
            bgfx::getAvailTransientIndexBuffer(maxI) >= maxI) {
            bgfx::allocTransientVertexBuffer(&tvbSolid, maxV, colorLayout);
            bgfx::allocTransientIndexBuffer(&tibSolid, maxI);
            ColorVertex* vptr = (ColorVertex*)tvbSolid.data; uint16_t* iptr = (uint16_t*)tibSolid.data;
            uint32_t vb=0, ib=0;
            for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
                auto t = tilemap.get(x,y);
                if (t == Tina::Game::TileType::Air || t == Tina::Game::TileType::Water || t == Tina::Game::TileType::Lava) continue;
                auto c4 = tileColor(t);
                float x0=(float)x, y0=(float)y, x1=x0+1.0f, y1=y0+1.0f;
                vptr[vb+0] = { x0,y0,0.0f, c4[0],c4[1],c4[2],c4[3] };
                vptr[vb+1] = { x1,y0,0.0f, c4[0],c4[1],c4[2],c4[3] };
                vptr[vb+2] = { x1,y1,0.0f, c4[0],c4[1],c4[2],c4[3] };
                vptr[vb+3] = { x0,y1,0.0f, c4[0],c4[1],c4[2],c4[3] };
                iptr[ib+0]= (uint16_t)(vb+0); iptr[ib+1]= (uint16_t)(vb+1); iptr[ib+2]= (uint16_t)(vb+2);
                iptr[ib+3]= (uint16_t)(vb+0); iptr[ib+4]= (uint16_t)(vb+2); iptr[ib+5]= (uint16_t)(vb+3);
                vb+=4; ib+=6;
            }
            if (ib>0) {
                tvbSolid.size = vb * sizeof(ColorVertex); tibSolid.size = ib * sizeof(uint16_t);
                bgfx::Encoder* enc = bgfx::begin();
                enc->setVertexBuffer(0, &tvbSolid);
                enc->setIndexBuffer(&tibSolid);
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                enc->submit(1, progColor);
                bgfx::end(enc);
            }
        }

        // 构建水网格（透明叠加）
        bgfx::TransientVertexBuffer tvbWater; bgfx::TransientIndexBuffer tibWater;
        if (bgfx::getAvailTransientVertexBuffer(maxV, colorLayout) >= maxV &&
            bgfx::getAvailTransientIndexBuffer(maxI) >= maxI) {
            bgfx::allocTransientVertexBuffer(&tvbWater, maxV, colorLayout);
            bgfx::allocTransientIndexBuffer(&tibWater, maxI);
            ColorVertex* vptr = (ColorVertex*)tvbWater.data; uint16_t* iptr = (uint16_t*)tibWater.data;
            uint32_t vb=0, ib=0;
            for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
                int wv = (int)tilemap.water(x,y); if (wv<=0) continue;
                float hfrac = (float)wv / 255.0f; if (hfrac <= 0.01f) continue;
                float x0=(float)x, y0=(float)y, x1=x0+1.0f; float yh = y0 + std::min(1.0f, hfrac);
                auto cw = tileColor(Tina::Game::TileType::Water); float alphaW = std::min(1.0f, std::max(0.25f, cw[3]*(0.6f+0.4f*hfrac)));
                vptr[vb+0] = { x0,y0,0.0f, cw[0],cw[1],cw[2], alphaW };
                vptr[vb+1] = { x1,y0,0.0f, cw[0],cw[1],cw[2], alphaW };
                vptr[vb+2] = { x1,yh,0.0f, cw[0],cw[1],cw[2], alphaW };
                vptr[vb+3] = { x0,yh,0.0f, cw[0],cw[1],cw[2], alphaW };
                iptr[ib+0]= (uint16_t)(vb+0); iptr[ib+1]= (uint16_t)(vb+1); iptr[ib+2]= (uint16_t)(vb+2);
                iptr[ib+3]= (uint16_t)(vb+0); iptr[ib+4]= (uint16_t)(vb+2); iptr[ib+5]= (uint16_t)(vb+3);
                vb+=4; ib+=6;
            }
            if (ib>0) {
                tvbWater.size = vb * sizeof(ColorVertex); tibWater.size = ib * sizeof(uint16_t);
                bgfx::Encoder* enc = bgfx::begin();
                enc->setVertexBuffer(0, &tvbWater);
                enc->setIndexBuffer(&tibWater);
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
                enc->submit(2, progColor);
                bgfx::end(enc);
            }
        }

        // UI 文本（视图3，像素坐标，最后绘制）
        float hudY = (float)toolbar.barHeight() + 12.0f;
        uiRenderer.drawTextEx(3,
                              16.0f, hudY,
                              (float)pxW - 32.0f, 28.0f,
                              1,1,1,1,
                              "WASD 移动 | 左键执行工具 | 滚轮/数字键切换",
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







