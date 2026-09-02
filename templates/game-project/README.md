# MyGame

一个 Tina 游戏项目模板。核心约定只有一条：**与平台无关的代码写在 `core/`，与平台相关的写在 `platforms/<平台>/`**。

```
core/                 可移植内容。可以链接引擎模块，绝不链接平台目标。
platforms/desktop/    Windows + Linux + macOS 的组装根。
assets/               源资源与 cook recipe。
```

`core/` 只编译一次，被每个前端链接，所以玩法代码只写一遍。

## 这条约定是被强制的，不是靠文档提醒

`tina_add_game_content()` 在 configure 阶段就拒绝内容库链接平台目标。原因是这类错误在尝试移植之前完全看不见：一个引用了 `Tina::Desktop::CreateEngine` 的翻译单元必须链接 `Tina::DesktopBootstrap` → GLFW，于是整个文件变成桌面专用。引擎自己的 `samples/` 里有 12 个示例正是这样，把玩法和组装根写在同一个 `main.cpp`，没有任何东西警告过它们。

`tina_verify_game_content_portable()` 在根 `CMakeLists.txt` 末尾再走一遍完整链接图，抓间接可达的情况——内容库链接了一个自己链接 GLFW 的辅助库，同样不可移植，但逐调用检查看不到。

## 创建

别手工复制这个目录，用生成器：

```bash
cmake -DNAME=Wuxia -DDEST=~/Wuxia -DDEPS=<vcpkg树>/x64-windows \
  -P <sdk前缀>/lib/cmake/Tina/TinaNewProject.cmake
```

它复制模板、把两个标识符改成你的名字，并写一份 `CMakePresets.json`，把下面那些必需参数固化进去。**`DEPS` 建议给**：不给也能生成，但 presets 里会留一个占位符要你手填。

## 构建

```bash
cmake --preset default          # configure
cmake --build --preset default  # 全建
cmake --build --preset content  # 只建内容库
```

`content` 那个 preset 只编 `<name>_content` 静态库，不链接、不 staging 资源——改玩法代码时查语法错误用它。

## 为什么需要 presets 而不是两行命令

只写 `-DTina_DIR=<sdk前缀>/lib/cmake/Tina` 会在三个地方分别失败，而三处报错都指向错误的方向。生成的 presets 就是为了把这三条一次配好。三条都是当前 SDK 的真实状态，不是模板的取舍：

**1. 还要给出 vcpkg 树。** `TinaConfig.cmake` 走 `find_dependency(xxHash)` 和 `find_dependency(mikktspace)`，而这两个包的 config **不随 SDK 安装**——它们是 `Tina::Core` / `Tina::Scene` 上的 `LINK_ONLY` 私有依赖，静态库链接时消费者必须自备。缺了就在 `find_package(Tina)` 那一行报 "Could not find a package configuration file provided by xxHash"，看起来像 Tina 装坏了，其实是它的依赖没被打包。SDK 只装了 `xxhash.dll`，没装 `xxHashConfig.cmake`。

用 `CMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` 也行，但不必要——vcpkg 树进 `CMAKE_PREFIX_PATH` 就够。同一个 vcpkg 树里的 Freetype 也是这么找到的。

**2. 多配置生成器要限住配置类型。** SDK 通常只 `--install --config Debug`，装出来的 imported target 只有 Debug 的 `IMPORTED_LOCATION`。Visual Studio 生成器默认要 Debug/Release/RelWithDebInfo/MinSizeRel 四个，于是 generate 阶段对每个缺失配置各报一条 `IMPORTED_LOCATION not set for imported target "Tina::Runtime"`。`-DCMAKE_CONFIGURATION_TYPES=Debug` 让它只要装过的那个。要四配置就四配置都装。

**3. 别硬写生成器版本。** `-G "Visual Studio 17 2022"` 在只装了 VS 18 的机器上报 `MSB8020: 无法找到 Visual Studio 2022 的生成工具`，且是在探测 `VCTargetsPath` 时炸的，错误里根本不提生成器参数。生成的 presets 因此**不带** `generator` 键——写死一个版本就是把这个坑留给下一台机器，留空让 CMake 挑一个装着的。

组件名要的是**平台包**，不是引擎模块：`DesktopBootstrap` 带 GLFW/bgfx 后端和 `CreateEngine`，`GameSDK` 是可移植集合。**没有 `Runtime` 这个组件**——写了它 `find_package` 会把整个包报成 not found，看起来像安装损坏而不是参数写错。

## 改名改的是什么

生成器替换两个 token，共 16 处（代码与构建文件里 `MyGame` 10 处、`mygame` 6 处）。**改文件夹名没有任何作用**——CMake 不看目录名，起作用的是文件内容：`MyGame` 是 C++ 命名空间和 CMake 工程名，`mygame` 是 target 名前缀。

两者分开替换，因为大小写规约不同：命名空间用 `Wuxia`，target 用小写 `wuxia`。一次性 case-insensitive 替换会把 target 名写成大写，在 Windows 上看不出问题，到 Linux 才炸。

`NAME` 必须是 C++ 标识符（字母开头，后接字母数字下划线）。`my-game` 这类会被生成器当场拒掉，而不是等到第一次编译时在一个你没写过的文件里报语法错误。

## 跑起来是什么样

这条链路实测跑通过：从已安装的 SDK 生成、`--preset default` configure、两个 build preset 都编过，`content` preset 确实只产出 `.lib` 而没有 exe。跑起来是个 1280×720 窗口并停在那里（模板没有自动退出路径，关窗口才退）。stderr 为空，且可执行文件旁边真的多出了 `content/manifest.tmnft` 和 `content/objects/` ——那是启动时 cook 的产物，说明资源链路是活的，不只是链接上了。

