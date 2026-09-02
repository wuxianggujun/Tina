# Tina Modern Desktop UI 设计规范与落地计划

> 状态：InProgress，父任务为 `UI-MODERN-DESKTOP-001`。TMD-00..TMD-07 已完成并通过集中门禁；
> TMD-08 Desktop Shell reference 已实现结构、命令、Menu/Dialog/Tooltip、Splitter、产品 icon atlas 与响应式档位；
> 专用真实 DPI 视觉门禁已就绪，100%/150% 配对证据因当前宿主性能与固定 200% 环境暂缓。
> TMD-09 EditorApp Compact 与 TMD-10 OS scheme 接线已完成，并通过统一产品/Runtime/Platform gate。
> TMD-11 已通过 benchmark unit 与冻结 workload 确定性 gate；开发机墙钟仍为 provisional，父任务继续等待真实 DPI 配对视觉与最终文档收口。
>
> 文档职责：冻结目标视觉语言、Desktop 组合方式、工程边界、实施切片和验收标准。
>
> 当前实现事实仍以 [Retained UI](ui.md)、[UI 框架设计](ui-framework.md)、源码、CMake 与实际门禁为准；
> 任务状态只在 [Backlog](backlog.md) 维护。本文不是新的 UI Runtime，也不替代 Accepted ADR。

## 结论

Tina 的桌面 UI 不应直接复制 Material Design 3 的移动端控件尺寸和页面构图。推荐的产品语言是
**Tina Modern Desktop**：

- 从 Material 3 借用语义颜色、State Layer、Shape、Typography 和 Token 思路；
- 从 Fluent 2 借用桌面键鼠、Focus、Command Bar、菜单和窗口级工作流；
- 从 VS Code、JetBrains、Figma 等生产力工具借用紧凑面板、Inspector、SplitView、Tab 与高信息密度；
- 保留 Tina 现有 retained tree、`UIContext` 唯一 owner、固定容量事务、StyleRole、StyleClass、Motion、
  Semantics、DisplayList 和单一 GPU pipeline。

现有 `Tina Studio Compact` 不是要恢复的旧设计，而是新体系的第一个 `Compact` 密度基线。新体系再提供
`Comfortable`，用于 UI Showcase、设置页、启动器和普通桌面应用。两种密度共享同一组控件行为、Semantics、
资源链和渲染链，只改变 Typography、Spacing 和尺寸指标。

## 为什么当前观感仍不够现代

当前框架能力已经足够支撑现代化，但 Theme 仍有几个明显限制：

| 当前事实 | 体验问题 | 目标修正 |
| --- | --- | --- |
| `UITheme` 使用 `surface0/1/2`、`textPrimary`、`buttonNormal` 等平铺字段 | 组件容易直接绑定具体颜色，层级语义不够清楚 | 改为 `surfaceContainer*`、`onSurface*`、`primaryContainer` 等语义 Token |
| Hover/Pressed 大量调用 `lightenChannel()` / `darkenChannel()` | 不同底色的状态反馈强弱不一致，容易显得机械 | 统一由 State Layer 颜色与透明度合成最终 chrome |
| 默认 shadow alpha 为 0，`UIElevation` 只有 `None/Low` | Popup、Menu、Modal 与普通 Panel 的层次主要靠边框 | 建立语义 Elevation；首版映射到现有填充、边框和无模糊假影 |
| 默认 Typography 为 28/22/20/18/18/14 | 对桌面工具栏、Inspector 和数据列表偏大 | 提供 Compact 与 Comfortable 两套明确 ramp |
| Showcase 仍有大量 40-46 px 控件、固定卡片和局部手写颜色 | 展示页不能证明紧凑 Desktop 工作流 | 先把 Showcase 改成真实 Desktop 工作台，再迁移 Editor |
| Focus、Selected、Hovered 的颜色有时接近 | 状态可辨识度不足 | 选中容器、State Layer、Focus ring 三层职责分开 |

因此，下一步优先级应是统一设计 Token 和现有控件 chrome，而不是继续无序增加普通控件。

## 目标与非目标

### 目标

1. Dark/Light 使用同一套语义 Token 和组件配方。
2. Compact/Comfortable 共享行为，只改变密度指标。
3. Editor、Showcase、2D/3D 产品 UI 能使用同一设计语言。
4. Hover、Pressed、Focus、Selected、Checked、Open、Dragging、Disabled 有稳定且可测试的视觉规则。
5. 所有视觉变化继续沿 Layout/Hit/Paint/Semantics 的既有事务提交发布。
6. 新组件优先是现有 Element/Behavior 的组合 profile，不增加不必要的 Widget kind 或状态机。
7. 文字、图标、焦点、对比度、缩放和 Semantics 同时进入验收，不把“好看”与“可用”分开。

### 非目标

- 不实现 Android 风格 48/56 dp 全局尺寸、Bottom Navigation、Floating Action Button 或移动端抽屉导航。
- 不实现 Ripple；桌面反馈使用 State Layer、Focus ring 和短 Motion。
- 不为了现代感默认加入大圆角、渐变、毛玻璃、Mica、Acrylic 或多层模糊阴影。
- 不建立 QML/HTML/CSS Runtime、第二套 Style resolver、第二棵 UI 树或第二条 renderer。
- 不为 Icon 建立独立 Asset、atlas、Texture 或 shader 系统。
- 不保留旧 Theme 字段、旧 chrome helper 或兼容 alias；迁移完成后直接删除旧设计入口。
- 不把 Command Bar、Inspector、Status Bar 等产品组合全部升级成内建 Widget；只有出现真实复用和状态需求时才冻结公开类型。

## 设计原则

### 1. 工作区优先

Desktop 第一屏应直接是可操作工作区。Editor 不需要营销式 Hero、装饰大卡片或占据首屏的功能说明。
Command、Hierarchy、Viewport、Inspector、Timeline 和 Status 应按扫描与重复操作效率组织。

### 2. 层级来自语义，不来自装饰

优先使用 surface 层级、间距、排版、Divider、Selected container 和 Focus ring。圆角与阴影只强化已存在的层级，
不负责创造层级。

### 3. 高密度但不拥挤

Compact 允许 24-34 logical px 控件，但命中区域、文字基线、图标尺寸和行间距必须稳定。不能通过缩小字体、
压缩到重叠或隐藏状态反馈来追求密度。

### 4. 状态必须独立可辨

- `Selected/Checked` 表达持久值；
- `Hovered/Pressed` 表达瞬时输入；
- `Focused/FocusVisible` 表达键盘、Gamepad 或辅助技术位置；
- `Disabled` 只表达命令不可执行，不能冒充 selected；
- `Open` 表达 transient surface 已打开。

### 5. 一个行为真相源

IconButton 复用 Button Activate，Switch 复用 Toggle，Segmented 复用 RadioButton，FormField 复用 TextEdit，
Dialog 复用 Modal，Desktop Shell 复用 SplitView/TabView/Menu/Tooltip。外观 profile 不复制输入、Focus、UIA 或状态。

### 6. Desktop 逻辑像素

全部规范数值都是 logical px。Platform 继续负责 `contentScale`，UI Layout、Hit、Glyph、DisplayList 和截图 ROI
读取同一逻辑几何，不在产品代码中手工乘 DPI。

## 架构边界

```text
Tina::Desktop
  native window + optional OS color-scheme observation
                    |
                    v
Tina::Runtime primary-window UI facade
  phase/lifetime guard + theme request staging
                    |
                    v
Tina::UI::UIContext (per-window 唯一 owner)
  Element/Behavior/Focus/Capture/Tooltip/Menu/SplitView/TabView stores
  Theme + StyleRole + StyleClass + State Layer resolver
  Layout -> Hit -> Paint -> Semantics committed transaction
                    |
                    v
UI DisplayList -> existing UI-Render bridge -> existing GPU pipeline
```

`Tina::Desktop` 只处理原生窗口偏好与组合，不拥有另一套控件。`UIContext` 仍是唯一 retained owner；新的 Token、
State Layer 和组件 profile 只进入现有 role/paint/layout 解析链。

## 公开设计契约

以下是目标 API 形状，需由 `UI-MODERN-DESKTOP-001` 的实施切片落地后才属于当前事实。

```cpp
enum class UIColorScheme : u8 {
    Dark,
    Light,
};

enum class UIDensity : u8 {
    Compact,
    Comfortable,
};

struct UIColorTokens final { /* fixed semantic colors */ };
struct UIStateLayerTokens final { /* fixed state alpha/easing values */ };
struct UIShapeTokens final { /* fixed radius scale */ };
struct UISpacingTokens final { /* fixed logical spacing scale */ };
struct UIElevationTokens final { /* fixed level recipes */ };
struct UIControlMetrics final { /* density-bound control geometry */ };
struct UITypographyScale final { /* density-bound text ramp */ };

struct UITheme final {
    UIColorScheme colorScheme = UIColorScheme::Dark;
    UIDensity density = UIDensity::Compact;
    UIColorTokens colors{};
    UIStateLayerTokens states{};
    UIShapeTokens shapes{};
    UISpacingTokens spacing{};
    UIElevationTokens elevations{};
    UIControlMetrics controls{};
    UITypographyScale typography{};
};

[[nodiscard]] UITheme makeModernDesktopTheme(
    UIColorScheme scheme,
    UIDensity density) noexcept;
```

