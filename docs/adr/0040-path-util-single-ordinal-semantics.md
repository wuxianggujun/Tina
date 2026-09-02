# ADR 0040：路径包含判定统一为序数折叠，私有头留在 src/

- 状态：Accepted
- 日期：2026-09-01

## 背景

资源管线、编辑器和烘焙工具都要回答同一个问题：一条路径是否落在某个创作根目录之内。这个判定
此前没有单一实现，而是手抄副本，且**副本之间语义不一致**。

`pathComponentEquals` 有 10 份，分成两种互不兼容的 Windows 写法（逐份核对函数体所得）：

| Windows 折叠方式 | 份数 | 位置 |
| --- | --- | --- |
| `::CompareStringOrdinal(..., TRUE)` | 6 | `editor/src/EditorProjectCreation.cpp`、`editor/src/EditorProjectWorkspace.cpp`、`editor/app/EditorSourceImportIngress.cpp`、`editor/app/EditorSourceImportLaunchOptions.cpp`、`editor/app/EditorSourceImportSelection.cpp`、`editor/app/EditorWorkspaceState.hpp` |
| `std::towlower` 逐 `wchar_t` | 4 | `src/asset/CatalogCook.cpp`、`src/asset/SourceImportCapture.cpp`、`src/asset/SourceImportPipeline.cpp`、`tools/assetc/main.cpp` |

两种写法都喂给 `pathIsSameOrDescendant`（9 份逐字节相同），而后者决定一条路径是否逃出创作
根。也就是说资源管线和编辑器可以对同一对路径给出**相反的判定**——这不只是重复，是围栏上的
一道缝。

`std::towlower` 那 4 份错两次：

1. 它受 locale 影响。土耳其语 locale 把 `'I'` 折叠到 `ı`（U+0131）而非 `'i'`，而 NTFS 的
   折叠是 locale 无关的。
2. 它一次只折叠一个 `wchar_t`。BMP 之外的码点在 Windows 上是代理对，逐 `wchar_t` 折叠根本
   触不到那个码点。

UTF-8 字节转路径同样是抄的：`src/` 与 `tools/` 下有 7 份等价的解码 helper 定义（包含两个各自
名为 `Utf8Path.hpp` 的头），另有 45 处直接调用已 deprecated 的
`std::filesystem::u8path`；路径转 UTF-8 则有三种写法混用：`u8string()`、
`generic_u8string()`，以及 `samples/3d_product/main.cpp` 的 `path::string()`——后者在 Windows
上是活动窄代码页而不是 UTF-8，对非 ASCII 有损，而它的返回值直接喂给期待 UTF-8 的资源 API。

## 决定

在 `src/core/io/PathUtil.hpp` 的 `Tina::Core::Detail` 下建立唯一定义点。

**头留在 `src/` 而不是 `include/`。** 这里每个函数都点名 `std::filesystem::path`，而没有任何
公共 Tina 头依赖 `<filesystem>`（公共面把路径以 UTF-8 文本发布，见
`core/io/ApplicationPaths.hpp`）。消费方通过把 `${PROJECT_SOURCE_DIR}/src` 加到**私有**包含
路径来取用，`src/desktop` 与 `src/runtime` 早已是这个形态；引号包含从 `src/` 起算，写作
`#include "core/io/PathUtil.hpp"`。本次为 `src/asset`、`editor/src`、`editor/app`、
`src/save`、`tools/assetc`、`tina_tests` 补上了这条私有路径。

**Windows 折叠统一为序数**，即 `::CompareStringOrdinal(..., TRUE)`：locale 无关、整串比较，
与 NTFS 一致。`towlower` 的 4 份按此改正。

`pathComponentEquals` 内联了一段 ASCII 快路径，只在某位置出现非 ASCII 码元时才落到跨 DLL 的
序数调用。资源路径几乎全是 ASCII，而这个头被约 20 个翻译单元包含。一趟扫描足够，因为序数折叠
是**按位**一对一的：若两侧在某下标都是 ASCII 且折叠后不等，后面是什么都不影响结论。这是快
路径，不是第二套语义。

**两个编码器刻意不合并**：`pathToUtf8` 用 `u8string()`，给人读或给调用方重新打开；
`pathToUtf8Generic` 用 `generic_u8string()`，给成为身份或持久化字节的场合——`.tmeta` 的
source-import 路径、glTF 的 identityLocator、recipe 解析出的 payload 路径、编辑器存下再比较的
workspace 根。把其中任何一处换成 `pathToUtf8` 都会静默作废所有既有产物。

