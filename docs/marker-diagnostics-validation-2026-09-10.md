# Marker 身份、Editor 启动与中文诊断路径验收（2026-09-10）

## 范围与结论

用户授权本次编译、定向测试，并结束无窗口的旧 Editor 后验证新版真实窗口。结果：

- `Marker2D` restore → capture 身份保留、payload 冲突拒绝及 World 边界回归通过。
- Editor 中文 TEMP 路径上的 fatal 追加在 Windows ACP 936 下成功，文件严格 UTF-8。
- 首次实际启动暴露的 `Words + Ellipsis` 非法组合已修复；新版无参数启动显示真实窗口，正常关闭 exit 0。
- 两轮集中增量构建 exit 0；59 个不同的定向 GoogleTest 用例通过，无失败或禁用用例。

本记录不等同于完整 Editor 交互、真实资产导入、3D 操作、GPU 像素或性能验收，也不描述后续安装发布。

## 源码与构建环境

- 仓库：`C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina`
- 基线提交：`c8d10a956685c6e29efb4586ac48ab49ce0eabef`
- 最终已验证源码差异指纹：`cdb3c9073e7429ba90eb7f56a4ed13cdc14df9dd`
- 指纹算法：将下列路径的 `git diff --binary HEAD` 输出送入 `git hash-object --stdin`；不是 SHA256：
  `src include editor tests cmake CMakeLists.txt CMakePresets.json vcpkg.json vcpkg-configuration.json`。
- 常驻树：`out/build/windows-msvc-vnext-bgfx-product-2d`；配置 `Release`。
- Windows / Visual Studio 18 / MSVC `14.50.35717`；CMake `4.2.3-msvc3`。
- `VCPKG_ROOT=D:/Programs/vcpkg`，triplet `x64-windows`。
- 保留既有 CMake cache 与能力开关；首次 build 自动 regenerate 一次，未 clean、未新建 build tree。
- 源码/文档/证据写为 UTF-8，MSVC 使用 `/utf-8`；Windows 文件路径使用原生 `std::filesystem::path`。

### 构建

```powershell
$BuildTree = 'C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina/out/build/windows-msvc-vnext-bgfx-product-2d'
& 'D:/Programs/CMake/bin/cmake.exe' --build $BuildTree --config Release `
  --target tina_scene_tests tina_asset_format_tests tina_editor_app_tests tina_editor_desktop `
  --parallel 2 -- /nr:false

# 修复首次真实启动暴露的问题后，仅重编直接受影响的 Editor target。
& 'D:/Programs/CMake/bin/cmake.exe' --build $BuildTree --config Release `
  --target tina_editor_app_tests tina_editor_desktop --parallel 2 -- /nr:false