### 约束

- `UIColorScheme` 只表示一份确定的 Dark/Light Theme，不包含 `System`。OS 跟随属于 Desktop/Platform adapter，
  adapter 解析后仍向 UI 提交确定的 Dark 或 Light。
- Color scheme 可以在 live root 上事务切换。
- Density 在 root 首个节点创建后不可热切换；切换密度需要重建该 root。这样避免一部分节点更新布局、另一部分仍
  保存旧尺寸。Theme setter 收到不同 density 时必须 fail closed。
- `UIStyleTokenId` 继续表示产品注册的可变 ColorToken；它不与内建 `UIColorTokens` 建立第二套同名 registry。
- `UIStyleRoleId` 保持组件语义入口，StyleClass 仍是产品变体，local override 仍是最后一级逃生口。
- Theme 验证必须检查所有颜色、有限数值、非负尺寸、递增 spacing、合法 density/scheme 和控件几何关系。

## 颜色规范

### 语义 Token

| Token | 用途 |
| --- | --- |
| `background` | Window/root 最底层背景 |
| `surface` | 普通无框内容面 |
| `surfaceContainerLow` | Dock、Command Bar、次级 band |
| `surfaceContainer` | 普通控件容器、输入框、列表背景 |
| `surfaceContainerHigh` | Popup、Menu、Tooltip、Elevated Surface |
| `onSurface` | 主文字和主图标 |
| `onSurfaceVariant` | 次级文字、元数据、非主图标 |
| `outline` | 强边界、输入框边界 |
| `outlineVariant` | Divider、弱分组边界 |
| `primary` / `onPrimary` | 明确的主操作 |
| `primaryContainer` / `onPrimaryContainer` | 选中项、Tonal、轻强调区域 |
| `error` / `onError` | 破坏性命令和错误 |
| `errorContainer` / `onErrorContainer` | 错误提示容器 |
| `success*` | 完成、健康状态 |
| `warning*` | 需要注意但可继续的状态 |
| `focusRing` | 可见焦点，不与 selected 共用 |
| `scrim` | Modal 背后的输入屏障 |
| `shadow` | Floating/Modal 假影；普通 Panel 不使用 |

### Dark v1

| Token | 值 |
| --- | --- |
| `background` | `#101216` |
| `surface` | `#15181D` |
| `surfaceContainerLow` | `#1B1F25` |
| `surfaceContainer` | `#22272E` |
| `surfaceContainerHigh` | `#2B3139` |
| `onSurface` | `#E8EBF0` |
| `onSurfaceVariant` | `#B3BAC4` |
| `outline` | `#78818C` |
| `outlineVariant` | `#3E4650` |
| `primary` | `#79B8FF` |
| `onPrimary` | `#08233F` |
| `primaryContainer` | `#173B62` |
| `onPrimaryContainer` | `#D5E9FF` |
| `error` | `#FFB4AB` |
| `onError` | `#690005` |
| `errorContainer` | `#5C2024` |
| `onErrorContainer` | `#FFDAD6` |
| `success` | `#62C98F` |
| `onSuccess` | `#062316` |
| `successContainer` | `#173D29` |
| `onSuccessContainer` | `#B5F3CE` |
| `warning` | `#F2C14E` |
| `onWarning` | `#2B1D00` |
| `warningContainer` | `#51400B` |
| `onWarningContainer` | `#FFE8A3` |
| `focusRing` | `#9ACBFF` |
| `scrim` | `#00000099` |
| `shadow` | `#00000066` |

### Light v1

| Token | 值 |
| --- | --- |
| `background` | `#F5F7FA` |
| `surface` | `#FFFFFF` |
| `surfaceContainerLow` | `#F0F3F6` |
| `surfaceContainer` | `#E8EDF2` |
| `surfaceContainerHigh` | `#DDE4EA` |
| `onSurface` | `#1B1E23` |
| `onSurfaceVariant` | `#555D67` |
| `outline` | `#737D88` |
| `outlineVariant` | `#C4CCD4` |
| `primary` | `#1769AA` |
| `onPrimary` | `#FFFFFF` |
| `primaryContainer` | `#D4E8FF` |
| `onPrimaryContainer` | `#001D35` |
| `error` | `#BA1A1A` |
| `onError` | `#FFFFFF` |
| `errorContainer` | `#FFDAD6` |
| `onErrorContainer` | `#410002` |
| `success` | `#176B42` |
| `onSuccess` | `#FFFFFF` |
| `successContainer` | `#B8F2CF` |
| `onSuccessContainer` | `#002111` |
| `warning` | `#765A00` |
| `onWarning` | `#FFFFFF` |
| `warningContainer` | `#FFE49A` |
| `onWarningContainer` | `#251A00` |
| `focusRing` | `#005EA8` |
| `scrim` | `#00000066` |
| `shadow` | `#0000003D` |

这些值是 v1 视觉基线。调整必须同时更新 Dark/Light、对比度测试、Showcase baseline 和视觉证据，不能在单个控件
里直接改十六进制颜色。

### 对比度

- 正文与背景目标至少 4.5:1；大字号文字至少 3:1。
- Focus ring、Icon、边界和非文字状态目标至少 3:1。
- Disabled 不要求达到普通内容对比度，但仍必须可识别，且 Semantics 不删除。
- Error、Warning、Success 不能只依赖颜色；同时使用文本、Icon 或状态名称。

## State Layer 规范

State Layer 是语义前景色覆盖在容器上的透明层。实现阶段可以在 CPU 端预合成最终 `UIStraightSrgba8Color`，
继续输出现有 `UIBoxPaint`/control paint，不增加 GPU pass。

| 状态 | Layer alpha | 规则 |
| --- | ---: | --- |
| Rest | 0% | 使用基础 container |
| Hovered | 8% | 使用当前内容色或 primary 色 |
| FocusVisible | 10% | State Layer + 独立 Focus ring |
| Pressed | 12% | 不改变布局尺寸，不做“按下缩小” |
| Dragging | 16% | 只用于正在拖动的 Thumb/Splitter/Tab 等 |
| Selected/Checked | 100% container | 先切到 `primaryContainer`，再叠加 Hover/Pressed |
| Open | 100% container | Anchor 保留轻选中容器；Popup 自己使用 elevated surface |
| Disabled content | 38% | 相对 `onSurface` 的 alpha |
| Disabled container | 12% | 相对 `onSurface` 的 alpha，屏蔽 Hover/Pressed |

状态优先级为：Disabled 最高；Selected/Checked/Open 先决定基础容器；Pressed/Dragging/Hover 再决定 State Layer；
Focus ring 独立绘制。`FocusVisible` 从既有 Focus 与最近输入来源派生，不建立第二个 focus owner：Keyboard、Gamepad、
UIA Focus 显示 ring，Pointer focus 默认只保留焦点而不显示强 ring。

## Typography

### 字号 ramp

| Role | Compact | Comfortable | 用途 |
| --- | ---: | ---: | --- |
| Display | 24 | 28 | 空状态标题、少量产品级标题；控件内禁用 |
| Title | 20 | 22 | 页面/主要 Dock 标题 |
| Section | 16 | 18 | Inspector section、对话框 section |
| Body | 15 | 16 | 正文、列表主标签 |
| Control | 14 | 15 | Button、Tab、Menu、输入框 |
| Caption | 12 | 13 | 路径、计数、辅助信息、Status Bar |

### 规则

- 控件内不得使用 Display/Title。
- 单行工具控件 line-height 为 1.2；多行正文为 1.35；Caption 为 1.2。
- 字体大小不随 viewport 宽度缩放，只随显式 density 和 DPI logical-to-pixel 映射。
- 路径、文档 Tab、状态文本使用框架级 `Ellipsis`；Semantics 始终保留完整 UTF-8 文本。
- v1 继续使用当前已解析的单一字体 face，不伪造粗体。只有字体 resolver 能稳定提供多 face、CJK fallback 和
  header-isolation 后，才增加 `UIFontWeight`；该能力不阻塞颜色、密度和 chrome 现代化。

## Spacing

| Token | logical px | 用途 |
| --- | ---: | --- |
| `space0` | 0 | 无间距 |
| `space1` | 2 | Icon 内部微调、相邻像素层 |
| `space2` | 4 | 紧凑同组元素 |
| `space3` | 6 | Compact 控件内部 gap |
| `space4` | 8 | 标准控件 gap、Panel 内小间距 |
| `space5` | 12 | Section 内间距 |
| `space6` | 16 | Panel padding、Section 间距 |
| `space7` | 20 | Comfortable 内容块 |
| `space8` | 24 | 页面 band 间距 |
| `space9` | 32 | 空状态和大区域间距 |