**两个逃逸判定也不合并**：`pathHasParentComponent` 只回答"含不含 `..`"；`pathEscapesRoot`
是完整围栏（空、绝对、带根名或根目录、含 `..`）。凡判定结果决定文件系统访问的，用后者——单用
窄的那个会让绝对路径直接穿过去。

`pathIsSameOrDescendant` 从 9 份旧副本继承了两条性质，调用点依赖它们，因此写成契约而非巧合：
空 ancestor 返回 true（循环一次都不进，所以没证明根非空的调用方等于没有围栏）；ancestor 末尾
的分隔符会多出一个空末组件，从而与没有末组件的 candidate 比不上。

第二条有个反直觉之处，`tests/core/PathUtilTests.cpp` 把它钉住了：**`lexically_normal` 并不能
解决它**。那是纯词法改写，会保留末尾分隔符，所以两侧都规范化之后差异照旧。要让两种写法比得上，
调用方得自己去掉那个空末组件（对规范化后的值取 `parent_path` 即可），或者用
`weakly_canonical`——后者要访问文件系统，因此要求路径已存在。选哪个取决于路径是否必须已存在，
故不在这里代做。

### 解码器会抛异常，不是静默通过

`pathFromUtf8Bytes` 遇到无效 UTF-8 时抛 `std::system_error`（MSVC 在 u8string→native 转换阶段
报 "No mapping for the Unicode character exists in the target multi-byte code page"）。它既不校验
也不放行，所以不是 `noexcept`。

这一条是本次改动过程中由测试发现的，先前的头注释把它写成了"不校验"，读起来像是宽松通过——照那
句去写的调用方会以为自己安全。调用方必须二选一：先用 `Core::isStrictUtf8` 校验，或者置于 try
块内。`noexcept` 函数里两者皆无就等于把一条畸形路径变成 `std::terminate`，而这类路径来自文件
对话框，是外部输入。

### 三处刻意不统一

- `src/asset/GltfFileSnapshot.cpp` 用 `CompareStringOrdinal(..., FALSE)`，即**大小写敏感**。
  它比较的是已规范化的最终句柄路径，是第三种语义，不是漏改。
- `src/asset/SourceImportCapture.cpp` 保留 `towlower` 的**变换**（不是比较）。它的输出写进
  `.tmeta` 作为被导入源文件的持久化身份，改折叠方式会让所有已烘焙资产对不上自己的源。
- `editor/src/EditorProjectCreation.cpp` 的 `CompareStringOrdinal` 用于 Windows 保留设备名
  （`NUL`、`CON` 等）检查，不是路径组件比较。

## 结果

跨模块的沙箱判定不再可能分歧，`towlower` 的 locale 与代理对缺陷一并消失。

`samples/3d_product` 的 `path::string()` 同时修掉（改用 `u8string()`，分隔符保持原样，因此不动
gate 可见的输出）。这处比"非 ASCII 会乱码"要重一档：它的返回值喂给
`cookGltfFileToCatalogRequest`，而按 `docs/public-api.md` 该函数是 `noexcept` 边界且**要求输入
是 strict UTF-8**。ACP 编码的非 ASCII 路径不满足这个前提，于是违约输入落在一个不能抛的边界
上——参见上面解码器抛异常那一节。

代价是 `src/save` 等模块的私有包含路径多了 `${PROJECT_SOURCE_DIR}/src`。

`editor/app/EditorWorkspaceState.hpp` 中另有 20 处 deprecated 的
`std::filesystem::u8path` 未在本次改动范围内：它们是标准库调用而非手抄副本，且在一个 5400 行
且有并发改动的头里做 20 处跨行编辑，风险高于收益。

## 被拒绝方案

**把这些函数放进 `include/tina/core/io/`。** 会把 `<filesystem>` 带进公共 SDK 面，与既有约定
冲突——公共面一律以 UTF-8 文本发布路径。

**统一成单一编码器。** 见上：`u8string` 与 `generic_u8string` 的差别决定既有产物是否还能匹配。

**只保留 `pathEscapesRoot` 一个逃逸判定。** `pathHasParentComponent` 的调用点已在自己那侧做
了 `is_absolute()` 检查，合并会把那层检查变成隐式的，反而更难看出围栏是否完整。
