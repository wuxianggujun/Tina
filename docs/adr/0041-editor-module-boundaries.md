# ADR 0041：编辑器是引擎之上的工具树，不是引擎模块

- 状态：Accepted
- 日期：2026-09-01
- 决策者：Tina maintainers

## 背景

在本 ADR 之前，编辑器混在引擎源码树里：`src/editor`（`Tina::Editor` authoring）与
`src/editor_app`（`TinaEditor.exe` 组合）与 `src/core`、`src/render`、`src/runtime` 平铺在
同一层，公共头在 `include/tina/editor`。这带来三个具体后果。

**一、Android 为 arm64 交叉编译了编辑器。** 根 `CMakeLists.txt` 里
`add_subdirectory(src/editor)` 是裸调用，而 `android/app/CMakeLists.txt` 把整棵引擎树
`add_subdirectory` 进来。证据是构建产物里真实存在
`android/app/.cxx/Debug/*/arm64-v8a/tina/src/editor/libtina_editor.a`，install 规则还会复制它。
手机上不会有编辑器，这是纯粹的构建时间与包体浪费。

同一份文件里 `src/editor_app` 因为包在 `TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX`
内而**没有**被编译（`build.gradle` 已 `GLFW=OFF`）。两个事实并列说明：决定编不编的是 guard，
不是目录深度。

**二、`tina_editor` 被打进 Game SDK 包。** 它在 `tina_sdk_export_targets` 里，
`include/tina/editor` 也在 install 列表里。它并不在 `tina_game_sdk` 的 INTERFACE 链接里，
所以游戏消费者不会链接到它——但头文件和 CMake target 仍然发出去了，是一份没人消费的
authoring ABI。

**三、文档与代码不符。** `docs/architecture.md` 把 `tina_editor` 列在**基础模块**表里，写
「只依赖 Core/AssetFormat」。实际 `target_link_libraries` 是
`PUBLIC Tina::Core Tina::Math Tina::AssetFormat Tina::Asset`，源头是公共头
`Navigation2DAuthoringDocument.hpp` 直接 `#include <tina/asset/CatalogCook.hpp>`。
`Tina::Asset` 是产品模块，于是「基础模块依赖产品模块」——分层描述是假的。

## 决定

**编辑器移出 `src/`，成为顶层 `editor/` 树，并接受它位于引擎之上。**

```
editor/
├── include/tina/editor/      与 include/tina/editor_app/
├── src/                      Tina::Editor
├── app/                      Tina::EditorApp + TinaEditor.exe
└── tests/
```

include 写法保持 `<tina/editor/X.hpp>` 不变，所以引用编辑器头的 75 个文件一行未改。

三条随之确定的边界：

**编辑器允许依赖产品模块。** 这正是它该在 `src/` 之外的理由。`src/` 下的模块受分层约束
（基础模块不得依赖产品模块），而 `editor/` 消费引擎，依赖 Asset/Runtime/Scene 是它的正常
形态。`tina_editor` → `Tina::Asset` 这条边不再是违例，但必须**如实写在文档里**，因此
`architecture.md` 新增「工具模块」表，依赖列写实际四项。

**`TINA_BUILD_EDITOR` 默认 `PROJECT_IS_TOP_LEVEL`。** 顶层开发照旧；把 Tina 作为子目录
消费的工程（Android 即是）默认不编。Android 另在 `build.gradle` 显式传
`-DTINA_BUILD_EDITOR=OFF`——默认值已经够用，显式写出来是为了让意图可 grep，与那里已有的
十个 OFF 开关一致。

**编辑器退出 SDK 包。** 从 `tina_sdk_export_targets`、`tina_configure_game_sdk_target`
调用和 install 头目录三处移除。这是 breaking change：`find_package(Tina)` 之后不再有
`Tina::Editor`。按仓库「不为旧设计留兼容层」的约定，不留转发别名。

## 后果

**编辑器不能作为独立工程构建。** `editor/src` 与 `editor/app` 共 7 处
`#include "core/io/PathUtil.hpp"`，而 `PathUtil.hpp` 是 `src/` 下的私有头，按 ADR 0040 刻意
不进公共 SDK 面（`<filesystem>` 不暴露）。因此 `editor/` 只能在引擎源码树内构建，
`target_include_directories(... PRIVATE ${PROJECT_SOURCE_DIR}/src)` 必须保留。

这也是**不拆独立仓库**的两条理由之一。另一条：authoring 写出的 wire format 与 runtime 读的
是同一套 `asset_format`，同仓库意味着 schema bump 一次改完，分仓会把它变成跨仓版本对齐。

**`tina_editor` 必须自己声明 PUBLIC include 目录。** 原先靠
`tina_configure_game_sdk_target()` 提供，而该函数用 `set_target_properties` **覆盖**
`INTERFACE_INCLUDE_DIRECTORIES` 指向引擎 `include/` 根。退出 SDK 后这个覆盖消失，需显式
`target_include_directories(tina_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)`。

**暴露了一处此前靠巧合成立的测试依赖。** `tina_editor_app_tests` 编译 `editor/app` 的私有
头，而那些私有头 include 了十个引擎模块的公共头（core/render/asset/asset_format/runtime/
scene/ui/desktop/editor/editor_app）。`tina_editor_app` 通过 PRIVATE 链接拿到它们，
消费者本不该继承——但 `Tina::EditorApp` 的 PUBLIC include 目录原先是引擎整个 `include/` 根，
于是任何 `tina/...` 头都能解析，无论是否属于契约。PUBLIC 目录收窄到 `editor/include` 后
编译失败，测试改为自己显式链接所需的引擎 target。这不是本次改动引入的缺陷，是它显影的。

**`TinaEditor.exe` 仍然安装。** 它走 `tina_install_product()` 的独立 component，与 SDK
export 无关。退出 SDK 包影响的是「作为库发给游戏消费者」，不是「产品能否安装」。

**Linux editor gate 会重建一次。** `tools/linux/run-gcc13-editor-gate.sh` 的复用 stamp 基于
路径+内容哈希，文件移动使其失效一次，属预期。

## 备选方案

**只加 guard，不移目录。** 五行改动即可解决 Android 与包体，但留下「编辑器是引擎模块」的
错误结构信号，也不解决 `architecture.md` 分层描述作假的问题。

**移目录，但公共头留在 `include/tina/editor`。** 引用文件同样一行不用改，改动面更小，但
`include/` 下仍留着编辑器的公共 ABI，物理独立不彻底，SDK install 列表也仍要逐项排除。

**拆独立仓库。** 被上面「后果」第一条否掉：需要引擎私有头，且 schema 对齐成本真实。
