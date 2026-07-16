# 渲染与 2D/3D 验收

## 当前实现

bgfx 负责跨平台渲染，现有游戏包含 Tile、Sprite、Primitive、Text、Particle 和 UI 绘制路径。Shader 通过 shaderc 在构建阶段生成平台产物。

`Smoke3DScene` 提供独立的最小 3D 运行路径：`Camera3D` 使用右手坐标、Y-up、-Z forward 的透视矩阵，`SimpleMeshRenderer` 提交带顶点色的静态索引 Cube，并显式启用 depth write 和 `DEPTH_TEST_LESS`。vertex/index buffer 由 Renderer 对象持有，在 ShaderManager 和 bgfx shutdown 前释放。

## 已知问题

- view ID 与 UI 常量耦合，Pass 的 clear/load/store 规则不集中；
- Scene 和部分系统仍可直接接触 bgfx；
- 当前 3D 只验证 Camera、静态 Mesh、离线 shader 和深度链路，尚未接入 glTF/Cooked Asset、材质与纹理；
- GPU 资源销毁尚未统一注册到通用设备生命周期，但 2D/UI sampler 与 3D vertex/index buffer 已分别做 RAII 回收和退出验证。

## 验收目标

1. 2D + UI：主菜单或独立冒烟场景能显示背景、Sprite、中文文本和可点击按钮，调整窗口后布局正确。
2. 3D：`--smoke-3d` 使用透视相机、深度测试和静态 Cube，可旋转观察并正确退出。投影封装显式接受 degrees，并有矩阵回归测试；运行门禁还必须截图确认 Cube 可见，避免“资源创建成功但画面错误”的假阳性。
3. 所有 bgfx handle 在 shutdown 前按所有权释放，重复启动/退出不泄漏资源。

首期不建设自研 RHI，不实现 PBR、阴影和完整 Render Graph。
