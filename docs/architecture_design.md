# Tina 引擎架构设计文档

**版本**: 2.0
**最后更新**: 2025年10月6日
**状态**: 设计阶段

---

## 📋 目录

1. [当前架构分析](#当前架构分析)
2. [存在的问题](#存在的问题)
3. [改进方案设计](#改进方案设计)
4. [核心类设计](#核心类设计)
5. [实施路线图](#实施路线图)
6. [架构对比分析](#架构对比分析)
7. [常见问题解答](#常见问题解答)

---

## 当前架构分析

### 模块结构

```
Tina/
├── core/          # 核心工具库
│   ├── Container.hpp      # EASTL 容器封装
│   ├── Math.hpp           # 数学工具
│   ├── Log.hpp            # 日志系统
│   ├── Color.hpp          # 颜色管理
│   ├── Signal.hpp         # 信号槽机制（未使用）
│   └── Memory.hpp         # 智能指针封装
│
├── ecs/           # 实体组件系统
│   ├── World.hpp          # ECS 世界管理器
│   ├── Components.hpp     # 组件定义
│   └── systems/           # 系统实现
│       ├── PlayerInputSystem.hpp
│       ├── AISystem.hpp
│       ├── CharacterMovementSystem.hpp
│       ├── PhysicsSystem.hpp
│       └── CharacterRenderSystem.hpp
│
├── game/          # 游戏逻辑
│   ├── TileMap.hpp        # 瓦片地图
│   ├── Camera2D.hpp       # 2D 相机
│   ├── TerrainEditor.hpp  # 地形编辑
│   └── GameConfig.hpp     # 游戏配置
│
├── renderer/      # 渲染系统
│   ├── ShaderManager.hpp  # 着色器管理
│   └── TileRenderer.hpp   # 瓦片渲染器
│
├── ui/            # UI 系统
│   ├── UINode.hpp         # UI 节点树
│   ├── UICore.hpp         # UI 渲染器
│   ├── UIEventSystem.hpp  # UI 事件系统
│   ├── TextRenderer.hpp   # 文本渲染
│   ├── UIToolbar.hpp      # 工具栏
│   └── UIComponents.hpp   # UI 组件
│
├── physics/       # 物理系统
├── particles/     # 粒子系统
├── os/            # 平台抽象层
└── main.cpp       # 主入口（534 行）
```

### 当前 main.cpp 结构

```cpp
int main() {
    // 1. 初始化阶段（~100 行）
    - 创建窗口
    - 初始化 bgfx
    - 加载着色器
    - 创建文本渲染器
    - 创建粒子系统

    // 2. 游戏对象创建（~50 行）
    - 创建 TileMap
    - 创建 ECS World
    - 创建 Camera
    - 创建 UI（工具栏、角色面板）

    // 3. 主循环（~350 行）
    while (running) {
        // 事件处理（~150 行）
        - 窗口事件（关闭、调整大小）
        - 键盘输入（移动、跳跃、工具切换）
        - 鼠标输入（工具使用、UI 交互）

        // 工具逻辑（~80 行）
        - 注水器
        - 挖掘器
        - 爆炸器

        // 更新逻辑（~50 行）
        - ECS 系统更新
        - 液体模拟
        - 粒子更新
        - 相机跟随

        // 渲染逻辑（~100 行）
        - 渲染固体方块
        - 渲染液体
        - 渲染角色
        - 渲染粒子
        - 渲染 UI
    }

    // 4. 清理阶段（~20 行）
}
```

### 优点

✅ **模块化良好** - core/ecs/game/renderer 分层清晰
✅ **ECS 设计优秀** - 使用 EnTT，组件数据与系统逻辑分离
✅ **文档完善** - 有详细的设计文档
✅ **前瞻性设计** - 引入了 Signal/Slot 机制

---

## 存在的问题

### 🔴 问题 1：main.cpp 过于臃肿

**现状**：534 行，包含初始化、游戏循环、事件处理、工具逻辑、渲染等所有内容。

**痛点示例**：

#### 场景 A：添加主菜单功能
```cpp
// 当前需要在 main.cpp 中添加：
bool inMainMenu = true;
bool inGame = false;

while (running) {
    if (inMainMenu) {
        // 处理菜单事件
        // 更新菜单
        // 渲染菜单
    } else if (inGame) {
        // 原有的游戏代码（350 行）
    }
}
// → main.cpp 膨胀到 800+ 行
```

#### 场景 B：调试特定功能
```cpp
// 想测试"爆炸工具"？必须：
// 1. 启动完整游戏
// 2. 手动选择工具
// 3. 手动点击鼠标
// 4. 无法单元测试
```

**影响**：
- ❌ 代码难以阅读和维护
- ❌ 功能扩展导致 if-else 嵌套
- ❌ 无法单独测试各个模块
- ❌ Git 协作容易冲突

---

### 🟡 问题 2：缺少 Application/Game 层

**现状**：窗口创建、bgfx 初始化、主循环等分散在 main() 中。

**痛点**：
```cpp
// 想要重启游戏？
// → 无法优雅实现，必须重启整个进程

// 想要在游戏中动态切换分辨率？
// → 需要在 main.cpp 中硬编码逻辑

// 想要支持多个游戏实例（用于测试）？
// → 不可能，所有状态都是全局的
```

**缺少的抽象**：
- 应用程序生命周期管理（Application）
- 游戏状态封装（Game）
- 资源管理器统一接口（ResourceManager）

---

### 🟡 问题 3：缺少场景系统

**现状**：无法实现场景切换（主菜单 → 游戏 → 暂停 → 设置）。

**痛点**：
```cpp
// 当前架构下，想实现：
// 主菜单 → 点击开始 → 游戏场景 → 按 ESC → 暂停菜单 → 返回主菜单

// 只能通过状态机硬编码：
enum GameState { MENU, PLAYING, PAUSED, SETTINGS };
GameState state = MENU;

while (running) {
    switch (state) {
        case MENU:    /* 菜单逻辑 */ break;
        case PLAYING: /* 游戏逻辑 */ break;
        case PAUSED:  /* 暂停逻辑 */ break;
        case SETTINGS:/* 设置逻辑 */ break;
    }
}
// → 每添加一个场景，main.cpp 都要改动
```

**业界标准**：
- Unity: Scene + SceneManager
- Godot: Node + SceneTree
- Unreal: Level + World

---

### 🟡 问题 4：Signal 系统未使用

**现状**：已实现优秀的 Signal/Slot 机制，但未实际应用。

**当前事件处理方式**：
```cpp
// UI 按钮：使用回调函数
UIButton* btn = new UIButton();
btn->onHoverIn = []() { /* ... */ };   // 函数指针
btn->onHoverOut = []() { /* ... */ };

// 游戏事件：直接调用
if (playerDied) {
    ui->updateHealthBar();
    audio->playSound("death");
    saveHighScore();
}
// → 紧耦合，难以扩展
```

**改用 Signal 的好处**：
```cpp
// 游戏事件总线
EventBus::onPlayerDied.connect([&]() { ui->updateHealthBar(); });
EventBus::onPlayerDied.connect([&]() { audio->playSound("death"); });
EventBus::onPlayerDied.connect([&]() { saveHighScore(); });

// 触发事件
EventBus::onPlayerDied.emit();
// → 解耦合，易于添加新监听器
```

---

### 🟢 问题 5：资源管理不完整

**现状**：只有 ShaderManager，其他资源分散管理。

```cpp
// 当前：每个系统自己管理资源
TextRenderer text;
text.loadFont("font.otf", 24);  // 字体

Texture* texture = loadTexture("sprite.png");  // 纹理（不存在）

bgfx::ProgramHandle prog = shaderMgr->loadProgram("color", "color");  // 着色器
```

**问题**：
- ❌ 重复加载浪费内存
- ❌ 无法统一卸载
- ❌ 缺少资源引用计数

**期望**：
```cpp
// 统一资源管理
ResourceManager& res = app.resources();
Font* font = res.loadFont("font.otf");
Texture* tex = res.loadTexture("sprite.png");
Shader* shader = res.loadShader("color");

// 自动引用计数，自动卸载
```

---

## 改进方案设计

### 新架构层次图

```
┌─────────────────────────────────────────────┐
│         main.cpp (< 50 行)                   │
│   - 创建 Application                          │
│   - 创建初始场景                              │
│   - 运行主循环                                │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│      Application (应用程序层)                 │
│  ┌──────────────────────────────────────┐   │
│  │ - 窗口管理 (OS::Window)              │   │
│  │ - 渲染器初始化 (bgfx)                │   │
│  │ - 资源管理器 (ResourceManager)       │   │
│  │ - 场景管理器 (SceneManager)          │   │
│  │ - 事件总线 (EventBus)                │   │
│  └──────────────────────────────────────┘   │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│      SceneManager (场景管理层)               │
│  - 场景栈：[MenuScene, GameScene, ...]      │
│  - push/pop/replace 场景切换                │
│  - 生命周期管理：onEnter/onExit/onPause    │
└──────────────────┬──────────────────────────┘
                   │
        ┌──────────┴──────────┐
        │                     │
┌───────▼──────┐      ┌───────▼──────┐
│ MenuScene    │      │ GameScene    │
│  - UI 按钮    │      │  - TileMap   │
│  - 标题       │      │  - ECS World │
│              │      │  - Camera    │
└──────────────┘      │  - UI        │
                      │  - Tools     │
                      └──────────────┘
```

### 核心改进点

#### 1. **Application 统一生命周期**
- 封装窗口创建和 bgfx 初始化
- 管理主循环和帧率控制
- 提供全局服务访问（资源、事件、场景）

#### 2. **Scene 抽象游戏状态**
- 每个场景独立文件
- 生命周期回调（onEnter/onExit/onPause/onResume）
- 支持场景栈（可暂停、可恢复）

#### 3. **EventBus 统一事件分发**
- 基于 Signal/Slot 实现
- 解耦事件发送者和接收者
- 支持一对多订阅

#### 4. **ResourceManager 资源管理**
- 统一加载接口
- 自动引用计数
- 延迟加载和卸载

---

## 核心类设计

### 1. Application 类

```cpp
// src/engine/Application.hpp
namespace Tina::Engine {

class Application {
public:
    struct Config {
        int windowWidth = 1280;
        int windowHeight = 720;
        const char* windowTitle = "Tina";
        bool vsync = true;
        uint32_t msaa = 8;
    };

    Application(const Config& config);
    ~Application();

    // 主循环
    void run();
    void quit();

    // 访问核心系统
    Tina::os::WindowHandle window() const { return m_window; }
    ResourceManager& resources() { return *m_resources; }
    EventBus& events() { return *m_eventBus; }
    SceneManager& scenes() { return *m_sceneManager; }
    // 新增：全局着色器管理器（单例于应用生命周期）
    Renderer::ShaderManager& shaders() { return *m_shaderManager; }

    // 帧率信息
    float deltaTime() const { return m_deltaTime; }
    float fps() const { return m_fps; }

private:
    void init();
    void shutdown();
    void processEvents();
    void update(float dt);
    void render();

private:
    Config m_config;
    bool m_running = false;
    float m_deltaTime = 0.0f;
    float m_fps = 60.0f;

    Tina::os::WindowHandle m_window;
    Memory::UniquePtr<ResourceManager> m_resources;
    Memory::UniquePtr<EventBus> m_eventBus;
    Memory::UniquePtr<SceneManager> m_sceneManager;
};

} // namespace Tina::Engine
```

**职责**：
- ✅ 管理应用程序生命周期
- ✅ 初始化核心子系统
- ✅ 驱动主循环
- ✅ 提供全局服务访问点（事件、场景、资源、着色器）

---

### 2. Scene 基类

```cpp
// src/engine/Scene.hpp
namespace Tina::Engine {

class Scene {
public:
    virtual ~Scene() = default;

    // 生命周期回调
    virtual void onEnter() {}   // 场景进入时（一次）
    virtual void onExit() {}    // 场景退出时（一次）
    virtual void onPause() {}   // 场景暂停（push 新场景时）
    virtual void onResume() {}  // 场景恢复（pop 返回时）

    // 主循环
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    virtual void handleEvent(const Tina::os::Event& event) {}

    // 场景控制
    bool isActive() const { return m_active; }

protected:
    Application* app() const { return m_app; }  // 访问 Application

private:
    Application* m_app = nullptr;
    bool m_active = true;

    friend class SceneManager;
};

} // namespace Tina::Engine
```

**生命周期示例**：
```
主菜单 → 点击开始游戏 → 游戏场景 → 按 ESC → 暂停菜单 → 返回游戏

MenuScene::onEnter()
    ↓ push(GameScene)
MenuScene::onPause()
GameScene::onEnter()
    ↓ push(PauseScene)
GameScene::onPause()
PauseScene::onEnter()
    ↓ pop()
PauseScene::onExit()
GameScene::onResume()
```

---

### 3. SceneManager

```cpp
// src/engine/SceneManager.hpp
namespace Tina::Engine {

class SceneManager {
public:
    explicit SceneManager(Application* app);

    // 场景切换
    void push(Memory::UniquePtr<Scene> scene);     // 压入新场景（暂停当前）
    void pop();                                     // 弹出当前场景（恢复上一个）
    void replace(Memory::UniquePtr<Scene> scene);  // 替换当前场景

    // 主循环调用
    void update(float dt);
    void render();
    void handleEvent(const Tina::os::Event& event);

    // 查询
    Scene* currentScene() const;
    bool isEmpty() const { return m_scenes.empty(); }

private:
    Application* m_app;
    Container::Vector<Memory::UniquePtr<Scene>> m_scenes;  // 场景栈
};

} // namespace Tina::Engine
```

**使用示例**：
```cpp
// 从主菜单进入游戏
app.scenes().push(Memory::makeUnique<GameScene>());

// 暂停游戏（游戏场景不销毁，只是暂停）
app.scenes().push(Memory::makeUnique<PauseScene>());

// 从暂停返回游戏
app.scenes().pop();

// 从游戏返回主菜单（销毁游戏场景）
app.scenes().replace(Memory::makeUnique<MenuScene>());
```

---

### 4. 资源系统（更新）

为了解决多处重复加载/释放与生命周期不一致的问题，引入统一的资源中心与全局 Shader 管理：

- 全局 ShaderManager：由 Application 持有，着色器程序句柄在应用期统一创建/缓存/销毁。
- Resource 抽象：统一状态机（EMPTY/READY/FAILURE）+ 引用计数。
- ResourceManager：按路径缓存、异步加载、自动卸载，内建基础文件监视（mtime）。
- ResourceManagerHub：集中路由到具体资源管理器，支持增量热重载 `reload(path)` 与全量 `reloadAll()`。
- ResourceRef<T>：RAII 资源句柄，作用域结束自动释放引用，避免悬空指针。

内置资源类型：
- Texture2DResource（纹理）：使用 bimg_decode 解码；失败时创建 2x2 棋盘占位纹理；由 TextureManager 管理。
- FontResource（字体）：缓存字体字节，并按需为不同像素大小创建/复用 FT_Face；由 FontManager 管理。

TextRenderer 的接入方式：
- 初始化：`text.initialize(app.shaders(), app.resources());`
- 加载字体：`text.loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 24);`
- 说明：TextRenderer 不再直接持有 FreeType 库/Face，而是通过 FontResource 获取相应字号的 Face；字形图集仍在 TextRenderer 内部维护。

纹理接入建议：
- UI 图标、游戏贴图等统一通过 `app.resources().load<Texture2DResource>(path)` 获取；无需手动销毁 bgfx 纹理句柄。

---

### 4. EventBus（基于 Signal）

```cpp
// src/engine/EventBus.hpp
namespace Tina::Engine {

class EventBus {
public:
    // 窗口事件
    Core::Signal<int, int> onWindowResized;  // width, height
    Core::Signal<> onWindowClosed;

    // 输入事件（高层抽象）
    Core::Signal<int, int> onMouseClicked;   // x, y
    Core::Signal<float> onMouseWheel;        // delta
    Core::Signal<int> onKeyPressed;          // keycode

    // 游戏事件
    Core::Signal<float, float> onPlayerMoved;     // x, y
    Core::Signal<> onPlayerDied;
    Core::Signal<int> onScoreChanged;             // score

    // UI 事件
    Core::Signal<const std::string&> onButtonClicked;  // button name

    // 分发 OS 事件到 Signal
    void dispatch(const Tina::os::Event& event);
};

} // namespace Tina::Engine
```

**使用示例**：
```cpp
// 订阅事件
app.events().onPlayerDied.connect([]() {
    Log::info("玩家死亡！");
});

app.events().onPlayerDied.connect_member(&ui, &UI::showGameOver);

// 触发事件
app.events().onPlayerDied.emit();
```

---

### 5. GameScene（迁移后的 main.cpp 逻辑）

```cpp
// src/game/GameScene.hpp
namespace Tina::Game {

class GameScene : public Engine::Scene {
public:
    void onEnter() override;
    void onExit() override;
    void onPause() override;   // 暂停时停止更新
    void onResume() override;  // 恢复时继续更新

    void update(float dt) override;
    void render() override;
    void handleEvent(const Tina::os::Event& event) override;

private:
    void setupUI();
    void handleToolSelection(int tool);
    void handleToolUse(float worldX, float worldY);

private:
    // 游戏对象（从 main.cpp 迁移）
    Memory::UniquePtr<TileMap> m_tilemap;
    Memory::UniquePtr<ECS::World> m_ecsWorld;
    Memory::UniquePtr<Camera2D> m_camera;
    Memory::UniquePtr<Particles::ParticleSystem2D> m_particles;

    // 渲染器
    Memory::UniquePtr<Renderer::TileRenderer> m_tileRenderer;
    Memory::UniquePtr<UI::TextRenderer> m_textRenderer;
    Memory::UniquePtr<UI::UIRenderer> m_uiRenderer;

    // UI 组件
    Memory::UniquePtr<UI::UIToolbar> m_toolbar;
    Memory::UniquePtr<UI::UICharacterPanel> m_characterPanel;

    // 游戏状态
    int m_currentTool = 0;
    entt::entity m_playerEntity;
};

} // namespace Tina::Game
```

---

### 重构后的 main.cpp

```cpp
// src/main.cpp (简化到 ~40 行)
#include "engine/Application.hpp"
#include "game/GameScene.hpp"
#include "core/Log.hpp"

using namespace Tina;

int main(int argc, char* argv[]) {
    // 初始化日志
    Core::Log::InitWithFile("Tina", Core::Log::Level::Info,
                            "logs/tina.log", 10*1024*1024, 5, false);
    TINA_INFO("启动 Tina 游戏引擎");

    // 配置应用
    Engine::Application::Config config;
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.windowTitle = "Tina - 2D Sandbox Game";
    config.vsync = true;
    config.msaa = 8;

    try {
        // 创建应用
        Engine::Application app(config);

        // 创建并推入游戏场景
        auto gameScene = Memory::makeUnique<Game::GameScene>();
        app.scenes().push(std::move(gameScene));

        // 运行主循环
        app.run();

        TINA_INFO("游戏正常退出");
        return 0;

    } catch (const std::exception& e) {
        TINA_ERROR("游戏崩溃: {}", e.what());
        return -1;
    }
}
```

---

## 进阶设计（已实现特性）

### 6. ResourceManager 资源管理系统

#### 设计概览

资源管理系统已完整实现（[src/engine/Resource.hpp](../src/engine/Resource.hpp)），采用**状态机 + 引用计数 + 异步加载**的设计。

#### 核心组件

##### 6.1 Resource 抽象类

```cpp
// src/engine/Resource.hpp
class Resource {
public:
    enum class State { EMPTY = 0, READY, FAILURE };

    explicit Resource(Path path);
    virtual ~Resource() = default;

    // 状态管理
    State getState() const { return m_state; }
    const Path& getPath() const { return m_path; }

    // 引用计数
    u32 incRefCount();  // 增加引用
    u32 decRefCount();  // 减少引用

    // 资源类型（用于路由到对应管理器）
    virtual ResourceType getType() const = 0;

    // 异步加载接口
    void requestLoad(FileSystem& fs);
    void unloadNow();

protected:
    // 派生类实现：解析数据、释放资源
    virtual bool load(const FileSystem::Content& blob) = 0;
    virtual void unload() = 0;

private:
    Path m_path;
    State m_state = State::EMPTY;
    u32 m_refcount = 0;
};
```

**状态转换**：
```
EMPTY → (requestLoad) → READY   ✅ 加载成功
      → (requestLoad) → FAILURE ❌ 加载失败
      ← (unloadNow)   ← READY   🔄 卸载资源
```

---

##### 6.2 ResourceManager 管理器基类

```cpp
class ResourceManager {
public:
    explicit ResourceManager(FileSystem& fs);

    // 加载资源（自动缓存、引用计数）
    Resource* load(const Path& path);

    // 卸载资源（引用计数归零时自动卸载）
    void unload(Resource& resource);

    // 热重载所有资源
    void reloadAll();

    // 每帧驱动文件系统回调
    void update();

protected:
    // 派生类实现：创建具体资源实例
    virtual Resource* createResource(const Path& path) = 0;

private:
    FileSystem& m_fs;
    HashMap<String, UniquePtr<Resource>> m_resources;  // 路径缓存
};
```

**工作流程**：
```cpp
// 1. 加载资源
Resource* tex = textureMgr.load("sprites/player.png");
// → 首次加载：创建 Resource → 异步读取文件 → 回调解析数据
// → 重复加载：返回缓存 + 增加引用计数

// 2. 使用资源
if (tex->getState() == Resource::State::READY) {
    render(tex);
}

// 3. 卸载资源
textureMgr.unload(*tex);
// → 减少引用计数 → 计数归零时调用 unloadNow()
```

---

##### 6.3 FileSystem 异步文件接口

```cpp
struct FileSystem {
    using Content = Vector<u8>;
    using ContentCallback = std::function<void(const Content&, bool success)>;

    struct AsyncHandle { u64 id; bool valid() const; };

    // 异步读取文件（后台线程）
    virtual AsyncHandle getContent(const Path& file, ContentCallback cb) = 0;

    // 每帧驱动回调（主线程）
    virtual void processCallbacks() = 0;
};

// 创建文件系统（实现在 FileSystem.cpp）
UniquePtr<FileSystem> CreateFileSystem();
```

**异步加载流程**：
```
主线程                        后台线程
  │                              │
  ├──► getContent("file.txt")    │
  │    └─ 创建任务 ──────────────┼──► 读取文件
  │                              │    解码数据
  ├──► processCallbacks()        │
  │    └─ 执行回调 ◄─────────────┴─ 完成通知
  │       └─ Resource::load()
  │           └─ 状态 → READY
```

---

##### 6.4 ResourceManagerHub 类型路由

```cpp
class ResourceManagerHub {
public:
    // 注册管理器
    void add(ResourceType type, ResourceManager* manager);

    // 加载资源（自动路由到对应管理器）
    Resource* load(ResourceType type, const Path& path);

    // 全局操作
    void reloadAll();  // 热重载所有资源
    void update();     // 驱动所有管理器

private:
    HashMap<u64, ResourceManager*> m_rms;  // 类型 → 管理器
};
```

**使用示例**：
```cpp
// 注册管理器
ResourceManagerHub hub;
hub.add(TextureResource::TYPE, &textureMgr);
hub.add(FontResource::TYPE, &fontMgr);
hub.add(ShaderResource::TYPE, &shaderMgr);

// 加载资源
Resource* tex = hub.load(TextureResource::TYPE, "player.png");
Resource* font = hub.load(FontResource::TYPE, "font.ttf");
```

---

#### 示例：BlobResource 实现

```cpp
// Blob 资源（原样字节，用于测试）
struct BlobResource : Resource {
    static inline const ResourceType TYPE{"blob"};

    using Resource::Resource;

    ResourceType getType() const override { return TYPE; }

    bool load(const FileSystem::Content& blob) override {
        data = blob;  // 直接复制字节
        return true;
    }

    void unload() override {
        data.clear();
    }

    Vector<u8> data;  // 文件内容
};

// Blob 管理器
struct BlobManager : ResourceManager {
    using ResourceManager::ResourceManager;

    Resource* createResource(const Path& path) override {
        return new BlobResource(path);
    }
};
```

---

#### 扩展：自定义资源类型

要添加新的资源类型（如纹理、字体），需实现：

##### 步骤 1：定义资源类

```cpp
struct TextureResource : Resource {
    static inline const ResourceType TYPE{"texture"};

    using Resource::Resource;

    ResourceType getType() const override { return TYPE; }

    bool load(const FileSystem::Content& blob) override {
        // 解析图片格式（PNG/JPG）
        // 创建 bgfx 纹理句柄
        m_handle = bgfx::createTexture(...);
        return bgfx::isValid(m_handle);
    }

    void unload() override {
        if (bgfx::isValid(m_handle)) {
            bgfx::destroy(m_handle);
        }
    }

    bgfx::TextureHandle getHandle() const { return m_handle; }

private:
    bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
};
```

##### 步骤 2：定义管理器

```cpp
struct TextureManager : ResourceManager {
    using ResourceManager::ResourceManager;

    Resource* createResource(const Path& path) override {
        return new TextureResource(path);
    }

    // 便捷接口（类型转换）
    TextureResource* loadTexture(const Path& path) {
        return static_cast<TextureResource*>(load(path));
    }
};
```

##### 步骤 3：注册到 Hub

```cpp
FileSystem* fs = CreateFileSystem();
TextureManager texMgr(*fs);
ResourceManagerHub hub;
hub.add(TextureResource::TYPE, &texMgr);
```

---

#### 集成到 Application

```cpp
class Application {
public:
    // 访问资源系统（未来集成）
    FileSystem& fileSystem() { return *m_fileSystem; }
    ResourceManagerHub& resources() { return *m_resourceHub; }

private:
    UniquePtr<FileSystem> m_fileSystem;
    UniquePtr<ResourceManagerHub> m_resourceHub;

    // 主循环中驱动文件系统
    void update(float dt) {
        m_resourceHub->update();  // 处理异步加载回调
        m_sceneManager->update(dt);
    }
};
```

---

#### 设计优势

| 特性 | 实现方式 | 收益 |
|------|---------|------|
| **自动缓存** | 按路径哈希缓存 | 避免重复加载 |
| **引用计数** | incRefCount/decRefCount | 自动内存管理 |
| **异步加载** | 后台线程 + 主线程回调 | 避免卡顿 |
| **类型路由** | ResourceType + Hub | 扩展性强 |
| **热重载** | reloadAll() | 开发迭代快 |
| **状态机** | EMPTY/READY/FAILURE | 可靠性高 |

---

#### 当前状态

✅ **已实现**：Resource、ResourceManager、FileSystem、Hub、BlobResource
⚠️ **未集成到 Application**：需要在 Application 中创建 FileSystem 和 Hub
🔜 **待扩展**：TextureResource、FontResource、AudioResource 等

---

### 7. 线程模型说明

#### 当前实现：单线程 + 异步I/O

```
┌─────────────────────────────────────────────┐
│           主线程（主循环）                    │
│  ┌──────────────────────────────────────┐  │
│  │ 1. processEvents()  处理输入           │  │
│  │ 2. update(dt)       更新逻辑           │  │
│  │    └─ ECS Systems                     │  │
│  │    └─ Physics                         │  │
│  │    └─ ResourceHub.update()  ◄─┐      │  │
│  │       └─ processCallbacks()    │      │  │
│  │ 3. render()         提交渲染指令      │  │
│  │ 4. bgfx::frame()    提交帧            │  │
│  └──────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
                   │
                   │ 文件加载任务
                   ▼
┌─────────────────────────────────────────────┐
│         后台线程（文件I/O）                   │
│  - 读取文件                                  │
│  - 解压/解码                                 │
│  - 完成后通知主线程 ─────────────────────┘  │
└─────────────────────────────────────────────┘
```

#### 线程安全策略

**主线程职责**：
- 游戏逻辑（ECS、物理、AI）
- 渲染提交（bgfx）
- 事件分发（EventBus）
- 资源回调处理（FileSystem::processCallbacks）

**后台线程职责**：
- 文件I/O（FileSystem::getContent）
- 数据解码（PNG、JSON 解析）
- 不访问游戏状态

**同步机制**：
- 主线程每帧调用 `ResourceHub.update()` 处理回调
- 后台线程完成文件加载后，将结果放入队列
- 回调在主线程中执行，避免竞态条件

**优势**：
- ✅ 简单可靠（无锁设计）
- ✅ 避免卡顿（文件I/O异步）
- ✅ 易于调试（逻辑单线程）

---

### 8. 实现细节与最佳实践

#### 8.1 帧时间控制（deltaTime）

**防止超大跳帧**（[Application.cpp:136-139](../src/engine/Application.cpp#L136-L139)）：
```cpp
// 限制 deltaTime 防止超大跳帧（例如调试器断点）
if (m_deltaTime > 0.1f) {
    m_deltaTime = 0.1f;  // 最大 100ms = 10 FPS
}
```

**原因**：
- 调试时断点会导致帧时间超长
- 物理模拟需要稳定的时间步长
- 避免"螺旋死亡"（lag → 更新慢 → 更多 lag）

---

#### 8.2 DPI 缩放支持

**获取真实像素尺寸**（[Application.cpp:71](../src/engine/Application.cpp#L71)）：
```cpp
// 窗口逻辑尺寸 vs 实际像素尺寸
int logicW = config.windowWidth;   // 例如：1280
int logicH = config.windowHeight;  // 例如：720

// 获取实际像素尺寸（高DPI显示器可能是 2560x1440）
SDL_GetWindowSizeInPixels(window, &m_pixelWidth, &m_pixelHeight);

// 渲染使用像素尺寸
bgfx::setViewRect(0, 0, 0, m_pixelWidth, m_pixelHeight);
```

**原因**：
- macOS Retina 屏幕：逻辑尺寸 1280x720，像素尺寸 2560x1440
- Windows 高DPI：150% 缩放 → 像素尺寸放大 1.5 倍
- bgfx 渲染需要真实像素尺寸，否则模糊

---

#### 8.3 错误处理策略

**初始化失败检查**（[Application.cpp:38-41](../src/engine/Application.cpp#L38-L41)）：
```cpp
if (m_window == INVALID_WINDOW_HANDLE) {
    TINA_ERROR("创建窗口失败");
    return;  // 提前退出
}

if (!bgfx::init(bgfxInit)) {
    TINA_ERROR("bgfx 初始化失败");
    destroyWindow(m_window);  // 清理已创建的资源
    return;
}
```

**场景空检查**（[Application.cpp:120-123](../src/engine/Application.cpp#L120-L123)）：
```cpp
if (m_sceneManager->isEmpty()) {
    TINA_ERROR("未设置初始场景。请使用 app.scenes().push(scene) 添加场景");
    return;  // 防止崩溃
}
```

**资源加载失败**（[Resource.hpp:69](../src/engine/Resource.hpp#L69)）：
```cpp
void requestLoad(FileSystem& fs) {
    m_handle = fs.getContent(m_path, [this](const Content& data, bool ok) {
        if (!ok) { fail(); return; }           // 文件读取失败
        if (!load(data)) { fail(); return; }   // 数据解析失败
        m_state = State::READY;
    });
}

void fail() { m_state = State::FAILURE; }  // 标记失败状态
```

**原则**：
- ✅ 提前检查，避免空指针崩溃
- ✅ 清理已分配资源，避免泄漏
- ✅ 记录详细日志，便于调试
- ✅ 使用状态机标记失败，上层可查询

---

#### 8.4 场景生命周期管理

**push 新场景**（[SceneManager.cpp:22-24](../src/engine/SceneManager.cpp#L22-L24)）：
```cpp
// 1. 暂停当前场景（保留状态）
if (!m_scenes.empty()) {
    m_scenes.back()->onPause();
    m_scenes.back()->m_active = false;
}

// 2. 进入新场景
scene->m_app = m_app;  // 注入 Application 引用
scene->onEnter();
m_scenes.push_back(std::move(scene));
```

**pop 返回上一场景**（[SceneManager.cpp:38-43](../src/engine/SceneManager.cpp#L38-L43)）：
```cpp
// 1. 退出当前场景（销毁资源）
m_scenes.back()->onExit();
m_scenes.pop_back();

// 2. 恢复上一场景（继续更新）
if (!m_scenes.empty()) {
    m_scenes.back()->m_active = true;
    m_scenes.back()->onResume();
}
```

**生命周期时序**：
```
GameScene                          PauseScene
    │                                  │
    ├─ onEnter()                       │
    ├─ update(dt) ────────┐            │
    ├─ render()           │            │
    ├─ update(dt)         │            │
    ├─ render()           │            │
    │                     │            │
    ├─ onPause() ◄────── push(PauseScene)
    │ (停止更新)          │            ├─ onEnter()
    │                     │            ├─ update(dt)
    │                     │            ├─ render()
    │                     │            ├─ onExit() ◄──── pop()
    ├─ onResume()                      │
    ├─ update(dt) ────────┘            │
    ├─ render()
    ├─ onExit()
```

---

### 9. EventBus 当前实现与限制

#### 当前实现特性

**立即分发模式**（[EventBus.cpp:5-35](../src/engine/EventBus.cpp#L5-L35)）：
```cpp
void EventBus::dispatchOSEvent(const Event& event) {
    switch (event.type) {
        case Event::KEY:
            if (event.key.down)
                onKeyPressed.emit(keycode, isRepeat);  // 立即调用所有订阅者
            break;
        // ...
    }
}
```

**Signal 基于立即调用**：
```cpp
// 订阅事件
app.events().onKeyPressed.connect([](int key, bool repeat) {
    Log::info("按键: {}", key);  // 立即执行
});

// 触发事件
app.events().onKeyPressed.emit(SDLK_SPACE, false);
// → 同步调用所有 connect 的回调
```

---

#### 当前限制

🟡 **限制 1：无事件优先级**
- 订阅者按 `connect()` 顺序执行
- 无法控制 UI 事件优先于游戏逻辑

🟡 **限制 2：无事件队列**
- 立即分发，无法延迟处理
- 无法在帧末统一处理事件

🟡 **限制 3：鼠标坐标未传递**（[EventBus.cpp:22-24](../src/engine/EventBus.cpp#L22-L24)）
```cpp
onMouseButtonPressed.emit(button, 0, 0);  // ← 坐标固定为 (0, 0)
```
**原因**：OS::Event 中未存储鼠标按键时的坐标，需要结合 `MOUSE_MOVE` 事件维护当前位置。

🟡 **限制 4：跨场景事件处理不明确**
- 当前只分发给活跃场景（栈顶）
- 暂停的场景不接收事件

---

#### 未来扩展方向

##### 扩展 1：事件优先级

```cpp
class EventBus {
    // 高优先级订阅（先执行）
    void connectPriority(Signal& signal, Callback cb, int priority);
};
```

##### 扩展 2：事件队列

```cpp
class EventBus {
    void enqueueEvent(Event event);  // 入队
    void processQueue();             // 帧末处理
};
```

##### 扩展 3：事件过滤

```cpp
class EventBus {
    // 订阅时指定过滤器
    void connectFiltered(Signal& signal, Callback cb, FilterFn filter);
};
```

**当前建议**：
- ✅ 使用当前实现满足基本需求
- ✅ 复杂事件逻辑在 Scene 层处理
- ✅ 等待实际需求再扩展高级特性

---

### 10. 场景数据传递设计（未来扩展）

#### 当前状态

⚠️ **当前未实现**：场景之间无法直接传递数据（如游戏分数、玩家状态）。

#### 问题场景

```cpp
// 场景 A：游戏结束，需要将分数传递给场景 B
class GameScene : public Scene {
    void onPlayerDied() {
        int finalScore = 1000;
        // ❌ 如何将 finalScore 传给 GameOverScene？
        app()->scenes().push(Memory::makeUnique<GameOverScene>());
    }
};

// 场景 B：需要显示场景 A 传来的分数
class GameOverScene : public Scene {
    void onEnter() override {
        // ❌ 如何获取 finalScore？
        displayScore(???);
    }
};
```

---

#### 设计方案（未来实现）

##### 方案 1：构造函数传参

```cpp
// GameOverScene 接收数据
class GameOverScene : public Scene {
public:
    explicit GameOverScene(int score) : m_score(score) {}

    void onEnter() override {
        displayScore(m_score);
    }

private:
    int m_score;
};

// GameScene 传递数据
void onPlayerDied() {
    app()->scenes().push(
        Memory::makeUnique<GameOverScene>(m_finalScore)
    );
}
```

**优点**：简单直接，类型安全
**缺点**：只能在场景创建时传递，无法用于 `pop()` 返回

---

##### 方案 2：SceneContext 共享数据

```cpp
// 定义场景上下文
struct SceneContext {
    int playerScore = 0;
    string playerName = "Player";
    GameConfig config;
};

// SceneManager 持有上下文
class SceneManager {
public:
    SceneContext& context() { return m_context; }

private:
    SceneContext m_context;
};

// 场景 A：写入数据
class GameScene : public Scene {
    void onPlayerDied() {
        app()->scenes().context().playerScore = m_finalScore;
        app()->scenes().push(Memory::makeUnique<GameOverScene>());
    }
};

// 场景 B：读取数据
class GameOverScene : public Scene {
    void onEnter() override {
        int score = app()->scenes().context().playerScore;
        displayScore(score);
    }
};
```

**优点**：支持 `push/pop/replace` 所有场景切换方式
**缺点**：全局共享状态，需要手动管理数据生命周期

---

##### 方案 3：EventBus 事件通知

```cpp
// 定义场景事件
struct SceneDataEvent {
    string key;
    int value;
};

class EventBus {
public:
    Core::Signal<const SceneDataEvent&> onSceneData;
};

// 场景 A：发送数据
class GameScene : public Scene {
    void onPlayerDied() {
        app()->scenes().push(Memory::makeUnique<GameOverScene>());

        // 发送分数
        app()->events().onSceneData.emit({"finalScore", m_finalScore});
    }
};

// 场景 B：订阅数据
class GameOverScene : public Scene {
    void onEnter() override {
        app()->events().onSceneData.connect([this](const SceneDataEvent& e) {
            if (e.key == "finalScore") {
                displayScore(e.value);
            }
        });
    }
};
```

**优点**：解耦场景，支持一对多通知
**缺点**：类型不安全（需要手动匹配 key），订阅管理复杂

---

#### 推荐方案

**阶段 1**：使用**构造函数传参**（方案 1）
- 适用于 `push` 和 `replace`
- 简单可靠，类型安全

**阶段 2**：引入 **SceneContext**（方案 2）
- 适用于复杂数据共享
- 统一管理游戏全局状态

**阶段 3**：扩展 **EventBus**（方案 3）
- 适用于跨场景通知
- 解耦场景依赖

---

### 11. 测试策略

#### 测试分层

```
┌──────────────────────────────────────┐
│     单元测试（Unit Tests）            │
│  - Resource 状态机                    │
│  - ResourceManager 缓存逻辑           │
│  - SceneManager 场景栈操作            │
│  - Signal 订阅/触发                   │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│     集成测试（Integration Tests）     │
│  - Application 初始化流程             │
│  - EventBus → Scene 事件分发          │
│  - FileSystem 异步加载                │
│  - 场景生命周期（onEnter/onExit）    │
└──────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│     功能测试（Functional Tests）      │
│  - 完整游戏流程                       │
│  - 场景切换（Menu → Game → Pause）   │
│  - 资源加载和卸载                     │
│  - 性能测试（FPS、内存）              │
└──────────────────────────────────────┘
```

---

#### 单元测试示例

##### 测试 SceneManager

```cpp
// tests/engine/SceneManager_test.cpp
#include <gtest/gtest.h>
#include "engine/SceneManager.hpp"

class MockScene : public Scene {
public:
    bool entered = false;
    bool exited = false;
    bool paused = false;
    bool resumed = false;

    void onEnter() override { entered = true; }
    void onExit() override { exited = true; }
    void onPause() override { paused = true; }
    void onResume() override { resumed = true; }

    void update(float dt) override {}
    void render() override {}
};

TEST(SceneManagerTest, PushScene) {
    Application app(config);
    auto scene = Memory::makeUnique<MockScene>();
    MockScene* ptr = scene.get();

    app.scenes().push(std::move(scene));

    EXPECT_TRUE(ptr->entered);  // onEnter 被调用
    EXPECT_EQ(app.scenes().currentScene(), ptr);
}

TEST(SceneManagerTest, PopScene) {
    Application app(config);
    auto scene1 = Memory::makeUnique<MockScene>();
    auto scene2 = Memory::makeUnique<MockScene>();
    MockScene* ptr1 = scene1.get();
    MockScene* ptr2 = scene2.get();

    app.scenes().push(std::move(scene1));
    app.scenes().push(std::move(scene2));

    EXPECT_TRUE(ptr1->paused);  // 场景1被暂停
    EXPECT_TRUE(ptr2->entered); // 场景2进入

    app.scenes().pop();

    EXPECT_TRUE(ptr2->exited);  // 场景2退出
    EXPECT_TRUE(ptr1->resumed); // 场景1恢复
}
```

##### 测试 Resource 状态机

```cpp
// tests/engine/Resource_test.cpp
TEST(ResourceTest, StateTransition) {
    FileSystem* fs = CreateFileSystem();
    BlobManager manager(*fs);

    // 初始状态：EMPTY
    Resource* res = manager.load("test.txt");
    EXPECT_EQ(res->getState(), Resource::State::EMPTY);

    // 驱动异步加载
    manager.update();  // 处理回调

    // 加载成功：READY
    EXPECT_EQ(res->getState(), Resource::State::READY);

    // 卸载资源
    res->unloadNow();
    EXPECT_EQ(res->getState(), Resource::State::EMPTY);
}

TEST(ResourceTest, ReferenceCount) {
    BlobManager manager(*fs);

    Resource* res1 = manager.load("test.txt");
    Resource* res2 = manager.load("test.txt");  // 返回缓存

    EXPECT_EQ(res1, res2);  // 同一个对象
    // 引用计数 = 2

    manager.unload(*res1);  // 减少引用计数
    EXPECT_EQ(res1->getState(), Resource::State::READY);  // 还有引用

    manager.unload(*res2);  // 引用计数归零
    EXPECT_EQ(res1->getState(), Resource::State::EMPTY);  // 自动卸载
}
```

---

#### 集成测试示例

##### 测试 Application 生命周期

```cpp
// tests/engine/Application_test.cpp
TEST(ApplicationTest, FullLifecycle) {
    Application::Config config;
    config.windowWidth = 800;
    config.windowHeight = 600;

    Application app(config);

    // 初始化成功
    EXPECT_NE(app.window(), nullptr);
    EXPECT_EQ(app.windowWidth(), 800);

    // 添加测试场景
    class TestScene : public Scene {
        void update(float dt) override {
            if (dt > 0) {
                app()->quit();  // 第一帧退出
            }
        }
        void render() override {}
    };

    app.scenes().push(Memory::makeUnique<TestScene>());

    // 运行主循环（应该立即退出）
    app.run();

    // 清理成功（不崩溃）
}
```

##### 测试 EventBus 分发

```cpp
// tests/engine/EventBus_test.cpp
TEST(EventBusTest, KeyEventDispatch) {
    Application app(config);

    int keyPressed = -1;
    app.events().onKeyPressed.connect([&](int key, bool repeat) {
        keyPressed = key;
    });

    // 模拟按键事件
    Tina::os::Event event;
    event.type = Tina::os::Event::Type::KEY;
    event.key.down = true;
    event.key.key_code = SDLK_SPACE;
    event.key.is_repeat = false;

    app.events().dispatchOSEvent(event);

    EXPECT_EQ(keyPressed, SDLK_SPACE);  // 事件被分发
}
```

---

#### 功能测试示例

##### 测试场景切换流程

```cpp
// tests/game/SceneSwitching_test.cpp
TEST(GameTest, SceneSwitchingFlow) {
    Application app(config);

    // 1. 进入主菜单
    app.scenes().push(Memory::makeUnique<MenuScene>());
    EXPECT_EQ(app.scenes().sceneCount(), 1);

    // 模拟点击"开始游戏"
    app.scenes().push(Memory::makeUnique<GameScene>());
    EXPECT_EQ(app.scenes().sceneCount(), 2);

    // 模拟按 ESC（暂停游戏）
    app.scenes().push(Memory::makeUnique<PauseScene>());
    EXPECT_EQ(app.scenes().sceneCount(), 3);

    // 模拟点击"返回游戏"
    app.scenes().pop();
    EXPECT_EQ(app.scenes().sceneCount(), 2);

    // 模拟退出到主菜单
    app.scenes().replace(Memory::makeUnique<MenuScene>());
    EXPECT_EQ(app.scenes().sceneCount(), 1);
}
```

---

#### 测试工具推荐

| 工具 | 用途 | 链接 |
|------|------|------|
| **Google Test** | C++ 单元测试框架 | [GitHub](https://github.com/google/googletest) |
| **Catch2** | 轻量级测试框架 | [GitHub](https://github.com/catchorg/Catch2) |
| **Valgrind** | 内存泄漏检测 | [官网](https://valgrind.org/) |
| **Tracy** | 性能分析工具 | [GitHub](https://github.com/wolfpld/tracy) |
| **RenderDoc** | 渲染调试工具 | [官网](https://renderdoc.org/) |

---

### 12. 调试工具设计（未来实现）

#### 12.1 场景树可视化

**目标**：实时查看场景栈状态

```
┌────────────────────────────────┐
│   Scene Stack (Debug View)     │
├────────────────────────────────┤
│ [2] PauseScene (Active)        │  ← 当前场景
│     - UI: 3 buttons            │
│     - State: paused            │
├────────────────────────────────┤
│ [1] GameScene (Paused)         │  ← 暂停的场景
│     - Entities: 150            │
│     - Tilemap: 100x50          │
├────────────────────────────────┤
│ [0] MenuScene (Paused)         │  ← 底层场景
│     - UI: 5 buttons            │
└────────────────────────────────┘
```

**实现方式**：
```cpp
class SceneManager {
public:
    void debugPrintStack() const {
        for (size_t i = 0; i < m_scenes.size(); ++i) {
            Scene* scene = m_scenes[i].get();
            Log::info("[{}] {} ({})",
                i,
                typeid(*scene).name(),
                scene->isActive() ? "Active" : "Paused"
            );
        }
    }
};
```

---

#### 12.2 事件追踪器

**目标**：记录所有 Signal 触发和订阅

```
┌──────────────────────────────────────┐
│     Event Log (Last 10 events)       │
├──────────────────────────────────────┤
│ [10:23:15.123] onKeyPressed(SPACE)   │
│   └─ Listener 1: GameScene::jump     │
│   └─ Listener 2: DebugCamera::toggle │
├──────────────────────────────────────┤
│ [10:23:15.456] onMouseMoved(10, 5)   │
│   └─ Listener 1: UI::onHover         │
└──────────────────────────────────────┘
```

**实现方式**：
```cpp
class EventBus {
public:
    // 启用事件日志
    void enableEventLog(bool enable) { m_logEvents = enable; }

    // 记录事件
    void logEvent(const string& eventName) {
        if (m_logEvents) {
            Log::info("Event: {}", eventName);
        }
    }

private:
    bool m_logEvents = false;
};
```

---

#### 12.3 资源监控面板

**目标**：查看资源加载状态和内存占用

```
┌─────────────────────────────────────────┐
│     Resource Monitor                    │
├─────────────────────────────────────────┤
│ Loaded Resources: 15                    │
│ Total Memory: 45.2 MB                   │
├─────────────────────────────────────────┤
│ sprites/player.png     [READY]  2.5 MB  │
│ fonts/arial.ttf        [READY]  1.2 MB  │
│ sounds/bgm.ogg         [LOADING] ...    │
│ textures/tileset.png   [FAILURE] ERROR  │
└─────────────────────────────────────────┘
```

**实现方式**：
```cpp
class ResourceManagerHub {
public:
    struct ResourceStats {
        int totalCount = 0;
        int readyCount = 0;
        int loadingCount = 0;
        int failureCount = 0;
        size_t totalMemory = 0;
    };

    ResourceStats getStats() const {
        // 遍历所有管理器，统计资源状态
    }
};
```

---

#### 12.4 性能监控器

**目标**：实时显示 FPS、帧时间、内存占用

```
┌─────────────────────────────────────┐
│   Performance Monitor               │
├─────────────────────────────────────┤
│ FPS: 60.2 (avg: 59.8)               │
│ Frame Time: 16.5 ms                 │
│ Memory: 128.5 MB / 512 MB           │
├─────────────────────────────────────┤
│ CPU: 35%  ██████░░░░░░░░░░░░░░░░    │
│ GPU: 42%  ████████░░░░░░░░░░░░░░    │
└─────────────────────────────────────┘
```

**实现方式**：
```cpp
class Application {
public:
    struct PerformanceStats {
        float currentFPS;
        float avgFPS;
        float deltaTime;
        size_t memoryUsed;
    };

    PerformanceStats getStats() const {
        return {m_fps, m_avgFPS, m_deltaTime, getMemoryUsage()};
    }
};
```

---

#### 12.5 ImGui 集成

**推荐方案**：使用 [Dear ImGui](https://github.com/ocornut/imgui) 实现调试 UI

```cpp
// 调试窗口示例
void renderDebugUI() {
    if (ImGui::Begin("Debug Panel")) {
        if (ImGui::CollapsingHeader("Scene Stack")) {
            app->scenes().debugPrintStack();
        }

        if (ImGui::CollapsingHeader("Performance")) {
            auto stats = app->getStats();
            ImGui::Text("FPS: %.1f", stats.currentFPS);
            ImGui::Text("Frame Time: %.2f ms", stats.deltaTime * 1000);
        }

        if (ImGui::CollapsingHeader("Resources")) {
            auto stats = app->resources().getStats();
            ImGui::Text("Loaded: %d", stats.totalCount);
            ImGui::Text("Memory: %.2f MB", stats.totalMemory / 1024.0 / 1024.0);
        }
    }
    ImGui::End();
}
```

---

## 实施路线图

### 当前进度总结

✅ **已完成阶段**：
- ✅ 阶段 1：基础架构（Application、Scene、SceneManager、EventBus）
- ✅ 阶段 2：迁移游戏逻辑（GameScene 实现）
- ⚠️ 阶段 3：集成 Signal 系统（EventBus 基本实现，未全面应用）
- ❌ 阶段 4：扩展场景（MenuScene、PauseScene 未实现）
- ❌ 阶段 5：资源管理（Resource.hpp 已实现，未集成到 Application）

---

### 阶段 1：基础架构（✅ 已完成）

#### 任务清单
- [x] 创建 `src/engine/` 目录
- [x] 实现 `Application` 类（217 行）
  - [x] 窗口创建（SDL3）
  - [x] bgfx 初始化
  - [x] 主循环驱动（帧率控制、事件处理）
- [x] 实现 `Scene` 基类（73 行）
  - [x] 生命周期回调（onEnter/onExit/onPause/onResume）
  - [x] 主循环接口（update/render/handleEvent）
- [x] 实现 `SceneManager`（100 行）
  - [x] 场景栈管理（push/pop/replace/clear）
  - [x] 生命周期触发
- [x] 实现 `EventBus`（39 行）
  - [x] 基于 Signal 的事件分发
  - [x] OS 事件转换（键盘、鼠标、窗口）

#### 验收标准（✅ 已通过）

| 标准 | 验证方法 | 结果 |
|------|---------|------|
| **窗口创建成功** | 启动程序，窗口正常显示 | ✅ 通过 |
| **bgfx 初始化成功** | 查看日志："bgfx 初始化成功 - 渲染器: xxx" | ✅ 通过 |
| **帧率稳定** | FPS ≥ 59.5（预期 60）| ✅ 通过 |
| **场景生命周期正确** | 测试场景的 onEnter/onExit 被调用 | ✅ 通过 |
| **事件分发正常** | 按键/鼠标事件触发 Signal | ✅ 通过 |
| **无内存泄漏** | Valgrind 检查（或 Visual Studio 检测）| ⚠️ 待验证 |

#### 实际实现
- [Application.cpp](../src/engine/Application.cpp) - 217 行
- [Scene.hpp](../src/engine/Scene.hpp) - 73 行
- [SceneManager.cpp](../src/engine/SceneManager.cpp) - 100 行
- [EventBus.cpp](../src/engine/EventBus.cpp) - 39 行

---

### 阶段 2：迁移游戏逻辑（✅ 已完成）

#### 任务清单
- [x] 创建 `GameScene` 类
- [x] 将 main.cpp 的初始化代码迁移到 `GameScene::onEnter()`
- [x] 将 main.cpp 的更新代码迁移到 `GameScene::update()`
- [x] 将 main.cpp 的渲染代码迁移到 `GameScene::render()`
- [x] 将 main.cpp 的事件处理迁移到 `GameScene::handleEvent()`
- [x] 简化 main.cpp 为启动入口

#### 迁移映射表

| main.cpp 代码段 | 迁移目标 | 状态 |
|----------------|---------|------|
| 创建 TileMap | GameScene::onEnter() | ✅ 完成 |
| 创建 ECS World | GameScene::onEnter() | ✅ 完成 |
| 创建 Camera | GameScene::onEnter() | ✅ 完成 |
| 创建 UI | GameScene::onEnter() | ✅ 完成 |
| 键盘/鼠标事件 | GameScene::handleEvent() | ✅ 完成 |
| 工具逻辑 | GameScene::handleToolUse() | ✅ 完成 |
| ECS 更新 | GameScene::update() | ✅ 完成 |
| 液体更新 | GameScene::update() | ✅ 完成 |
| 渲染地形 | GameScene::render() | ✅ 完成 |
| 渲染 UI | GameScene::render() | ✅ 完成 |

#### 验收标准（✅ 已通过）

| 标准 | 验证方法 | 结果 |
|------|---------|------|
| **游戏正常运行** | 启动后能进入游戏场景 | ✅ 通过 |
| **玩家控制正常** | 移动、跳跃、工具切换响应 | ✅ 通过 |
| **渲染正确** | 瓦片、角色、UI 正常显示 | ✅ 通过 |
| **性能无回退** | FPS 与重构前一致 | ✅ 通过 |
| **main.cpp 简化** | 行数 < 100 行 | ⚠️ 待验证 |

#### 实际实现
- [GameScene.cpp](../src/game/GameScene.cpp) - 实现文件
- [GameScene.hpp](../src/game/GameScene.hpp) - 87 行头文件

---

### 阶段 3：集成 Signal 系统（⚠️ 部分完成）

#### 任务清单
- [x] EventBus 实现基础事件列表
  - [x] 窗口事件（onWindowResized, onWindowClosed）
  - [x] 键盘事件（onKeyPressed, onKeyReleased）
  - [x] 鼠标事件（onMouseButtonPressed, onMouseMoved, onMouseWheel）
- [ ] UI 组件改用 Signal
  - [ ] UIButton 使用 Signal 替代函数指针
  - [ ] UIToolbar 使用 EventBus
- [ ] 游戏事件改用 EventBus
  - [ ] onPlayerMoved、onPlayerJumped 实际触发
  - [ ] onToolUsed 连接到工具系统

#### 当前状态

✅ **已实现**：
- EventBus 基于 Signal 的事件分发
- OS 事件自动转换为 Signal

⚠️ **部分完成**：
- UI 组件仍使用函数指针（`std::function<>`）
- 游戏事件定义了但未实际使用

❌ **未实现**：
- Signal 连接管理策略（何时 connect/disconnect）
- 事件优先级和过滤

#### 验收标准（待完成）

| 标准 | 验证方法 | 结果 |
|------|---------|------|
| **UI 事件解耦** | 按钮点击通过 Signal 通知 | ⚠️ 待实现 |
| **游戏事件触发** | onPlayerJumped 被正确 emit | ⚠️ 待实现 |
| **无内存泄漏** | Signal 订阅正确断开 | ⚠️ 待验证 |

#### 后续工作
1. 为 UIButton 添加 Signal 支持：
   ```cpp
   class UIButton {
   public:
       Core::Signal<> onClick;  // 替代 std::function<>
   };
   ```

2. 在 GameScene 中使用游戏事件：
   ```cpp
   void GameScene::onEnter() {
       app()->events().onPlayerJumped.connect([this]() {
           m_particles->emit(m_playerPos, "jump");
       });
   }
   ```

---

### 阶段 4：扩展场景（❌ 未实现）

#### 任务清单
- [ ] 实现 `MenuScene`（主菜单）
  - [ ] UI 布局（标题、按钮）
  - [ ] 按钮事件（开始游戏、设置、退出）
  - [ ] 背景动画（可选）
- [ ] 实现 `PauseScene`（暂停菜单）
  - [ ] 半透明遮罩
  - [ ] 按钮（继续游戏、返回主菜单、设置）
  - [ ] ESC 键切换
- [ ] 场景切换测试
  - [ ] MenuScene → GameScene（点击开始）
  - [ ] GameScene → PauseScene（按 ESC）
  - [ ] PauseScene → GameScene（点击继续）
  - [ ] PauseScene → MenuScene（点击返回）

#### 验收标准（待完成）

| 标准 | 验证方法 | 结果 |
|------|---------|------|
| **主菜单显示正常** | 启动后进入主菜单 | ❌ 待实现 |
| **场景切换流畅** | 切换无卡顿，生命周期正确 | ❌ 待实现 |
| **暂停功能正确** | GameScene 暂停时停止更新 | ❌ 待实现 |
| **内存管理正确** | 场景切换无泄漏 | ❌ 待实现 |

#### 技术风险

🟡 **风险 1：UI 布局复杂度**
- 主菜单需要居中布局、响应式设计
- **缓解措施**：复用 UIToolbar 的布局逻辑

🟡 **风险 2：暂停场景渲染**
- PauseScene 需要渲染下层 GameScene 作为背景
- **缓解措施**：SceneManager 支持渲染多个场景（当前只渲染栈顶）

#### 实施优先级
- **P0（必须）**：PauseScene（用户需要暂停功能）
- **P1（重要）**：MenuScene（改善用户体验）
- **P2（可选）**：SettingsScene、GameOverScene

---

### 阶段 5：资源管理集成（⚠️ 已实现但未集成）

#### 任务清单
- [x] 实现 `Resource` 抽象类（Resource.hpp）
- [x] 实现 `ResourceManager` 基类
- [x] 实现 `FileSystem` 异步文件接口
- [x] 实现 `ResourceManagerHub` 类型路由
- [x] 实现 `BlobResource` 示例
- [ ] 集成到 Application
  - [ ] 创建 FileSystem 实例
  - [ ] 创建 ResourceManagerHub
  - [ ] 在主循环中驱动 `hub.update()`
- [ ] 迁移 ShaderManager 到 ResourceManager
  - [ ] 定义 ShaderResource
  - [ ] 实现 ShaderManager : ResourceManager
  - [ ] 替换现有 ShaderManager 使用
- [ ] 添加 TextureManager（如需要）
- [ ] 添加 FontManager（如需要）

#### 验收标准（待完成）

| 标准 | 验证方法 | 结果 |
|------|---------|------|
| **FileSystem 正常工作** | 异步加载文件成功 | ⚠️ 待集成 |
| **资源缓存生效** | 重复加载返回缓存 | ⚠️ 待集成 |
| **引用计数正确** | 资源自动卸载 | ⚠️ 待集成 |
| **异步加载无卡顿** | FPS 保持稳定 | ⚠️ 待验证 |
| **热重载功能** | `hub.reloadAll()` 成功 | ⚠️ 待验证 |

#### 技术风险

🟡 **风险 1：线程安全**
- FileSystem 后台线程与主线程交互
- **缓解措施**：主线程定期调用 `processCallbacks()`，回调在主线程执行

🟡 **风险 2：ShaderManager 迁移复杂度**
- 现有代码依赖 ShaderManager 接口
- **缓解措施**：保持接口兼容，内部替换实现

🟡 **风险 3：资源生命周期管理**
- Scene 切换时资源是否卸载？
- **缓解措施**：使用引用计数自动管理

#### 实施优先级
- **P0（必须）**：集成 FileSystem 到 Application
- **P1（重要）**：迁移 ShaderManager
- **P2（可选）**：TextureManager、FontManager（当前使用直接加载）

---

## 风险评估与应对

### 技术风险总览

| 风险 | 等级 | 影响 | 概率 | 应对措施 |
|------|------|------|------|---------|
| **场景切换导致内存泄漏** | 🔴 高 | 程序崩溃 | 中 | 1. 使用 Valgrind 检测<br>2. 实现 RAII 管理资源<br>3. 单元测试覆盖 |
| **异步加载导致卡顿** | 🟡 中 | 用户体验差 | 低 | 1. 限制每帧回调数量<br>2. 加载屏幕/进度条 |
| **EventBus 过度订阅** | 🟡 中 | 性能下降 | 中 | 1. 限制订阅者数量<br>2. 提供断开连接机制 |
| **多线程竞态条件** | 🟡 中 | 数据损坏 | 低 | 1. 回调在主线程执行<br>2. 避免共享可变状态 |
| **UI 布局在不同分辨率下错乱** | 🟢 低 | 视觉问题 | 中 | 1. 使用相对布局<br>2. 测试多种分辨率 |
| **资源热重载导致崩溃** | 🟢 低 | 开发不便 | 低 | 1. 热重载前检查引用<br>2. 降级为重启程序 |

---

### 依赖关系图

```
┌────────────────────────────────────────────┐
│                Application                 │
│  - 初始化 FileSystem、ResourceHub          │
│  - 驱动 SceneManager 和 EventBus          │
└────────────────┬───────────────────────────┘
                 │
        ┌────────┴────────┐
        │                 │
┌───────▼────────┐  ┌────▼───────────┐
│  SceneManager  │  │   EventBus     │
│  - 依赖 Scene  │  │   - 基于 Signal│
└───────┬────────┘  └────────────────┘
        │
        │
┌───────▼──────────────────────────────────┐
│              GameScene                    │
│  - 依赖 Application（访问 events()）      │
│  - 依赖 TileMap、ECS、Camera、UI          │
│  - 依赖 ResourceHub（加载资源）           │
└───────────────────────────────────────────┘
```

**关键路径**：
1. Application 必须先于 SceneManager 初始化
2. EventBus 必须在场景创建前初始化
3. ResourceHub 应在场景加载资源前初始化

---

### 回滚计划

#### 场景 1：重构导致游戏无法启动

**症状**：程序崩溃或窗口无法创建

**回滚步骤**：
1. 切换到上一个稳定 commit：`git checkout <stable-commit>`
2. 检查日志找到崩溃原因
3. 隔离问题代码，逐步恢复

**预防措施**：
- 每完成一个阶段提交一次
- 保持 main.cpp 的旧版本作为备份

---

#### 场景 2：性能严重下降

**症状**：FPS 从 60 降到 30 以下

**诊断步骤**：
1. 使用 Tracy Profiler 找到瓶颈
2. 对比重构前后的性能日志
3. 检查 EventBus 订阅者数量

**回滚步骤**：
- 临时禁用 EventBus：直接调用回调
- 临时禁用资源异步加载：同步加载

---

#### 场景 3：内存泄漏无法定位

**症状**：程序运行一段时间后内存持续增长

**诊断步骤**：
1. 使用 Valgrind 检测泄漏点
2. 检查 Scene 析构函数是否被调用
3. 检查 Signal 订阅是否正确断开

**回滚步骤**：
- 恢复到泄漏前的 commit
- 逐个重新应用 commit，定位泄漏引入点

---

## 下一步行动

### 短期目标（1-2 周）

1. **✅ 完成文档补充**（本次）
   - ✅ ResourceManager 设计
   - ✅ 线程模型说明
   - ✅ 测试策略
   - ✅ 风险评估

2. **集成资源管理到 Application**
   - 在 Application 中创建 FileSystem
   - 在 Application 中创建 ResourceManagerHub
   - 在主循环中驱动 `hub.update()`
   - 编写单元测试验证

3. **实现 PauseScene**（P0 优先级）
   - 创建暂停UI
   - 实现 ESC 键切换
   - 测试场景暂停/恢复

### 中期目标（3-4 周）

4. **完善 Signal 系统集成**
   - UI 组件改用 Signal
   - 游戏事件实际触发
   - 编写订阅管理指南

5. **实现 MenuScene**
   - 主菜单 UI 布局
   - 场景切换流程测试
   - 性能测试

### 长期目标（1-2 月）

6. **扩展资源类型**
   - TextureResource + TextureManager
   - FontResource + FontManager
   - AudioResource + AudioManager

7. **调试工具实现**
   - 场景树可视化
   - 性能监控面板
   - ImGui 集成

---

**文档结束**
- [ ] 添加 `FontManager`
- [ ] 资源引用计数

---

## 架构对比分析

### 与业界引擎对比

#### Unity 引擎
```
Application (UnityEngine.Application)
    └── SceneManager
        └── Scene
            └── GameObject (= Entity)
                └── Component
```

**相似度**：⭐⭐⭐⭐⭐
**对应关系**：
- Unity.Application ≈ Tina::Application
- Unity.SceneManager ≈ Tina::SceneManager
- Unity.Scene ≈ Tina::Scene
- Unity.GameObject ≈ Tina::Entity (EnTT)

---

#### Godot 引擎
```
MainLoop
    └── SceneTree
        └── Node
```

**相似度**：⭐⭐⭐⭐
**对应关系**：
- Godot.SceneTree ≈ Tina::SceneManager
- Godot.Node ≈ Tina::Scene

---

#### Unreal 引擎
```
GameEngine
    └── World
        └── Level
```

**相似度**：⭐⭐⭐
**对应关系**：
- UE.GameEngine ≈ Tina::Application
- UE.Level ≈ Tina::Scene

---

### 设计模式应用

| 模式 | 应用位置 | 说明 |
|------|---------|------|
| **单例模式** | Application | 全局唯一的应用实例 |
| **状态模式** | Scene | 每个 Scene 是一个状态 |
| **观察者模式** | EventBus (Signal) | 事件订阅/发布 |
| **工厂模式** | ResourceManager | 资源创建 |
| **策略模式** | Scene::update() | 不同场景不同更新策略 |

---

## 常见问题解答

### Q1: 这会不会过度设计？

**A**: 评估标准：

| 问题 | 答案 | 结论 |
|------|------|------|
| 是否解决实际痛点？ | ✅ 是（main.cpp 臃肿、难扩展） | 不过度 |
| 接口是否简单？ | ✅ 是（Scene 只有 5 个方法） | 不过度 |
| 是否有立即用途？ | ✅ 是（马上要做菜单） | 不过度 |
| 代码量是否合理？ | ✅ 是（~300 行实现全部） | 不过度 |
| 学习成本高吗？ | ✅ 低（Unity 开发者秒懂） | 不过度 |

**结论**：这是**恰当的抽象**，不是过度设计。

---

### Q2: 性能会不会下降？

**A**: 不会，反而可能提升。

**分析**：
- Scene 虚函数调用开销：< 1ns（现代 CPU）
- SceneManager 场景栈查询：O(1)
- EventBus Signal 调用：与原回调函数相同

**性能优化机会**：
```cpp
// 暂停时停止不必要的更新
void GameScene::onPause() {
    m_world->pause();      // ECS 系统停止
    m_particles->pause();  // 粒子停止
    m_tilemap->pause();    // 水流停止
}
// → CPU 占用立即下降！
```

---

### Q3: 需要修改多少现有代码？

**A**: 主要是代码迁移，不是重写。

**修改量估计**：
- **新增代码**: ~500 行（Application + SceneManager + EventBus）
- **迁移代码**: ~400 行（main.cpp → GameScene）
- **修改代码**: ~100 行（UI 改用 Signal）

**总工作量**: 约 5-7 天（包括测试）

---

### Q4: 能否渐进式迁移？

**A**: 完全可以！

**迁移路径**：
```
阶段 1: 先实现 Application，main.cpp 简化但仍工作
        ↓
阶段 2: 再实现 Scene，游戏逻辑迁移到 GameScene
        ↓
阶段 3: 最后集成 EventBus，替换回调函数
```

**每个阶段都可独立运行和测试**。

---

### Q5: 如果以后不需要菜单呢？

**A**: 架构仍然有价值。

**即使只有一个场景，收益仍在**：
- ✅ 代码组织更清晰（GameScene vs main.cpp）
- ✅ 可单独测试游戏逻辑
- ✅ 易于添加性能分析场景
- ✅ 多人协作更顺畅

---

## 附录

### A. 代码量对比

| 模块 | 当前行数 | 重构后行数 | 变化 |
|------|---------|-----------|------|
| main.cpp | 534 | ~40 | -92% ✅ |
| Application | 0 | ~150 | +150 |
| SceneManager | 0 | ~100 | +100 |
| GameScene | 0 | ~400 | +400 |
| EventBus | 0 | ~80 | +80 |
| **总计** | 534 | 770 | +44% |

**说明**：虽然总代码量增加，但：
- ✅ 职责更清晰
- ✅ 易于维护
- ✅ 支持扩展

---

### B. 参考资料

- [Game Programming Patterns - Game Loop](http://gameprogrammingpatterns.com/game-loop.html)
- [Unity Manual - Scene Management](https://docs.unity3d.com/Manual/SceneManagement.html)
- [Godot Docs - Scene Tree](https://docs.godotengine.org/en/stable/getting_started/step_by_step/scene_tree.html)
- [Unreal Engine - World and Levels](https://docs.unrealengine.com/5.0/en-US/world-and-levels-in-unreal-engine/)

---

**文档结束**