同一 command cluster 使用 2-4 px；不同 cluster 使用 Divider 或 8-12 px。Dock 内部不能用一组组浮动卡片替代
清晰 section；卡片不能嵌套卡片。

## Shape

| Token | Radius | 用途 |
| --- | ---: | --- |
| `none` | 0 | Divider、Viewport、连续表格区域 |
| `extraSmall` | 2 | 极紧凑选中条、细标签 |
| `small` | 4 | Compact Button、Tab、输入框（源码字段为 `smallRadius`，避免 Windows 宏冲突） |
| `medium` | 6 | Comfortable control、Dock surface |
| `large` | 8 | Modal、Menu、独立浮层 |
| `full` | half extent | Badge、Switch track；不用于普通 Button/Card |

相邻 Segmented/Tab group 只圆最外侧角，共享边保持方角。普通页面 section 不做悬浮大圆角卡片。

## Elevation

| Level | 语义 | v1 映射 |
| --- | --- | --- |
| `Sunken` | Viewport、输入区域、数据工作区 | `background/surface` + `outlineVariant`，无 shadow |
| `Flat` | 普通 band、Dock | `surfaceContainerLow`，无 shadow |
| `Raised` | Elevated Surface、选中工具组 | `surfaceContainer` + 1 px outline |
| `Floating` | Menu、Popup、Tooltip | `surfaceContainerHigh` + outline + 0/2 px 无模糊 shadow |
| `Modal` | Dialog | `surfaceContainerHigh` + stronger outline + scrim + 0/2 px shadow |

当前 renderer 没有 blur radius，v1 不伪装成柔和 Material shadow。未来若增加 blur，仍映射这些语义 level，
不修改组件业务代码。

## Density 与控件尺寸

| 指标 | Compact | Comfortable |
| --- | ---: | ---: |
| Command Bar 高度 | 36 | 44 |
| Context Toolbar 高度 | 32 | 40 |
| 标准 Button 高度 | 30 | 36 |
| IconButton | 28 x 28 | 36 x 36 |
| TextEdit / Dropdown 高度 | 30 | 38 |
| Checkbox 命中区域 | 28 x 28 | 36 x 36 |
| Checkbox mark | 16 | 18 |
| Switch | 36 x 20 | 44 x 24 |
| Tab 高度 | 30 | 38 |
| MenuItem 高度 | 28 | 36 |
| List row 高度 | 26 | 34 |
| Tree row 高度 | 24 | 32 |
| Status Bar 高度 | 24 | 28 |
| Splitter 可见线 | 1 | 1 |
| Splitter 命中宽度 | 6 | 8 |
| Tooltip 最大宽度 | 320 | 360 |
| Dialog 建议最小宽度 | 420 | 480 |

数值是默认值，不是强制覆盖显式业务布局。Icon-only 工具条和 Inspector 使用 Compact；设置页、首次启动和
Showcase 的阅读型区域可使用 Comfortable。同一 root 不混用两套基础 density，局部更大命中目标应通过明确组件
配置表达。

## Iconography

- Compact 常用 16 px Icon，主要命令 20 px；Comfortable 常用 20 px，极少使用 24 px。
- 使用统一单色图标集合、1.5-2 px 等效描边和一致 viewport，不混用 filled/outline 风格。
- 颜色默认来自 `onSurfaceVariant`；Active 使用 `onPrimaryContainer`；Primary Button 使用 `onPrimary`；Danger 使用
  `onError` 或 `error`。
- 继续使用 `UIIconContent -> UIImageSource -> Image storage -> resolver/pin -> ImageQuad -> Texture2D -> shader`。
- decorative Icon 固定 `UISemanticsMode::Exclude`、`UIPointerHitPolicy::Ignore`。
- IconButton 的 accessible name 和 Tooltip 都由 Button root 提供；文件名和 AssetId 不能成为可访问名称。
- 不使用文字方框模拟熟悉的 Undo/Redo/Save/Delete 等图标。

## Motion

| Token | 时长 | Easing | 用途 |
| --- | ---: | --- | --- |
| `instant` | 0 ms | Linear | Reduced motion、不可插值状态 |
| `fast` | 80 ms | EaseOut | Hover exit、Pressed release |
| `standard` | 120 ms | EaseOut | State Layer、Tooltip/Menu fade |
| `emphasized` | 160 ms | EaseInOut | Tab/selected indicator、small panel change |
| `panel` | 200 ms | EaseInOut | 非模态 panel 显隐上限 |

- 常规 Button 不做缩放、弹簧或布局位移。
- Popup/Menu/Tooltip 只做 opacity + 2-4 px visual offset；Hit、barrier 和 Semantics 仍由 committed open 状态一次发布。
- Modal 不做长入场动画；命令可用性不能等待动画结束。
- Reduced motion 把 duration 归零，但不改变 Tooltip delay、输入顺序、Focus 或 callback。
- 继续复用 `Core::IMonotonicClock` 和现有 Motion store，不建立桌面动画 update loop。

## 组件视觉规范

### Surface、Divider、Badge

- `UISurface` 继续是普通 Panel profile：Plain=`surface`，Filled=`surfaceContainerLow`，Elevated=`Raised`。
- `UIDivider` 默认 `outlineVariant` 1 px；只有 section 边界使用 strong outline；Accent divider 极少使用。
- `UIBadge` 只用于短状态或计数。Neutral、Accent、Danger 分别映射 surface/primary/error container；不用于长说明。

### Button 与 IconButton

| Variant | 使用场景 |
| --- | --- |
| Primary | 一个 command cluster 中最重要且安全的下一步；Play、Confirm、Create |
| Tonal | 默认桌面命令；Add、Apply、Refresh |
| Outlined | 次级但需要稳定边界；Save As、Browse |
| Text | 低强调命令；Undo、Redo、Cancel、导航 |
| Danger | Delete、Discard、Remove 等破坏性动作 |

`UIIconButton` 应作为第一方多节点 component profile：一个现有 Button root + 一个
`makeIconElement(UIIconContent, ...)` child，可选 Tooltip Anchor。它不增加 Button kind、Activate store、Focus 路径或
GPU 状态。默认尺寸来自 density，必须提供 accessible name。

### TextEdit 与 FormField

TextEdit 本体保持现有 TextInput 状态机。现代表单由 `UIFormField` component profile 组合：Label、TextEdit、可选
helper/error text 和可选 leading/trailing IconButton。Error 由产品验证状态选择 StyleClass/semantic description，
不能复制 TextEdit buffer 或 selection。

- Label 放在输入框上方，不把 placeholder 当 label。
- Focus 使用 primary outline；Invalid 使用 error outline；两者同时存在时 error 保留，外层 focus ring 仍可见。
- 单行高度使用 density 指标，多行由显式 visual row 配置决定。
- Clear/Reveal 等 trailing action 是独立 Button，必须可聚焦并有 name。

### Checkbox、Radio、Segmented、Switch

- Checkbox 表达独立布尔值；Radio/Segmented 表达互斥选择；Switch 表达立即生效的设置。
- Segmented 只圆组外侧角，Selected 使用 `primaryContainer`，不能用 disabled 表达 active。
- Switch 继续复用 Toggle 状态；thumb 与 track 必须在 36x20/44x24 内稳定，不随 hover 改变 layout。
- Label 默认由相邻 Label 或 control root semantics 提供，不为 mark/thumb 建子 semantics。

### Slider 与 ProgressBar

- Slider track 4 px，thumb 12/14 px；命中区域由 `sliderHeight` 提供，Dragging 使用 16% state layer 和可见 focus ring。
- ProgressBar 是只读状态，不获取 Focus；determinate fill 使用 primary，错误状态显式使用 error。
- 数值文本在独立 Label 中显示，不能绘制在过窄 track 上造成重叠。

### Dropdown、Menu、Tooltip

- Dropdown anchor 外观接近 TextEdit，不使用 Primary filled button。
- Menu 使用 Floating elevation，MenuItem 按 density 高度排列；Separator 只占 1 px 视觉线和明确上下 padding。
- Tooltip 使用高对比 `surfaceContainerHigh/onSurface`，最大宽度受 density 限制，不可获取 Focus 或阻挡命中。
- 三者继续使用各自现有关系、barrier、delay 和 committed metrics，不合并状态机。

### ListView、TreeView、TabView

- 列表/树以整行 state layer 表达 hover，Selected 使用 primary container；Focus ring 与 selected 独立。
- Tree disclosure、辅助 Icon 使用 `onSurfaceVariant`；行缩进使用 spacing Token。
- Tab 默认是紧凑文档导航，不做大号移动端 pill。Selected 使用底边 indicator 或轻 container，关闭按钮是独立 IconButton。
- 虚拟行复用时必须清理 Hover/Selected/FocusVisual cache，继续使用固定 row pool。

### SplitView、Splitter

- Splitter 可见线为 1 px，命中宽度更大；Hover/Dragging 只改变 line/state layer，不改变 pane geometry。
- Pane minimum、fraction 和 committed metrics 继续由现有 SplitView contract 管理。

### Modal 与 Dialog

