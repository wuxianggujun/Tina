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
- ✅ 提供全局服务访问点

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

## 实施路线图

### 阶段 1：基础架构（估计 1-2 天）

#### 任务清单
- [ ] 创建 `src/engine/` 目录
- [ ] 实现 `Application` 类（~150 行）
  - 窗口创建
  - bgfx 初始化
  - 主循环驱动
- [ ] 实现 `Scene` 基类（~50 行）
- [ ] 实现 `SceneManager`（~100 行）
- [ ] 实现 `EventBus`（~80 行）

#### 验证方式
```cpp
// 测试代码
class TestScene : public Scene {
    void update(float dt) override {
        Log::info("TestScene running");
    }
    void render() override {}
};

int main() {
    Application app(config);
    app.scenes().push(new TestScene());
    app.run();  // 应该正常运行
}
```

---

### 阶段 2：迁移游戏逻辑（估计 2-3 天）

#### 任务清单
- [ ] 创建 `GameScene` 类
- [ ] 将 main.cpp 的初始化代码迁移到 `GameScene::onEnter()`
- [ ] 将 main.cpp 的更新代码迁移到 `GameScene::update()`
- [ ] 将 main.cpp 的渲染代码迁移到 `GameScene::render()`
- [ ] 将 main.cpp 的事件处理迁移到 `GameScene::handleEvent()`
- [ ] 简化 main.cpp 为启动入口

#### 迁移映射表

| main.cpp 代码段 | 迁移目标 |
|----------------|---------|
| 创建 TileMap | GameScene::onEnter() |
| 创建 ECS World | GameScene::onEnter() |
| 创建 Camera | GameScene::onEnter() |
| 创建 UI | GameScene::setupUI() |
| 键盘/鼠标事件 | GameScene::handleEvent() |
| 工具逻辑 | GameScene::handleToolUse() |
| ECS 更新 | GameScene::update() |
| 液体更新 | GameScene::update() |
| 渲染地形 | GameScene::render() |
| 渲染 UI | GameScene::render() |

---

### 阶段 3：集成 Signal 系统（估计 1-2 天）

#### 任务清单
- [ ] EventBus 实现完整事件列表
- [ ] UI 组件改用 Signal
  ```cpp
  // 旧：UIButton::onClick = []() {};
  // 新：UIButton::onClick.connect([]() {});
  ```
- [ ] 游戏事件改用 EventBus
  ```cpp
  // 旧：直接调用
  // 新：EventBus::onPlayerDied.emit();
  ```

#### 收益
- ✅ 解耦事件发送和接收
- ✅ 支持多个监听器
- ✅ 易于调试（可打印所有监听器）

---

### 阶段 4：扩展场景（估计 1-2 天）

#### 任务清单
- [ ] 实现 `MenuScene`（主菜单）
  - UI 按钮（开始游戏、设置、退出）
  - 标题动画
- [ ] 实现 `PauseScene`（暂停菜单）
  - 继续游戏
  - 返回主菜单
  - 设置
- [ ] 场景切换测试
  ```
  MenuScene → GameScene → PauseScene → GameScene → MenuScene
  ```

---

### 阶段 5：资源管理（估计 1-2 天）

#### 任务清单
- [ ] 实现 `ResourceManager` 基础接口
- [ ] 迁移 `ShaderManager` 到 ResourceManager
- [ ] 添加 `TextureManager`（如需要）
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
