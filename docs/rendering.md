# 渲染与 2D/3D 验收

## 当前实现

bgfx 负责跨平台渲染，现有游戏包含 Tile、Sprite、Primitive、Text、Particle 和 UI 绘制路径。Shader 通过 shaderc 在构建阶段生成平台产物。

## 已知问题

- view ID 与 UI 常量耦合，Pass 的 clear/load/store 规则不集中；
- Scene 和部分系统仍可直接接触 bgfx；
- 当前没有能证明透视相机、深度和静态 Mesh 的最小 3D 场景；
- GPU 资源销毁和设备生命周期没有统一注册与验证。

## 验收目标

1. 2D + UI：主菜单或独立冒烟场景能显示背景、Sprite、中文文本和可点击按钮，调整窗口后布局正确。
2. 3D：独立冒烟场景使用透视相机、深度测试和静态 Cube/Mesh，可旋转观察并正确退出。
3. 所有 bgfx handle 在 shutdown 前按所有权释放，重复启动/退出不泄漏资源。

首期不建设自研 RHI，不实现 PBR、阴影和完整 Render Graph。