`UIDialog` 应是基于现有 Modal 的 bounded component profile：Modal root + Surface + Title + Body + Action row。
Modal barrier、Focus scope、restore 和 Dialog semantics 仍由现有 Modal 提供。

- 默认宽 420/480，最大宽度受 viewportMargin 限制。
- Action row 右对齐：Cancel/Text 或 Outlined，主动作 Primary，破坏动作 Danger。
- 不在 Dialog 中嵌套装饰 card；长内容使用一个 ScrollView。

### Snackbar

`UISnackbarHost` 是调用方持有的窗口级 fixed-capacity 状态，最多排队 4 条 bounded strict UTF-8 消息；显式
`MonotonicTimePoint` 驱动 Entering/Visible/Exiting/Hidden，相同状态机同时决定 120 ms 入场和 100 ms 退场
opacity/visual-offset Motion。可选 action 使用非零 token 返回业务层，不复制命令状态，也不自动请求 Focus。
`buildSnackbarHost()` 通过一个精确预算事务构建 overlay root、Floating surface、tone bar、polite live-region Label
与可选 action Button；UIContext、UITreeUpdater 和 Runtime phase facade 使用同一 recipe。队列耗尽返回
`CapacityExceeded`，产品可选择丢弃非关键反馈，但不能让已成功的业务事务回滚。

Windows UIA 将 `UISemanticsLiveSetting::Polite/Assertive` 映射到 `UIA_LiveSettingPropertyId`，且只在已发布
live-region 的文本或 setting 真正变化时发出 `UIA_LiveRegionChangedEventId`。Snackbar 不是 Tooltip，不建立
Anchor、hover delay、Popup barrier 或并行可访问状态。

### 后置组件

- `UIChip`：仅在 Filter/Tag 有真实产品场景时，以 Button/Toggle/Radio capability 组合，不新增并行选择状态。
- `UISearchField`：优先由 `UIFormField + UIIconButton` 组合；只有重复使用后才冻结 recipe。

## Desktop Shell 实现

### 推荐树结构

```text
Root Surface(background), Column
|- Command Bar (36 Compact / 44 Comfortable)
|  |- brand/project identity
|  |- primary command groups
|  `- window/application commands
|- Document Tab strip
|- Context Toolbar
|- Workspace (horizontal SplitView A)
|  |- Left Dock: Hierarchy + Project Assets
|  |- Splitter A
|  `- Main (horizontal SplitView B)
|     |- Center (vertical SplitView C)
|     |  |- Viewport / active document
|     |  |- Splitter C
|     |  `- Timeline / output panel
|     |- Splitter B
|     `- Right Dock: Inspector
|- Status Bar
|- Tooltip/Menu/Popup anchored overlays in the same root
`- Modal/Dialog in the same root focus/modal scope
```

三个嵌套 SplitView 足以表达左 Dock、中央 Viewport、右 Inspector 和底部 Timeline，不需要引入 Desktop 专用
Dock Runtime。应用状态只保存 pane fraction、active tab 和 collapsed intent；UI 仍发布唯一 committed geometry。
显式收起必须同时将 pane/splitter 设为 `Collapsed` 并把 fraction 落到对应边界（primary 为 `0`、secondary 为
`1`），否则隐藏内容仍会占用布局空间。恢复时重新发布完整 layout 并还原收起前的 fraction。

### 默认尺寸

| 区域 | Compact 建议 | 规则 |
| --- | ---: | --- |
| 左 Dock | 240-320 | 默认 272，最小 200 |
| 右 Inspector | 280-400 | 默认 320，最小 240 |
| Timeline | 160-320 高 | 默认 220，可折叠 |
| Viewport | 最小 480 x 320 | 获得剩余空间，不包在装饰 card 中 |
| Status Bar | 24 高 | 只放状态、计数、错误入口 |
| Window | 最小 960 x 640 | 低于此值优先折叠右 Dock/Timeline，不缩放字体 |

Timeline 的六个 frame slot 在 Compact Editor 中使用固定 `44 logical px` 宽度，并显式保持 `flex shrink=0`；
slot 只承载可扫描的帧号，时长、Sprite 与事件数放在 selected-frame summary，summary 过长时使用 paint-only
ellipsis。这样动态 authoring 文案不会改变播放、添加、复制、删除命令的相对位置。

### Command Bar

- 高复用工具使用 IconButton + Tooltip；不熟悉或高风险命令使用 Icon+Text。
- 每组最多一个 Primary；普通命令 Tonal/Text；Delete/Discard 使用 Danger。
- Undo/Redo、Save、Play/Pause、Viewport tool 使用熟悉图标，不重复显示冗长说明。
- 使用 Divider 和 8-12 px group gap，不把每个命令放进独立 card。

TinaEditor 使用私有 `EditorToolbarGroup` 把紧凑 icon command 聚成一个低层级 surface，并通过 90 ms
BackgroundColor transition 提供 hover 状态；`EditorIconButton`/`EditorIconToggleButton` 仍复用公共 Button、Radio、
Tooltip 和 Image atlas 链。该私有 recipe 不增加公共 Widget kind，也不要求旧文字工具条兼容。
TinaEditor 的主 Command Bar 不承载 document path 或产品名，中央只保留 2D/3D workspace selector；等宽 grow region
保护右侧 play/history/save controls。路径由 document session 持有，Save As 使用平台 dialog；Document Tab strip
只在用户实际打开外部 scene/Catalog document 时出现。

### Dock 与 Inspector

- Dock 是全高 surface band，不是浮动 card。
- 左右 Dock Header 提供向外收起的 IconButton；`View` 菜单提供可勾选入口，确保隐藏后仍可恢复。
- `Help > About Tina Editor` 打开单动作 Modal Dialog，关闭后恢复 Help anchor focus。
- Section header 使用 Section/Control typography，支持 collapse 时才提供 disclosure Button。
- 属性行建议使用 `minmax(label, control)` 两列；Label 左对齐，Control stretch，错误/helper 放在下一行。
- 大量条目使用 ListView/TreeView 虚拟化；不为每行创建独立 card。

TinaEditor 的 `EditorPanelHeader`、`EditorSectionHeader`、`EditorPropertyRow` 与 `EditorSearchField` 是 EditorApp 私有
固定预算组合：PanelHeader 是无圆角、无描边的扁平 surface band，title 可收缩且 action 区占据剩余空间并末端对齐；
SectionHeader 用 3 个节点组合 section title 和可伸缩的 subtle horizontal Divider。PropertyRow 直接消费第一方
Grid，以68 px label track + `Fr` value track 统一父级约束；SearchField 组合 decorative Search icon 与唯一 TextEdit。
Hierarchy 搜索按 ASCII 大小写不敏感匹配完整 UTF-8 label byte sequence，显示匹配项及祖先，搜索期间展开匹配路径，
并按 stable ID 保留当前选择；被过滤的选择回退到 document root。
Inspector Transform 固定发布 Position、Rotation、Scale 三行父级 Grid；每行使用52 px label track，右侧按
workspace 以一至三个 `Fr` cell 发布轴字段。每个轴字段再用12 px 居中的 X/Y/Z label + `Fr` TextEdit，整体
限制在52..96 logical px。拖动 Inspector 只重新分配 track，不切换方向、不重建节点，也不使用宽度阈值。
World2D entity 的 Position/Scale 显示 X/Y，Rotation
只显示 Z；World3D entity 显示完整九轴。Asset Inspector、TileMap document 或无 entity selection 时折叠整个
Transform 区块。旧的九行 NumberField 与逐轴 step Button 已删除；普通信息文字使用 Theme primary，warning/error
tone 只表达真实反馈状态。Transform Header 右侧承载 Apply action；其他 Inspector PropertyRow 使用68 logical px
label + `Fr` value Grid，单值 TextEdit 最大宽度为132 logical px。Sprite Size/Pivot 与 ShadowOccluder Start/End
分别把 X/Y 放在同一属性行，轴标签水平和垂直居中。
节点专属属性按上下文与严格 Node kind 发布：同类型多选只显示该类型固有的 property section，`AnimatedSprite2D`
显示 Rendering + Animation，其余 Node 只显示对应区段。不存在 Components Header、Add/Remove Component 或兼容
Menu；Asset Inspector、TileMap document、无 Node selection 或多选类型不一致时折叠全部专属 root。
Hierarchy Header、Parent ID 行和 Apply Parent wrapper 只在 entity context 可见；TileMap Header、状态摘要和四个
action row 只在 TileMap document 可见。因此 Asset Inspector 的稳定组成是 Identity + metadata/dependencies，
Scene node 是 Identity + Transform + 节点专属属性 + Hierarchy，TileMap document 是 Identity + TileMap；
Scene 无 entity selection 时退化为 Identity。实现保存每个 contextual root 与 PropertyRow 的完整构建 layout，
切换时只发布 `Visible`/`Collapsed`，不会把 Grid 恢复成不完整的 Flex/default 布局。

### Viewport

- Viewport 是第一视觉信号，占用最大剩余区域，背景使用 `Sunken`。
- Overlay 工具、gizmo、marquee 继续使用现有 Canvas/Line/Ellipse/Image 和 Ignore hit 子节点；真实交互由 viewport
  route owner 处理。
- 右上角 orientation control 使用 Tina 自身世界坐标：2D 为无底盘的紧凑 X/Y 罗盘，3D 为带球面层、
  高光、经纬线和深度衰减轴的球形 X/Y/Z View Gizmo。完整控件区域建立 Pointer barrier，不能把空白处的输入
  穿透给 viewport；3D 正轴端点使用标准 Button 行为切换
  `Right` / `Top` / `Front` 视图，2D 保持固定 Orthographic 方向反馈。
- Viewport 不发布 footer；Grid、Catalog、camera 的正常状态不常驻显示，tool mode 由 toolbar 选中态表达，缩放由
  视口滚轮完成，不重复提供按钮、Slider 或常驻百分比。

### 响应规则

1. `>= 1280` logical px：左右 Dock 与 Timeline 可同时显示。
2. `960-1279`：按 min size 压缩 Dock，Timeline 默认折叠或降低高度。
3. `< 960`：Desktop 最小支持宽度；右 Dock 通过显式命令切换显示，不能继续压缩字体和控件直到重叠。
4. Window resize 只修改 SplitView/Layout intent；Render viewport 从最后成功 committed rect 在下一帧更新。
5. 100%/150%/200% DPI 使用相同 logical layout，截图和 Hit 坐标必须一致。

### Focus 与命令顺序

- Focus 顺序按 tree/document reading order：Command Bar -> Tabs -> Left -> Viewport toolbar/content -> Right -> Timeline -> Status actions。
- Menu/Modal 打开时只在对应 committed scope 内导航；关闭后恢复合法 anchor/previous focus。
- Tooltip 不进入 Focus 顺序；decorative Icon 不进入 semantics tree。
- Keyboard/Gamepad 调用与 Pointer/UIA 调用复用同一默认 action，不建立 Desktop command bypass。
- 全局快捷键必须在 TextEdit printable input 之前区分修饰键；裸字母不能抢占文本输入。

## Theme 与 Desktop 数据流

### 显式切换

```text
User selects Dark/Light
  -> application records requested UIColorScheme
  -> next valid UI update phase builds same-density UITheme candidate
  -> UIContext validates all tokens and preflights dirty capacity
  -> role-bound chrome + text metrics stage
  -> one successful commit publishes Layout/Hit/Paint/Semantics
  -> failure keeps previous theme and committed snapshots
