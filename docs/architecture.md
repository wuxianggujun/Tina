# 架构总览

## 当前实现

Tina 仍以单个游戏可执行文件为主，源码按 Core、Engine、Renderer、UI、ECS 和 Game 组织。第三方包由 vcpkg manifest 管理，bgfx、EASTL、EABase 保持固定源码版本。

依赖方向应保持为：Core → Platform/Engine → ECS/Renderer/UI → Game。第三方类型不得继续向不相关层扩散。

## 已知问题

- Application 仍承担过多初始化、主循环和服务访问职责；
- Scene 同时管理状态、相机、UI、渲染视图和事件订阅；
- GameScene 混合世界、ECS、输入、音频、UI 和渲染编排；
- Renderer 中存在多套未统一的命令与高层绘制入口。

## 目标契约

- 不新增全局 Singleton 或 Service Locator；
- 初始化必须显式返回错误，失败后按逆序释放已创建资源；
- 2D/3D 共享右手 Y-up 世界，2D 位于 XY 平面；
- bgfx 只出现在渲染实现层；
- 自研 UI 只输出后端无关 DisplayList；
- 每个阶段都能独立测试和提交，不进行不可验证的大爆炸重写。
