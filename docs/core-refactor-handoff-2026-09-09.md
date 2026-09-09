# 2026-09-09 核心模块重构与 Editor 界面收口

## 续接边界

本批接续 Tina 的 3D 场景运行时、动画事件与 Editor Play 工作树，不覆盖此前未提交修改。
目标是补齐实际消费链与所有权缺陷，而不是再新增一层 Manager 或兼容旧 API。
Editor 只整理现代桌面布局、视觉层级与基础操作，不引入另一套 UI 框架。

## 已落地源码

### Render / Runtime

- Shader payload 唯一现行版本为 **v3**；新增 `ShaderKind::PostProcess` 与对应 Render kind。
  CLI cook、Asset upload、bgfx program linking、Null 类型/寿命校验与 SDK shader include 安装已接线。
- PostProcess 使用 fullscreen triangle + `tina_postprocess.sh`；source/auxiliary 占 stage 0/1，
  作者 sampler 从 stage 2 起，最多 8 个。错误 kind、失效 program、已退役的显式材质纹理在 submit 前拒绝。
- `PrimaryPostProcessSettings` 接收当前 packet 的 Shader/ShaderUniforms refs。Runtime 管理目标、resize
  事务与至多两张 RGBA16F ping-pong；自定义步骤在内建效果之后、最终 tone mapping/UI 之前执行。
- 三种 Shader kind 共用一个显式 typed lookup；Shader 反射/注册改为 RAII 回滚。
  author uniform/sampler 的小型索引缓存按硬件上限内联，不在不可失败的 submit 阶段分配。
- 新消费者 `tina_sample_postprocess_custom` 以同一程序、两套独立材质验证完整路径；只有显式
  `--verify` 才启用双相像素比较，不能把 Null 接受 binary 写成 GPU 效果通过。

### Asset 材质实例

- `ShaderMaterialInstanceId` 改为 Core `GenerationPool` 的正式强类型 ID；同时检查 owner/index/generation。
  不保留旧二元组构造，registry move 保留身份，跨 registry/stale ID 拒绝，generation 耗尽永久退役槽位。
- 活跃实例由稳定 pool storage 托管，FramePin 直接借用稳定 entry；销毁实例先要求 pin 已归还。
- 存在该 shader 的材质实例时，直接退役和 Catalog replacement 都失败并保留 owner，不让 CPU lease
  看似活着、GPU program 却已被另一条路径销毁。

### Editor 工作台配方

- 复用现有 Dark/Light 语义 surface、Compact typography 与全部控件行为。
- 文档标签宽度由内容决定，并约束在 96..224 logical px；长标题省略但无障碍文本仍完整。
- 面板标题让出空间给右侧命令；图标不参与压缩，section/property label 和 value 区域允许正确收缩。
- 不扩建编辑器产品能力，不修改菜单/文件/undo/Play 的业务路径；没有截图时不记录视觉通过。

Gameplay3D 与动画保留上轮工作树实现；车队审查未返回证据前不把它们记成本批新增修复。

### Core JSON parser budget

- `JsonDocument::parse` 改为 nlohmann SAX 事件直接构建 Tina-owned 节点，不再先生成完整第三方 DOM。
- `maxInputBytes`、`maxDepth` 与 `maxNodes` 在解析/节点分配阶段 fail closed；公开 `JsonParseOptions` 注释已同步。
- 现有 JSON 访问 API 与错误码保持不变；`JsonDocumentTest` 过滤运行 7/7 通过，`tina_tests` 增量编译 exit 0。

## 迁移

1. 所有旧 Shader payload 必须重新 cook；不按旧版本嗅探，也不复用旧 compiled program 绑定新 ABI。
2. Shader sample 的 cook 依赖包含 cooker executable，工具更新会触发 payload 重建。
3. 材质实例只通过 registry 创建和销毁；读取诊断身份使用 `owner()` / `index()` / `generation()`，
   不构造或持久化 runtime ID。
4. Primary custom effect refs 必须每帧重新 intern，不能把上一帧 settings 缓存成持久 material owner。

## 验证状态

本次未收到自动运行门禁的选择，执行范围取 **compile-only**；`testRuns=0 sampleRuns=0`，
没有启动 Editor、sample 或 GoogleTest。回归代码覆盖当前 Shader schema、PostProcess kind/retirement、
Runtime packet refs/resize 回滚/suspend/ping-pong，以及材质实例 owner/move/pin/失败回滚，但尚未执行。

