# ADR 0042：清屏颜色由 RenderScene 每帧携带

- 状态：Proposed
- 日期：2026-09-02
- 决策者：Tina maintainers

## 背景

`src/render/bgfx/BgfxRenderDevice.cpp:266` 有一行 `constexpr u32 kClearRgba = 0x102a43ff;`，
四个 view 配置函数（`configureSurfaceClearView`、`configureMesh3DView`、`configureSprite2DView`、
`configureUIView`）把它直接交给 `bgfx::setViewClear`。这是引擎里唯一的背景色，产品无法改。

公共 API 面上跟清屏相关的只有三个布尔 —— `RenderPassPlan::clearColor` 与
`RenderPostProcess` 里两个 —— **都只表达「要不要清」，没有「清成什么色」**。
`RenderPerspectiveCameraInput`、`Mesh3DLightingDesc`、`RenderSceneWriter` 上都没有颜色入口。

后果不是审美问题。`samples/3d_voxel` 加入随时间转动的方向光后，地面明暗与色温确实随之变化
（同机位对照捕获：明暗阶数 35→53，动态范围 29..63 → 8..74），但天空占据画面上半部且
**恒为那片深蓝**，把变化冲淡到"看不出有日夜循环"。旗舰样例 `samples/3d_product` 同样没有天空：
它上传 EnvironmentMap，但那张图只在 `fs_tina_opaque3d_mr.sc` 的 `shadeImageBasedLighting()` 里
被 `s_iblDiffuse`/`s_iblSpecular` 采样用于**光照**，从不作为背景绘制。

已确认的边界条件：

- `kClearRgba` 只有 5 处引用，全部在 `BgfxRenderDevice.cpp` 内；
- 没有任何测试、gate 或文档断言 `0x102a43`；`--expect-pixel-fingerprint` 是调用方传值的 opt-in，
  仓库内没有记录在案的基线；
- `docs/rendering.md` 的冻结条款约束 `RenderPassKind` **枚举**，不涉及清屏颜色；
- backbuffer 未启用 `BGFX_RESET_SRGB_BACKBUFFER`，shader 末尾自行 `linearToSrgb()` 后写出，
  因此 framebuffer 里存的是 sRGB 编码字节。

## 决定

`RenderSceneView` 每帧携带一个**非可选**的 `RenderLinearColor clearColor`，默认值等于原
`0x102a43` 的线性等价色；`RenderSceneWriter::setClearColor()` 供产品每帧覆盖，一帧只允许设一次；
bgfx 后端不再持有颜色常量，改为把场景给的线性色编码成 sRGB RGBA8 后交给 `bgfx::setViewClear`。

## 结果

- 天空成为逐帧可变的场景属性，日夜循环、水下、室内换色都不再需要碰后端；
- 颜色**只有一个定义点**（`RenderScene`）。原先默认值散落在四个后端调用点，新增后端会各自重复一次；
- 接口收**线性**色而非 sRGB 字节，与 `baseColorFactor`、`Mesh3DDirectionalLight::colorR` 同一标尺，
  后端负责编码。天空与受光面因此可以按同一组数值推理；
- 默认值经 linear→sRGB→u8 往返后逐字节还原 16/42/67，故未调用 `setClearColor()` 的既有样例
  与 gate 输出不变。这是本 ADR 唯一的"兼容"承诺，且它是数值恒等而非兼容层；
- 成本：`RenderSceneView` 的私有构造函数再多一个参数（现已 13 个），这条链子越来越长，
  迟早要换成聚合结构体。本 ADR 不做那次重构，以免把两件事绞在一起；
- 门禁：`tina_render_scene_tests` 覆盖默认值、覆盖后的取值、一帧设两次被拒、
  非有限/负分量被拒；`tina_render_bgfx_tests` 的 `BgfxClearColorTest` 覆盖 256 个 sRGB 字节的
  编码往返；`samples/3d_voxel --capture-luma` 的对照捕获须显示天空像素随太阳高度改变。

  2026-09-02 实测（`--frames=120 --selftest-edits --capture-luma`）：对照帧钉住太阳相位与
  俯仰角，只让清屏色变，画面顶部 24 行读出 `sky_dawn=74,46,58` / `sky_noon=104,158,214`。
  两组都是零方差的整数，即该区域确为纯清屏像素、无几何混入；数值与两个线性常量各自的
  sRGB 编码逐字节相符，故这条链路从产品 API 到帧缓冲无中间篡改。
  注意对照必须先把俯仰角抬起来：`--selftest-edits` 为了挖方块把镜头压到最低，
  此时顶部 24 行全是被太阳照亮的草地，会把光照变化误读成天空变化。

## 被拒绝方案

- **在后端保留默认、API 用 `std::optional`。** 语义上能区分"场景没说"和"场景说了这个色"，
  但代价是默认值继续留在后端，且四个调用点各要写一次 fallback。逐帧场景本来就每帧都有背景，
  "没说"没有独立含义，不值得为它付两处定义的代价。
- **接口收 sRGB `u32`，透传给 bgfx。** 最省事，也保持逐字节等价。拒绝理由是它把编码泄进公共 API：
  产品要自己做 gamma 才能让天空和光照对得上，而这正是本 ADR 想消除的那类知识。
- **实现真正的天空盒（渐变/大气散射）。** 那是画一个东西，不是清一块屏，需要新 pass、新 shader
  与顶点数据，且会触碰被冻结的 `RenderPassKind`。清屏色是它的前置条件而非替代品，另开切片。
- **让产品自己画一个覆盖全屏的远处 mesh。** 不动引擎就能做，但要占一个 draw call、要处理远平面
  与深度写入，且每个产品都得重写一遍。背景色是渲染器的固有职责。
