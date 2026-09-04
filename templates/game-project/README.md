# MyGame

一个 Tina 游戏项目模板。核心约定只有一条：**与平台无关的代码写在 `core/`，与平台相关的写在 `platforms/<平台>/`**。

```
core/                 可移植内容。可以链接引擎模块，绝不链接平台目标。
platforms/desktop/    Windows + Linux + macOS 的组装根。
assets/               源资源与 cook recipe。
```

`core/` 只编译一次，被每个前端链接，所以玩法代码只写一遍。

## 这条约定是被强制的，不是靠文档提醒

`tina_add_game_content()` 在 configure 阶段就拒绝内容库链接平台目标。原因是这类错误在尝试移植之前完全看不见：一个引用了 `Tina::Desktop::CreateEngine` 的翻译单元必须链接 `Tina::DesktopBootstrap` → GLFW，于是整个文件变成桌面专用。引擎自己的 `samples/` 曾经全是这个样子——玩法和组装根写在同一个 `main.cpp`，没有任何东西警告过它们。现在那 28 个示例都按这套目录重排过了，所以模板和引擎示例是同一套约定，可以直接照着抄。

有 8 个示例没有 `core/`，那不是例外而是一句声明：这些程序除了组装根什么都没有（`platform` 是 GLFW + Null renderer，`desktop` 只演示 `CreateEngine` 返回什么），硬造一个空内容库只会让强制性看起来存在于它并不存在的地方。

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

**1. 用 `DesktopBootstrap` 时还要给出 vcpkg 树。** 这个组件把 `glfw3` 和 `Freetype` 以 `LINK_ONLY` 私有依赖挂在 adapter 上——你的代码从不提它们，但静态库链接时消费者必须自备，而它们的 config 不随 SDK 安装。缺了就在 `find_package(Tina)` 那一行报 "Could not find a package configuration file provided by glfw3"。

这是**按组件**的，不是无条件的：只用 `Tina::GameSDK` 的工程完全不需要这个前缀。Tina 自己的哈希与切线生成依赖（xxHash、MikkTSpace）已经 vendored 进静态库，不再要求消费者提供。

用 `CMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` 也行，但不必要——vcpkg 树进 `CMAKE_PREFIX_PATH` 就够。

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

两个前提：cooker 必须存在（`TINA_BUILD_TOOLS=OFF` 构建出来的 SDK 不带它，`tina_find_assetc()` 会返回 `TINA_ASSETC-NOTFOUND-NOT-PACKAGED`），以及交叉编译时得用 `TINA_ASSETC_EXECUTABLE` 指向一个宿主机构建——cooker 跑在构建机上，目标架构的版本执行不了。拿不准就先查 `tina_find_assetc()`，不满足就保留启动时 cook。

**recipe 里的平台标签不是移植障碍。** `TargetPlatform` 只有 `Invalid` / `Any` / `WindowsX64` / `LinuxX64`，写不了 `AndroidArm64`。但**加载器从不校验这个字段**——它只出现在 cook 侧代码里，`CatalogFile.cpp` / `CookedAssetFile.cpp` / `AssetSystem.cpp` 一次都不读，只有 `isKnownTargetPlatform` 这个"是不是已知枚举"的检查。要跨平台就写 `platform Any`（`android/app/content/android.recipe` 就是这么做的）；写 `WindowsX64` 也一样能在别处加载。同理，今天没有任何 cooker 设置过压缩 `pixelFormat`，所有纹理都是未压缩 `Rgba8Unorm`，移动端 GPU 也能用。

**浏览器和 Android 现在都能拿到文件。** web 侧走 `--preload-file`，cooked catalog 落进 MEMFS；Android 侧走 APK `assets/` + 首启解压：`android/app/build.gradle` 用宿主 `tina_assetc`（`-Ptina.assetc=<路径>`）把 catalog cook 进一个生成的 assets source set，`TinaActivity.extractContent()` 在第一次启动时把整棵 assets 树复制到 `getFilesDir()/content` 并交给 `EngineConfig::contentRoot`。两边内容代码都不用改一个字。

Android 那条链上有两个非显然的点。一是 cook 在**构建机**上跑而不是设备上：cook 是确定性的，设备能贡献的只有延迟和发热，所以设备只付一次"复制已 cook 好的字节"的代价。二是全仓库依然没有一处 `AAssetManager`，这是刻意的——解压一次换来 `Core::readFile` 在所有平台是唯一读路径，内容 bug 因此不可能是平台专属的。字节精确性打包时不会被破坏：cooked object 未压缩，zip 存储是无损的，而 catalog 打开时会拿每个 object 的实际大小对 `cookedFileBytes` 校验，所以任何被改动过的文件会在 `openAndBindCatalog` 当场失败。

**没有 `tina_add_android_frontend()`。** Java 用一个固定名字解析 `System.loadLibrary`，所以 Android 没有"每个游戏一个共享库"可产出：游戏是靠引擎的 JNI 桥链接它的内容库上设备的。在那个桥改成从游戏定义的工厂取 application 之前，这样一个函数只会包装一次对引擎源码的改动。这也是 Android 的 cook 接在 `android/app/build.gradle` 而不是 `tina_cook_catalog()` 上的原因——后者输出到 `$<TARGET_FILE_DIR:target>`，那在 Android 上是 NDK 的 `.so` 目录，AGP 只从那里收 `.so`。

**模板不带 web 入口。** `tina_add_web_frontend()` 已经可用，但 SDK 头里没有 `Tina::Web::CreateEngine`，浏览器组装根目前是 `samples/web/platforms/web/WebMain.cpp` 里手写的 `EngineCompositionFactories`（`createEngineFactories()`，第 465 行起）。把它抄进模板等于分叉那份代码。要做 web 端：建 `platforms/web/`，调 `tina_add_web_frontend()`，入口以 `samples/web/platforms/web/WebMain.cpp` 为基础——它用 `emscripten_set_main_loop` 驱动 `EngineHost::start/tick`，而不是 `run()`。
