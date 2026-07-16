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

## vNext 数据与内存所有权

- Scene 只生成后端无关 `RenderScene`，其 RenderItem/Light/Camera 数据位于当前帧
  RenderExtraction Arena；
- Pass Scheduler 和 bgfx backend 在同帧消费 descriptor，不保存跨 reset 裸指针；
- Buffer/Texture/Shader/Pipeline 使用 generation handle，由 RenderDevice registry 持久拥有；
- Asset upload payload 使用独立 staging allocation。若 bgfx 接口可能延迟读取 CPU 指针，
  必须复制或绑定 completion release，禁止引用 FrameArena；
- resource create/destroy 与 GPU upload 只在 Render/GPU Upload phase 执行；Worker 只能准备
  CPU 数据和 immutable descriptor；
- 每种 GPU 资源记录 current/peak count 与估算字节，deferred destroy 完成后才递减实际计数；
- UI DisplayList 与 RenderScene 都是帧数据，但使用独立 Arena/容量指标，避免互相挤占预算。

RenderDevice 由主线程唯一驱动 `beginFrame -> submit passes -> endFrame/present`。窗口 resize 只在
帧边界重建/重置 surface；最小化或 framebuffer 为0时进入 SurfaceSuspended，跳过 attachment
创建和 Present。首期 device lost 返回 fatal run error 并安全退出，不承诺透明重建全部资源。

Opaque3D、Sprite2D、UI、Present 定义稳定的相对顺序，不要求每帧强制提交空 draw。纯 UI 帧
可以跳过两个 World pass 且不需要 Camera；Headless Null Present 是可计数的 no-op。任何 pass
是否启用都由当前不可变 RenderScene/Surface 状态决定，不能在执行中临时插入另一套顺序。

每个 typed handle 除 index/generation 外还属于特定 Device/registry；Debug 使用 owner cookie
拒绝跨设备误用。Destroy 进入 backend retirement ledger，只有 GPU completion/fence 后才释放
staging 并递减实际资源计数，不能把固定“N帧延迟”当通用安全保证。Pass 失败必须闭合 marker、
回收 CPU frame data并停止依赖 Pass，但已提交 ticket 仍走正常 retire。

世界/Scene 使用右手 Y-up/-Z forward；UI 使用左上原点、Y-down 逻辑坐标。UI Render extraction
是唯一坐标转换点，layout、hit-test 和 shader 不得分别做隐式翻转。

`tina_bench` 分别记录 extraction、sort、batch、submit 和 GPU frame p50/p95/p99；NullRenderDevice
负责无 GPU 的顺序、handle 和资源归零契约。统一预算见
[性能预算与内存系统](performance-memory.md)。

GPU execution 指标只有校准 timestamp、valid/disjoint 标记和延迟 frame mapping 完整时参与
硬门禁；CPU submit/bgfx 估算 stats 只能作为 informational。VSync-off throughput 与 VSync-on
frame pacing 使用不同 workload，不能混为一份 FPS 结论。
