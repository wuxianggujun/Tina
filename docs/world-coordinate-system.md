# 世界坐标系定义（Tina/2D 横版）

本文档描述 Tina 项目中当前使用的 2D 世界坐标系、相机与投影、网格/分块规则，以及窗口尺寸变化时的同步流程。用于团队协作与长期维护时快速对齐约定。

## 总体约定
- 坐标系类型：右手系的 2D 平面坐标，使用正交投影。
- 原点位置：世界坐标 `(0, 0)` 位于世界“左下角”。
- 轴方向：`+X` 向右，`+Y` 向上。
- 单位定义：1.0 世界单位 = 1 个 Tile 的宽/高（正方形瓦片）。
- 深度约定：缺省将 `z = 0` 用于地图/大多数精灵；保留 `0..1000` 的正向深度范围给分层需要（UI/特效可放在不同 z）。

## 相机（Camera2D）
- 语义：相机位置以“视区左下角”的世界坐标表示，即 `(camX, camY)` 就是屏幕左下角在世界坐标中的位置。
- 视口像素：记录为 `(vpW, vpH)`，来自窗口像素大小。
- 视区大小（世界单位）：配置项 `viewH`（世界高度），视区宽度由宽高比推导：`viewW = viewH * vpW / vpH`。
- 具体实现：`Tina::Game::Camera2D`（见 `src/game/Camera2D.hpp:13`）。
  - 生成矩阵：`buildViewProj(float* outView16, float* outProj16)`（见 `src/game/Camera2D.cpp:10`）。
  - 视图矩阵（View）：平移将世界移动到相机左下角为原点：`T(-camX, -camY, 0)`（见 `src/game/Camera2D.cpp:12`）。
  - 投影矩阵（Proj）：正交投影，原点左下、Y 向上：`Ortho(left=0, right=viewW, bottom=0, top=viewH, near=0, far=1000)`（见 `src/game/Camera2D.cpp:19`）。

> 每帧使用：在渲染前调用 `camera.buildViewProj(...)` 并传给 bgfx：`bgfx::setViewTransform(0, view, proj)`（见 `src/main.cpp:304`）。

## 世界 → NDC 关系（直觉化）
设世界点 `(x, y)`，相机 `(camX, camY)`，视区宽高 `(viewW, viewH)`，则其归一化设备坐标（NDC）近似为：

```
X_ndc = (x - camX) * (2 / viewW) - 1
Y_ndc = (y - camY) * (2 / viewH) - 1
```

由于采用正交投影，NDC 与屏幕像素不会因相机缩放而产生透视畸变；缩放仅改变 `viewW/viewH` 从而改变世界单位与屏幕像素的映射比例。

## Tile 与世界坐标的映射
- Tile 索引 `(i, j)`（整数）对应的左下角世界坐标为 `(i, j)`；该 Tile 的四个顶点为：
  - `(i, j)`, `(i+1, j)`, `(i+1, j+1)`, `(i, j+1)`。
- 如果需要取 Tile 中心，可用 `(i + 0.5, j + 0.5)`。
- 当前实现中，颜色方块的几何直接以世界坐标构造，顶点着色器使用 `u_modelViewProj` 乘上位置（参见 `resources/shaders/color_vs.sc:1`）。

## 分块（Chunk）与可见性裁剪
- 目的：地图较大时避免一次性提交全部三角形，减少 CPU/GPU 负载。
- 规则：
  - Chunk 尺寸：`32 × 32` Tile。
  - Chunk 世界 AABB：`[cx*32, (cx+1)*32] × [cy*32, (cy+1)*32]`。
  - 每个非空 Chunk 构建独立 `VB/IB`，记录 `indexCount`。
  - 每帧计算相机视区 `ViewRect = [camX, camX+viewW] × [camY, camY+viewH]`，仅提交与之相交的 Chunk。
- 代码位置：
  - 构建与记录：`src/main.cpp:140` 定义 `TileChunk`，`src/main.cpp:151` 起构建所有 Chunk。
  - 渲染时裁剪与提交：`src/main.cpp:304` 起设置相机矩阵，`src/main.cpp:311` 起做 AABB 相交并提交。

## 窗口大小变化与同步
- 事件来源：`WINDOW_SIZE`（SDL）。
- 同步流程（见 `src/main.cpp:187`）：
  - 调用 `ResetBgfxWithSize(newW, newH, resetFlags)` 更新 bgfx 视图矩形（view 0）。
  - 调用 `pipeline.setViewport(newW, newH)` 同步管线内部视图设置。
  - 调用 `camera.setViewportPixels(newW, newH)` 更新相机的 `(vpW, vpH)`，从而更新 `viewW`。
- 相机的投影矩阵在每帧渲染前重新计算，无需在事件中手动更新矩阵，只需更新相机的像素视口参数。

## 渲染与状态
- bgfx 视图：使用 View 0 进行 2D 世界渲染（`bgfx::setViewRect` 在重置中设置）。
- 矩阵设置：每帧 `bgfx::setViewTransform(0, view, proj)`（见 `src/main.cpp:305`）。
- 渲染状态：瓦片渲染当前使用 `BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A`，默认关闭背面剔除以避免顶点顺序差异导致的不可见。

## 键鼠交互（临时约定）
- `W/A/S/D`：相机平移（步长 2.0 世界单位/次）。
- `E/C`：放大/缩小（调整 `viewH`）。
- 上述仅用于调试浏览世界；后续会替换为持续按键状态驱动的平滑移动与滚轮缩放。

## 参考代码（入口行）
- 相机类定义：`src/game/Camera2D.hpp:13`
- 相机矩阵实现：`src/game/Camera2D.cpp:10`
- 分块结构定义：`src/main.cpp:140`
- 相机创建与初始参数：`src/main.cpp:198`
- 窗口大小事件同步：`src/main.cpp:187`
- 每帧设置相机矩阵：`src/main.cpp:304`

## 未来扩展建议
- 坐标分层：约定不同系统（地图/角色/特效/UI）使用不同 `z` 层，或在 2D 管线内增加排序键。
- 物理坐标：与 Box2D 对齐时，可在“世界单位=像素/米”的层面定义换算比例，并将 Tile 尺寸与物理尺寸对齐。
- 纹理化：将当前颜色顶点替换为采样纹理图集，仍保持相同的坐标系与相机。
