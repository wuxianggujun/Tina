# ADR 0059：单轨 2D 投影、仿射 quad 与空间排序

- 状态：Accepted
- 日期：2026-09-10
- 范围：Render / Scene / Asset / Gameplay2D / Editor

## 背景

只投影 Scene sprite 的锚点、同时把正交 Tile/Particle/Trail 写入同一个 writer，会使画面和拾取使用不同
坐标系。原有 center/rotation/width/height/scale render item 无法表达菱形地面；把空间深度量化并与
authored order 相加，又会使高度遮挡被作者排序值覆盖。Camera2D 的可写投影 basis 未持久化也会有损保存。

## 决定

1. 逻辑世界使用 XY + elevation，Render 只接收渲染平面的 `Sprite2DQuad`：center + 两条 half-axis。
   删除旧 render TRS 字段，不提供旧 API 包装或第二条几何路径。负 scale 通过带符号的轴保留。
2. `Sprite2DProjection` 是无 owner 的值上下文，取自当前 writer 的已解析 Camera2D。
   `billboard()` 投影锚点，再在渲染平面施加旋转后的 pivot 偏移；`ground()` 投影完整中心和两轴。
   Scene sprite/Particle 是 billboard，Tile/Trail 是 ground。backend 只展开四角，不再做投影或三角函数计算。
3. 排序为 `sortingLayer -> sortDepth -> orderInLayer -> stableEntityKey -> insertionOrder`。
   等距 `sortDepth = -(x+y)*tileHeight/2 + elevation*elevationStep`，使用 finite double，不量化、不饱和。
   完整 i32 authored order 仅在同 layer、同 depth 决胜；sortingLayer 仍是显式跨空间覆盖控制。
4. Tile 保留逻辑 `cellSizeMeters` 标量；渲染的两个投影轴表达菱形，并不需要把逻辑网格格式改成屏幕坐标。
   streaming/emit 从实际 Camera2D 四角逆投影到 map elevation，再减 map origin，得到保守 map-local AABB。
5. World2D 直接升为 schema v7、480-byte entity record。Camera 的四个等距参数均有独立 wire slot；
   descriptor 的 inactive-mode 参数也写读对称。旧 schema fail closed，不双读、不静默回默认值。
6. Editor 使用同一个 basis 处理网格、导航、资源落点、拾取、gizmo 和 Tile 笔刷；独立预览相机保留
   authored basis，不强制覆盖为默认等距参数。Sprite 拾取复用渲染 quad 和排序，优先最上层可见对象。

## 性能与寿命

- 调用方持有 `TileMapSpriteScratch`，复用 chunks/sprites 容量；每层只解析一次 Tileset，每 chunk 只查一次 cells，
  直接追加最终输出，不再逐格查找 chunk、不再逐帧重建临时向量或复制中间 sprite 集合。
- Particle 连续相同 handle 在一次 extract 中复用解析结果；下一次 extract 必须重新 intern 当前 packet ref。
- Trail 在 append 时缓存 segment half-axis；extract 只插值宽度并投影，不重复 hypot/atan2/sin/cos。
- Render culling 每相机只计算一次旋转；quad validation 与 backend 展开共用同一几何约定。
- Editor collision overlay 读取 preview 组件，不每帧重新解析完整 World2D 文档。

上述是可审计的工作量减少，不等于已测得 FPS 或延迟改善；性能数值需单独 profiling。

## 代价与验证边界

这是 source/wire breaking change，所有 SDK、sample、Editor、测试 consumer 同批迁移；新公开头和 CMake 必须同批交付。
空间排序是每 quad 一个 anchor depth，不解决跨多个深度平面的复杂相交透明面；需要拆分几何或显式 layer。
Trail 仍是固定 elevation 的平面 ribbon，不引入 3D ribbon 或任意高度曲线。
碰撞 Box 使用精确投影线段，Circle 使用解析 ellipse；Capsule 显示包含端点和半径的保守 bounds，
不恢复 ADR 0025 禁止的弦环近似。

集中验证覆盖：非默认 basis、负 scale/旋转 pivot、地面/实体/FX 对齐、极端 authored order、fractional depth、
旋转相机/地图 origin/elevation 的边缘裁剪、相机参数 wire 与 Scene round-trip、scratch/resolver 复用及跨帧失效。
真实 Editor 交互、GPU 像素结果和性能采样必须与编译/单测证据分别报告。