```

### 跟随系统

系统跟随是后置 Desktop adapter：Windows/macOS/Linux adapter 只发布 `Dark` 或 `Light` preference event；Runtime
在 owner thread 的有效 UI phase 消费。Headless 与无 adapter 平台使用应用显式默认值。公开 `UITheme` 不暴露 HWND、
GLFW、WinRT、Cocoa、DBus 或 platform enum。

当前实现通过 `Desktop::CreateEngineOptions::followSystemColorScheme` 显式 opt-in，默认关闭。GLFW 私有 observer
当前只在 Windows 读取 `AppsUseLightTheme`；查询不到或非 Windows adapter 时不发布事件。Runtime 私有
`PrimaryWindowUIColorSchemeCoordinator` 在 UI Update phase 应用最后一个完整 preference event，保持 active
density；包含 `PlatformEventStreamReset` 的批次被忽略，adapter 在事件未成功 append 时下一帧重试。

### Density

Density 在 root 构建前选择。产品创建 root 时同时保留对应 `UIControlMetrics` 用于显式 layout；Color scheme 切换必须
保持同一 density。若用户未来需要运行时 density 设置，应销毁并以保存的业务状态重建 root，不在 live tree 上做半份
metric 替换。

## 代码组织

### 公开头

```text
include/tina/ui/
|- UIDesignTokens.hpp       # scheme/density/color/state/spacing/shape/elevation/control metrics
|- UITheme.hpp              # UITheme aggregate + makeModernDesktopTheme
|- UIIconButton.hpp         # Button + Icon bounded component recipe
|- UIFormField.hpp          # Label + TextEdit + helper/error composition recipe
|- UIDialog.hpp             # Modal-based dialog composition recipe
`- UI.hpp                    # umbrella export
```

不要为 CommandBar、Dock、Inspector、StatusBar 立即增加内建 kind；先在 Showcase/Product composition 中用
Surface/Divider/Button/SplitView/TabView 组合。重复场景与状态需求得到证据后再冻结 recipe。

### 私有实现

```text
src/ui/detail/
|- UIThemeValidation.*
|- UIStateLayerResolver.*
|- UIModernDesktopChrome.*
|- UIIconButtonRecipe.*       # 若 public constexpr recipe 无法完整表达 transaction
|- UIFormFieldRecipe.*
`- UIDialogRecipe.*
```

`UIContext.cpp` 只协调 owner、capacity、transaction 和 publication；颜色合成、chrome 解析和多节点 recipe 不继续堆入
巨型 Context 实现。私有模块不反向拥有 `UIContext`，不绕开 committed snapshot。

### 产品接入

```text
samples/ui_showcase/          # 第一视觉消费者；同时展示 Compact/Comfortable、Dark/Light
samples/desktop_shell/        # Desktop Shell reference；嵌套 SplitView 工作流与第一个 SplitView 产品消费者
editor/app/                   # 体系稳定后的 Compact 迁移；遵守 Editor 大功能统一验证节奏
samples/2d_tilemap_bgfx/      # 只迁移产品 Theme/role，不改 gameplay
samples/3d_product/           # 只迁移产品 Theme/role，不改 Render/Asset owner
```

## 破坏式迁移规则

用户已明确不需要兼容旧设计，因此实施时使用一次清晰迁移，不保留双轨：

1. 新 Token aggregate 与 validation 先进入同一变更。
2. `UITheme`、全部 chrome factory、StyleRole resolver、Showcase、2D/3D 产品调用点和测试同步迁移。
3. 删除 `surface0/1/2`、`borderLight/borderDark`、`textPrimary/textSecondary`、`buttonNormal` 等旧平铺字段。
4. 删除 `makeDefaultProductTheme()`、`makeLightProductTheme()`，统一改为 `makeModernDesktopTheme()`。
5. 删除 `lightenChannel()` / `darkenChannel()` 在产品 chrome 中的使用，改为 State Layer resolver。
6. 不增加 deprecated alias、compatibility overload、旧主题转换器或运行时双读。
7. 编译器错误和 `rg` 结果作为迁移清单；不需要提交用于拆分任务的脚本。
8. ADR 0023 只追加实现注记；若实施需要反转其 Accepted 原则，必须新建 superseding ADR，不改写历史理由。

建议完成后的静态清理命令：

```powershell
rg -n "surface[012]|borderLight|borderDark|buttonNormal|makeDefaultProductTheme|makeLightProductTheme|lightenChannel|darkenChannel" `
  include/tina/ui src/ui samples tests docs `
  -g "!docs/adr/**" -g "!docs/ui-modern-desktop.md"
