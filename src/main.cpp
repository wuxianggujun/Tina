// 使用统一 os 接口（SDL3 实现）创建窗口，事件循环打印日志，bgfx 完成最小渲染循环。

#include <cstdint>
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
// 使用 bimg 进行跨平台纹理加载
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/readerwriter.h>
#include <bx/file.h>
#include <bx/error.h>

#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <functional>
#include <cstdlib>
#include "core/Log.hpp"
#include "os/OS.hpp"
#include "engine/Resource.hpp"
#include "core/Path.hpp"
#include "core/Container.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/ShaderManager.hpp"
#include "core/Time.hpp"
#include "game/TileMap.hpp"
#include "game/Camera2D.hpp"
#include <box2d/box2d.h>
#include "physics/Physics2D.hpp"
#include "ui/TextRenderer.hpp"
#include "ui/TextRenderer.hpp"
#include "ui/UICore.hpp"
#include <cstdio>

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
    Tina::Core::Log::InitWithFile("Tina", Tina::Core::Log::Level::Info,
                                  "logs/tina.log", 10ull*1024ull*1024ull, 5, false);
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
    // 注：UI 文本渲染器绑定在 TextRenderer 初始化完成后进行（见下文）

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
    #ifdef SDL_PROP_WINDOW_COCOA_WINDOW_POINTER
        nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    #endif
