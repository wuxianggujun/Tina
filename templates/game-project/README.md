# MyGame

Tina 0.3.0 桌面游戏模板。游戏只链接 **`Tina::GameSDK`**，对应一个 `Tina.lib` / `libTina.a`；Editor 不在 Game SDK 内。

## 目录与职责

```text
core/                 游戏状态、玩法、场景、UI 和资源 owner
platforms/desktop/    Windows/Linux/macOS 的窗口、启动与部署组合
assets/               构建期 authoring 输入，不随游戏发布
```

内容与入口分开编译，但不同平台仍各自编译自己的二进制。`tina_validate_content_dependencies()` 检查游戏自有链接图，遇到 GameSDK 即停止；它不能证明任意 C++ 源码可移植。可移植性必须由对应平台的编译和测试验证。

## 创建与构建

使用已安装 SDK 的生成器，`DEPS` 指向与 SDK 生产工具链匹配的依赖目录：

```powershell
cmake -DNAME=Wuxia -DDEST=C:/Games/Wuxia `
  -DDEPS=C:/vcpkg/installed/x64-windows `
  -P D:/ProgramData/Tina/lib/cmake/Tina/TinaNewProject.cmake
cd C:/Games/Wuxia
cmake --preset default
cmake --build --preset default
```

默认配置是 **Release**。`content` build preset 只编译游戏内容静态库，用于语法/编译检查，不产生完整产品或 cook 资源。只有完整 frontend build 才产出可运行目录。

生成的 presets 不固定 Visual Studio 版本，但编译器 family/version、架构、操作系统和 CRT 必须匹配 SDK。多配置生成器只启用已安装配置；不得把 Debug 映射成 Release。需要调试信息时可以显式将非 Debug 配置映射到 Release SDK，但游戏自己的编译 flags 仍由该配置决定。

## 单库与能力

```cmake
find_package(Tina 0.3.0 EXACT CONFIG REQUIRED COMPONENTS Desktop)
tina_add_game_content(mygame_content SOURCES GameApplication.cpp)
tina_add_desktop_frontend(mygame SOURCES main.cpp CONTENT mygame_content INSTALL)
```

`Desktop`、`Physics2D`、`UIFreetype` 等是 **已编入 archive 的能力**，不是独立链接库。旧模块 targets 和 `DesktopBootstrap` component 已删除。

完整 Desktop SDK 的私有第三方链接闭包仍需匹配的 GLFW、字体等依赖 packages，哪怕游戏仅引用 Core。bgfx/bx/bimg 作为 SDK 私有第三方 archives 随包提供，不复制进 Tina 核心 archive。使用 Null 配置构建的 SDK 不需要未启用的图形依赖。

## 入口与资源数据流

`core/GameApplication.hpp` 提供唯一游戏工厂：

```cpp
std::unique_ptr<Tina::IGameApplication> createApplication() noexcept;
```

前端填写 `EngineConfig::contentRoot`，内容层通过 context 取得该值并解析相对路径。ContentRoot 只校验路径，不替调用方证明文件存在。模板应用只打开并绑定 `content/manifest.tmnft`，不在启动时读取 recipe 或执行 cooker。

```text
recipe / PNG / glTF -> host cooker（构建机）
  -> candidate Catalog -> 验证与可回滚发布
  -> 产品目录 content/manifest.tmnft + objects/
  -> Runtime AssetSystem -> weak handle / resident lease
```

模板前端已使用：

```cmake
tina_cook_catalog(mygame
    RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
    DESTINATION "content")
```

可追加 `GLTFS` / `TEXTURES` / `SOURCE_ROOT`；间接引用的 buffers、images 等必须列入 `DEPENDS`。游戏自有资源流程使用 `COOKER <executable-target-or-absolute-path>` 和 `COOKER_ARGS`，不再把离线处理塞进 Runtime。输入或 cooker 变更会触发构建期重新 cook；失败保留上一份 Catalog，构建仍明确失败，不静默继续发布旧内容。这里是可回滚的目录替换，不是并发读者下的原子目录交换。

SDK 需包含 `tina_assetc`（生产时开启 `TINA_BUILD_TOOLS`），或设置 `TINA_ASSETC_EXECUTABLE`。交叉编译必须提供构建机可执行的 host tool，不能运行目标架构产物。没有 cooker 时直接配置失败，没有启动时 cook 兼容分支。

## 运行与验证

完整 build 后启动 `build/bin/Release/desktop/mygame.exe`（Windows）。模板创建空窗口，关闭窗口结束；这是引擎启动模板，不是完整玩法 Demo。

验证时检查：

1. `content/manifest.tmnft` 在 **启动前** 已存在。
2. 产品目录无需源 recipe/PNG/glTF，安装目录也使用相同 cooked 布局。
3. 修改 recipe 后重新 build 触发 cook；损坏输入导致 build 失败且保留旧 manifest。
4. `Core::buildInfo()` 的 build-id 与 SDK 安装清单一致。

源码、文档和 CMake 为 UTF-8；SDK 对 MSVC 消费者传播 `/utf-8`。新增平台入口需保持命令行和文件路径的显式 UTF-8 转换。

## 平台边界

该模板仅带 desktop 入口。Web/Android 的 in-tree 示例不代表安装包已有完整公开组合入口：Web 尚无公开 `CreateEngine`，Android JNI 仍连接固定 gallery 工厂。移植需先建立公开 frontend seam，不能复制私有 bgfx 头或把目标平台 archive 当 host cooker 使用。