## 入口与资源

`core/GameApplication.hpp` 里只有一个 seam：

```cpp
std::unique_ptr<Tina::IGameApplication> createApplication() noexcept;
```

每个前端都调这一个函数，把结果交给 `EngineHost`。移植到新平台 = 加一个 `platforms/<平台>/` 兄弟目录，**不动 `core/`**。

它**不带参数**是故意的。定位文件是资源加载里唯一真正平台相关的部分：桌面在可执行文件旁边解析路径，浏览器只有预加载的虚拟文件系统，Android 要先把资源解出来。这个答案由前端填进 `EngineConfig::contentRoot`（`Tina::Core::ContentRoot`），`core/` 通过每个 phase context 上的 `engineConfig()` 拿回来。所以内容代码只说"我要 `assets/game.recipe`"，从不说它在哪。

```cpp
// platforms/desktop/main.cpp —— 只有前端知道平台给了什么
config.contentRoot = *Tina::Core::ContentRoot::Create(*Tina::Core::applicationDirectory());

// core/GameApplication.cpp —— 内容只认相对路径
auto recipeFile = context.engineConfig().contentRoot.resolve("assets/game.recipe");
```

`ContentRoot` 是值不是服务，不碰文件系统：`resolve()` 只校验调用方要的东西（拒绝绝对路径、`\` 分隔符、`.` / `..` 组件、空组件、NUL），返回的路径可能不存在，存在性仍归调用方检查。空根上 `resolve()` 返回 `NotFound` 而不是 `InvalidArgument`——参数没问题，是这个产品没被给过加载位置，区分这两者就是"改调用点"和"改前端启动"的差别。

资源链路：

```
recipe 文本  ->  cook  ->  catalog (manifest + objects/)  ->  AssetSystem  ->  handle
```

内容只从 **catalog** 加载，从不读 recipe——recipe 是创作期输入，发布版不必携带。recipe 由前端的 `CMakeLists.txt` 用 `tina_product_data_file()` 部署到可执行文件旁边，构建树和安装副本里是同一个相对路径，所以桌面上以可执行文件目录为根在两边都对。

## 已知不可用的部分

这些是引擎当前的真实状态，不是模板的取舍。都对照已安装的 SDK 头文件核实过。

**模板默认在启动时 cook，但已经可以改成构建期。** cooker `tina_assetc` 现在随 SDK 安装（`<前缀>/bin/tina_assetc`），换成构建期 cook 只要在前端的 `CMakeLists.txt` 里加一行：

```cmake
tina_cook_catalog(mygame
    RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/game.recipe"
    DESTINATION "content"
)
```

`DESTINATION` 就是内容侧 `contentRoot.resolve("content")` 里的那个字符串，所以 C++ 不用改，`openContent` 里的 cook 段可以直接删掉，只留 bind。

两个前提：cooker 必须存在（`TINA_BUILD_EXAMPLES=OFF` 构建出来的 SDK 不带它，`tina_find_assetc()` 会返回 `TINA_ASSETC-NOTFOUND-NOT-PACKAGED`），以及交叉编译时得用 `TINA_ASSETC_EXECUTABLE` 指向一个宿主机构建——cooker 跑在构建机上，目标架构的版本执行不了。拿不准就先查 `tina_find_assetc()`，不满足就保留启动时 cook。

**recipe 必须写一个桌面平台，但这不影响别处加载。** `TargetPlatform` 只有 `Invalid` / `Any` / `WindowsX64` / `LinuxX64`，所以 `assets/game.recipe` 写不了 `AndroidArm64`。不过**加载器从不校验这个字段**——它只出现在 cook 侧代码里，`CatalogFile.cpp` / `CookedAssetFile.cpp` / `AssetSystem.cpp` 一次都不读。同理，今天没有任何 cooker 设置过 `pixelFormat`，所有纹理都是未压缩 `Rgba8Unorm`，移动端 GPU 也能用。所以按 `WindowsX64` cook 的 catalog 在任何平台都能加载，平台标签不是移植障碍。

真正的障碍只有一个：**拿不到文件**。见下。

**浏览器和 Android 上今天一个文件都读不到。** `EngineConfig::contentRoot` 解决的是"内容代码怎么问路径"，不是"平台上有没有那个文件"。web 侧还没有 `--preload-file` / `--embed-file`，MEMFS 是空的；Android 侧全仓库没有一处 `AAssetManager`，而 NDK 的 `ifstream` 读不了 APK 内部。两边都是"没有文件"，不是"格式不对"——`resolve()` 会正常返回一个路径，随后 `readFile` 报 `NotFound`。这也是模板暂时只有桌面前端的原因。

**没有 `tina_add_android_frontend()`。** Java 用一个固定名字解析 `System.loadLibrary`，所以 Android 没有"每个游戏一个共享库"可产出：游戏是靠引擎的 JNI 桥链接它的内容库上设备的。在那个桥改成从游戏定义的工厂取 application 之前，这样一个函数只会包装一次对引擎源码的改动。

**模板不带 web 入口。** `tina_add_web_frontend()` 已经可用，但 SDK 头里没有 `Tina::Web::CreateEngine`，浏览器组装根目前是 `samples/web/main.cpp` 里 134 行手写的 `EngineCompositionFactories`。把它抄进模板等于分叉那份代码。要做 web 端：建 `platforms/web/`，调 `tina_add_web_frontend()`，入口以 `samples/web/main.cpp` 为基础——它用 `emscripten_set_main_loop` 驱动 `EngineHost::start/tick`，而不是 `run()`。
