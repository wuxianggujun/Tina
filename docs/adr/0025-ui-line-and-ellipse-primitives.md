# ADR 0025：UI Line exact quad 与 Ellipse coverage 图元

- 状态：Accepted
- 日期：2026-08-10
- 决策者：Tina maintainers

## 背景

Tina Retained UI 原先只有 axis-aligned rectangle。Editor 的 2D/3D grid、gizmo 轴线和旋转环只能把斜线
拆成水平条与垂直条，或把圆环拆成多段弦；即使按像素长度增加细分，长线仍会呈阶梯，圆环仍会呈块状网格。
问题不在相机投影，而在 UI paint/DisplayList 缺少能表达真实线段和椭圆的 backend-neutral 图元。

UI 仍必须保持固定容量、失败原子性、backend-neutral committed snapshot、axis-aligned clip，以及公共头不泄漏
bgfx 类型。部分越界的圆形和图片不能通过先裁 destination bounds 再拉伸来处理，否则几何会变形。

## 决定

1. `UIBoxPaint` 以 `UIBoxPrimitiveKind::{Rectangle,Ellipse,Line}` 选择 box 图元；Canvas 增加
   `SolidEllipse` 与 `SolidLine`。Ellipse 使用 bounds，零 stroke 为填充、正 stroke 为向内描边；Line 使用
   Element-local endpoints 与 logical thickness。非法、退化或非正厚度 Line fail closed，不回退为 Rectangle。
2. committed paint 使用显式 `SolidEllipse`/`SolidLine` kind。Line 保存 logical world endpoints、thickness 与
   覆盖真实线宽的 conservative envelope；Ellipse 保存完整 logical bounds 与 stroke width。
3. UI-Render bridge 在 logical 空间按线方向法向构造四角，对每个角分别应用 framebuffer `scaleX/scaleY`，
   然后以 `SolidQuad + UISolidQuadVertices` 发布。公共 API 不增加 `rotationRadians` 或 rotated-quad 兼容入口。
4. Ellipse 使用独立 `UIDrawCommandKind::SolidEllipse`。bgfx 统一 coverage shader 从 local UV 与 pixel extent
   计算解析椭圆 coverage；描边用外椭圆 coverage 减去内椭圆 coverage。shape 参数随顶点携带，保持相邻
   compatible command 的 batch 能力，不增加 per-command uniform。
5. axis-aligned destination bounds 保持完整几何，不在 logical-to-framebuffer 投影时预裁切；最终可见区域由
   committed clip、DisplayList clip 与 backend scissor 裁剪，避免部分越界图元变形。
6. Editor grid 和 gizmo 每条 segment 使用一个 Line；rotation ring 使用一个 Ellipse outline。删除所有按像素
   阶梯细分、L 形拼接和多段弦圆环代码及其额外 node/paint 容量预算。

## 结果

- 2D/3D grid、斜向 gizmo 与 rotation ring 的几何不再依赖分段近似；非等比 framebuffer scale 仍保持正确；
- 每条线段与每个椭圆各消耗一个 retained paint entry 和一个 DisplayList command，容量与成本可直接统计；
- DisplayList checksum 纳入 exact vertices 与 ellipse stroke，非法四边形、描边和非有限输入 fail closed；
- bgfx vertex 增加 shape extent/parameter，solid/Glyph coverage program 统一承载 Rectangle、RoundedRect 与 Ellipse；
- clip 仍为 axis-aligned scissor，Line 端帽当前为 butt cap，Ellipse stroke 当前为向内 uniform screen-pixel
  width；round cap/join、任意 path、Bezier 和 rotated rounded rectangle 不属于本决定；
- `UILayoutStyle::clipDescendants` 现以默认关闭的 axis-aligned border-box clip owner 补齐 Editor viewport
  边界；`viewportPreviewLayer_` 显式启用后，grid/gizmo 的完整 Line/Ellipse envelope 保持不变，并由既有
  committed clip、DisplayList clip 与 backend scissor 在相邻 chrome 前裁剪。该契约不是 rounded clip，也
  不额外改变 clip owner 自身的 paint clip；viewport-level Popup 继续使用专用 anchor/clip policy；
- 2026-08-10 当前 Windows 宿主的 200% DPI 证据已完成：Editor 2D/3D grid、斜向 gizmo 与单 Ellipse
  rotation ring 截图无可见阶梯，UI-003 以 logical 960×540 → capture 1920×1080 的专用 raster baseline
  独立复跑通过；`RENDER-LINES-001` 仍需 100% DPI Editor/UI-003 金标与跨 GPU 证据后才能转为 Done。

## 被拒绝方案

- 继续提高水平条/垂直条阶梯细分：只降低单阶尺寸，不能消除几何错误，并线性放大 node/paint 成本；
- 用更多弦段逼近圆环：仍有可见折角，且无法以单一 stroke 参数表达稳定 DPI 行为；
- 公开 `rotationRadians` rotated quad：只覆盖矩形旋转，不能自然表达 Ellipse，且把 integration 投影细节扩散到
  authoring API；Line 所需的 exact vertices 留在 Render boundary 更窄；
- 把 grid/gizmo 全部移进 world pass：会把 Editor overlay 的 clip、paint order 与 UI 状态拆到另一条路径；当前
  backend-neutral UI 图元已经能用更小改动关闭根因。
