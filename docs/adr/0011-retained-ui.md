# ADR 0011：自研 Retained UI，输出后端无关 DisplayList

- 状态：Accepted
- 日期：2026-07-16

## 背景

Tina 已有 Retained Tree、generation NodeId、Focus/Capture、Modal、Theme/DPI、虚拟列表和中文
IME 基线。切换到 Immediate UI 或另一套游戏 UI 会丢失现有交互语义；让 UI 继续直接依赖
bgfx 又会阻塞 Null 测试和 Render 边界收敛。

## 决定

`tina_ui` 保持完全自研 Retained UI，每窗口一个 `UIContext`，AppState 只拥有并注册 roots；
FreeType 只存在于可选生产 adapter `tina_ui_freetype`，Headless/Null 不解析该依赖。
每个有序 Pointer transition 最多 hit-test 一次，再按 Capture → Target → Bubble 路由；布局采用 Measure/Layout 与按需扩展的
Flex 子集。UI 输出 Quad/Text/Clip 的后端无关 DisplayList，由 Render 批处理。RmlUi、ImGui
不作为游戏 UI 依赖；开发调试工具是否使用 ImGui 属于未来独立决定，不能进入产品 UI API。

## 结果

- 生命周期、输入、中文和 Theme 能按 Tina 产品需求演进；
- Null UI 可以验证布局/路由，不链接 bgfx；
- Tina 必须自行承担控件、可访问性、文本 shaping 和视觉回归成本；
- 控件按真实场景增加，首期不追求桌面 GUI 工具包完整度。

## 被拒绝方案

- 用 ImGui/RmlUi 替换游戏 UI：与现有 Retained 生命周期和产品需求不匹配；
- UI 直接提交 bgfx：第三方类型和 GPU 生命周期泄漏到 Widget 层。
