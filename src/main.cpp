// 使用统一 os 接口（SDL3 实现）创建窗口，事件循环打印日志，bgfx 完成最小渲染循环。

#include <cstdint>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include "core/Log.hpp"
#include "os/OS.hpp"
#include "engine/Resource.hpp"
#include "core/Path.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/ShaderManager.hpp"
#include "core/Time.hpp"
#include "game/TileMap.hpp"
#include "game/Camera2D.hpp"
#include "physics/Physics2D.hpp"

using Tina::os::Event;

// 将窗口尺寸更新到 bgfx
static void ResetBgfxWithSize(int w, int h, uint32_t resetFlags)
{
    if (w <= 0 || h <= 0) { w = 1280; h = 720; }
    bgfx::reset((uint32_t)w, (uint32_t)h, resetFlags);
    bgfx::setViewRect(0, 0, 0, (uint16_t)w, (uint16_t)h);
}

// 打印事件的简单帮助函数（用于测试事件系统）
static void LogEvent(const Event& e)
{
    switch (e.type) {
        case Event::Type::QUIT: TINA_INFO("事件: QUIT"); break;
        case Event::Type::WINDOW_CLOSE: TINA_INFO("事件: WINDOW_CLOSE"); break;
        case Event::Type::WINDOW_MOVE: TINA_INFO("事件: WINDOW_MOVE x={}, y={}", e.win_move.x, e.win_move.y); break;
        case Event::Type::WINDOW_SIZE: TINA_INFO("事件: WINDOW_SIZE w={}, h={}", e.win_size.w, e.win_size.h); break;
        case Event::Type::FOCUS: TINA_INFO("事件: FOCUS gained={}", e.focus.gained); break;
        case Event::Type::KEY: TINA_INFO("事件: KEY down={}, code={}, repeat={}", e.key.down, (int)e.key.key_code, e.key.is_repeat); break;
        case Event::Type::CHAR: TINA_INFO("事件: CHAR utf8_first_byte=0x{:02x}", e.text_input.utf8 & 0xff); break;
        case Event::Type::MOUSE_BUTTON: TINA_INFO("事件: MOUSE_BUTTON down={}, button={}", e.mouse_button.down, (int)e.mouse_button.button); break;
        case Event::Type::MOUSE_MOVE: TINA_INFO("事件: MOUSE_MOVE dx={}, dy={}", e.mouse_move.xrel, e.mouse_move.yrel); break;
        case Event::Type::MOUSE_WHEEL: TINA_INFO("事件: MOUSE_WHEEL amount={}", e.mouse_wheel.amount); break;
        case Event::Type::DROP_FILE: TINA_INFO("事件: DROP_FILE handle={}", e.file_drop.handle); break;
        default: break;
    }
}

