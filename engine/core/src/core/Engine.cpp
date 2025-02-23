// //
// // Created by wuxianggujun on 2025/1/31.
// //
//
// #include "tina/core/Engine.hpp"
// #include "tina/core/Context.hpp"
// #include "tina/window/WindowManager.hpp"
// #include "tina/event/EventQueue.hpp"
// #include <bgfx/bgfx.h>
// #include <bgfx/platform.h>
// #ifdef _WIN32
// #include <windows.h>
// #endif
// #include "tina/core/Timer.hpp"
// #include "tina/scene/Scene.hpp"
// #include "tina/renderer/RenderCommand.hpp"
// #include "tina/renderer/ShaderManager.hpp"
// #include "tina/renderer/TextureManager.hpp"
// #include <psapi.h> // Windows内存信息API
// #include "tina/resources/ResourceManager.hpp"
// #include "tina/resources/TextureLoader.hpp"
//
// namespace Tina::Core
// {
//     Engine* Engine::s_Instance = nullptr;
//
//     Engine::Engine()
//         : m_uniformManager(), m_textureManager(), m_shaderManager(), m_context(Context::getInstance())
//           , m_mainWindow(BGFX_INVALID_HANDLE)
//           , m_windowWidth(1280)
//           , m_windowHeight(720)
//           , m_isShutdown(false)
//           , m_isInitialized(false)
//     {
//         s_Instance = this;
//         TINA_CORE_LOG_INFO("Engine created.");
//     }
//
//     Engine::~Engine()
//     {
//         if (!m_isShutdown)
//         {
//             shutdown();
//         }
//         s_Instance = nullptr;
//         TINA_CORE_LOG_INFO("Engine destroyed.");
//     }
//
//     bool Engine::initialize()
//     {
//         if (m_isInitialized)
//         {
//             TINA_CORE_LOG_WARN("Engine already initialized");
//             return true;
//         }
//
//         try
//         {
//             // 初始化窗口管理器
//             if (!m_context.getWindowManager().initialize())
//             {
//                 TINA_CORE_LOG_ERROR("Failed to initialize window manager");
//                 return false;
//             }
//
//             // 创建主窗口
//             Window::WindowConfig config{};
//             config.width = m_windowWidth;
//             config.height = m_windowHeight;
//             config.title = "Tina Engine";
//
//             m_mainWindow = m_context.getWindowManager().createWindow(config);
//
//             if (!isValid(m_mainWindow))
//             {
//                 TINA_CORE_LOG_ERROR("Failed to create main window");
//                 return false;
//             }
//
//             // 初始化渲染系统
//             bgfx::Init init;
//             init.type = bgfx::RendererType::Count; // 自动选择渲染器
//             init.resolution.width = m_windowWidth;
//             init.resolution.height = m_windowHeight;
//             init.resolution.reset = BGFX_RESET_VSYNC;
//             init.platformData.nwh = m_context.getWindowManager().getNativeWindowHandle(m_mainWindow);
//             init.platformData.ndt = m_context.getWindowManager().getNativeDisplayHandle();
//
//             if (!bgfx::init(init))
//             {
//                 TINA_CORE_LOG_ERROR("Failed to initialize bgfx");
//                 return false;
//             }
//
//             // 注册资源加载器
//             auto& resourceManager = ResourceManager::getInstance();
//             resourceManager.registerLoader<Texture2D>(std::make_unique<TextureLoader>());
//
//             m_isInitialized = true;
//             TINA_CORE_LOG_INFO("Engine initialized successfully");
//             return true;
//         }
//         catch (const std::exception& e)
//         {
//             TINA_CORE_LOG_ERROR("Error during initialization: {}", e.what());
//             return false;
//         }
//     }
//
//     void Engine::getWindowSize(uint32_t& width, uint32_t& height) const
//     {
//         width = m_windowWidth;
//         height = m_windowHeight;
//     }
//
//     void Engine::logMemoryStats()
//     {
// #ifdef _WIN32
//         PROCESS_MEMORY_COUNTERS_EX pmc;
//         if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
//         {
//             static float lastWorkingSet = 0;
//             static float lastPrivateUsage = 0;
//             static float peakWorkingSet = 0;
//             float currentWorkingSet = pmc.WorkingSetSize / (1024.0f * 1024.0f);
//             float currentPrivateUsage = pmc.PrivateUsage / (1024.0f * 1024.0f);
//
//             peakWorkingSet = std::max(peakWorkingSet, currentWorkingSet);
//
//             // 基本内存统计
//             TINA_CORE_LOG_DEBUG("Memory Statistics:");
//             TINA_CORE_LOG_DEBUG("\tWorking Set: {:.2f}MB (实际占用物理内存) [{:+.2f}MB] (峰值: {:.2f}MB)",
//                            currentWorkingSet,
//                            currentWorkingSet - lastWorkingSet,
//                            peakWorkingSet);
//             TINA_CORE_LOG_DEBUG("\tPrivate Usage: {:.2f}MB (进程独占内存) [{:+.2f}MB]",
//                            currentPrivateUsage,
//                            currentPrivateUsage - lastPrivateUsage);
//             TINA_CORE_LOG_DEBUG("\tVirtual Memory: {:.2f}MB (虚拟内存)",
//                            pmc.PagefileUsage / (1024.0 * 1024.0));
//
//             // 检查内存增长
//             static int growthCount = 0;
//             if (currentWorkingSet - lastWorkingSet > 1.0f) // 1MB增长阈值
//             {
//                 if (++growthCount >= 3) // 连续3次增长
//                 {
//                     TINA_CORE_LOG_WARN("\t内存持续增长! 可能存在泄漏");
//                     growthCount = 0;
//                 }
//             }
//             else
//             {
//                 growthCount = 0;
//             }
//
//             lastWorkingSet = currentWorkingSet;
//             lastPrivateUsage = currentPrivateUsage;
//
//             // 获取更详细的内存信息
//             MEMORY_BASIC_INFORMATION memInfo;
//             size_t committedTotal = 0;
//             size_t reservedTotal = 0;
//             size_t privateTotal = 0;
//             size_t imageTotal = 0; // DLL和EXE
//             size_t mappedTotal = 0; // 内存映射文件
//             void* addr = nullptr;
//
//             while (VirtualQuery(addr, &memInfo, sizeof(memInfo)))
//             {
//                 if (memInfo.State == MEM_COMMIT)
//                 {
//                     committedTotal += memInfo.RegionSize;
//                     if (memInfo.Type == MEM_PRIVATE)
//                         privateTotal += memInfo.RegionSize;
//                     else if (memInfo.Type == MEM_IMAGE)
//                         imageTotal += memInfo.RegionSize;
//                     else if (memInfo.Type == MEM_MAPPED)
//                         mappedTotal += memInfo.RegionSize;
//                 }
//                 else if (memInfo.State == MEM_RESERVE)
//                     reservedTotal += memInfo.RegionSize;
//
//                 addr = (void*)((char*)memInfo.BaseAddress + memInfo.RegionSize);
//                 if ((size_t)addr >= 0x7FFF0000)
//                     break;
//             }
//
//             TINA_CORE_LOG_DEBUG("\tMemory Allocation:");
//             TINA_CORE_LOG_DEBUG("\t\tCommitted: {:.2f}MB (已分配且可访问)", committedTotal / (1024.0 * 1024.0));
//             TINA_CORE_LOG_DEBUG("\t\tPrivate: {:.2f}MB (进程私有已分配)", privateTotal / (1024.0 * 1024.0));
//             TINA_CORE_LOG_DEBUG("\t\tImage: {:.2f}MB (DLL和可执行文件)", imageTotal / (1024.0 * 1024.0));
//             TINA_CORE_LOG_DEBUG("\t\tMapped: {:.2f}MB (内存映射文件)", mappedTotal / (1024.0 * 1024.0));
//             TINA_CORE_LOG_DEBUG("\t\tReserved: {:.2f}MB (预留未分配)", reservedTotal / (1024.0 * 1024.0));
//
//             // 获取堆信息
//             HANDLE hHeap = GetProcessHeap();
//             if (hHeap)
//             {
//                 PROCESS_HEAP_ENTRY entry;
//                 entry.lpData = nullptr;
//                 size_t totalHeapSize = 0;
//                 size_t usedHeapSize = 0;
//                 size_t freeHeapSize = 0;
//                 size_t largestFreeBlock = 0;
//                 size_t totalBlocks = 0;
//                 size_t usedBlocks = 0;
//                 size_t smallBlocks = 0; // 小于1KB的块
//
//                 while (HeapWalk(hHeap, &entry))
//                 {
//                     totalHeapSize += entry.cbData;
//                     totalBlocks++;
//
//                     if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
//                     {
//                         usedHeapSize += entry.cbData;
//                         usedBlocks++;
//                         if (entry.cbData < 1024)
//                             smallBlocks++;
//                     }
//                     else
//                     {
//                         freeHeapSize += entry.cbData;
//                         largestFreeBlock = std::max(largestFreeBlock, (size_t)entry.cbData);
//                     }
//                 }
//
//                 float fragmentation = (totalHeapSize > 0) ? (freeHeapSize * 100.0f / totalHeapSize) : 0.0f;
//                 float avgBlockSize = (usedBlocks > 0) ? (usedHeapSize / (float)usedBlocks) : 0.0f;
//                 float smallBlockRatio = (usedBlocks > 0) ? (smallBlocks * 100.0f / usedBlocks) : 0.0f;
//
//                 TINA_CORE_LOG_DEBUG("\tHeap Analysis:");
//                 TINA_CORE_LOG_DEBUG("\t\tTotal: {:.2f}MB ({} blocks)", totalHeapSize / (1024.0 * 1024.0), totalBlocks);
//                 TINA_CORE_LOG_DEBUG("\t\tUsed: {:.2f}MB ({} blocks, avg {:.2f}KB)",
//                                usedHeapSize / (1024.0 * 1024.0),
//                                usedBlocks,
//                                avgBlockSize / 1024.0f);
//                 TINA_CORE_LOG_DEBUG("\t\tFree: {:.2f}MB (largest block: {:.2f}MB)",
//                                freeHeapSize / (1024.0 * 1024.0),
//                                largestFreeBlock / (1024.0 * 1024.0));
//                 TINA_CORE_LOG_DEBUG("\t\tFragmentation: {:.1f}%", fragmentation);
//                 TINA_CORE_LOG_DEBUG("\t\tSmall Blocks (<1KB): {:.1f}% ({} blocks)",
//                                smallBlockRatio, smallBlocks);
//
//                 // 内存警告和建议
//                 if (fragmentation > 15.0f)
//                     TINA_CORE_LOG_WARN("\t\t高内存碎片化! 建议实现内存池");
//
//                 if (usedHeapSize > 0.8f * totalHeapSize)
//                 {
//                     TINA_CORE_LOG_WARN("\t\t堆内存使用率过高! 建议:");
//                     TINA_CORE_LOG_WARN("\t\t1. 增加堆大小 (当前 {:.0f}MB)", totalHeapSize / (1024.0 * 1024.0));
//                     TINA_CORE_LOG_WARN("\t\t2. 检查大块内存分配");
//                     TINA_CORE_LOG_WARN("\t\t3. 实现资源缓存池");
//                 }
//
//                 if (smallBlockRatio > 50.0f)
//                 {
//                     TINA_CORE_LOG_WARN("\t\t小内存块过多! 建议:");
//                     TINA_CORE_LOG_WARN("\t\t1. 使用对象池管理小对象");
//                     TINA_CORE_LOG_WARN("\t\t2. 合并小内存分配");
//                     TINA_CORE_LOG_WARN("\t\t3. 检查临时对象创建");
//                 }
//
//                 if (fragmentation > 10.0f && largestFreeBlock < 1024 * 1024) // 1MB
//                 {
//                     TINA_CORE_LOG_WARN("\t\t内存碎片化严重且无大块可用! 建议:");
//                     TINA_CORE_LOG_WARN("\t\t1. 执行堆整理");
//                     TINA_CORE_LOG_WARN("\t\t2. 实现内存池");
//                     TINA_CORE_LOG_WARN("\t\t3. 预分配大块内存");
//                 }
//             }
//         }
// #endif
//     }
//
//     bool Engine::run()
//     {
//         if (!m_isInitialized)
//         {
//             TINA_CORE_LOG_ERROR("Engine not initialized");
//             return false;
//         }
//
//         try
//         {
//             bool running = true;
//             Timer timer(true);
//             constexpr float targetFrameTime = 1.0f / 60.0f;
//
//             while (running && !m_isShutdown)
//             {
//                 float deltaTime = timer.getSeconds(true);
//                 timer.reset();
//
//                 // 处理窗口事件
//                 m_context.getWindowManager().pollEvents();
//
//                 // 处理事件队列
//                 Event event;
//                 while (m_context.getWindowManager().pollEvent(event))
//                 {
//                     if (event.type == Event::WindowClose &&
//                         event.windowHandle.idx == m_mainWindow.idx)
//                     {
//                         running = false;
//                         break;
//                     }
//
//                     if (event.type == Event::WindowResize)
//                     {
//                         // 只更新内部状态
//                         m_windowWidth = event.windowResize.width;
//                         m_windowHeight = event.windowResize.height;
//                     }
//                     // 无论Engine是否处理了事件,都传递给Scene
//                     if (m_activeScene)
//                     {
//                         m_activeScene->onEvent(event);
//                     }
//                 }
//
//                 // 检查窗口是否应该关闭
//                 const Window* mainWindow = m_context.getWindowManager().getWindow(m_mainWindow);
//                 if (!mainWindow || mainWindow->shouldClose())
//                 {
//                     running = false;
//                     continue;
//                 }
//
//                 // 更新和渲染场景
//                 if (m_activeScene)
//                 {
//                     m_activeScene->onUpdate(deltaTime);
//                     m_activeScene->onRender();
//                 }
//
//                 // 帧率限制
//                 if (const float frameTime = timer.getSeconds(true); frameTime < targetFrameTime)
//                 {
//                     if (const float sleepTime = (targetFrameTime - frameTime) * 1000.0f; sleepTime > 0)
//                     {
//                         std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(sleepTime)));
//                     }
//                 }
//             }
//
//             return true;
//         }
//         catch (const std::exception& e)
//         {
//             TINA_CORE_LOG_ERROR("Error during main loop: {}", e.what());
//             return false;
//         }
//     }
//
//     void Engine::shutdown()
//     {
//         TINA_CORE_LOG_INFO("Engine shutting down");
//
//         // 1. 先关闭场景
//         if (m_activeScene)
//         {
//             TINA_CORE_LOG_DEBUG("Destroying active scene");
//             m_activeScene.reset();
//             TINA_CORE_LOG_DEBUG("Active scene destroyed");
//         }
//
//         // 2. 完成所有渲染命令
//         TINA_CORE_LOG_DEBUG("Finalizing render commands");
//         RenderCommandQueue::getInstance().executeAll();
//
//         // 3. 提交一帧确保所有渲染完成
//         TINA_CORE_LOG_DEBUG("Submitting final frame");
//         bgfx::frame();
//
//         // 4. 关闭资源管理器
//         TINA_CORE_LOG_DEBUG("Shutting down resource managers");
//
//         m_uniformManager.shutdown();
//         m_textureManager.shutdown();
//         m_shaderManager.shutdown();
//
//         // 5. 关闭BGFX
//         TINA_CORE_LOG_DEBUG("Shutting down BGFX");
//         if (bgfx::getInternalData() && bgfx::getInternalData()->context)
//         {
//             bgfx::shutdown();
//         }
//
//         TINA_CORE_LOG_INFO("Engine shutdown completed");
//     }
//
//     const char* Engine::getVersion() const
//     {
//         return TINA_VERSION;
//     }
//
//     Context& Engine::getContext()
//     {
//         return m_context;
//     }
//
//     Scene* Engine::createScene(const std::string& name)
//     {
//         m_activeScene = MakeUnique<Scene>(name);
//         return m_activeScene.get();
//     }
//
//     void Engine::setActiveScene(Scene* scene)
//     {
//         if (scene)
//         {
//             m_activeScene.reset(scene);
//             TINA_CORE_LOG_INFO("Set active scene: {}", scene->getName());
//         }
//     }
// }
