# ADR 0011：自研 Retained UI，输出后端无关 DisplayList

- 状态：Accepted
- 日期：2026-07-16

## 背景

Tina 已有 Retained Tree、generation NodeId、Focus/Capture、Modal、Theme/DPI、虚拟列表和中文
IME 基线。切换到 Immediate UI 或另一套游戏 UI 会丢失现有交互语义；让 UI 继续直接依赖
bgfx 又会阻塞 Null 测试和 Render 边界收敛。

## 决定

`tina_ui` 保持完全自研 Retained UI，每窗口一个 `UIContext`，`IGameState` 只拥有并注册 roots；
FreeType 只存在于可选生产 adapter `tina_ui_freetype`，Headless/Null 不解析该依赖。
每个有序 Pointer transition 最多 hit-test 一次，再按 Capture → Target → Bubble 路由；布局采用 Measure/Layout 与按需扩展的
Flex 子集。UI 输出 Quad/Text/Clip 的后端无关 DisplayList，由 Render 批处理。RmlUi、ImGui
不作为游戏 UI 依赖；开发调试工具是否使用 ImGui 属于未来独立决定，不能进入产品 UI API。

vNext 进一步固定：Runtime WindowRecord 唯一拥有 UIContext；`IGameState` 只持有 move-only root
owner 和带 owner WindowId 的 UINodeId。Runtime 先把 committed State policy 转为一个
UIInputScopeSnapshot，再对 eligible roots 全局路由一次。UI 使用细粒度 dirty、每帧最多一次
Measure/Arrange、持久 local PaintCache、稳定 paint/hit/semantics snapshot。Transform/scroll/
clip 只更新 composite snapshot，不错误重建 local PaintCache。无变化 UI 的 style/layout/PaintCache rebuild 和 Tina heap
allocation 增量为0。

DisplayList 不包含 Widget/UINode 指针、backend vertex layout、ViewId、uniform 或 bgfx handle。
DisplayList 使用确定性 intern 的 effective clip 和 packet-local FrameResourceRef，资源/Atlas pin
由 RenderFramePacket 保活。Renderer 只合并相邻且 pipeline/texture/sampler/blend/clip 兼容的命令，禁止跨透明 paint order
全局重排。Text layout 与 glyph raster 分离；raster completion 沿用 advance，只触发 Paint dirty。

## 结果

- 生命周期、输入、中文和 Theme 能按 Tina 产品需求演进；
- Null UI 可以验证布局/路由，不链接 bgfx；
- Tina 必须自行承担控件、可访问性、文本 shaping 和视觉回归成本；
- 控件按真实场景增加，首期不追求桌面 GUI 工具包完整度。
- dirty、DisplayList、glyph queue 和 Atlas 都必须有容量、Metrics 与资源退役门禁。

## 被拒绝方案

- 用 ImGui/RmlUi 替换游戏 UI：与现有 Retained 生命周期和产品需求不匹配；
- UI 直接提交 bgfx：第三方类型和 GPU 生命周期泄漏到 Widget 层。