int main(int /*argc*/, char* /*argv*/[])
{
    Tina::Core::Log::Init("Tina", Tina::Core::Log::Level::Info);
    TINA_INFO("启动 Tina，使用 SDL3 后端的 os 事件系统");

    // 1) 通过 os 接口创建窗口（底层 SDL3）
    const int winW = 1280;
    const int winH = 720;
    Tina::os::InitWindowArgs args{};
    args.width = winW; args.height = winH; args.name = "Tina (os + SDL3 + bgfx)";
    Tina::os::WindowHandle window = Tina::os::createWindow(args);
    if (window == Tina::os::INVALID_WINDOW_HANDLE) {
        TINA_ERROR("创建窗口失败");
        return -1;
    }

    // 2) 获取原生窗口句柄以初始化 bgfx（从 SDL_Window 属性提取）
    void* nwh = nullptr; // native window handle
    {
        SDL_Window* sdl_win = (SDL_Window*)window;
        SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
#if defined(_WIN32)
#ifdef SDL_PROP_WINDOW_WIN32_HWND_POINTER
        nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
        nwh = SDL_GetPointerProperty(props, "SDL.window.win32.hwnd", nullptr);
#endif
#elif defined(__APPLE__)
        // TODO: Cocoa/Metal 句柄
#elif defined(__linux__)
        // TODO: Wayland/X11 句柄
#endif
    }
    if (nwh == nullptr) {
        TINA_ERROR("获取原生窗口句柄失败。当前视频后端: {}", SDL_GetCurrentVideoDriver());
        Tina::os::destroyWindow(window);
        return -1;
    }
    TINA_INFO("窗口创建完成，原生句柄 nwh={}", (void*)nwh);

    // 3) 初始化 bgfx
    bgfx::PlatformData pd{}; pd.nwh = nwh;
    bgfx::Init init{};
    init.type = bgfx::RendererType::Count; // 自动选择
    init.platformData = pd;
    int pxW = winW, pxH = winH;
    SDL_GetWindowSizeInPixels((SDL_Window*)window, &pxW, &pxH);
    init.resolution.width = (uint32_t)pxW;
    init.resolution.height = (uint32_t)pxH;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        TINA_ERROR("bgfx 初始化失败: 后端={} 像素尺寸={}x{} nwh={} ", (int)init.type, (int)init.resolution.width, (int)init.resolution.height, nwh);
        Tina::os::destroyWindow(window);
        return -1;
    }
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    ResetBgfxWithSize(pxW, pxH, init.resolution.reset);

    // 3.x) 初始化 Renderer/Pipeline
    Tina::Gfx::Renderer renderer; renderer.setDisplaySize(pxW, pxH);
    Tina::Gfx::Pipeline pipeline(renderer);
    pipeline.setViewport(pxW, pxH);

    // 3.x.1) 创建着色器管理器并加载着色器程序
    Tina::renderer::ShaderManager shaderManager;
    shaderManager.initialize(); // 使用默认路径 "resources/shaders"
    bgfx::ProgramHandle prog_flat = shaderManager.loadProgram("flat", "flat");
    bgfx::ProgramHandle prog_color = shaderManager.loadProgram("color", "color");

    // 3.x.2) 生成一个简易泰拉瑞亚风格的颜色方块地图，并构建网格（世界坐标，每 tile=1.0）
    struct ColorVertex { float x, y, z; float r, g, b, a; };
    bgfx::VertexLayout colorLayout;
    colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .end();

    Tina::Game::TileMapConfig mapCfg; mapCfg.width = 160; mapCfg.height = 90; // 16:9
    Tina::Game::TileMap tilemap(mapCfg);
    tilemap.generate();

    auto tileColor = [](Tina::Game::TileType t)->Tina::Container::Array<float,4>{
        switch (t) {
            case Tina::Game::TileType::Grass: return { {0.18f, 0.72f, 0.28f, 1.0f} };
            case Tina::Game::TileType::Dirt:  return { {0.55f, 0.38f, 0.22f, 1.0f} };
            case Tina::Game::TileType::Stone: return { {0.55f, 0.55f, 0.58f, 1.0f} };
            default:                          return { {0.0f,  0.0f,  0.0f,  0.0f} };
        }
    };
    // 分块构建网格（chunk 渲染）：仅提交视区内 chunk
    struct TileChunk {
        int cx=0, cy=0; // chunk 坐标
        float minx=0, miny=0, maxx=0, maxy=0; // AABB（世界）
        bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        bool valid() const { return bgfx::isValid(vb) && bgfx::isValid(ib) && indexCount>0; }
    };
    const int chunkSize = 32;
    const int cxCount = (mapCfg.width + chunkSize - 1) / chunkSize;
    const int cyCount = (mapCfg.height + chunkSize - 1) / chunkSize;
    Tina::Container::Vector<TileChunk> chunks;
    chunks.reserve((size_t)cxCount * cyCount);
    for (int cy = 0; cy < cyCount; ++cy) {
        for (int cx = 0; cx < cxCount; ++cx) {
            const int x0i = cx * chunkSize;
            const int y0i = cy * chunkSize;
            const int x1i = (x0i + chunkSize > mapCfg.width ? mapCfg.width : x0i + chunkSize);
            const int y1i = (y0i + chunkSize > mapCfg.height ? mapCfg.height : y0i + chunkSize);
            Tina::Container::Vector<ColorVertex> v;
            Tina::Container::Vector<uint32_t>    idx;
            v.reserve((size_t)(x1i-x0i)*(y1i-y0i));
            idx.reserve(v.capacity()*6);
            for (int y = y0i; y < y1i; ++y) {
                for (int x = x0i; x < x1i; ++x) {
                    auto t = tilemap.get(x,y);
                    if (t == Tina::Game::TileType::Air) continue;
                    const auto c = tileColor(t);
                    const float x0 = (float)x;
                    const float y0 = (float)y;
                    const float x1 = x0 + 1.0f;
                    const float y1 = y0 + 1.0f;
                    const uint32_t base = (uint32_t)v.size();
                    v.push_back({ x0, y0, 0.0f, c[0], c[1], c[2], c[3] });
                    v.push_back({ x1, y0, 0.0f, c[0], c[1], c[2], c[3] });
                    v.push_back({ x1, y1, 0.0f, c[0], c[1], c[2], c[3] });
                    v.push_back({ x0, y1, 0.0f, c[0], c[1], c[2], c[3] });
                    idx.push_back(base + 0);
                    idx.push_back(base + 1);
                    idx.push_back(base + 2);
                    idx.push_back(base + 0);
                    idx.push_back(base + 2);
                    idx.push_back(base + 3);
                }
            }
            TileChunk ch{}; ch.cx=cx; ch.cy=cy; ch.minx=(float)x0i; ch.miny=(float)y0i; ch.maxx=(float)x1i; ch.maxy=(float)y1i;
            if (!v.empty()) {
                const bgfx::Memory* memv = bgfx::copy(v.data(), (uint32_t)(v.size()*sizeof(ColorVertex)));
                ch.vb = bgfx::createVertexBuffer(memv, colorLayout);
                const bgfx::Memory* memi = bgfx::copy(idx.data(), (uint32_t)(idx.size()*sizeof(uint32_t)));
                ch.ib = bgfx::createIndexBuffer(memi, BGFX_BUFFER_INDEX32);
                ch.indexCount = (uint32_t)idx.size();
            }
            chunks.push_back(ch);
        }
    }

    // 2D 相机（统一坐标系：世界单位=tile，原点左下，y 向上）
    Tina::Game::Camera2D camera;
    camera.setViewportPixels(pxW, pxH);
    camera.setViewHeightWorld((float)mapCfg.height); // 初始：纵向完全显示
    camera.setPosition(0.0f, 0.0f);

    // 物理世界（Box2D）：重力向下，创建边界包围整个地图
    Tina::Physics::Physics2D physics(-30.0f);
    physics.createBounds(0.0f, 0.0f, (float)mapCfg.width, (float)mapCfg.height + 10.0f, 1.0f);

    // 重建指定 chunk（当瓦片变化时调用）
    auto rebuildChunk = [&](int cx, int cy){
        if (cx < 0 || cy < 0) return;
        const int idx = cy * cxCount + cx;
        if (idx < 0 || idx >= (int)chunks.size()) return;
        auto& ch = chunks[(size_t)idx];
        if (bgfx::isValid(ch.vb)) { bgfx::destroy(ch.vb); ch.vb = BGFX_INVALID_HANDLE; }
        if (bgfx::isValid(ch.ib)) { bgfx::destroy(ch.ib); ch.ib = BGFX_INVALID_HANDLE; }
        ch.indexCount = 0;

        const int x0i = cx * chunkSize;
        const int y0i = cy * chunkSize;
        const int x1i = (x0i + chunkSize > mapCfg.width ? mapCfg.width : x0i + chunkSize);
        const int y1i = (y0i + chunkSize > mapCfg.height ? mapCfg.height : y0i + chunkSize);
        Tina::Container::Vector<ColorVertex> v;
        Tina::Container::Vector<uint32_t>    idxv;
        v.reserve((size_t)(x1i-x0i)*(y1i-y0i));
        idxv.reserve(v.capacity()*6);
        for (int y = y0i; y < y1i; ++y) {
            for (int x = x0i; x < x1i; ++x) {
                auto t = tilemap.get(x,y);
                if (t == Tina::Game::TileType::Air) continue;
                const auto c = tileColor(t);
                const float x0 = (float)x, y0 = (float)y, x1 = x0+1.0f, y1 = y0+1.0f;
                const uint32_t base = (uint32_t)v.size();
                v.push_back({ x0, y0, 0.0f, c[0], c[1], c[2], c[3] });
                v.push_back({ x1, y0, 0.0f, c[0], c[1], c[2], c[3] });
                v.push_back({ x1, y1, 0.0f, c[0], c[1], c[2], c[3] });
                v.push_back({ x0, y1, 0.0f, c[0], c[1], c[2], c[3] });
                idxv.push_back(base+0); idxv.push_back(base+1); idxv.push_back(base+2);
                idxv.push_back(base+0); idxv.push_back(base+2); idxv.push_back(base+3);
            }
        }
        if (!v.empty()) {
            const bgfx::Memory* memv = bgfx::copy(v.data(), (uint32_t)(v.size()*sizeof(ColorVertex)));
            ch.vb = bgfx::createVertexBuffer(memv, colorLayout);
            const bgfx::Memory* memi = bgfx::copy(idxv.data(), (uint32_t)(idxv.size()*sizeof(uint32_t)));
            ch.ib = bgfx::createIndexBuffer(memi, BGFX_BUFFER_INDEX32);
            ch.indexCount = (uint32_t)idxv.size();
        }
    };

    // 爆炸：移除半径内的 tile，生成物理碎块，并重建受影响的 chunk
    auto explodeAt = [&](float wx, float wy, float radius){
        const float r2 = radius*radius;
        const int x0 = (int)std::max(0, (int)std::floor(wx - radius));
        const int y0 = (int)std::max(0, (int)std::floor(wy - radius));
        const int x1 = (int)std::min(mapCfg.width-1, (int)std::ceil(wx + radius));
        const int y1 = (int)std::min(mapCfg.height-1, (int)std::ceil(wy + radius));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float cx = x + 0.5f, cy = y + 0.5f;
                const float dx = cx - wx, dy = cy - wy;
                if (dx*dx + dy*dy > r2) continue;
                auto t = tilemap.get(x,y);
                if (t == Tina::Game::TileType::Air) continue;
                tilemap.set(x,y, Tina::Game::TileType::Air);
                const auto c = tileColor(t);
                const float len = std::sqrt(dx*dx + dy*dy) + 1e-3f;
                const float nx = dx/len, ny = dy/len;
                const float speed = 20.0f * (1.0f - (len/radius)*0.5f);
                physics.spawnDebris(cx, cy, 0.9f, c[0], c[1], c[2], c[3], nx*speed, ny*speed);
            }
        }
        const int cx0 = x0 / chunkSize; const int cx1 = x1 / chunkSize;
        const int cy0 = y0 / chunkSize; const int cy1 = y1 / chunkSize;
        for (int cyi = cy0; cyi <= cy1; ++cyi) {
            for (int cxi = cx0; cxi <= cx1; ++cxi) rebuildChunk(cxi, cyi);
        }
    };

    // 3.5) 初始化资源系统（异步文件系统 + 资源 Hub/Manager）
    using namespace Tina::Engine;
    auto fs = CreateFileSystem();
    BlobManager blob_rm(*fs);
    ResourceManagerHub hub; hub.add(BlobResource::TYPE, &blob_rm);
    // 示例：异步读取一个配置文件（展示资源 READY）
    Resource* cfg_res = hub.load(BlobResource::TYPE, Tina::Core::Path(Tina::Core::string_view{"resources/config/settings.yaml", sizeof("resources/config/settings.yaml") - 1}));

    // 3.6) 帧计时与固定步（基于 docs/frame_timing.md）
    Tina::Core::TimeConfig time_cfg{};
    time_cfg.tick_rate = 60;
    time_cfg.max_substeps = 4;
    time_cfg.dt_min = 1.0 / 120.0;
    time_cfg.dt_max = 1.0 / 20.0;
    time_cfg.frame_cap_hz = 0; // 0 关闭限帧，依赖 VSync；可改为 60 等数值
    Tina::Core::FrameTimer frame_timer; frame_timer.reset();
    Tina::Core::FixedStepTicker ticker(time_cfg);
    const bool vsync_on = (init.resolution.reset & BGFX_RESET_VSYNC) != 0;
    double fps_log_t = 0.0; // 上次 FPS 打印的时间戳（秒）

    // 4) 事件循环（使用 os::getEvent），固定逻辑步 + 可变渲染，并驱动资源系统 update()
    bool running = true;
    bool fullscreen = false;
    Tina::os::WindowState prev_state{};
    bool relative_mouse = false;
    while (running) {
        frame_timer.beginFrame();
        double dt = frame_timer.deltaSeconds();
        if (dt < time_cfg.dt_min) dt = time_cfg.dt_min;
        if (dt > time_cfg.dt_max) dt = time_cfg.dt_max;
        ticker.accumulate(dt);

        // 驱动资源系统回调
        hub.update();
        if (cfg_res && cfg_res->getState() == Resource::State::READY) {
            TINA_INFO("配置资源加载完成: {} ({} bytes)",
                      cfg_res->getPath().c_str(),
                      (int)static_cast<BlobResource*>(cfg_res)->data.size());
            cfg_res = nullptr; // 仅打印一次
        }
        Event ev;
        while (Tina::os::getEvent(ev)) {
            LogEvent(ev);
            switch (ev.type) {
                case Event::Type::QUIT:
                case Event::Type::WINDOW_CLOSE:
                    running = false; break;
                case Event::Type::WINDOW_SIZE:
                    ResetBgfxWithSize(ev.win_size.w, ev.win_size.h, init.resolution.reset);
                    pipeline.setViewport(ev.win_size.w, ev.win_size.h);
                    camera.setViewportPixels(ev.win_size.w, ev.win_size.h);
                    break;
                case Event::Type::KEY:
                    if (ev.key.down) {
                        if (ev.key.key_code == Tina::os::KeyCode::F11) {
                            if (!fullscreen) { prev_state = Tina::os::setFullScreen(window); fullscreen = true; TINA_INFO("切换全屏"); }
                            else { Tina::os::restoreWindow(window, prev_state); fullscreen = false; TINA_INFO("退出全屏"); }
                        } else if (ev.key.key_code == Tina::os::KeyCode::R) {
                            relative_mouse = !relative_mouse;
                            Tina::os::setRelativeMouseMode(window, relative_mouse);
                            TINA_INFO("相对鼠标模式: {}", relative_mouse);
                        } else if (ev.key.key_code == Tina::os::KeyCode::W) {
                            camera.moveBy(0.0f, +2.0f);
                        } else if (ev.key.key_code == Tina::os::KeyCode::S) {
                            camera.moveBy(0.0f, -2.0f);
                        } else if (ev.key.key_code == Tina::os::KeyCode::A) {
                            camera.moveBy(-2.0f, 0.0f);
                        } else if (ev.key.key_code == Tina::os::KeyCode::D) {
                            camera.moveBy(+2.0f, 0.0f);
                        } else if (ev.key.key_code == Tina::os::KeyCode::E) { // 放大（视区高度变小）
                            camera.setViewHeightWorld(camera.viewH() * 0.9f);
                        } else if (ev.key.key_code == Tina::os::KeyCode::C) { // 缩小（改用 C 键，Q 未在 KeyCode 中定义）
                            camera.setViewHeightWorld(camera.viewH() * 1.1111f);
                        } else if (ev.key.key_code == Tina::os::KeyCode::ESCAPE) {
                            running = false;
                        }
                    }
                    break;
                case Event::Type::DROP_FILE: {
                    int n = Tina::os::getDropFileCount(ev.file_drop.handle);
                    TINA_INFO("拖拽文件数量: {}", n);
                    for (int i = 0; i < n; ++i) {
                        const char* path = Tina::os::getDropFile(ev.file_drop.handle, i);
                        TINA_INFO("  文件[{}]: {}", i, path ? path : "<null>");
                    }
                    Tina::os::finishDrag(ev.file_drop.handle);
                } break;
                case Event::Type::MOUSE_BUTTON:
                    if (ev.mouse_button.down && ev.mouse_button.button == Tina::os::MouseButton::LEFT) {
                        float mx=0.0f, my=0.0f; SDL_MouseButtonFlags btn = SDL_GetMouseState(&mx, &my); (void)btn;
                        const float u = (pxW>0)? mx / (float)pxW : 0.0f;
                        const float v = (pxH>0)? 1.0f - my / (float)pxH : 0.0f; // SDL 原点在左上
                        const float wx = camera.x() + u * camera.viewW();
                        const float wy = camera.y() + v * camera.viewH();
                        explodeAt(wx, wy, 3.5f);
                    }
                    break;
                default: break;
            }
        }

        // 固定逻辑步（物理更新）
        ticker.step([&](double fixed_dt){ physics.step((float)fixed_dt); });

        // 渲染（可使用 alpha 做插值渲染）
        const double alpha = ticker.alpha(); (void)alpha;

        renderer.beginFrame();
        pipeline.begin();
        // 相机：设置视图/投影
        float view_mtx[16], proj_mtx[16];
        camera.buildViewProj(view_mtx, proj_mtx);
        bgfx::setViewTransform(0, view_mtx, proj_mtx);

        // 计算视区（世界坐标）
        const float vx0 = camera.x();
        const float vy0 = camera.y();
        const float vx1 = vx0 + camera.viewW();
        const float vy1 = vy0 + camera.viewH();

        // 提交可见 chunk（颜色方块）
        if (bgfx::isValid(prog_color)) {
            for (const auto& ch : chunks) {
                // AABB 相交测试
                if (ch.maxx <= vx0 || ch.minx >= vx1 || ch.maxy <= vy0 || ch.miny >= vy1) continue;
                if (!ch.valid()) continue;
                bgfx::Encoder* enc = renderer.getEncoder();
                enc->setVertexBuffer(0, ch.vb);
                enc->setIndexBuffer(ch.ib);
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                enc->submit(0, prog_color);
                bgfx::end(enc);
            }
        }

        // 渲染物理碎块（一次性批量）
        {
            const auto& dlist = physics.debris();
            if (!dlist.empty() && bgfx::isValid(prog_color)) {
                const uint32_t quadCount = (uint32_t)dlist.size();
                const uint32_t vcount = quadCount * 4;
                const uint32_t icount = quadCount * 6;
                bgfx::TransientVertexBuffer tvb; bgfx::TransientIndexBuffer tib;
                if (bgfx::allocTransientBuffers(&tvb, colorLayout, vcount, &tib, icount, true)) {
                    struct Vtx { float x,y,z,r,g,b,a; };
                    Vtx* vptr = (Vtx*)tvb.data;
                    uint32_t* iptr = (uint32_t*)tib.data;
                    uint32_t vb = 0, ib = 0;
                    for (uint32_t i = 0; i < quadCount; ++i) {
                        const auto& d = dlist[i];
                        const b2Vec2 p = b2Body_GetPosition(d.body);
                        const b2Rot q = b2Body_GetRotation(d.body);
                        const float c = q.c, s = q.s;
                        const float hs = d.size * 0.5f;
                        const float local[4][2] = {{-hs,-hs},{+hs,-hs},{+hs,+hs},{-hs,+hs}};
                        for (int k=0;k<4;++k) {
                            const float lx = local[k][0], ly = local[k][1];
                            const float wx = p.x + lx*c - ly*s;
                            const float wy = p.y + lx*s + ly*c;
                            vptr[vb + k] = { wx, wy, 0.0f, d.r, d.g, d.b, d.a };
                        }
                        iptr[ib+0]=vb+0; iptr[ib+1]=vb+1; iptr[ib+2]=vb+2;
                        iptr[ib+3]=vb+0; iptr[ib+4]=vb+2; iptr[ib+5]=vb+3;
                        vb += 4; ib += 6;
                    }
                    bgfx::Encoder* enc = renderer.getEncoder();
                    enc->setVertexBuffer(0, &tvb);
                    enc->setIndexBuffer(&tib);
                    enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    enc->submit(0, prog_color);
                    bgfx::end(enc);
                }
            }
        }
        pipeline.submit();
        
        renderer.endFrame();

        // 每秒打印一次帧率/帧时间与目标设定，便于核对
        const double now_sec = frame_timer.sinceStartupSeconds();
        if (now_sec - fps_log_t >= 1.0) {
            TINA_INFO("FPS: {:.1f} | frame: {:.2f} ms | VSync: {} | Cap: {} Hz | Fixed: {} Hz",
                      frame_timer.fps(),
                      frame_timer.frameSeconds() * 1000.0,
                      vsync_on ? "on" : "off",
                      time_cfg.frame_cap_hz,
                      time_cfg.tick_rate);
            fps_log_t = now_sec;
        }

        // 可选限帧（关闭 VSync 时生效更明显）
        if (time_cfg.frame_cap_hz > 0) {
            const double target = 1.0 / double(time_cfg.frame_cap_hz);
            const double used = frame_timer.frameSeconds();
            if (used < target) {
                const double remain = target - used;
                const auto ns = (long long)(remain * 1e9);
                if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
            }
        }
    }

    // 5) 清理：先销毁依赖 bgfx 资源的对象/句柄，再关闭 bgfx
    for (auto& ch : chunks) { if (bgfx::isValid(ch.vb)) bgfx::destroy(ch.vb); if (bgfx::isValid(ch.ib)) bgfx::destroy(ch.ib); }
    shaderManager.cleanup();
    // 额外提交一帧，确保销毁命令被渲染线程消费
    bgfx::frame();
    bgfx::shutdown();
    Tina::os::destroyWindow(window);
    TINA_INFO("退出 Tina");
    return 0;
}
