# 架构总览

## 当前实现

Tina 仍以单个游戏可执行文件为主，源码按 Core、Engine、Renderer、UI、ECS 和 Game 组织。第三方包由 vcpkg manifest 管理，bgfx、EASTL、EABase 保持固定源码版本。

依赖方向应保持为：Core → Platform/Engine → ECS/Renderer/UI → Game。第三方类型不得继续向不相关层扩散。

## 已知问题

- Application 仍承担过多初始化、主循环和服务访问职责；
- Scene 同时管理状态、相机、UI、渲染视图和事件订阅；
- GameScene 混合世界、ECS、输入、音频、UI 和渲染编排；
- Renderer 中存在多套未统一的命令与高层绘制入口。

## 旧架构删除状态

结论：旧文档已经替换，但旧源码架构没有完全删除。当前也不存在 `TINA_BUILD_LEGACY` 开关或一套可独立运行的新 Runtime；现有旧架构就是 Tina 当前主实现，不能直接整目录删除。

| 范围 | 状态 | 证据或影响 |
| --- | --- | --- |
| 旧阶段文档 | 已删除/替换 | `docs` 只保留当前架构、契约、验证和 Roadmap |
| 单体游戏 target | 仍在使用 | 主程序仍由一个 `Tina` executable 汇集 Engine、Game、Renderer、UI 和 ECS |
| Core compatibility | 仍在使用 | 主程序和测试仍链接 `Tina::CoreLegacy` |
| `Application` 组合根 | 仍在使用 | 继续持有 Window、Input、Event、Scene、Resource、Audio 和渲染服务 |
| Scene/GameScene 旧职责 | 仍在使用 | GameScene 仍混合玩法、ECS、输入、UI、音频与渲染编排 |
| EnTT 边界 | 尚未收敛 | `World` 暴露 `entt::registry`，GameScene 直接访问 |
| bgfx 边界 | 尚未收敛 | Renderer、UI 和部分公共结构仍直接暴露 bgfx handle/type |
| 路径资源系统 | 仍在使用 | Cooked Asset、稳定 AssetId 和独立 GPU upload queue 尚未落地 |
| 新 Runtime 接口 | 尚未落地 | `EngineHost`、`EngineContext`、RenderDevice 和 NullRenderDevice 仍是目标设计 |

因此不能用“删除旧 `src`”作为下一步。正确顺序是：建立新边界和测试 → 迁移调用点 → 确认旧接口零引用 → 通过 2D/UI/3D 验收 → 在独立提交中删除旧实现。

旧模块只有同时满足以下条件才能删除：

1. 已有明确替代模块和所有权关系；
2. 生产代码与测试不再 include、链接或调用旧接口；
3. Windows/Linux 构建和直接 GoogleTest 通过；
4. 2D、UI、3D 冒烟和资源回收门禁通过；
5. 删除操作不夹带新功能，能够独立回滚。

## 目标契约

- 不新增全局 Singleton 或 Service Locator；
- 初始化必须显式返回错误，失败后按逆序释放已创建资源；
- 2D/3D 共享右手 Y-up 世界，2D 位于 XY 平面；
- bgfx 只出现在渲染实现层；
- 自研 UI 只输出后端无关 DisplayList；
- 每个阶段都能独立测试和提交，不进行不可验证的大爆炸重写。
