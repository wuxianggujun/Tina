# 等距 2D 修复交接（2026-09-10）

## 实现与边界

本轮按 [ADR 0059](adr/0059-isometric-2d-extraction.md) 完成单轨迁移：Render 只消费已投影的仿射 quad；
Scene/Particle 使用 billboard，Tile/Trail 投影完整地面几何。空间 depth 与完整 authored order 分开排序，
保留旋转 pivot、负 scale、地图 origin/elevation 和非默认相机 basis。World2D 升为 v7 / 480 bytes，
旧 schema 直接拒绝，没有旧 API 别名、兼容分支或双读路径。

性能改动包括 Tile 可见集与 scratch 复用、每层一次资源解析/每 chunk 一次 cells 查询、Particle 同次提取的
连续 handle 解析复用、Trail append 阶段缓存几何、每相机一次旋转计算，以及移除 Editor 碰撞覆盖层每帧的整文档解析。
这些是源码可核对的工作量减少，不是 FPS 或延迟提升的实测结果。

Editor 的网格、导航、落点、拾取、gizmo 与笔刷使用相同 basis；拾取忽略全透明 Sprite，框选按实际 quad
边界而不是离屏 pivot 筛选。Box 碰撞覆盖使用投影四边，Circle 使用解析 ellipse，Capsule 仅显示保守 bounds。

## 编译证据

- 环境：Windows / MSVC 14.50.35717 / Visual Studio 18 2026 / CMake 4.2.3-msvc3 / Release。
- 常驻树：`out/build/windows-msvc-vnext-bgfx-product-2d`，未 clean、未创建其他 build tree。
- 以下 **23 个 target 最终均完成编译与链接**，包括相应 header-isolation 编译；没有运行测试程序。

```text
tina_editor_desktop
tina_tests
tina_render_scene_tests
tina_render_bgfx_tests
tina_asset_format_tests
tina_asset_tests
tina_scene_tests
tina_gameplay2d_tests
tina_physics2d_tests
tina_editor_tests
tina_editor_app_tests
tina_sample_2d
tina_sample_2d_infrastructure
tina_sample_2d_infrastructure_bgfx
tina_sample_2d_authored_scene
tina_sample_2d_tilemap
tina_sample_terraria
tina_sample_2d_catalog
tina_sample_asset
tina_sample_2d_custom_shader
tina_sample_2d_shader_materials
tina_sample_2d_shader_lighting
tina_sample_video_playback
```

构建使用 `cmake --build ... --config Release --target ... --parallel 2 -- /nr:false`。
首次批次 `j-aolq63` 在既有 bgfx contract test 的错误常量声明处退出 1；改为引用正式 Shadow 定义。
续批 `j-3b33h0` 在 Asset sample 残留的 `AssetRetirementStats::released` 处退出 1；直接迁移为 `releasedTotal`，
并把诊断字段改为 `retirementReleasedTotal`。该批另外发现的三处 Physics shutdown 返回值忽略已改为断言或诊断检查。
最后只重编失败、未执行及直接受影响目标，`j-3y5ecm` **exit 0，无编译错误或警告**；之前成功的无关目标未重复构建。
日志保存在 `C:/Users/wuxianggujun/.fastctx/jobs/<job-id>/output.log`。

源码检查：`git diff --check` 通过；新头文件、测试与 ADR 已纳入 Git 变更清单，未提交。
受影响公开头的第三方类型/include 定向扫描无命中。Web consumer 只完成源码迁移，未进行 Web/Linux 构建。

## 产物与资源核验

- Editor：`out/build/windows-msvc-vnext-bgfx-product-2d/bin/Release/TinaEditor.exe`
- 大小：9,981,952 bytes；写入时间：2026-09-10 18:02:39 +08:00。
- SHA-256：`2E027E23B6A7C1F499FE4BF740C380C1D8FA650DA29B964ED7BE0C3D8E398403`。

| 资源 | 本轮收尾状态 |
| --- | --- |
| buildTree | 常驻树从 18,381,580,928 增至 18,436,603,742 bytes；为后续增量构建保留，不做清理 |
| process | 三个构建 job 均已退出；核查 cmake/MSBuild/cl/link/mspdbsrv/shaderc/assetc/msdfgen 及相关产品/测试进程，匹配数 0 |
| helper | 未创建额外 helper/watchdog/窗口管理器；没有运行中的本轮后台构建 |
| container / volume / image | 本轮未创建或启动；没有本轮待回收项，不对其他任务的资源作全局释放声明 |
| cache | 未创建专用临时缓存；常驻 CMake/vcpkg 缓存随上述 build tree 保留 |
| agent | 未创建子代理 |

本轮没有创建临时 build tree 或临时目录，不执行无关目录删除。

## 未执行项

`testRuns=0`、`sampleRuns=0`、`editorRuns=0`。本轮只编译，没有 GoogleTest、smoke、真实 Editor 交互、GPU 像素或 profiling 证据。
回归用例已覆盖非默认 basis、旋转 pivot/负 scale、Tile/FX 投影、极端 authored order、fractional depth、
旋转/逆投影裁剪、相机 wire/Scene round-trip 与跨帧资源解析；这些用例已编译，尚未运行。
后续运行验收应分别记录单测、编辑器地面/宿主/特效对齐、保存重开以及固定场景性能采样，不能以本轮 Build 证据代替。

## 后续启动故障修复

用户双击旧 Release 时，2026-09-10 18:29:41 +08:00 的 `tina_editor_crash.txt` 明确记录
`one physical control may have only one binding in the default input context`，context 为
`EngineHost::Create — EngineConfig validation`。Editor 的 `S` 同时绑定 Save / PlayerBackward，`D`
同时绑定 Duplicate / PlayerRight，导致创建窗口前主动退出；与 SDK 是否安装到 D 盘无关。

修复将每组重复绑定合为共享 frame action，由编辑/Playing 状态与 Ctrl / Alt 决定行为；同步迁移 input
cancellation、玩家轴输入和快捷键消费者，删除旧 action，不留别名。静态绑定表增加 `static_assert` 唯一性
检查，EngineConfig 运行时校验保留。Windows 无参数启动返回错误时显示可见提示；带参数自动化仍只返回退出码
并写日志。中文提示使用 UTF-8 源文件、MSVC `/utf-8` 与 `MessageBoxW`，不经过 ANSI 代码页。

该启动修复随后只增量构建 `tina_editor_desktop` / Release，job `j-ac1310` 退出 0；CMake 因显式补齐
`user32` 链接自动重新生成，仍复用原常驻 build tree。没有运行 GoogleTest 或 sample。
用户授权的一次无参数窗口启动调用在 Computer Use 阶段被物理 Escape 中止，未取得窗口截图或成功启动证据；
不能将编译结果记为启动验收通过。之后用户要求其他会话继续编译，本任务停止构建和窗口操作。

启动前的 fatal 日志和产物指纹已保存在本地 `artifacts/editor-startup-20260910-185205/`，不纳入版本控制。
该次增量构建前常驻树为 18,436,603,742 bytes；中止后未重新采样大小或核验 Editor/工具 helper 进程状态，
因此不宣称运行验证资源已全部回收。上文的产物 hash 与资源表仅对应此前 23-target 编译批次。