```

允许 Accepted ADR 和本文的迁移说明保留历史名称；现行源码、主题文档和新业务代码必须零命中。

## 实施任务

下面的 `TMD-*` 是 `UI-MODERN-DESKTOP-001` 内部切片，不在 Backlog 重复维护状态。

截至 2026-08-19，TMD-00..TMD-07 已完成：破坏式 semantic Theme、Dark/Light x
Compact/Comfortable、State Layer/FocusVisible/Elevation、统一 metrics、基础控件及导航/集合/浮层 chrome
已经迁移；`UIIconButton`、`UIFormField`、`UIDialog` composition profile 已进入直接 UIContext、
`UITreeUpdater` 和 Runtime phase facade；Showcase 已是 Desktop workbench，并通过 UI 771/771、
Runtime UI 142/142、UI-Render 28/28、UIA 13/13 与 12/12 smoke 矩阵。
TMD-08 的真实 100%/150% DPI 配对证据仍暂缓；TMD-09/TMD-10 已完成本轮集中 gate；
TMD-11 的 benchmark unit（10/10）与冻结 workload 确定性 gate 已通过：`ui_static_commit_v1`、
`ui_component_build_v1`、`ui_style_state_v1`、`ui_motion_v1`（seed 0/1/2，对应 0/64/1024 active
tracks）均为 `status=ok`，counter/checksum/allocation/clean-rebuild invariant 保持。Debug 开发机墙钟仍为
`provisional`，approved fixed-machine baseline/hard gate 继续由 `PERF-002` 跟踪；父任务仍待 TMD-08
真实 100%/150% DPI 配对视觉和随后最终文档收口。

| ID | 工作 | 主要产物 | 验收重点 |
| --- | --- | --- | --- |
| TMD-00 | 设计冻结与现状审计 | 本文、现有 Theme/Showcase 截图与 token inventory | 当前/目标边界清楚，任务进入 Backlog/Roadmap |
| TMD-01 | 语义 Token 与破坏式 Theme API | `UIDesignTokens.hpp`、新 `UITheme`、Dark/Light x density factories | 无旧字段/alias；header isolation；完整 validation |
| TMD-02 | State Layer、FocusVisible、Elevation | 私有 resolver、role chrome mapping、state precedence tests | 不再直接 lighten/darken；不新增 GPU pass/focus owner |
| TMD-03 | Compact/Comfortable metrics | Typography/Spacing/Shape/control tables，density rebuild rule | 同 root 单一 density；非法指标 fail closed |
| TMD-04 | 基础控件 chrome 迁移 | Surface/Divider/Badge/Button/TextEdit/Checkbox/Radio/Switch/Slider/Progress | Dark/Light/state matrix；layout/hit/semantics 不回归 |
| TMD-05 | 导航、集合和浮层 chrome 迁移 | Dropdown/Menu/Tooltip/List/Tree/Tab/Splitter/Popup/Modal | transient barrier、virtual row reuse、focus restore 不回归 |
| TMD-06 | 缺失的桌面 authoring profile | `UIIconButton`、`UIFormField`、`UIDialog` | bounded component transaction；复用现有行为与 semantics |
| TMD-07 | Showcase 重构 | 真实 Desktop workbench、density/theme selector、全部组件状态 | 无嵌套卡片/重叠；首屏可操作；结构化与视觉证据 |
| TMD-08 | Desktop Shell reference（`samples/desktop_shell/`） | 嵌套 SplitView、Command Bar、Tabs、Viewport、Inspector、Timeline、Status | resize/DPI/focus/menu/dialog/tooltip/splitter 工作流 |
| TMD-09 | 产品迁移 | 2D/3D Theme；最后迁移 EditorApp Compact | 不改 gameplay/asset/render owner；Editor 单次大功能 gate |
| TMD-10 | OS scheme 后置接线 | 私有 platform observer + Runtime owner-thread coordinator | public header 无平台类型；无 adapter 时 deterministic |
| TMD-11 | 完整门禁与文档收口 | Unit/Runtime/UIA/Render/Visual/Perf/Consumer/docs | Definition of Done 全部满足后父任务 Done |

### TMD-01：Token foundation

- 新建强类型 Token structs 和合法性检查。
- `UITheme` 改为 aggregate，不保留旧字段。
- 新建四个 canonical factory 组合：Dark/Light x Compact/Comfortable。
- `setProductTheme()` 对 scheme 变化保持现有失败原子性；live root density 变化拒绝。
- 更新 UIContext、Runtime facade、StyleRole resolver、header isolation、Public API 文档。

### TMD-02：统一状态解析

- 新增 `UIStyleState::FocusVisible`，从现有 focus + input modality 派生。
- 新增纯函数 State Layer 合成与状态优先级测试。
- 将 Focus ring 与 Selected container 分开。
- 将 Elevation level 映射到现有 fill/outline/shadow fields，不修改 DisplayList ABI。
- Theme/stylesheet/local override 优先级保持不变。

### TMD-03：密度指标

- 把控制高度、Icon、row、toolbar、splitter hit extent 从 Showcase/Product 局部魔法值集中到 `UIControlMetrics`。
- 所有数值 finite、非负，thumb/mark/inset 必须能放入对应 control extent。
- Compact/Comfortable 分别增加 default geometry tests。
- 运行时 color scheme 切换保持 metrics bit-identical。

### TMD-04/TMD-05：现有控件迁移

- 先迁移无状态 Surface/Divider/Badge，再迁移 Button/TextEdit/selection controls。
- 第二批迁移 Dropdown/Menu/Tooltip、List/Tree/Tab 和 Modal/Popup。
- 每个 role 只从 semantic tokens 取色；literal 颜色只允许测试 fixture 或明确产品 StyleClass。
- 所有控件覆盖 Rest/Hover/Pressed/FocusVisible/Disabled，以及适用的 Selected/Checked/Open/Dragging。
- commit/capacity failure 必须保留旧 theme、old committed paint 和 semantics snapshot。

### TMD-06：新 profile

- 已新增 `UIIconButtonConfig/Parts`、`UIFormFieldConfig/Parts`、`UIDialogConfig/Parts` 和精确的
  `required*BuildBudget()`。三者通过 `buildIconButton()/buildFormField()/buildDialog()` 暴露给直接
  `UIContext`、`UITreeUpdater` 与 Runtime phase facade。
- `UIIconButton` 的 wrapper 下由 Button 和可选 Tooltip 同级，Icon 是 Button child；Button 是唯一行为与
  accessibility root，Tooltip 继续使用既有独立 Anchor 关系。
- `UIFormField` 组合 Label、input row、唯一 TextEdit、helper/error 与至多两个可选 Icon action；error 优先成为
  TextEdit accessible description，并选择 `TextInputInvalid`/`TextError` role，不复制 TextInput 或 Activate 状态。
- `UIDialog` 组合既有 Modal、Surface、Title、Body、action row 与至多四个 Button；Modal 仍是唯一 barrier 和
  Focus Scope owner。
- 三条入口在首个节点前验证 UTF-8、variant、Tooltip/multiline/layout 参数并预留精确 node/text/Behavior
  容量；任一步失败或 transaction 逃逸均整棵回滚。显式活动 build transaction 期间 Runtime profile 构建会被拒绝。
- 2026-08-19 集中门禁：`tina_ui_tests` 761/761、`tina_runtime_ui_tests` 142/142、
  `tina_ui_render_integration_tests` 28/28、`tina_ui_uia_tests` 13/13；三个新公开头均接入
  header-isolation，且无 bgfx、GLFW、FreeType、UIA/COM 或 HWND 类型泄漏。

### TMD-07/TMD-08：先证明 Desktop，不先改 Editor

Showcase 应从固定控件卡片墙改为可操作工作台：左侧导航、中央组件画布、右侧 Token/State Inspector、顶部命令条、
底部状态栏。它同时承担以下证据：

- Dark/Light 即时切换；
- Compact/Comfortable 通过重建 root 切换；
- 所有控件状态和 transient overlay；
- IconButton、FormField、Dialog；
- 960/1280/1600 logical width；
- 100%/150%/200% DPI；
- keyboard-only、pointer、Gamepad、UIA 路径。

TMD-07 当前实现采用一棵 root 和五个明确的 Desktop band：Command Bar、Explorer、可滚动 Component Canvas、
Token/State Inspector、Status Bar。普通 section 是中央 Canvas 的全宽 Flow 子项，不再使用双列卡片墙或嵌套卡片；
Dialog 是 root 下唯一真正的 Overlay/Modal。默认窗口为 `1280x800` 且可调整，CLI 接受
`--width=960..3840`、`--height=640..2160`、`--theme=dark|light` 与
`--density=compact|comfortable`。960px 下 Explorer/Inspector 使用受约束的较窄 track，中央组件仍由纵向
ScrollView 承担溢出，不缩放字号、不覆盖相邻区域。

Density 不是 live-root 属性。`ShowcaseApplication` 持有唯一 `ShowcaseUIState`，保存 scheme/density、FormField
文本、Slider/Checkbox/Radio、Dropdown/List/Tree selection、Tree expansion、组件画布与局部 ScrollView offset、
Dialog open 状态。

Density 重建采用 destroy-then-rebuild，顺序由 Context 不变式决定，不是实现偏好：

1. `createRoot()` 在任何子节点存在前就使 `liveRootCount` 变为 1，而 `setProductTheme()` 只在零 live root 时接受
   density 变化。因此 density 必须在 root 构建前通过 `PrimaryWindowUIRootBuilder::setProductTheme()` 确立；
   在 `createRoot()` 之后 stage 会被拒绝，即使是第一个 root。
2. 交接时 `ShowcaseState::updateFrame()` 先释放本状态的 root，再 `requestReplace()`；replacement 因而在零
   live root 下 stage 新 density 并构建。稳定态与交接峰值都只有一个 live root，`rootCapacity` 为 1。
3. 代价是失去「replacement 失败时旧像素仍在」这一性质。因此 `updateUI()` 在已释放 root 而交接未提交时
   返回错误，fail closed，绝不呈现无 root 的窗口。

不采用「新 root 先建立、旧 root 后释放」的双 root 原子交接：`buildCommittedStructure()` 与
`buildLayoutOrder()` 会遍历全部 root 并一起送入 Layout/Hit/Paint/Semantics；candidate 中创建 Modal 会立即
关闭 active root 的 Tooltip/Menu；layout 容量 guard 使用跨 root 的 `nodes.activeCount()`。要让第二个 slot
真正只服务交接，必须把 staged/live 贯穿全部发布路径、输入路由与每个 root-scoped mutation 边界，并为两棵树
做容量预算 —— 那等于在 UIContext 内建第二棵 UI 树，与本文「不建立第二棵 UI 树」和「UIContext 只协调」冲突。

Window 级 StyleClass/ColorToken/StyleSheet 由应用状态只注册一次（`styleRegistrationClosed` 在首个节点后永久
置位，销毁全部 root 也不重开），replacement root 复用稳定 ID。Color scheme 仍在现有 root 上通过
`setProductTheme()` 事务切换。这样 root replacement 不复制 UI 状态机，也不会用自动演示重放掩盖状态丢失。
`--auto-demo` 会在最终值建立后往返两次 density、恢复初始 scheme、打开并关闭 Dialog，并由结构化 JSON 验证
三次 state/root 生命周期和跨 root 状态连续性。

Showcase 通过后才迁移 EditorApp，避免把视觉 foundation 调试和复杂 authoring workflow 回归混在一起。

### TMD-08 当前实现

`samples/desktop_shell/` 是 Desktop Shell reference，与 Showcase 分工不同：Showcase 是组件目录与
Dark/Light x density 证据，Shell 证明真实工作流。它是本文推荐树结构的第一个消费者，也是
SplitView 的第一个产品消费者。

一棵 retained root，四个常驻 band（Command Bar、Workspace、Status Bar，加上 Workspace 内的 Viewport Context Toolbar），
外部文档存在时才插入 Document Tabs；
Workspace 由三个嵌套 SplitView 组成：A 为 `左 Dock | main`，B 为 `center | Inspector`，C 为
`Viewport | Timeline`。每个 pane 都是普通 Element 子树，没有 Dock Runtime，也没有第二棵 UI 树。

几何只读不算：产品从 `splitViewMetrics()` 读取 UI 已发布的唯一 committed 几何，不自己推算 pane 矩形。

Pane 可见性分两层，避免响应式档位与显式命令互相覆盖：

- `timelineHideRequested` / `inspectorHideRequested` 是命令写入的用户意图；
- 解析结果 = 宽度档位 `OR` 用户意图。这样本文响应规则 3 的「右 Dock 通过显式命令切换显示」才真正可达，
  而不是只由宽度决定。

两个实现约束值得记录，它们不是 bug：

1. Focus 目标必须已经是 committed keyboard-focus candidate，而 GameStateEnter 阶段没有任何提交，
   因此初始 focus 必须延后到首次成功发布之后。
2. `setSplitViewFraction()` 改的是 retained 状态，committed 几何要到下一次 commit 才变化，
   所以验证 splitter 效果必须跨一次发布，不能同帧读回。

Command Bar 的五个 icon-only 工具现在使用 `DesktopShellIconAtlas.hpp` 提供的产品 fixture atlas，
并通过既有 `Texture2DFrameResourceResolver`、FramePin、Texture2D、DisplayList 和 GPU shader 链发布。
Shell 不拥有第二套 atlas manager 或 Icon runtime；按钮仍由 `UIIconButton` 复用 Button 行为，装饰 Icon child
默认 Ignore/Exclude，Save Tooltip 与 Button 同 root 绑定。自动 gate 同时验证 atlas upload、每帧 resolve/pin
命中、ImageQuad 资源复用、root 释放后的 texture invalidation 和零 frame borrow。

`--auto-demo` 脚本驱动的是命令回调写入的同一份 durable state，然后从 committed 状态读回验证：Menu 开关
取自 Menu store，Dialog 开关取自 committed Modal 几何，Tooltip 取自 committed tooltip metrics，
Splitter 取自跨帧 committed pane 宽度。它不合成指针与键盘输入，因此不声明端到端输入验证；输入路由由
`tina_runtime_ui_tests` 覆盖。

TMD-08 的真实 DPI 证据统一由 `tools/windows/RunTmd08DesktopShellVisualGate.ps1` 采集。脚本在 build/launch
之前调用 Windows scale probe，实际主显示器缩放与 `-ExpectedScalePercent` 不一致时立即失败，不允许用窗口尺寸或
process DPI compatibility 冒充 OS 100%/150%。每轮固定运行 Dark/Light × Compact/Comfortable ×
960/1280/1600 logical width，验证 sample JSON、logical/framebuffer/contentScale、committed pane geometry、
focus/Menu/Dialog/Tooltip/Splitter workflow、icon atlas resolve/release、关键区域非空，以及 theme/density raster
差分。门禁只接受 FreeType 产品 preset，并记录真实字体路径/哈希，避免 placeholder 冒充 typography 证据。
第二轮通过 `-PeerReportPath` 对照第一轮，要求 EXE 与字体 SHA-256 相同并逐 case 比较 logical geometry。

```powershell
# OS Settings 为 100% 时：构建一次并保存第一份报告
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunTmd08DesktopShellVisualGate.ps1 `
  -ExpectedScalePercent 100 `
  -OutJson artifacts\gates\tmd-08-desktop-shell-100pct.json