#elif defined(__linux__)
        const char* videoDriver = SDL_GetCurrentVideoDriver();
        if (videoDriver) {
            if (SDL_strcmp(videoDriver, "x11") == 0) {
                // X11 窗口句柄
    #ifdef SDL_PROP_WINDOW_X11_WINDOW_NUMBER
                nwh = (void*)(uintptr_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    #endif
            } else if (SDL_strcmp(videoDriver, "wayland") == 0) {
                // Wayland 窗口句柄
    #ifdef SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER
                nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    #endif
            }
        }
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
    init.resolution.reset = (uint32_t)(BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X8);
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
    bgfx::ProgramHandle prog_text = shaderManager.loadProgram("text", "text");
    // 新增：用于贴图网格（资源纹理）的独立程序，后续优先使用；若缺失则回退到 text
    bgfx::ProgramHandle prog_sprite = shaderManager.loadProgram("sprite", "sprite");

    // 3.x.1.1) 初始化 UI 渲染器，用于 HUD/提示文本
    Tina::UI::UIRenderer ui;
    bool ui_ok = ui.initialize(shaderManager, nullptr); (void)ui_ok;

    // 3.x.2) 生成一个简易泰拉瑞亚风格的颜色方块地图，并构建网格（世界坐标，每 tile=1.0）
    struct ColorVertex { float x, y, z; float r, g, b, a; };
    bgfx::VertexLayout colorLayout;
    colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .end();

    // 用于纹理化网格（与 text_vs/fs 一致）：Position(3), TexCoord0(2), Color0(U8N)
    struct TexVertex { float x,y,z; float u,v; uint8_t r,g,b,a; };
    bgfx::VertexLayout texLayout;
    texLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
    .end();

    // 加载泥土纹理（使用 bimg 进行跨平台纹理加载）
    bgfx::TextureHandle dirtTex;
    // 贴图网格使用的统一采样器名：s_tex
    bgfx::UniformHandle uSpriteTex;      // 统一名（推荐）

    // 跨平台纹理加载函数（使用 bimg）
    auto loadTexturePNG = [](const char* path)->bgfx::TextureHandle {
        // 读取文件到内存
        FILE* file = fopen(path, "rb");
        if (!file) {
            return BGFX_INVALID_HANDLE;
        }

        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (fileSize <= 0) {
            fclose(file);
            return BGFX_INVALID_HANDLE;
        }

        void* fileData = malloc(fileSize);
        if (!fileData) {
            fclose(file);
            return BGFX_INVALID_HANDLE;
        }

        size_t readSize = fread(fileData, 1, fileSize, file);
        fclose(file);

        if (readSize != (size_t)fileSize) {
            free(fileData);
            return BGFX_INVALID_HANDLE;
        }

        // 使用 bimg 解码图片（使用静态分配器，确保生命周期）
        static bx::DefaultAllocator s_allocator;
        bimg::ImageContainer* imageContainer = bimg::imageParse(
            &s_allocator, fileData, (uint32_t)fileSize, bimg::TextureFormat::RGBA8);
        free(fileData);

        if (!imageContainer) {
            return BGFX_INVALID_HANDLE;
        }

        // 复制图片数据到 bgfx 管理的内存（避免生命周期问题）
        const bgfx::Memory* mem = bgfx::copy(
            imageContainer->m_data,
            imageContainer->m_size
        );

        bgfx::TextureHandle handle = bgfx::createTexture2D(
            (uint16_t)imageContainer->m_width,
            (uint16_t)imageContainer->m_height,
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
            mem
        );

        // 立即释放 bimg 容器，因为数据已经被复制
        bimg::imageFree(imageContainer);

        return handle;
    };

    // 优先尝试相对路径，失败则回退到可执行目录拼接路径
    auto tryLoadTexture = [&](const char* rel)->bgfx::TextureHandle {
        bgfx::TextureHandle h = loadTexturePNG(rel);
        if (!bgfx::isValid(h)) {
            const char* base = SDL_GetBasePath();
            if (base) {
                std::string abs(base);
                if (!abs.empty() && abs.back() != '/' && abs.back() != '\\') abs.push_back('/');
                abs += rel;
                SDL_free((void*)base);
                h = loadTexturePNG(abs.c_str());
            }
        }
        return h;
    };

    dirtTex = tryLoadTexture("resources/textures/dirt_block.png");
    if (!bgfx::isValid(dirtTex)) {
        TINA_WARN("泥土纹理加载失败: {} (将在纹理层跳过渲染)", "resources/textures/dirt_block.png");
    } else {
        TINA_INFO("泥土纹理加载成功: {}", "resources/textures/dirt_block.png");
    }
    uSpriteTex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    Tina::Game::TileMapConfig mapCfg; mapCfg.width = 160; mapCfg.height = 90; // 16:9
    Tina::Game::TileMap tilemap(mapCfg);
    
    // 按键状态跟踪（用于连续WASD移动）
    bool keyPressed[4] = {false, false, false, false}; // W, A, S, D
    enum KeyIndex { KEY_W = 0, KEY_A = 1, KEY_S = 2, KEY_D = 3 };
    tilemap.generate();

    auto tileColor = [](Tina::Game::TileType t)->Tina::Container::Array<float,4>{
        switch (t) {
            case Tina::Game::TileType::Grass:    return { {0.18f, 0.72f, 0.28f, 1.0f} };
            case Tina::Game::TileType::Dirt:     return { {0.55f, 0.38f, 0.22f, 1.0f} };
            case Tina::Game::TileType::Stone:    return { {0.55f, 0.55f, 0.58f, 1.0f} };
            case Tina::Game::TileType::Sand:     return { {0.94f, 0.86f, 0.51f, 1.0f} };
            case Tina::Game::TileType::Snow:     return { {0.95f, 0.95f, 0.98f, 1.0f} };
            case Tina::Game::TileType::Ice:      return { {0.68f, 0.85f, 0.90f, 1.0f} };
            case Tina::Game::TileType::Water:    return { {0.15f, 0.35f, 0.90f, 0.95f} };
            case Tina::Game::TileType::Lava:     return { {0.90f, 0.25f, 0.10f, 1.0f} };
            case Tina::Game::TileType::Coal:     return { {0.20f, 0.20f, 0.20f, 1.0f} };
            case Tina::Game::TileType::Iron:     return { {0.60f, 0.55f, 0.50f, 1.0f} };
            case Tina::Game::TileType::Gold:     return { {0.90f, 0.75f, 0.20f, 1.0f} };
            case Tina::Game::TileType::Diamond:  return { {0.85f, 0.95f, 0.95f, 1.0f} };
            case Tina::Game::TileType::Clay:     return { {0.72f, 0.45f, 0.30f, 1.0f} };
            case Tina::Game::TileType::Bedrock:  return { {0.15f, 0.15f, 0.15f, 1.0f} };
            case Tina::Game::TileType::Obsidian: return { {0.25f, 0.15f, 0.25f, 1.0f} };
            case Tina::Game::TileType::Wood:     return { {0.45f, 0.35f, 0.25f, 1.0f} };
            case Tina::Game::TileType::Leaves:   return { {0.25f, 0.60f, 0.30f, 1.0f} };
            case Tina::Game::TileType::CaveWall: return { {0.40f, 0.40f, 0.45f, 1.0f} };
            
            // 装饰类型
            case Tina::Game::TileType::Flower:         return { {0.95f, 0.45f, 0.75f, 1.0f} }; // 粉红色花朵
            case Tina::Game::TileType::Grass_Decoration: return { {0.35f, 0.75f, 0.40f, 1.0f} }; // 深绿色草丛
            case Tina::Game::TileType::Mushroom:       return { {0.65f, 0.35f, 0.25f, 1.0f} }; // 棕红色蘑菇
            case Tina::Game::TileType::Crystal:        return { {0.75f, 0.85f, 0.95f, 1.0f} }; // 淡蓝色水晶
            case Tina::Game::TileType::Rock:           return { {0.45f, 0.45f, 0.50f, 1.0f} }; // 灰色石块
            default:                             return { {0.0f,  0.0f,  0.0f,  0.0f} };
        }
    };
    // 分块构建网格（chunk 渲染）：仅提交视区内 chunk
    struct TileChunk {
        int cx=0, cy=0; // chunk 坐标
        float minx=0, miny=0, maxx=0, maxy=0; // AABB（世界）
        bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
        uint32_t indexCount = 0;
        b2BodyId staticBody{0}; // 该 chunk 的静态地形刚体（每 tile 一个 box）
        bool valid() const { return bgfx::isValid(vb) && bgfx::isValid(ib) && indexCount>0; }
    };
    const int chunkSize = 32;
    const int cxCount = (mapCfg.width + chunkSize - 1) / chunkSize;
    const int cyCount = (mapCfg.height + chunkSize - 1) / chunkSize;
    Tina::Container::Vector<TileChunk> chunks;
    chunks.reserve((size_t)cxCount * cyCount);
    // 水体叠加层与 chunks 同步的句柄数组
    Tina::Container::Vector<bgfx::VertexBufferHandle> chunksVBWater;
    Tina::Container::Vector<bgfx::IndexBufferHandle>  chunksIBWater;
    Tina::Container::Vector<uint32_t>                 chunksWaterIndexCount;
    chunksVBWater.reserve((size_t)cxCount * cyCount);
    chunksIBWater.reserve((size_t)cxCount * cyCount);
    chunksWaterIndexCount.reserve((size_t)cxCount * cyCount);
    Tina::Container::Vector<bgfx::VertexBufferHandle> chunksVBDirt;
    Tina::Container::Vector<bgfx::IndexBufferHandle>  chunksIBDirt;
    Tina::Container::Vector<uint32_t>                 chunksDirtIndexCount;
    chunksVBDirt.reserve((size_t)cxCount * cyCount);
    chunksIBDirt.reserve((size_t)cxCount * cyCount);
    chunksDirtIndexCount.reserve((size_t)cxCount * cyCount);

    // 预声明：重建指定 chunk（用于后续调用捕获，避免“初始化前使用”的编译器诊断）
    std::function<void(int,int,bool)> rebuildChunk;


    // 脏标记：仅对脏块做增量重建
    Tina::Container::Vector<uint8_t> chunkDirtySolid;
    Tina::Container::Vector<uint8_t> chunkDirtyWater;
    chunkDirtySolid.assign((size_t)cxCount * cyCount, 0);
    chunkDirtyWater.assign((size_t)cxCount * cyCount, 0);
    for (int cy = 0; cy < cyCount; ++cy) {
        for (int cx = 0; cx < cxCount; ++cx) {
            const int x0i = cx * chunkSize;
            const int y0i = cy * chunkSize;
            const int x1i = (x0i + chunkSize > mapCfg.width ? mapCfg.width : x0i + chunkSize);
            const int y1i = (y0i + chunkSize > mapCfg.height ? mapCfg.height : y0i + chunkSize);
            Tina::Container::Vector<ColorVertex> v;
            Tina::Container::Vector<uint32_t>    idx;
            Tina::Container::Vector<ColorVertex> vwater;
            Tina::Container::Vector<uint32_t>    idxwater;
            v.reserve((size_t)(x1i-x0i)*(y1i-y0i));
            idx.reserve(v.capacity()*6);
            vwater.reserve((size_t)(x1i-x0i)*(y1i-y0i));
            idxwater.reserve(vwater.capacity()*6);
            for (int y = y0i; y < y1i; ++y) {
                for (int x = x0i; x < x1i; ++x) {
                    auto t = tilemap.get(x,y);
                    if (t == Tina::Game::TileType::Air || t == Tina::Game::TileType::Water) {
                        // 水体叠加层：m_water>0 或 tile 为 Water
                        const int wv  = (int)tilemap.water(x, y);
                        const int wvL = (x>0              ) ? (int)tilemap.water(x-1, y) : wv;
                        const int wvR = (x<mapCfg.width-1 ) ? (int)tilemap.water(x+1, y) : wv;
                        const bool isWaterTile = (t == Tina::Game::TileType::Water);
                        float hC = isWaterTile ? 1.0f : (wv / 255.0f);
                        if (hC > 0.001f || isWaterTile) {
                            Tina::Game::TileType lt = (x>0) ? tilemap.get(x-1,y) : Tina::Game::TileType::Stone;
                            Tina::Game::TileType rt = (x<mapCfg.width-1) ? tilemap.get(x+1,y) : Tina::Game::TileType::Stone;
                            float hL = isWaterTile ? 1.0f : ((lt == Tina::Game::TileType::Air) ? (wvL / 255.0f) : hC);
                            float hR = isWaterTile ? 1.0f : ((rt == Tina::Game::TileType::Air) ? (wvR / 255.0f) : hC);
                            Tina::Game::TileType up = (y<mapCfg.height-1) ? tilemap.get(x,y+1) : Tina::Game::TileType::Stone;
                            if (up != Tina::Game::TileType::Air) { hC = hL = hR = 1.0f; }
                            const float topL = std::min(1.0f, std::max(0.0f, 0.5f*(hC + hL)));
                            const float topR = std::min(1.0f, std::max(0.0f, 0.5f*(hC + hR)));
                            const float x0 = (float)x, y0 = (float)y, x1 = x0 + 1.0f;
                            const float yL = y0 + topL, yR = y0 + topR;
                            const auto cw = tileColor(Tina::Game::TileType::Water);
                            const float a = std::min(1.0f, std::max(0.25f, cw[3] * (0.6f + 0.4f * hC)));
                            const uint32_t baseW = (uint32_t)vwater.size();
                            vwater.push_back({ x0, y0, 0.0f, cw[0], cw[1], cw[2], a });
                            vwater.push_back({ x1, y0, 0.0f, cw[0], cw[1], cw[2], a });
                            vwater.push_back({ x1, yR, 0.0f, cw[0], cw[1], cw[2], a });
                            vwater.push_back({ x0, yL, 0.0f, cw[0], cw[1], cw[2], a });
                            idxwater.push_back(baseW+0); idxwater.push_back(baseW+1); idxwater.push_back(baseW+2);
                            idxwater.push_back(baseW+0); idxwater.push_back(baseW+2); idxwater.push_back(baseW+3);
                        }
                        
                        // 岩浆叠加层：m_lava>0
                        const int lv  = (int)tilemap.lava(x, y);
                        const int lvL = (x>0              ) ? (int)tilemap.lava(x-1, y) : lv;
                        const int lvR = (x<mapCfg.width-1 ) ? (int)tilemap.lava(x+1, y) : lv;
                        float lavaH = lv / 255.0f;
                        if (lavaH > 0.001f) {
                            Tina::Game::TileType lt = (x>0) ? tilemap.get(x-1,y) : Tina::Game::TileType::Stone;
                            Tina::Game::TileType rt = (x<mapCfg.width-1) ? tilemap.get(x+1,y) : Tina::Game::TileType::Stone;
                            float lavaHL = (lt == Tina::Game::TileType::Air) ? (lvL / 255.0f) : lavaH;
                            float lavaHR = (rt == Tina::Game::TileType::Air) ? (lvR / 255.0f) : lavaH;
                            Tina::Game::TileType up = (y<mapCfg.height-1) ? tilemap.get(x,y+1) : Tina::Game::TileType::Stone;
                            if (up != Tina::Game::TileType::Air) { lavaH = lavaHL = lavaHR = 1.0f; }
                            const float lavaTopL = std::min(1.0f, std::max(0.0f, 0.5f*(lavaH + lavaHL)));
                            const float lavaTopR = std::min(1.0f, std::max(0.0f, 0.5f*(lavaH + lavaHR)));
                            const float x0 = (float)x, y0 = (float)y, x1 = x0 + 1.0f;
                            const float lavaYL = y0 + lavaTopL, lavaYR = y0 + lavaTopR;
                            const auto cl = tileColor(Tina::Game::TileType::Lava);
                            const float lavaA = std::min(1.0f, std::max(0.8f, cl[3] * (0.8f + 0.2f * lavaH))); // 岩浆更不透明
                            const uint32_t baseLava = (uint32_t)vwater.size();
                            vwater.push_back({ x0, y0, 0.0f, cl[0], cl[1], cl[2], lavaA });
                            vwater.push_back({ x1, y0, 0.0f, cl[0], cl[1], cl[2], lavaA });
                            vwater.push_back({ x1, lavaYR, 0.0f, cl[0], cl[1], cl[2], lavaA });
                            vwater.push_back({ x0, lavaYL, 0.0f, cl[0], cl[1], cl[2], lavaA });
                            idxwater.push_back(baseLava+0); idxwater.push_back(baseLava+1); idxwater.push_back(baseLava+2);
                            idxwater.push_back(baseLava+0); idxwater.push_back(baseLava+2); idxwater.push_back(baseLava+3);
                        }
                    } else { if (t == Tina::Game::TileType::Dirt) continue;
                        // 实心方块
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
            }
            TileChunk ch{}; ch.cx=cx; ch.cy=cy; ch.minx=(float)x0i; ch.miny=(float)y0i; ch.maxx=(float)x1i; ch.maxy=(float)y1i;
            if (!v.empty()) {
                const bgfx::Memory* memv = bgfx::copy(v.data(), (uint32_t)(v.size()*sizeof(ColorVertex)));
                ch.vb = bgfx::createVertexBuffer(memv, colorLayout);
                const bgfx::Memory* memi = bgfx::copy(idx.data(), (uint32_t)(idx.size()*sizeof(uint32_t)));
                ch.ib = bgfx::createIndexBuffer(memi, BGFX_BUFFER_INDEX32);
                ch.indexCount = (uint32_t)idx.size();
            }
            // 创建并保存水体叠加层缓冲句柄（与 chunks 对齐）
            if (!vwater.empty()) {
                const bgfx::Memory* memvw = bgfx::copy(vwater.data(), (uint32_t)(vwater.size()*sizeof(ColorVertex)));
                bgfx::VertexBufferHandle vbw = bgfx::createVertexBuffer(memvw, colorLayout);
                const bgfx::Memory* memiw = bgfx::copy(idxwater.data(), (uint32_t)(idxwater.size()*sizeof(uint32_t)));
                bgfx::IndexBufferHandle  ibw = bgfx::createIndexBuffer(memiw, BGFX_BUFFER_INDEX32);
                chunksVBWater.push_back(vbw);
                chunksIBWater.push_back(ibw);
                chunksWaterIndexCount.push_back((uint32_t)idxwater.size());
            } else {
                chunksVBWater.push_back(BGFX_INVALID_HANDLE);
                chunksIBWater.push_back(BGFX_INVALID_HANDLE);
                chunksWaterIndexCount.push_back(0);
            }
            // 构建泥土（纹理）网格（不依赖是否存在水层）
            Tina::Container::Vector<TexVertex> vdirt; Tina::Container::Vector<uint32_t> idxdirt;
            for (int y = y0i; y < y1i; ++y) {
                for (int x = x0i; x < x1i; ++x) {
                    if (tilemap.get(x,y) != Tina::Game::TileType::Dirt) continue;
                    const uint32_t baseT = (uint32_t)vdirt.size();
                    vdirt.push_back({ (float)x, (float)y, 0.0f, 0.0f, 0.0f, 255,255,255,255 });
                    vdirt.push_back({ (float)x+1.0f, (float)y, 0.0f, 1.0f, 0.0f, 255,255,255,255 });
                    vdirt.push_back({ (float)x+1.0f, (float)y+1.0f, 0.0f, 1.0f, 1.0f, 255,255,255,255 });
                    vdirt.push_back({ (float)x, (float)y+1.0f, 0.0f, 0.0f, 1.0f, 255,255,255,255 });
                    idxdirt.push_back(baseT+0); idxdirt.push_back(baseT+1); idxdirt.push_back(baseT+2);
                    idxdirt.push_back(baseT+0); idxdirt.push_back(baseT+2); idxdirt.push_back(baseT+3);
                }
            }
            if (!vdirt.empty()) {
                const bgfx::Memory* memtd = bgfx::copy(vdirt.data(), (uint32_t)(vdirt.size()*sizeof(TexVertex)));
                bgfx::VertexBufferHandle vbd = bgfx::createVertexBuffer(memtd, texLayout);
                const bgfx::Memory* memid = bgfx::copy(idxdirt.data(), (uint32_t)(idxdirt.size()*sizeof(uint32_t)));
                bgfx::IndexBufferHandle  ibd = bgfx::createIndexBuffer(memid, BGFX_BUFFER_INDEX32);
                chunksVBDirt.push_back(vbd);
                chunksIBDirt.push_back(ibd);
                chunksDirtIndexCount.push_back((uint32_t)idxdirt.size());
            } else {
                chunksVBDirt.push_back(BGFX_INVALID_HANDLE);
                chunksIBDirt.push_back(BGFX_INVALID_HANDLE);
                chunksDirtIndexCount.push_back(0);
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
    physics.setDebrisLimit(800);
    physics.createBounds(0.0f, 0.0f, (float)mapCfg.width, (float)mapCfg.height + 10.0f, 1.0f);

    // 根据当前瓦片为每个 chunk 生成简易静态碰撞体（每 tile 一个 box）
    auto buildChunkPhysics = [&](int cx, int cy){
        if (cx < 0 || cy < 0) return;
        const int idx = cy * cxCount + cx; if (idx < 0 || idx >= (int)chunks.size()) return;
        auto& ch = chunks[(size_t)idx];
        if (b2Body_IsValid(ch.staticBody)) b2DestroyBody(ch.staticBody);
        // 统计是否有实体 tile
        const int x0i = cx * chunkSize;
        const int y0i = cy * chunkSize;
        const int x1i = (x0i + chunkSize > mapCfg.width ? mapCfg.width : x0i + chunkSize);
        const int y1i = (y0i + chunkSize > mapCfg.height ? mapCfg.height : y0i + chunkSize);
        bool any = false;
        for (int y = y0i; y < y1i && !any; ++y)
            for (int x = x0i; x < x1i; ++x) {
                auto tt = tilemap.get(x,y);
                if (tt != Tina::Game::TileType::Air && tt != Tina::Game::TileType::Water) { any = true; break; }
            }
        if (!any) { ch.staticBody = {0}; return; }
        b2BodyDef bd = b2DefaultBodyDef(); bd.type = b2_staticBody;
        ch.staticBody = b2CreateBody(physics.world(), &bd);
        b2ShapeDef sd = b2DefaultShapeDef(); sd.density = 0.0f; sd.material.friction = 0.9f; sd.material.restitution = 0.0f;
        for (int y = y0i; y < y1i; ++y) {
            for (int x = x0i; x < x1i; ++x) {
                auto tt2 = tilemap.get(x,y);
                if (tt2 == Tina::Game::TileType::Air || tt2 == Tina::Game::TileType::Water) continue;
                b2Polygon poly = b2MakeOffsetBox(0.5f, 0.5f, b2Vec2{(float)x+0.5f,(float)y+0.5f}, b2Rot_identity);
                (void)b2CreatePolygonShape(ch.staticBody, &sd, &poly);
            }
        }
    };
    // 初次为所有 chunk 构建静态体
    for (int cy = 0; cy < cyCount; ++cy)
        for (int cx = 0; cx < cxCount; ++cx) buildChunkPhysics(cx, cy);

    // 重建指定 chunk（当瓦片变化时调用）
    rebuildChunk = [&](int cx, int cy, bool waterOnly = false){
        if (cx < 0 || cy < 0) return;
        const int idx = cy * cxCount + cx;
        if (idx < 0 || idx >= (int)chunks.size()) return;
        auto& ch = chunks[(size_t)idx];
        if (!waterOnly) {
            if (bgfx::isValid(ch.vb)) { bgfx::destroy(ch.vb); ch.vb = BGFX_INVALID_HANDLE; }
            if (bgfx::isValid(ch.ib)) { bgfx::destroy(ch.ib); ch.ib = BGFX_INVALID_HANDLE; }
            if (idx >= 0 && idx < (int)chunksVBDirt.size()) {
                if (bgfx::isValid(chunksVBDirt[(size_t)idx])) { bgfx::destroy(chunksVBDirt[(size_t)idx]); chunksVBDirt[(size_t)idx] = BGFX_INVALID_HANDLE; }
                if (bgfx::isValid(chunksIBDirt[(size_t)idx])) { bgfx::destroy(chunksIBDirt[(size_t)idx]); chunksIBDirt[(size_t)idx] = BGFX_INVALID_HANDLE; }
                if (idx < (int)chunksDirtIndexCount.size()) chunksDirtIndexCount[(size_t)idx] = 0;
            }
        }
        if (idx >= 0 && idx < (int)chunksVBWater.size()) {
            if (bgfx::isValid(chunksVBWater[(size_t)idx])) { bgfx::destroy(chunksVBWater[(size_t)idx]); chunksVBWater[(size_t)idx] = BGFX_INVALID_HANDLE; }
            if (bgfx::isValid(chunksIBWater[(size_t)idx])) { bgfx::destroy(chunksIBWater[(size_t)idx]); chunksIBWater[(size_t)idx] = BGFX_INVALID_HANDLE; }
            if (idx < (int)chunksWaterIndexCount.size()) chunksWaterIndexCount[(size_t)idx] = 0;
        }
        if (!waterOnly) { ch.indexCount = 0; }

        const int x0i = cx * chunkSize;
        const int y0i = cy * chunkSize;
        const int x1i = (x0i + chunkSize > mapCfg.width ? mapCfg.width : x0i + chunkSize);
        const int y1i = (y0i + chunkSize > mapCfg.height ? mapCfg.height : y0i + chunkSize);
        Tina::Container::Vector<ColorVertex> v;
        Tina::Container::Vector<uint32_t>    idxv;
        Tina::Container::Vector<ColorVertex> vwater;
        Tina::Container::Vector<uint32_t>    idxwater;
        v.reserve((size_t)(x1i-x0i)*(y1i-y0i));
        idxv.reserve(v.capacity()*6);
        vwater.reserve((size_t)(x1i-x0i)*(y1i-y0i));
        // Greedy 合批实心层（仅在完全重建时执行，waterOnly=true 时跳过）
        if (!waterOnly) {
            const int W = x1i - x0i; const int H = y1i - y0i;
            Tina::Container::Vector<int> solidKey; solidKey.assign((size_t)W * H, 0);
            for (int yy = 0; yy < H; ++yy)
                for (int xx = 0; xx < W; ++xx) {
                    auto tt = tilemap.get(x0i + xx, y0i + yy);
                    if (tt != Tina::Game::TileType::Air && tt != Tina::Game::TileType::Water)
                        solidKey[(size_t)yy * W + xx] = (int)tt;
                }
            Tina::Container::Vector<uint8_t> used; used.assign((size_t)W * H, 0);
            for (int yy = 0; yy < H; ++yy) {
                for (int xx = 0; xx < W; ++xx) {
                    const int k = solidKey[(size_t)yy * W + xx];
                    if (k == 0 || used[(size_t)yy * W + xx]) continue;
                    int w = 0; while (xx + w < W && solidKey[(size_t)yy * W + (xx + w)] == k && !used[(size_t)yy * W + (xx + w)]) ++w;
                    int h = 1; for (;;) {
                        if (yy + h >= H) break;
                        bool ok = true;
                        for (int xi=0; xi<w; ++xi) { size_t idxc = (size_t)(yy+h)*W + (xx+xi); if (solidKey[idxc] != k || used[idxc]) { ok=false; break; } }
                        if (!ok) break; ++h;
                    }
                    const float X0 = (float)(x0i + xx), Y0 = (float)(y0i + yy);
                    const float X1 = (float)(x0i + xx + w), Y1 = (float)(y0i + yy + h);
                    const auto C = tileColor((Tina::Game::TileType)k);
                    const uint32_t baseG = (uint32_t)v.size();
                    v.push_back({ X0, Y0, 0.0f, C[0], C[1], C[2], C[3] });
                    v.push_back({ X1, Y0, 0.0f, C[0], C[1], C[2], C[3] });
                    v.push_back({ X1, Y1, 0.0f, C[0], C[1], C[2], C[3] });
                    v.push_back({ X0, Y1, 0.0f, C[0], C[1], C[2], C[3] });
                    idxv.push_back(baseG+0); idxv.push_back(baseG+1); idxv.push_back(baseG+2);
                    idxv.push_back(baseG+0); idxv.push_back(baseG+2); idxv.push_back(baseG+3);
                    for (int yy2=0; yy2<h; ++yy2) for (int xx2=0; xx2<w; ++xx2) used[(size_t)(yy+yy2)*W + (xx+xx2)] = 1;
                }
            }
        }
        idxwater.reserve(vwater.capacity()*6);
        for (int y = y0i; y < y1i; ++y) {
            for (int x = x0i; x < x1i; ++x) {
                auto t = tilemap.get(x,y);
                if (t == Tina::Game::TileType::Air || t == Tina::Game::TileType::Water) {
                    // 水体叠加层
                    const int wv  = (int)tilemap.water(x, y);
                    if (wv == 0 && t != Tina::Game::TileType::Water) continue; // 没有水且不是水瓦片，跳过

                    const int wvL = (x>0              ) ? (int)tilemap.water(x-1, y) : wv;
                    const int wvR = (x<mapCfg.width-1 ) ? (int)tilemap.water(x+1, y) : wv;
                    const int wvU = (y<mapCfg.height-1) ? (int)tilemap.water(x, y+1) : 0;

                    const bool isWaterTile = (t == Tina::Game::TileType::Water);
                    float hC = isWaterTile ? 1.0f : (wv / 255.0f);

                    if (hC > 0.001f || isWaterTile) {
                        // 检查周围瓦片类型
                        Tina::Game::TileType lt = (x>0) ? tilemap.get(x-1,y) : Tina::Game::TileType::Stone;
                        Tina::Game::TileType rt = (x<mapCfg.width-1) ? tilemap.get(x+1,y) : Tina::Game::TileType::Stone;
                        Tina::Game::TileType up = (y<mapCfg.height-1) ? tilemap.get(x,y+1) : Tina::Game::TileType::Stone;

                        // 改进的水面高度计算：
                        // 1. 如果上方有水，当前格应该填满（避免空隙）
                        // 2. 如果左右有水且是空气，使用其水位进行插值
                        // 3. 如果左右是固体，使用当前格水位
                        float hL = hC;
                        float hR = hC;

                        if (up != Tina::Game::TileType::Air) {
                            // 上方是固体或水瓦片，当前格填满
                            hC = hL = hR = 1.0f;
                        } else if (wvU > 0) {
                            // 上方有水压，当前格应该接近填满（避免柱状水流中间有空隙）
                            hC = std::max(hC, 0.95f);
                            hL = hR = hC;
                        } else {
                            // 自由表面：考虑左右水位进行平滑插值
                            if (lt == Tina::Game::TileType::Air) {
                                hL = (wvL > 0) ? (wvL / 255.0f) : 0.0f;
                            }
                            if (rt == Tina::Game::TileType::Air) {
                                hR = (wvR > 0) ? (wvR / 255.0f) : 0.0f;
                            }
                        }

                        const float topL = std::min(1.0f, std::max(0.0f, 0.5f*(hC + hL)));
                        const float topR = std::min(1.0f, std::max(0.0f, 0.5f*(hC + hR)));
                        const float x0 = (float)x, y0 = (float)y, x1 = x0+1.0f;
                        const float yL = y0 + topL, yR = y0 + topR;
                        const auto cw = tileColor(Tina::Game::TileType::Water);
                        const float a = std::min(1.0f, std::max(0.25f, cw[3] * (0.6f + 0.4f * hC)));
                        const uint32_t baseW = (uint32_t)vwater.size();
                        vwater.push_back({ x0, y0, 0.0f, cw[0], cw[1], cw[2], a });
                        vwater.push_back({ x1, y0, 0.0f, cw[0], cw[1], cw[2], a });
                        vwater.push_back({ x1, yR, 0.0f, cw[0], cw[1], cw[2], a });
                        vwater.push_back({ x0, yL, 0.0f, cw[0], cw[1], cw[2], a });
                        idxwater.push_back(baseW+0); idxwater.push_back(baseW+1); idxwater.push_back(baseW+2);
                        idxwater.push_back(baseW+0); idxwater.push_back(baseW+2); idxwater.push_back(baseW+3);
                    }
                    
                    // 岩浆叠加层：m_lava>0
                    const int lv  = (int)tilemap.lava(x, y);
                    const int lvL = (x>0              ) ? (int)tilemap.lava(x-1, y) : lv;
                    const int lvR = (x<mapCfg.width-1 ) ? (int)tilemap.lava(x+1, y) : lv;
                    float lavaH = lv / 255.0f;
                    if (lavaH > 0.001f) {
                        Tina::Game::TileType lt = (x>0) ? tilemap.get(x-1,y) : Tina::Game::TileType::Stone;
                        Tina::Game::TileType rt = (x<mapCfg.width-1) ? tilemap.get(x+1,y) : Tina::Game::TileType::Stone;
                        float lavaHL = (lt == Tina::Game::TileType::Air) ? (lvL / 255.0f) : lavaH;
                        float lavaHR = (rt == Tina::Game::TileType::Air) ? (lvR / 255.0f) : lavaH;
                        Tina::Game::TileType up = (y<mapCfg.height-1) ? tilemap.get(x,y+1) : Tina::Game::TileType::Stone;
                        if (up != Tina::Game::TileType::Air) { lavaH = lavaHL = lavaHR = 1.0f; }
                        const float lavaTopL = std::min(1.0f, std::max(0.0f, 0.5f*(lavaH + lavaHL)));
                        const float lavaTopR = std::min(1.0f, std::max(0.0f, 0.5f*(lavaH + lavaHR)));
                        const float x0 = (float)x, y0 = (float)y, x1 = x0 + 1.0f;
                        const float lavaYL = y0 + lavaTopL, lavaYR = y0 + lavaTopR;
                        const auto cl = tileColor(Tina::Game::TileType::Lava);
                        const float lavaA = std::min(1.0f, std::max(0.8f, cl[3] * (0.8f + 0.2f * lavaH)));
                        const uint32_t baseLava = (uint32_t)vwater.size();
                        vwater.push_back({ x0, y0, 0.0f, cl[0], cl[1], cl[2], lavaA });
                        vwater.push_back({ x1, y0, 0.0f, cl[0], cl[1], cl[2], lavaA });
                        vwater.push_back({ x1, lavaYR, 0.0f, cl[0], cl[1], cl[2], lavaA });
                        vwater.push_back({ x0, lavaYL, 0.0f, cl[0], cl[1], cl[2], lavaA });
                        idxwater.push_back(baseLava+0); idxwater.push_back(baseLava+1); idxwater.push_back(baseLava+2);
                        idxwater.push_back(baseLava+0); idxwater.push_back(baseLava+2); idxwater.push_back(baseLava+3);
                    }
                }
                // 注意：固体瓦片已由 Greedy 合批算法（line 509-544）统一处理
                // 此处不再单独生成固体顶点，避免重复
            }
        }
        // 更新固体层缓冲（仅在完全重建时）
        if (!waterOnly) {
            if (!v.empty()) {
                const bgfx::Memory* memv = bgfx::copy(v.data(), (uint32_t)(v.size()*sizeof(ColorVertex)));
                ch.vb = bgfx::createVertexBuffer(memv, colorLayout);
                const bgfx::Memory* memi = bgfx::copy(idxv.data(), (uint32_t)(idxv.size()*sizeof(uint32_t)));
                ch.ib = bgfx::createIndexBuffer(memi, BGFX_BUFFER_INDEX32);
                ch.indexCount = (uint32_t)idxv.size();
            }
        }
        // 更新水层缓冲（总是重建）
        if (!vwater.empty()) {
            // 确保索引可写
            while ((int)chunksVBWater.size() <= idx) chunksVBWater.push_back(BGFX_INVALID_HANDLE);
            while ((int)chunksIBWater.size() <= idx) chunksIBWater.push_back(BGFX_INVALID_HANDLE);
            while ((int)chunksWaterIndexCount.size() <= idx) chunksWaterIndexCount.push_back(0);
            const bgfx::Memory* memvw = bgfx::copy(vwater.data(), (uint32_t)(vwater.size()*sizeof(ColorVertex)));
            chunksVBWater[(size_t)idx] = bgfx::createVertexBuffer(memvw, colorLayout);
            const bgfx::Memory* memiw = bgfx::copy(idxwater.data(), (uint32_t)(idxwater.size()*sizeof(uint32_t)));
            chunksIBWater[(size_t)idx] = bgfx::createIndexBuffer(memiw, BGFX_BUFFER_INDEX32);
            chunksWaterIndexCount[(size_t)idx] = (uint32_t)idxwater.size();
        } else {
            if (idx >= 0 && idx < (int)chunksVBWater.size() && bgfx::isValid(chunksVBWater[(size_t)idx])) { bgfx::destroy(chunksVBWater[(size_t)idx]); chunksVBWater[(size_t)idx] = BGFX_INVALID_HANDLE; }
            if (idx >= 0 && idx < (int)chunksIBWater.size() && bgfx::isValid(chunksIBWater[(size_t)idx])) { bgfx::destroy(chunksIBWater[(size_t)idx]); chunksIBWater[(size_t)idx] = BGFX_INVALID_HANDLE; }
            if (idx >= 0 && idx < (int)chunksWaterIndexCount.size()) chunksWaterIndexCount[(size_t)idx] = 0;
        }

        // 构建泥土（纹理）网格（仅在完全重建时）
        if (!waterOnly) {
            Tina::Container::Vector<TexVertex> vdirt; Tina::Container::Vector<uint32_t> idxdirt;
            for (int y = y0i; y < y1i; ++y) {
                for (int x = x0i; x < x1i; ++x) {
                    if (tilemap.get(x,y) != Tina::Game::TileType::Dirt) continue;
                    const uint32_t baseT = (uint32_t)vdirt.size();
                    vdirt.push_back({ (float)x, (float)y, 0.0f, 0.0f, 0.0f, 255,255,255,255 });
                    vdirt.push_back({ (float)x+1.0f, (float)y, 0.0f, 1.0f, 0.0f, 255,255,255,255 });
                    vdirt.push_back({ (float)x+1.0f, (float)y+1.0f, 0.0f, 1.0f, 1.0f, 255,255,255,255 });
                    vdirt.push_back({ (float)x, (float)y+1.0f, 0.0f, 0.0f, 1.0f, 255,255,255,255 });
                    idxdirt.push_back(baseT+0); idxdirt.push_back(baseT+1); idxdirt.push_back(baseT+2);
                    idxdirt.push_back(baseT+0); idxdirt.push_back(baseT+2); idxdirt.push_back(baseT+3);
                }
            }
            if (!vdirt.empty()) {
                // 确保索引可写
                while ((int)chunksVBDirt.size() <= idx) chunksVBDirt.push_back(BGFX_INVALID_HANDLE);
                while ((int)chunksIBDirt.size() <= idx) chunksIBDirt.push_back(BGFX_INVALID_HANDLE);
                while ((int)chunksDirtIndexCount.size() <= idx) chunksDirtIndexCount.push_back(0);
                const bgfx::Memory* memtd = bgfx::copy(vdirt.data(), (uint32_t)(vdirt.size()*sizeof(TexVertex)));
                if (idx >= 0 && idx < (int)chunksVBDirt.size() && bgfx::isValid(chunksVBDirt[(size_t)idx])) bgfx::destroy(chunksVBDirt[(size_t)idx]);
                chunksVBDirt[(size_t)idx] = bgfx::createVertexBuffer(memtd, texLayout);
                const bgfx::Memory* memid = bgfx::copy(idxdirt.data(), (uint32_t)(idxdirt.size()*sizeof(uint32_t)));
                if (idx >= 0 && idx < (int)chunksIBDirt.size() && bgfx::isValid(chunksIBDirt[(size_t)idx])) bgfx::destroy(chunksIBDirt[(size_t)idx]);
                chunksIBDirt[(size_t)idx] = bgfx::createIndexBuffer(memid, BGFX_BUFFER_INDEX32);
                chunksDirtIndexCount[(size_t)idx] = (uint32_t)idxdirt.size();
            } else {
                if (idx >= 0 && idx < (int)chunksVBDirt.size() && bgfx::isValid(chunksVBDirt[(size_t)idx])) { bgfx::destroy(chunksVBDirt[(size_t)idx]); chunksVBDirt[(size_t)idx]=BGFX_INVALID_HANDLE; }
                if (idx >= 0 && idx < (int)chunksIBDirt.size() && bgfx::isValid(chunksIBDirt[(size_t)idx])) { bgfx::destroy(chunksIBDirt[(size_t)idx]); chunksIBDirt[(size_t)idx]=BGFX_INVALID_HANDLE; }
                if (idx >= 0 && idx < (int)chunksDirtIndexCount.size()) chunksDirtIndexCount[(size_t)idx]=0;
            }
        }
        // 同步重建物理静态体
        if (!waterOnly) buildChunkPhysics(cx, cy);
    };

    // 爆炸：移除半径内的 tile，采样生成有限数量的物理碎块，并重建受影响的 chunk
    auto explodeAt = [&](float wx, float wy, float radius){
        struct SpawnCand { float cx, cy; float r,g,b,a; float weight; };
        Tina::Container::Vector<SpawnCand> cands;
        const float r2 = radius*radius;
        const int x0 = (int)std::max(0, (int)std::floor(wx - radius));
        const int y0 = (int)std::max(0, (int)std::floor(wy - radius));
        const int x1 = (int)std::min(mapCfg.width-1, (int)std::ceil(wx + radius));
        const int y1 = (int)std::min(mapCfg.height-1, (int)std::ceil(wy + radius));
        cands.reserve((size_t)(x1-x0+1)*(y1-y0+1));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float cx = x + 0.5f, cy = y + 0.5f;
                const float dx = cx - wx, dy = cy - wy;
                const float d2 = dx*dx + dy*dy;
                if (d2 > r2) continue;
                auto t = tilemap.get(x,y);
                if (t == Tina::Game::TileType::Air) continue;
                // 清除 tile（所有命中均清除）- 使用安全方法确保水位数据一致性
                tilemap.setSafe(x,y, Tina::Game::TileType::Air);
                if (t == Tina::Game::TileType::Water) {
                    auto randf = [](){ return (float)std::rand() / (float)RAND_MAX; };
                    const int waterParticles = 8; // 每个水瓦片 8 个粒子
                    for (int p = 0; p < waterParticles; ++p) {
                        float px = cx + (randf() - 0.5f) * 0.8f;
                        float py = cy + (randf() - 0.5f) * 0.8f;
                        float dxp = px - wx;
                        float dyp = py - wy;
                        float vx = dxp * 15.0f + (randf() - 0.5f) * 10.0f;
                        float vy = dyp * 15.0f + (randf() - 0.5f) * 10.0f;
                        physics.spawnWaterParticle(px, py, 0.30f, 0.15f, 0.35f, 0.90f, 0.70f, vx, vy, 4.0f);
                    }
                    continue; // 不加入固体碎片候选
                }
                const auto c = tileColor(t);
                const float len = std::sqrt(std::max(d2, 1e-6f));
                const float speed = 20.0f * (1.0f - (len/radius)*0.5f);
                cands.push_back({cx,cy, c[0],c[1],c[2],c[3], speed});
            }
        }
        // 采样生成碎块，限制单次爆炸的碎块数量
        const int maxSpawn = 300;
        const int total = (int)cands.size();
        if (total > 0) {
            const int spawn = std::min(maxSpawn, total);
            const float step = (float)total / (float)spawn;
            float acc = 0.0f;
            for (int i = 0; i < spawn; ++i) {
                int idx = (int)acc; if (idx >= total) idx = total - 1;
                const auto& s = cands[(size_t)idx];
                const float dx = s.cx - wx, dy = s.cy - wy;
                const float len = std::sqrt(dx*dx + dy*dy) + 1e-6f;
                const float nx = dx/len, ny = dy/len;
                physics.spawnDebris(s.cx, s.cy, 0.9f, s.r, s.g, s.b, s.a, nx*s.weight, ny*s.weight);
                acc += step;
            }
        }
        const int cx0 = x0 / chunkSize; const int cx1 = x1 / chunkSize;
        const int cy0 = y0 / chunkSize; const int cy1 = y1 / chunkSize;
        for (int cyi = cy0; cyi <= cy1; ++cyi) {
            for (int cxi = cx0; cxi <= cx1; ++cxi) { int cidx = cyi * cxCount + cxi; if (cidx >= 0 && cidx < (int)chunkDirtySolid.size()) { chunkDirtySolid[(size_t)cidx] = 1; chunkDirtyWater[(size_t)cidx] = 1; } }
        }
    };

    // 3.5) 初始化资源系统（异步文件系统 + 资源 Hub/Manager）
    using namespace Tina::Engine;
    auto fs = CreateFileSystem();
    BlobManager blob_rm(*fs);
    ResourceManagerHub hub; hub.add(BlobResource::TYPE, &blob_rm);
    // 示例：异步读取一个配置文件（展示资源 READY）
    Resource* cfg_res = hub.load(BlobResource::TYPE, Tina::Core::Path(Tina::Core::string_view{"resources/config/settings.yaml", sizeof("resources/config/settings.yaml") - 1}));

    // 3.7) 初始化文本渲染器（独立于 RmlUI，可用于直接绘制中文）
    Tina::UI::TextRenderer textRenderer;
    ui.setTextRenderer(&textRenderer);
    if (!textRenderer.initialize()) {
        TINA_ERROR("TextRenderer 初始化失败");
                } else {
        // 使用项目内置思源黑体
        if (!textRenderer.loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 28)) {
            TINA_WARN("加载默认字体失败，文本可能无法显示");
        }
    }

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
            // LogEvent(ev);  // 关闭事件日志打印
            switch (ev.type) {
                case Event::Type::QUIT:
                case Event::Type::WINDOW_CLOSE:
                    running = false; break;
                case Event::Type::WINDOW_SIZE:
                    ResetBgfxWithSize(ev.win_size.w, ev.win_size.h, init.resolution.reset);
                    pipeline.setViewport(ev.win_size.w, ev.win_size.h);
                    camera.setViewportPixels(ev.win_size.w, ev.win_size.h);
                    pxW = ev.win_size.w; pxH = ev.win_size.h; // 修正鼠标像素→世界坐标映射
                    break;
                case Event::Type::KEY:
                    if (ev.key.key_code == Tina::os::KeyCode::F11 && ev.key.down) {
                        if (!fullscreen) { prev_state = Tina::os::setFullScreen(window); fullscreen = true; TINA_INFO("切换全屏"); } else { Tina::os::restoreWindow(window, prev_state); fullscreen = false; TINA_INFO("退出全屏"); }
                    } else if (ev.key.key_code == Tina::os::KeyCode::R && ev.key.down) {
                        relative_mouse = !relative_mouse;
                        Tina::os::setRelativeMouseMode(window, relative_mouse);
                        TINA_INFO("相对鼠标模式: {}", relative_mouse);
                    } else if (ev.key.key_code == Tina::os::KeyCode::W) {
                        keyPressed[KEY_W] = ev.key.down; // 记录W键状态
                    } else if (ev.key.key_code == Tina::os::KeyCode::S) {
                        keyPressed[KEY_S] = ev.key.down; // 记录S键状态
                    } else if (ev.key.key_code == Tina::os::KeyCode::A) {
                        keyPressed[KEY_A] = ev.key.down; // 记录A键状态
                    } else if (ev.key.key_code == Tina::os::KeyCode::D) {
                        keyPressed[KEY_D] = ev.key.down; // 记录D键状态
                    } else if (ev.key.key_code == Tina::os::KeyCode::E && ev.key.down) { // 放大（视区高度变小）
                        camera.setViewHeightWorld(camera.viewH() * 0.9f);
                    } else if (ev.key.key_code == Tina::os::KeyCode::C && ev.key.down) { // 缩小（改用 C 键，Q 未在 KeyCode 中定义）
                        camera.setViewHeightWorld(camera.viewH() * 1.1111f);
                    } else if (ev.key.key_code == Tina::os::KeyCode::ESCAPE && ev.key.down) {
                        running = false;
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
        ticker.step([&](double fixed_dt){
            // 衰减碎块寿命（秒）并渐隐/过期移除
            physics.decayDebris((float)fixed_dt);
            // 先更新水体的元胞自动机（让湖水向空洞流动）
            int wx0=0, wy0=0, wx1=-1, wy1=-1;
            // 增加迭代次数，让水流更快响应（类似泰拉瑞亚）
            if (tilemap.stepWaterAdvanced(2, wx0, wy0, wx1, wy1)) {
                const int cx0 = std::max(0, wx0 / chunkSize);
                const int cy0 = std::max(0, wy0 / chunkSize);
                const int cx1 = std::min(cxCount - 1, wx1 / chunkSize);
                const int cy1 = std::min(cyCount - 1, wy1 / chunkSize);
                for (int cyi = cy0; cyi <= cy1; ++cyi)
                    for (int cxi = cx0; cxi <= cx1; ++cxi)
                        { int cidx = cyi * cxCount + cxi; if (cidx >= 0 && cidx < (int)chunkDirtyWater.size()) { chunkDirtyWater[(size_t)cidx] = 1; } }
            }
            // 水体对碎块的流动/浮力/阻力
            const b2Vec2 g = b2World_GetGravity(physics.world());
            const float buoyFactor = 0.85f; // 浮力系数（相对重力）
            const float linDrag = 1.2f;     // 线性阻力系数
            const float flowGain = 2.0f;    // 流动跟随强度
            const auto& dlist = physics.debris();
            for (const auto& d : dlist) {
                if (!b2Body_IsValid(d.body)) continue;
                b2Vec2 p = b2Body_GetPosition(d.body);
                int tx = (int)std::floor(p.x);
                int ty = (int)std::floor(p.y);
                if (tx < 0 || ty < 0 || tx >= mapCfg.width || ty >= mapCfg.height) continue;
                if (!tilemap.isWater(tx, ty)) continue;
                // 浮力：抵消部分重力
                float mass = b2Body_GetMass(d.body);
                b2Vec2 Fbuoy { -g.x * mass * buoyFactor, -g.y * mass * buoyFactor };
                // 阻力：与速度相反
                b2Vec2 v = b2Body_GetLinearVelocity(d.body);
                b2Vec2 Fdrag { -linDrag * v.x, -linDrag * v.y };
                // 流动：朝向水流速度
                float fx=0, fy=0; tilemap.waterFlowAdvanced(p.x, p.y, fx, fy);
                b2Vec2 Vt { fx, fy };
                b2Vec2 Fflow { flowGain * (Vt.x - v.x), flowGain * (Vt.y - v.y) };
                b2Vec2 F { Fbuoy.x + Fdrag.x + Fflow.x, Fbuoy.y + Fdrag.y + Fflow.y };
                b2Body_ApplyForceToCenter(d.body, F, true);
            }
            physics.step((float)fixed_dt);

            // 生成涓涓细流效果：水瓦片持续产生小水滴（默认关闭，避免“凭空增水”）
            if (false) {
                auto randf = [](){ return (float)std::rand() / (float)RAND_MAX; };
                // 遍历所有水瓦片，检查是否需要生成细流
                for (int y = 1; y < mapCfg.height - 1; ++y) {
                    for (int x = 1; x < mapCfg.width - 1; ++x) {
                        if (tilemap.get(x, y) != Tina::Game::TileType::Water) continue;
                        
                        // 瀑布效果：水从高处滴落
                        if (tilemap.get(x, y-1) == Tina::Game::TileType::Air) {
                            if (std::rand() % 200 < 1) { // 降低概率到 0.5%
                                physics.spawnWaterParticle(
                                    x + 0.5f + (randf() - 0.5f) * 0.3f,  // 轻微随机偏移
                                    y - 0.1f,
                                    0.2f,  // 小水滴
                                    0.15f, 0.35f, 0.90f, 0.8f,
                                    (randf() - 0.5f) * 2.0f,  // 轻微水平速度
                                    -2.0f + randf() * -1.0f,  // 向下速度
                                    1.5f   // 较短寿命
                                );
                            }
                        }
                        
                        // 水平细流：水从瓦片边缘流出
                        for (int dx : {-1, 1}) {
                            int nx = x + dx;
                            if (nx >= 0 && nx < mapCfg.width && 
                                tilemap.get(nx, y) == Tina::Game::TileType::Air && 
                                (y == 0 || tilemap.get(nx, y-1) != Tina::Game::TileType::Air)) { // 有支撑或在底部
                                if (std::rand() % 300 < 1) { // 降低水平流概率
                                    physics.spawnWaterParticle(
                                        x + 0.5f + dx * 0.4f,
                                        y + 0.3f + (randf() - 0.5f) * 0.2f,
                                        0.15f,  // 更小的水滴
                                        0.15f, 0.35f, 0.90f, 0.7f,
                                        dx * 3.0f + (randf() - 0.5f) * 1.0f,  // 水平流动
                                        (randf() - 0.5f) * 1.0f,  // 轻微竖直扰动
                                        1.0f    // 短寿命
                                    );
                                }
                            }
                        }
                        
                        // 压力流：水位差大时生成更多粒子
                        for (int dy : {-1, 1}) {
                            for (int dx : {-1, 0, 1}) {
                                int nx = x + dx, ny = y + dy;
                                if (nx >= 0 && nx < mapCfg.width && ny >= 0 && ny < mapCfg.height &&
                                    tilemap.get(nx, ny) == Tina::Game::TileType::Air) {
                                    // 检查周围是否有较多水瓦片（模拟压力）
                                    int waterCount = 0;
                                    for (int sy = y-1; sy <= y+1; ++sy) {
                                        for (int sx = x-1; sx <= x+1; ++sx) {
                                            if (sx >= 0 && sx < mapCfg.width && sy >= 0 && sy < mapCfg.height &&
                                                tilemap.get(sx, sy) == Tina::Game::TileType::Water) {
                                                waterCount++;
                                            }
                                        }
                                    }
                                    if (waterCount >= 6 && std::rand() % 400 < 1) { // 进一步降低高压力区域概率
                                        physics.spawnWaterParticle(
                                            x + 0.5f + (randf() - 0.5f) * 0.4f,
                                            y + 0.5f + (randf() - 0.5f) * 0.4f,
                                            0.18f,
                                            0.15f, 0.35f, 0.90f, 0.75f,
                                            (nx - x) * 2.5f + (randf() - 0.5f) * 2.0f,
                                            (ny - y) * 2.5f + (randf() - 0.5f) * 2.0f,
                                            1.2f
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 水粒子沉淀回瓦片（默认关闭，避免总量非守恒而“越流越多”）
            int sx0 = +mapCfg.width, sy0 = +mapCfg.height, sx1 = -1, sy1 = -1; bool settled = false;
            if (false) {
                Tina::Container::HashMap<long long, Tina::Container::Vector<b2BodyId>> cell;
                const auto& dlist2 = physics.debris();
                for (const auto& dp : dlist2) {
                    if (!dp.isWater) continue;
                    if (!b2Body_IsValid(dp.body)) continue;
                    b2Vec2 p2 = b2Body_GetPosition(dp.body);
                    b2Vec2 v2 = b2Body_GetLinearVelocity(dp.body);
                    float speed2 = std::sqrt(v2.x*v2.x + v2.y*v2.y);
                    int tx2 = (int)std::floor(p2.x);
                    int ty2 = (int)std::floor(p2.y);
                    if (tx2 < 0 || ty2 < 0 || tx2 >= mapCfg.width || ty2 >= mapCfg.height) continue;
                    if (tilemap.get(tx2,ty2) != Tina::Game::TileType::Air) continue;
                    bool support = (ty2 == 0) || (tilemap.get(tx2, ty2-1) != Tina::Game::TileType::Air);
                    if (speed2 < 1.2f && support) {
                        long long key = ((long long)ty2 << 32) | (unsigned)tx2;
                        cell[key].push_back(dp.body);
                    }
                }
                const int settleCount = 2; // 聚合阈值：至少 2 个粒子凝结为 1 个水瓦片
                for (auto& kv : cell) {
                    auto& bodies = kv.second;
                    if ((int)bodies.size() >= settleCount) {
                        int tx2 = (int)(kv.first & 0xffffffffu);
                        int ty2 = (int)(kv.first >> 32);
                        tilemap.set(tx2, ty2, Tina::Game::TileType::Water);
                        const int destroyN = settleCount;
                        for (int i = 0; i < destroyN && i < (int)bodies.size(); ++i) {
                            if (b2Body_IsValid(bodies[i])) b2DestroyBody(bodies[i]);
                        }
                        sx0 = std::min(sx0, tx2); sy0 = std::min(sy0, ty2);
                        sx1 = std::max(sx1, tx2); sy1 = std::max(sy1, ty2);
                        settled = true;
                    }
                }
            }
            if (settled) {
                const int rcx0 = std::max(0, sx0 / chunkSize);
                const int rcy0 = std::max(0, sy0 / chunkSize);
                const int rcx1 = std::min(cxCount - 1, sx1 / chunkSize);
                const int rcy1 = std::min(cyCount - 1, sy1 / chunkSize);
                for (int cyi = rcy0; cyi <= rcy1; ++cyi)
                    for (int cxi = rcx0; cxi <= rcx1; ++cxi)
                        { int cidx = cyi * cxCount + cxi; if (cidx >= 0 && cidx < (int)chunkDirtyWater.size()) { chunkDirtyWater[(size_t)cidx] = 1; } }
            }
        });

        // 渲染（可使用 alpha 做插值渲染）
        const double alpha = ticker.alpha(); (void)alpha;

        // 每帧WASD移动处理（基于按键状态，确保连续流畅移动）
        const float moveSpeed = 120.0f; // 世界单位/秒
        const float frameMove = moveSpeed * (float)frame_timer.frameSeconds(); // 本帧移动距离
        if (keyPressed[KEY_W]) camera.moveBy(0.0f, +frameMove);
        if (keyPressed[KEY_S]) camera.moveBy(0.0f, -frameMove);
        if (keyPressed[KEY_A]) camera.moveBy(-frameMove, 0.0f);
        if (keyPressed[KEY_D]) camera.moveBy(+frameMove, 0.0f);

        renderer.beginFrame();
        pipeline.begin();
        // 相机：设置视图/投影（view0: 像素对齐用于地图；view1: 非对齐用于动态碎块）
        float view_unsnapped[16], proj_mtx[16];
        camera.buildViewProj(view_unsnapped, proj_mtx);
        // 像素对齐（仅针对地图）：将相机位置按“每像素的世界单位”对齐，以减少栅格闪烁/走样
        const float stepX = camera.viewW() / (float)camera.vpW();
        const float stepY = camera.viewH() / (float)camera.vpH();
        const float camXsnap = std::floor(camera.x() / stepX) * stepX;
        const float camYsnap = std::floor(camera.y() / stepY) * stepY;
        float view_snapped[16]; bx::mtxTranslate(view_snapped, -camXsnap, -camYsnap, 0.0f);
        // 设置两个视图的矩阵
        bgfx::setViewTransform(0, view_snapped,   proj_mtx);
        bgfx::setViewTransform(1, view_unsnapped, proj_mtx);

        // 计算视区（世界坐标）
        const float vx0 = camera.x();
        const float vy0 = camera.y();
        const float vx1 = vx0 + camera.viewW(); const float vy1_local = vy0 + camera.viewH();
        // 重建：仅处理可见范围内的脏块，避免全图重建
        int rcx0 = std::max(0, (int)std::floor(vx0 / (float)chunkSize));
        int rcy0 = std::max(0, (int)std::floor(vy0 / (float)chunkSize));
        int rcx1 = std::min(cxCount - 1, (int)std::floor((vx1-1e-6f) / (float)chunkSize));
        int rcy1 = std::min(cyCount - 1, (int)std::floor((vy1_local-1e-6f) / (float)chunkSize));
        int rebuiltSolid = 0, rebuiltWater = 0;
        for (int cyi = rcy0; cyi <= rcy1; ++cyi) {
            for (int cxi = rcx0; cxi <= rcx1; ++cxi) {
                const int cidx = cyi * cxCount + cxi;
                bool ds = (cidx >= 0 && cidx < (int)chunkDirtySolid.size()) ? (chunkDirtySolid[(size_t)cidx] != 0) : false;
                bool dw = (cidx >= 0 && cidx < (int)chunkDirtyWater.size()) ? (chunkDirtyWater[(size_t)cidx] != 0) : false;
                if (!ds && !dw) continue;
                if (ds) { rebuildChunk(cxi, cyi, /*waterOnly*/false); ++rebuiltSolid; }
                else if (dw) { rebuildChunk(cxi, cyi, /*waterOnly*/true); ++rebuiltWater; }
                if (cidx >= 0 && cidx < (int)chunkDirtySolid.size()) chunkDirtySolid[(size_t)cidx] = 0;
                if (cidx >= 0 && cidx < (int)chunkDirtyWater.size()) chunkDirtyWater[(size_t)cidx] = 0;
            }
        }
        const float vy1 = vy0 + camera.viewH();

        // 提交可见 chunk（颜色方块）
        if (bgfx::isValid(prog_color)) {
            for (const auto& chunk : chunks) {
                // AABB 相交测试
                if (chunk.maxx <= vx0 || chunk.minx >= vx1 || chunk.maxy <= vy0 || chunk.miny >= vy1) continue;
                if (!chunk.valid()) continue;
                bgfx::Encoder* enc = renderer.getEncoder();
                enc->setVertexBuffer(0, chunk.vb);
                enc->setIndexBuffer(chunk.ib);
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
                enc->submit(0, prog_color); // 地图使用视图0（像素对齐）
                bgfx::end(enc);
            }
        }
        // 提交泥土（纹理）层（只使用 sprite 程序和统一采样器 s_tex）
        if (bgfx::isValid(prog_sprite) && bgfx::isValid(dirtTex)) {
            for (size_t i = 0; i < chunks.size(); ++i) {
                const auto& chunkD = chunks[i];
                if (chunkD.maxx <= vx0 || chunkD.minx >= vx1 || chunkD.maxy <= vy0 || chunkD.miny >= vy1) continue;
                if (i >= chunksVBDirt.size() || i >= chunksIBDirt.size() || i >= chunksDirtIndexCount.size()) continue;
                if (!bgfx::isValid(chunksVBDirt[i]) || !bgfx::isValid(chunksIBDirt[i]) || chunksDirtIndexCount[i] == 0) continue;
                bgfx::Encoder* encd = renderer.getEncoder();
                encd->setVertexBuffer(0, chunksVBDirt[i]);
                encd->setIndexBuffer(chunksIBDirt[i]);
                if (bgfx::isValid(uSpriteTex)) encd->setTexture(0, uSpriteTex, dirtTex);
                encd->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
                encd->submit(0, prog_sprite);
                bgfx::end(encd);
            }
        }


        // 碎块回收：控制数量、清理超界体
        physics.cleanupDebris();

        // 绘制水体叠加层（基于可见区域裁剪）
        if (bgfx::isValid(prog_color)) {
            for (size_t i = 0; i < chunks.size(); ++i) {
                const auto& chunkVis = chunks[i];
                if (chunkVis.maxx <= vx0 || chunkVis.minx >= vx1 || chunkVis.maxy <= vy0 || chunkVis.miny >= vy1) continue;
                if (i >= chunksVBWater.size() || i >= chunksIBWater.size() || i >= chunksWaterIndexCount.size()) continue;
                if (!bgfx::isValid(chunksVBWater[i]) || !bgfx::isValid(chunksIBWater[i]) || chunksWaterIndexCount[i] == 0) continue;
                bgfx::Encoder* encw = renderer.getEncoder();
                encw->setVertexBuffer(0, chunksVBWater[i]);
                encw->setIndexBuffer(chunksIBWater[i]);
                encw->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
                encw->submit(0, prog_color);
                bgfx::end(encw);
            }
        }

        // 渲染物理碎块（一次性批量）
        {
            const auto& dlist = physics.debris();
            if (!dlist.empty() && bgfx::isValid(prog_color)) {
                const uint32_t quadCount = (uint32_t)dlist.size();
                // 预估最大，实际会按可见性写入
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
                        // 视区裁剪（仅渲染可见碎块）
                        if (p.x + 1.0f < vx0 || p.x - 1.0f > vx1 || p.y + 1.0f < vy0 || p.y - 1.0f > vy1) {
                            continue;
                        }
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
                    if (ib > 0) {
                        // 调整计数（bgfx 使用 buffer 的 size 字段，允许少于分配量的有效数据）
                        tvb.size = vb * sizeof(Vtx);
                        tib.size = ib * sizeof(uint32_t);
                        bgfx::Encoder* enc = renderer.getEncoder();
                        enc->setVertexBuffer(0, &tvb);
                        enc->setIndexBuffer(&tib);
                        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
                        enc->submit(1, prog_color); // 碎块使用视图1（非对齐）
                        bgfx::end(enc);
                    }
                }
            }
        }
        pipeline.submit();

        // --- UI HUD（视图 2，像素坐标）---
        {
            int tx0 = (int)std::max(0.0f, std::floor(vx0));
            int ty0 = (int)std::max(0.0f, std::floor(vy0));
            int tx1 = (int)std::min((float)mapCfg.width - 1.0f, std::floor(vx1 - 1e-6f));
            int ty1 = (int)std::min((float)mapCfg.height - 1.0f, std::floor(vy1 - 1e-6f));
            long long waterSum = 0; int waterTiles = 0; int tilesCount = 0;
            if (tx1 >= tx0 && ty1 >= ty0) {
                for (int y = ty0; y <= ty1; ++y) {
                    for (int x = tx0; x <= tx1; ++x) {
                        int wv = (int)tilemap.water(x, y);
                        waterSum += wv;
                        if (wv > 0) ++waterTiles;
                        ++tilesCount;
                    }
                }
            }
            const double avgWater = tilesCount > 0 ? (double)waterSum / (255.0 * (double)tilesCount) : 0.0;

            ui.drawRect(2, 10.0f, 10.0f, 392.0f, 64.0f, 0.0f, 0.0f, 0.0f, 0.55f);
            char line1[160];
            std::snprintf(line1, sizeof(line1), "FPS: %.1f | 帧时: %.2f ms | 重建 S/W: %d/%d",
                          frame_timer.fps(), frame_timer.frameSeconds() * 1000.0, rebuiltSolid, rebuiltWater);
            ui.drawText(2, 18.0f, 30.0f, 1,1,1,1, line1);
            char line2[160];
            std::snprintf(line2, sizeof(line2), "视野水块: %d | 平均水位: %.1f%%",
                          waterTiles, avgWater * 100.0);
            ui.drawText(2, 18.0f, 52.0f, 0.8f,0.9f,1.0f,1, line2);
        }

        // 在 UI 视图 2 上直接绘制示例文本（验证中文管线）
        textRenderer.drawText(2, 16.0f, 56.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                              "中文渲染 OK —— Tina 引擎");
        
        renderer.endFrame();

        // 每秒打印一次帧率/帧时间与目标设定，便于核对
        const double now_sec = frame_timer.sinceStartupSeconds();
        if (now_sec - fps_log_t >= 1.0) {
            TINA_INFO("FPS: {:.1f} | frame: {:.2f} ms | VSync: {} | Cap: {} Hz | Fixed: {} Hz | RBld S/W: {}/{}",
                      frame_timer.fps(),
                      frame_timer.frameSeconds() * 1000.0,
                      vsync_on ? "on" : "off",
                      time_cfg.frame_cap_hz,
                      time_cfg.tick_rate, rebuiltSolid, rebuiltWater);
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
    // 注意：必须在 bgfx::shutdown() 之前显式释放 TextRenderer（内部会销毁 bgfx 句柄），
    // 否则其析构函数在 bgfx 已关闭后再调用 bgfx 接口，可能导致退出卡死。
    textRenderer.shutdown();
    for (auto& chunk : chunks) { if (bgfx::isValid(chunk.vb)) bgfx::destroy(chunk.vb); if (bgfx::isValid(chunk.ib)) bgfx::destroy(chunk.ib); }
    for (size_t i = 0; i < chunksVBWater.size(); ++i) {
        if (bgfx::isValid(chunksVBWater[i])) bgfx::destroy(chunksVBWater[i]);
        if (bgfx::isValid(chunksIBWater[i])) bgfx::destroy(chunksIBWater[i]);
    }
    for (size_t i = 0; i < chunksVBDirt.size(); ++i) {
        if (bgfx::isValid(chunksVBDirt[i])) bgfx::destroy(chunksVBDirt[i]);
        if (bgfx::isValid(chunksIBDirt[i])) bgfx::destroy(chunksIBDirt[i]);
    }
    if (bgfx::isValid(dirtTex)) bgfx::destroy(dirtTex);
    if (bgfx::isValid(uSpriteTex)) bgfx::destroy(uSpriteTex);

    // 清理渲染资源
    shaderManager.cleanup();
    // 额外提交一帧，确保销毁命令被渲染线程消费
    bgfx::frame();
    bgfx::shutdown();
    Tina::os::destroyWindow(window);
    TINA_INFO("退出 Tina");
    Tina::Core::Log::Shutdown();
    return 0;
}
















