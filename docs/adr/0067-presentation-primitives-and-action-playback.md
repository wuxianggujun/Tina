# ADR 0067：加法混合、全局位图字体、Canvas 旋转、音频 loop 与 Action 播放

- 状态：Accepted
- 日期：2026-09-15
- 决策者：Tina maintainers
- 部分替代 [0036](0036-gameplay-tooling-boundaries.md) 中「没有 relative/reverse/speed」条款；容量仍由 [0065](0065-demand-grown-runtime-owners.md) 约束

## 背景

真实 2D 产品在 Tina 上会卡住五处表现原语：发光要用加法混合而不是 ColorTransform 的加色；手绘字体必须作为 Catalog 资产被 UI 与世界文字共用，而不是每处注入一份 CPU atlas；指南针/标题层需要 Canvas 命令绕轴旋转；BGM 不能靠 Stopped 后再 Play，那至少隔一个 mix block；Pause/Resume 需要可暂停、可恢复、可从当前值出发的动作树，而不是只有 setter tween。

旧设计把这些能力拆成「以后再加」或用兼容桥模拟。本决定一次切到现行契约，旧 API 与旧 wire 直接拒绝。

## 决定

### 1. 单一 `Core::BlendMode`

`PremultipliedAlpha`（ONE, INV_SRC_ALPHA）与 `Additive`（ONE, ONE）是 GPU 合成模式。它是 Sprite2D / Particle2D / Trail2D / Particle3D / UI DisplayList 的 batch key，不是 ColorTransform。删除 `Particle3DBlendMode`。World2D schema v9、Fx2D schema v3 持久化该字段；缺省为 PremultipliedAlpha。

### 2. 全局 Font 驻留

`Asset::FontBindingRegistry` 是 Font 资产的唯一 intern 点：一份 metrics/`BitmapFontAtlas`、一组 page AssetId，UI rasterizer 与 `Scene::BitmapText2D` 共用 `shared_ptr`。不再把「每个文字 owner 自己 parse + make_shared」当作产品路径。`CreateEngineOptions::uiBitmapFont` 只保留无 Catalog 的测试/bootstrap 注入。

### 3. Canvas 朝向

`UICanvasCommand` 带 `rotationRadians` 与归一化 pivot。SolidRect / Image / SolidLine 在逻辑空间绕 pivot 旋转后投影为 DisplayList 的精确四顶点；圆角 Rect、Ellipse、NineSlice 遇非零旋转 fail closed。ImageQuad 获得与 SolidQuad 相同的 `vertices` 契约。

### 4. 音频 native loop

`AudioPlayDesc` 在 realtime mixer 内把 clip cursor 包进 `[loopStart, loopEnd)`。Looping voice 只在 `enqueueStop` 时发出 Stopped，不在 clip 末尾 natural-end。Stream 拒绝 loop。`playOneShotPcm` 固定 Once。

### 5. Action 播放面

`ActionRunner` 以 `pause`/`resume`/`pauseAll`/`resumeAll` 作为暂停原语，删除 `setPaused`。增加 authoring-time `reverse()`、`speed()`，以及采样当前值的 `tween*To`/`tween*By`。`Scene2DRuntime` 拥有 `ActionRunner`，但不擅自 advance：delta 仍由帧循环给出。

## 结果

- 发光、像素字、HUD 旋转、无缝 BGM、暂停菜单过渡成为引擎契约而不是产品 workaround。
- 旧 World2D v8、Fx2D v2、`Particle3DBlendMode`、`ActionRunner::setPaused` 全部 fail closed。
- 代价：UI 圆角/椭圆/九宫格不能旋转；音频 loop 不做 sample-accurate crossfade；Action reverse 是授权期改树，不是运行时倒放积压。

## 被拒绝方案

- **用 ColorTransform.add 冒充 glow**：加色发生在 shader 内、仍走 alpha 合成，不能把亮部叠到已绘制内容上。
- **Stopped 后再 enqueuePlay 模拟 loop**：owner 与 callback 至少相隔一个 mix block，接缝可听。
- **把 Action 绑到 Scene::EntityId**：违反 ADR 0036 D5；setter 仍是写目标。Scene2DRuntime 只拥有 runner。
- **Element 级旋转替代 Canvas 旋转**：会改 hit/layout；指南针/标题层是 paint-only。