# 切换到真实 150% 后复用同一二进制，并与 100% 报告交叉核验
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunTmd08DesktopShellVisualGate.ps1 `
  -ExpectedScalePercent 150 -SkipBuild `
  -PeerReportPath artifacts\gates\tmd-08-desktop-shell-100pct.json `
  -OutJson artifacts\gates\tmd-08-desktop-shell-150pct.json
```

两轮报告只证明同机、同 backend、同二进制的真实 DPI 一致性；混合 DPI 多显示器与跨 GPU golden 继续由
`UI-003` 跟踪。

### TMD-09：产品迁移

- 2D/3D sample 只替换 Theme、StyleRole 和必要 layout metrics，不改 Scene/Asset/Render 数据流。
- EditorApp 使用 Compact；设置、New Project、Dialog 可局部使用 Comfortable 只在独立 root/dialog profile 明确配置。
- 删除 Editor 局部手写 surface/accent/disabled-active 颜色。
- Command Bar、Dock、Viewport、Inspector、Timeline、Status Bar 按本文 Shell 结构重排；Editor 将 context tools 合并进 active Viewport，避免重复 band。
- 遵守 Editor “大功能闭环后统一验证”：迁移期间不按小切片反复构建和测试。

当前产品侧进度：2D sample 已使用 semantic Theme、产品 chrome 和 Compact control recipes；3D product
已进一步移除控件高度、列表/树行高、按钮 padding 与标题/正文/Caption 的旧字面量，统一读取
`UITheme.controls`、`UITheme.spacing` 和 `UITheme.typography`。本轮只改 authoring/metrics 层，Render、Asset、
Scene 和资源生命周期不变；Dark/Light 60 帧产品 smoke 均通过。EditorApp 源码现已固定使用
`makeModernDesktopTheme(Dark, Compact)`，Theme 在 `createRoot()` 前绑定；根 band 顺序为 Command Bar、
按需出现的 Document Tabs、Workspace、Status Bar，Workspace 使用三层嵌套 SplitView 表达
`Left Dock | Main`、`Center | Inspector`、`Viewport | Timeline`。Editor 的 Button/TextEdit/Tab、List/Tree
行高、Splitter、Status Bar、spacing/padding 均读取 Theme metrics，旧的局部 surface ColorToken、StyleClass
及自动换色路径已删除。Left Dock/Inspector 可从 Header 收起并由 `View` 菜单恢复，底部面板关闭时同步释放
SplitView fraction；三者恢复时保留最后拖拽尺寸。Command Bar 移除产品名，并在同一行放置菜单、中央 workspace selector、
历史/保存和运行命令；Document Tab 只承载外部 document，全部槽为空时整个 strip 为 `Collapsed`。Viewport 把
Scene/TileMap context 与 transform/snap/marquee/frame/view tools 合并为一行，画布不再发布 footer；Project Assets 使用
120 logical px 最小格宽的紧凑 virtual grid，以
`AssetKind #abcd` 保持类型优先的扫描层级，完整 AssetId 留在固定 22 logical px selected summary 与 Inspector；summary
在窄 Dock 仅做 ellipsis，不改变 retained 文本或后续布局。New/Open Project 只保留在 `File` 菜单，左侧 Dock
不再重复项目生命周期命令；打开当前 Asset 使用 divider 后的 ArrowRight。空 Source Imports 整段折叠；Inspector
Transform 使用52 px label + `Fr` value 的三行 Grid，并按 World2D/World3D 上下文折叠无效轴；
authoring 内容段统一使用 `EditorSectionHeader`，节点专属 property root 只按当前 selection 的严格 Node kind 发布，
不提供 Add/Remove Component 或兼容菜单；Hierarchy、Project Assets、
Source Imports 的计数使用 Neutral Badge，Inspector selection 使用 Accent Badge，Timeline 顶栏复用同一个扁平
`EditorPanelHeader`。Timeline 的六个帧槽使用固定紧凑宽度，只显示可扫描的帧号；时长、Sprite 与事件数留在
selected-frame summary，summary 过长时以 ellipsis 收敛，帧槽和播放/添加/复制/删除命令不会因动态文案改变位置。
常规信息色不再滥用 warning，旧 `Authoring` 提示段和 `Move X +1` 调试入口已删除，
authoring feedback 只通过 `UISnackbarHost` 发布；Status document/runtime 按 0.8/1.2 分配剩余宽度，二者与 Snackbar
message 都使用 ellipsis，完整 retained/semantics 文本不变。上述均为现行 Editor 私有组合，
不增加公共 Widget kind，也不保留旧设计兼容。此次不改 Render、Asset、Scene owner。2026-08-19 按 Editor 专项节奏完成一次集中
增量 build；`tina_editor_tests` 114/114、`tina_editor_app_tests` 13/13，Editor 2D/3D `--auto-demo`
各 600 帧均 exit 0。Desktop Shell 120 帧、2D 300 帧和 3D 60 帧 Theme smoke 也均 exit 0。