复用唯一常驻树 `out/build/windows-msvc-vnext-bgfx-product-2d`，MSVC 14.50 / CMake 4.2.3，Debug，
并显式启用 Physics3D，使本次 Editor 包含 3D physics bridge。重配该树时须同时带上 feature 列表：

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d -DTINA_BUILD_PHYSICS3D=ON `
  "-DVCPKG_MANIFEST_FEATURES=tests;platform-glfw;physics2d;physics3d;ui-freetype;audio-miniaudio"
```

- configure：exit **0**。
- 第一次构建批次：三个产品目标完成；`tina_tests` 因新文件漏 include `RenderFramePacket.hpp` 而失败，批次 exit **1**。
- 定向续编：`tina_tests` 完成；发现上轮 `PrefabPayloadHeader.cpp` 仍断言 v4，当前源码是 v5，批次 exit **1**。
- 对照源码扫描同类 schema 断言并修复后，只编译余下四个目标，exit **0**；没有重编已完成的产品或运行测试。
- 最后 `git diff --check`：exit **0**；公开头第三方 token 扫描只有已有注释，没有新第三方类型/include 泄漏。

所有产物位于该树的 `bin/Debug`，时间为 Asia/Shanghai：

| 产物 | 编译状态 | 时间（2026-09-09） | 字节 |
| --- | --- | --- | ---: |
| `TinaEditor.exe` | 完成 | 05:13:16 | 34,173,952 |
| `tina_sample_postprocess_custom.exe` | 完成 | 05:13:41 | 18,188,288 |
| `tina_sample_3d_authored_level.exe` | 完成 | 05:13:58 | 25,135,104 |
| `tina_tests.exe` | 完成，未运行 | 05:17:03 | 21,821,440 |
| `tina_asset_format_tests.exe` | 完成，未运行 | 05:24:39 | 4,464,640 |
| `tina_asset_tests.exe` | 完成，未运行 | 05:26:02 | 17,265,152 |
| `tina_gameplay3d_tests.exe` | 完成，未运行 | 05:26:14 | 90,624 |
| `tina_physics3d_tests.exe` | 完成，未运行 | 05:26:24 | 5,611,008 |

证据目录：`out/validation/core-refactor-20260909/`，保留原始 configure/build 日志与 `resources.json`。
基础 commit 为 `8827bb546396d4b50018d6e6472b27b15f5cae82`；101 个 modified/untracked 源码、测试与构建输入
的 SHA-256 指纹为 `3CFA1716D245B969831088C1280C1EF22E458030DD51AEDCF6E2086A208746A7`。
逐个二进制 SHA-256 在 JSON 中，后续获授权运行时先核对，不为相同输入重复编译。

## 资源收尾

| 资源 | 核验结果 |
| --- | --- |
| buildTree | 常驻核心树保留：16,385,138,767 → 16,970,562,153 bytes；用于后续增量编译，不做全量 wipe；无新临时 build tree |
| process/helper | 已结束 cmake/MSBuild/cl/link/lib/rc/mt/shaderc/assetc；确认 `mspdbsrv` PID 31884 于本轮构建开始后创建且编译器均已退出，再定向停止；复查残留 **0** |
| temporary | Shader cooker 三个已知临时 `.bin` 路径均不存在；收尾前/后残留均 **0 bytes**，最终 `.shaderpayload` 属正式产物保留 |
| container/volume/image | 本轮均未创建，owned 数量 **0/0/0**；未执行 Docker 或全局清理 |
| cache | 未创建独立一次性缓存；build tree 内缓存计入上方占用；共享 vcpkg cache 保留，单独占用未计量，不声称已释放 |
| agent | 3 个子代理未回传可用结果，全部 interrupted；活动子代理 **0**，其任务不计入完成项 |
| evidence | configure/build 首错和产物 hash 保留在证据目录，不是待删除的临时 build tree |

本批未安装到 D 盘，未提交/推送，未执行 Linux/Docker 或全局缓存清理。

## 下一步验收

人工先检查：打开项目与真实资源导入、3D Play/Stop、窗口缩放、窄 Inspector 下长标题与操作按钮。
获明确 test gate 授权后，再执行已编译的核心回归、已有 Editor tests 与相关短 smoke；
自定义后处理像素证据入口为 `tina_sample_postprocess_custom --verify --frames=24`。
这份编译记录不替代真实交互、GPU 像素或功能单元测试的结论。
