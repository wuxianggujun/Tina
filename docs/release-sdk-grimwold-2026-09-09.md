# 2026-09-09 · Release 安装与外部 Grimwold Demo

## 交付

| 产物 | 绝对路径 | 上海时间 / 大小 |
| --- | --- | --- |
| Release SDK | `D:/ProgramData/Tina` | `293,278,421 bytes`（含保留的原有文件） |
| Editor | `D:/ProgramData/Tina/TinaEditor/TinaEditor.exe` | 2026-09-09 08:01:08 / 9,952,256 bytes |
| Cooker | `D:/ProgramData/Tina/bin/tina_assetc.exe` | 2026-09-09 07:55:44 / 1,321,984 bytes |
| Grimwold | `C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Grimwold/build/bin/Release/desktop/grimwold.exe` | 2026-09-09 12:08:32 / 5,572,096 bytes |

复用 `out/build/windows-msvc-vnext-bgfx-product-2d`，保留 Physics3D / bgfx / FreeType / UIA / Audio / Physics2D。
编译 `tina_sdk_install_artifacts`、`tina_editor_desktop` 的最终结果为 exit 0，SDK 与 Editor 安装均 exit 0。
没有 clean-first，没有新建重型构建树，没有清空 D 盘目录，没有提交或推送现有工作区修改。

## SDK 分发修复

- `FindHarfBuzz.cmake` 同时服务 producer 和 installed consumer，不依赖 vcpkg config 中未设置的内部变量。
- `TinaRuntimeDependencies.cmake` 从目标配置的原始 DLL 解析间接依赖，再核对实际 executable，按文件内容更新 DLL。
- 明确修复了旧 Debug GLFW 与 Release GLFW 时间戳相同，以及旧 app-local PNG 被解析器优先选中的情况。
- SHA-256 核对覆盖 **34 个 SDK archive、349 个 Tina 公开头、Editor / Grimwold 的 16 个 DLL 对比项**，以及
  Cooker / Editor executable、字体 seed、新 postprocess shader authoring inputs 和安装 CMake helper。

不把仅有文件时间或 CMake install 的 “Up-to-date” 当作配置匹配的证据。旧目录中未被本轮安装规则管理的文件没有主动删除；
本轮交付与验证仅承诺 Release 配置，不承诺遗留 Debug export / 文件的完整性。

## 外部游戏验证

Grimwold 已重新链接安装版 SDK。联调中复现并修复了菜单→世界首帧查询未提交 HUD 布局的问题；
相机比例改用公开 Platform 窗口通知，未修改 Runtime 的阶段错误语义。
画面复查另发现重复的 UI 图集注册在旧状态退出时使新 HUD 失效；游戏现以 `GameResources` 共享唯一 UI 资源所有者，
root 各自注册 resolver，新增 `ui_atlas_bound` 不变量检查。最终 framebuffer 已确认图标、状态条、快捷栏恢复。

游戏新增可复用的 `--menu-smoke`、`--menu-world-smoke` 与 `tools/verify_sdk_demo.py`。最终校正 DLL 后的套件
**7/7 exit 0，无超时、无残留测试进程**：三组现有回归（Gameplay 683 条断言）、菜单 180 帧、菜单→世界 600 帧、
挖掘 600 帧、移动 600 帧。脚本不 configure/build，记录 executable / DLL hash，并给每个进程设置 60 秒墙钟上限。

用户后续授权了测试与游戏启动；最初的 Release 编译安装阶段没有启动产品或运行测试。此次没有执行 Editor、Tina 全套 GoogleTest、
Linux 构建或完整平台 / 视觉 gate。真实鼠标键盘、存档流程及性能仍是独立验收项，不能由自动 State 切换替代。

## 资源账簿

所有字节数均为明确目录内文件长度合计；保留项不是“已经删除”。

| 类别 | 核验结果 |
| --- | --- |
| buildTree / Tina | `C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina/out/build/windows-msvc-vnext-bgfx-product-2d`；16,970,562,153 → 17,389,576,278 bytes，常驻增量树保留 |
| buildTree / Grimwold | `C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Grimwold/build`；初始 170,529,458 bytes，最终占用见 `resources-final.json`，游戏产物及增量树保留 |
| installTree | `D:/ProgramData/Tina`；293,898,501 → 293,278,421 bytes，用户要求的安装目录保留 |
| process | cmake / MSBuild / cl / link / lib / rc / mt / mspdbsrv / shaderc / msdfgen / assetc / Grimwold / 三个回归程序剩余 0 |
| temporary build / directory | 本轮未创建，0 → 0 bytes；无额外 Linux / staging / consumer build tree |
| container / volume / image | 本轮创建及剩余均 0；未操作其他任务资源 |
| cache | 独占临时 cache 0；上述两棵常驻树保留。共享 `D:/Programs/vcpkg` 与 Codex runtime 未单独计量、未清空 |
| agent | 本阶段新建 0、活动子代理 0；历史三个子代理保持 interrupted |
| evidence | `out/validation/release-install-20260909-074921` 保留日志、hash、复现脚本与 framebuffer 图片；最终字节数见 `resources-final.json` |

Computer Use JavaScript session 已 reset。没有声称共享工具服务或全机缓存已经被释放。

## 证据

```text
C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina/out/validation/release-install-20260909-074921/
  build-release.log
  build-release-packaging-fix.log
  build-release-editor-staging.log
  runtime-closure-results.json
  install-audit.json
  demo-suite-shared-ui/summary.json
  resources-final.json
```

失败记录保留：外部 HarfBuzz 链接失败、SDK DLL 闭包第一次实现中的源/副本路径冲突、菜单→世界旧布局查询错误。
早期数字门禁未覆盖 HUD 缺图；修复前后的 framebuffer 均保留，最终记录以 `demo-suite-shared-ui` 为准。
只集中修复直接受影响项后续编、安装或重跑；没有以旧成功日志替代本轮结果。UTF-8 与 MSVC `/utf-8` 保持不变。