### TMD-10：OS scheme 后置接线

- `PlatformBackendCreateParams::publishSystemColorSchemeEvents` 默认关闭；GLFW observer 是私有实现，公开
  Platform event 只携带 Tina-owned `SystemColorScheme::Dark/Light`。
- `Desktop::CreateEngineOptions::followSystemColorScheme` 是产品 opt-in。关闭时现有 sample、CLI Theme、
  Headless 与自定义 factory 行为不变。
- Windows observer 查询 `AppsUseLightTheme`；无值、查询失败和当前 Linux adapter 都不发布事件，应用默认值
  因而确定。事件 append 遇到 capacity reset 时不提交 observer 状态，后续帧可重试。
- Runtime 私有 coordinator 在 owner-thread UI Update phase、游戏 `updateUI()` 之前应用最后一个完整事件，
  通过 `makeModernDesktopTheme(requestedScheme, activeDensity)` 保持 density。Theme 事务失败保留旧状态并沿
  Runtime 错误边界 fail closed；含 platform-event reset 的批次不消费不完整 preference。
- 已补 Platform payload validation/coalescing、observer 状态与 Runtime coordinator 测试；修复同帧后续事件触发
  capacity reset 时 observer 提前提交状态、导致下一帧不重试的问题。2026-08-19 集中 gate 为 `tina_tests`
  370/370、`tina_runtime_ui_tests` 145/145、`tina_platform_glfw_tests` 43/43，公开头 header-isolation 随
  `tina_tests` 编译通过。

## 测试与视觉门禁

### Unit

- Dark/Light x Compact/Comfortable factory exact token tests。
- Theme validation：NaN/Inf、负 spacing/radius/extent、非法 enum、几何不可能关系。
- State Layer exact composite、状态优先级、FocusVisible input modality。
- 每个 StyleRole 的 token provenance 与 chrome defaults。
- density metrics、IconButton/FormField/Dialog transaction rollback。
- Theme/density capacity failure 保留旧 retained/committed state。

### Integration

- `UIContext` theme switch 的 Layout/Hit/Paint/Semantics 一致提交。
- Runtime `PrimaryWindowUITreeUpdater` phase/lifetime 与 sticky error。
- UI-Render DisplayList checksum 在相同 theme/state 下确定。
- UIA role/name/action/focus 不因 decorative Icon 和 chrome 迁移改变。
- virtual List/Tree row reuse 不残留 state layer。

### Visual matrix

| 维度 | 值 |
| --- | --- |
| Scheme | Dark / Light |
| Density | Compact / Comfortable |
| DPI | 100% / 150% / 200% |
| Width | 960 / 1280 / 1600 logical px |
| State | Rest / Hover / Pressed / FocusVisible / Disabled / Selected / Checked / Open / Dragging |
| Surface | Base / Dock / Raised / Floating / Modal |

视觉 gate 至少检查：非空 raster、Token ROI、文字基线、Focus ring、选中与 hover 差分、Popup/Modal 层级、最长文案
不重叠、Icon 非空、Viewport 未被 UI 遮挡。跨 GPU exact golden 继续由 `UI-003` 跟踪，不能用单机截图冒充。

### 直接执行目标

实现阶段按影响选择最小目标并直接运行 GoogleTest executable，不使用 CTest：

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_ui_uia_tests `
  --parallel 1 -- /nr:false

out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_render_integration_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_uia_tests.exe
```

Showcase、2D/3D 与 Editor 产品 gate 按各自文档和 Editor 集中验证节奏执行；没有修改对应产品时不扩大门禁。

## 性能与容量

- Token structs 不拥有动态内存。
- Theme 切换沿现有 role binding 和 dirty transaction，失败前不改 live storage。
- State Layer resolver 为纯值计算；不为每帧/每节点创建 heap object。
- Style token reverse dependency、virtual row pool、Tooltip/Menu/SplitView/TabView fixed-capacity store 保持现状。
- IconButton/FormField/Dialog 使用 `UIElementBuildTransaction` 完整预算，失败整棵回滚。
- clean frame 不因现代 Theme 增加全树扫描或 DisplayList rebuild。
- `ui_style_state_v1`、`ui_component_build_v1`、`ui_motion_v1` 继续作为确定性基线；必要时新增
  `ui_modern_desktop_theme_v1`，但固定机墙钟仍由 `PERF-002` 管理。
- 2026-08-19 集中 gate：`tina_bench_tests` 10/10；`ui_static_commit_v1`、
  `ui_component_build_v1`、`ui_style_state_v1` 与 `ui_motion_v1` seed 0/1/2 均 `status=ok`。确定性不变量
  可阻断，Debug 开发机毫秒数仅作 `provisional` 证据。

## 无障碍与本地化

- 所有 Icon-only Button 必须在 Button root 提供显式 accessible name。
- decorative Icon/Divider 不发布 semantics；Badge 只发布完整文本。
- FormField 的 Label、helper/error 与 TextEdit description 建立确定关系；placeholder 不代替 name。
- Dialog 发布 Dialog role，打开后 focus 到作者指定 initial control，关闭后恢复合法来源。
- Tooltip 继续作为 Anchor HelpText fallback，不覆盖显式 description。
- 所有公开文本保持 strict UTF-8；MSVC target 保持 `/utf-8`。
- 中英文、长路径、CJK、组合字符和 200% DPI 必须进入布局/ellipsis/semantics 验收。

## 风险与处理

| 风险 | 处理 |
| --- | --- |
| Theme API 破坏面大 | 一次迁移全部 consumer；不保留双轨；编译器 + `rg` 关闭残留 |
| 状态颜色变多导致 paint 分支膨胀 | 私有 State Layer resolver + role chrome factory；UIContext 只编排 |
| Density 热切换造成半份布局 | live root 禁止变更 density；通过 root rebuild 切换 |
| Showcase 好看但 Editor 不适用 | Showcase 使用真实 Desktop Shell 和 Compact 工作流，不做营销卡片墙 |
| Dark/Light 某一侧对比度不足 | 对比度 unit + 双主题视觉 matrix，禁止单控件 literal 修补 |
| 阴影能力不足 | v1 使用 surface/outline/无模糊 shadow；blur 单独立项 |
| 新 profile 复制状态机 | IconButton/FormField/Dialog 只组合现有 Button/TextEdit/Modal |
| Editor 会话并行修改共享文件 | UI foundation 稳定后再迁移 EditorApp；迁移前重新读取最新 UIContext/Runtime facade |

## Definition of Done

`UI-MODERN-DESKTOP-001` 只有同时满足以下条件才能转 Done：

1. 新 semantic Theme、Dark/Light、Compact/Comfortable public contract 与 header isolation 落地。
2. 现行源码和业务代码无旧 Theme 字段、旧 factory、`lightenChannel/darkenChannel` 产品 chrome 残留。
3. 所有正式控件使用 State Layer/semantic tokens，状态矩阵有 Unit 与 Visual 证据。
4. `UIIconButton`、`UIFormField`、`UIDialog` 复用现有行为并通过失败原子性测试。
5. Showcase 使用真实 Desktop Shell，Dark/Light x density x DPI/width matrix 无空白、重叠和裁切。
6. 2D/3D product Theme 迁移完成；EditorApp Compact 迁移按其大功能 gate 闭环。
7. UI、Runtime UI、UI-Render、FreeType（有字体时）、UIA、Showcase/product 受影响门禁全部通过。
8. public headers 无 bgfx、GLFW、FreeType、UIA、HWND 等第三方或平台类型泄漏。
9. fixed-capacity、owner-thread、commit failure、generation/root destruction 与 clean-frame 性能契约无回归。
10. `git diff --check`、DOC-002 与最终 `git status --short` 完成，并报告未覆盖的跨 GPU/人工风险。

## 推荐的下一实施切片

TMD-07 已通过。TMD-08 的 `samples/desktop_shell/` 已提供结构、命令、Menu/Dialog/Tooltip、Splitter、
产品 icon atlas 与响应式档位的结构化证据，专用真实 DPI 视觉门禁也已就绪；真实 100%/150% 配对证据
暂缓。TMD-09 的 2D/3D 与 EditorApp、TMD-10 OS scheme 已通过本轮集中 build/test/smoke；公开头还通过
installed `DesktopBootstrap` consumer gate，验证 262 个安装头、relocation、component isolation 与外部运行。
TMD-11 的 benchmark unit 与冻结 workload 确定性 gate 已通过；固定机绝对时间仍由 `PERF-002` 跟踪，
不阻塞本任务以确定性契约验收。下一实施切片是补齐 Desktop Shell 真实 100%/150% DPI 配对视觉，随后完成
最终文档收口；在此之前父任务保持 InProgress。