```

| 批次 | target | job | 结果 |
| --- | --- | --- | --- |
| 首次统一增量构建 | Scene tests、AssetFormat tests、EditorApp tests、Editor desktop | `j-aszwam` | exit 0；包含 `Marker2DHeader.cpp` isolation 编译 |
| 启动修复后定向重编 | EditorApp tests、Editor desktop | `j-7us1pe` | exit 0；实际重编 `EditorWorkspaceUiRecipes.cpp` 并链接 |

两份日志按 `fatal error`、MSVC error/warning 编号、`CMake Error/Warning` 扫描均无命中；不将此扩展为所有工具输出均无诊断。

## 定向测试

直接运行以下 Release GoogleTest executable，未使用 CTest：

| executable | `--gtest_filter` | 最终结果 |
| --- | --- | --- |
| `tina_scene_tests.exe` | `SceneWorldTest.*:World2DSnapshotSceneTests.*` | 44/44，exit 0 |
| `tina_asset_format_tests.exe` | `World2DSnapshotTests.*` | 7/7，exit 0 |
| `tina_editor_app_tests.exe` | `EditorSourceImportLaunchOptionsTests.*` | 8/8，exit 0 |

共 59 个不同用例。Editor filter 在启动修复重编后重跑一次，故实际 GoogleTest 调用为 4 次；
Scene/AssetFormat 未因纯 Editor recipe 修改而重复运行。

新增的 4 条 Scene 回归覆盖：

- `SceneWorldTest.MarkerTagSupportsTypedQueriesAndResetsOnSlotReuse`
- `SceneWorldTest.MarkerTagRejectsForeignEntitiesAndWrongThreadAccess`
- `World2DSnapshotSceneTests.MarkerIdentitySurvivesRestoreCaptureAndRuntimeEdits`
- `World2DSnapshotSceneTests.CaptureRejectsMarkerMixedWithPayloadInsteadOfDroppingIdentity`

Marker tag 由 World 自有 slot 保存，不依赖 metadata 或一次性索引，不新增动态分配；复用已有 wire kind，
schema v7 / record 480 bytes 不变，没有兼容双轨。本轮未执行完整 Scene/Editor 测试集。

## 真实启动：失败、定位、修复、复验

1. 用户授权的旧 Editor PID `15712` 经身份核对后结束。
2. 第一次无参数启动 PID `28748`，在窗口显示前 exit 1。fatal report 明确记录：
   `UI wrapped text cannot use single-line Ellipsis overflow`，domain 9 / code 16；
   调用链为 `PrimaryWindowUITreeUpdater::setTextOverflow` → `IGameState::onEnter` → startup rollback。
3. `makeLabelElement()` 默认 `Words`；`EditorPanelHeader`、`EditorSectionHeader`、`EditorPropertyRow`
   三个 recipe 曾直接加 `Ellipsis`。在创建 descriptor 时显式设 `NoWrap`，保留 UI core 的非法组合校验。
4. 重编后的无参数启动 PID `17520` 通过 Computer Use 实际观察窗口截图：
   `Tina Editor - 2D / 3D`（window ID `13047322`），正常显示 2D Scene 等距网格、Hierarchy、
   Project Assets 与 Inspector；为空项目，无启动错误弹窗。
5. 对已观察到的窗口发送 `Alt+F4`，进程 exit 0，stdout `status=ok`，stderr 为空；
   crash report 仅含本次 armed marker，没有 fatal/crash。确认剩余 Editor 窗口为 0。

此次交互期间记录 `frames=8997`、`targetFrames=0`、`autoDemo=false`、`profileUi=false`；
约 103.9 秒的运行时长不是帧率或性能 benchmark。截图已在任务会话内查看，未另存 PNG。

## 中文路径诊断

诊断探针故意传非法参数 `--frames=0`，预期 exit 2，实际 exit 2；这不是失败的测试结果。
TEMP/TMP 指向自有 `fatal-中文日志` 目录，APPDATA/LOCALAPPDATA 也隔离到自有 fixture，未改用户设置。

- 实际进程 ACP 为 936，最终探针 PID `9960`。
- `tina_editor_crash.txt` 存在，严格 UTF-8 解码成功。
- fatal section、预期参数错误信息、origin 和 `"status":"fatal"` 均存在。
- `std::ofstream` 使用缓存原生 path；CrashHandler 的 UTF-8 参数从同一路径派生。
- 探针进程退出；初次与启动修复后的两次探针均通过。

## 产物指纹

产物目录：`C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina/out/build/windows-msvc-vnext-bgfx-product-2d/bin/Release`。
依赖 DLL 与字体按产品构建规则一同 stage，不单独搬运 EXE。

| 文件 | 字节数 | 修改时间（UTC+8） | SHA256 |
| --- | ---: | --- | --- |
| `TinaEditor.exe` | 9,982,976 | 2026-09-10 20:48:44 | `39ab845b37d13eb717182948762557a9dccf25fbe0615c7ccbf34f2abbd24f19` |
| `tina_scene_tests.exe` | 1,955,328 | 2026-09-10 20:20:35 | `ae7765abd314cfb1dd39b686d6a591ca82b68103bac940aac82689f902395439` |
| `tina_asset_format_tests.exe` | 1,256,448 | 2026-09-10 20:20:51 | `df14fed6491d17102cff88d58487c4c79b1da44d8a782334558a31dd89c612b4` |
| `tina_editor_app_tests.exe` | 1,537,536 | 2026-09-10 20:48:15 | `e738819b5c0672723f6500970ac9415394e80971f678737f5f87d78d74953a98` |

## 证据与资源收尾

本地证据目录：`out/validation/marker-diagnostics-20260910-201847`（构建产物，不入 Git）。
包含两轮 build log、GoogleTest log/JSON、初次失败与最终成功的 startup/fatal report、
`editor-startup-after-fix/window-verification.json`、前后资源 ledger 及复现脚本。

2026-09-10 21:11（UTC+8）资源实测：

| 资源 | 前 → 后 / 状态 |
| --- | --- |
| 首轮 `runtime-fixtures` | 48,219 bytes / 5 files → 0 bytes；路径已核验不存在 |
| 修复后 `editor-startup-after-fix/runtime-fixtures` | 66,802 bytes / 6 files → 0 bytes；路径已核验不存在 |
| 常驻 build tree / cache | 18,430,517,566 → 18,431,480,679 bytes；13,959 → 13,960 files，保留供核心 Windows 增量构建 |
| 临时 build tree | 本轮创建 0 |
| 本轮进程 | 已登记 Editor/runner PID 均不存在；compiler/linker/build/test 进程查询为空，两项 FastCtx job 均 exit 0 |
| container / volume / image | 本轮均未创建；不声称全系统无其他任务资源 |
| agent | 未创建子代理 |
| 证据目录 | 清除 fixture 后、写最终 ledger 前 138,035 bytes / 42 files，保留日志和脚本 |

删除前核验两个精确目标都位于本轮 evidence 目录内且祖先/子项无 reparse point；只删除这两个自有 fixture，
合计回收 115,021 bytes。计量使用逻辑文件长度，build tree 遍历跳过 reparse point。
共享 FastCtx/Computer Use 服务、外部 vcpkg cache 和常驻 build tree 均未停止或删除。

## 尚未验收

Ctrl+S / Ctrl+D、3D WASD、Play/Stop、真实资源导入/保存/重开、GPU 像素对照、性能 profiling，
以及更广的跨平台 CrashHandler 契约。因此 `EDITOR-STARTUP-001` 与 `CORE-DIAG-001` 继续 InProgress；
本轮完成的 `SCENE-MARKER-ROUNDTRIP-001` 标为 Done。
