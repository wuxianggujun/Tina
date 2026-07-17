# 高性能自研 UI

> 状态：vNext 目标契约已接受，M7 尚未实现。Tina UI 是游戏内 Retained UI，不是 Immediate UI，也不是桌面编辑器工具包。

## 当前 Legacy 基线

现有实现已经具备 Retained Tree、generation `NodeId`、Capture/Target/Bubble、Pointer Capture、
Focus/Tab/方向导航、Modal Focus Scope、Theme/DPI、Clip、ScrollView、十万行虚拟 ListView、
单行 TextEdit、FreeType 中文 Glyph Atlas、Windows IMM32 composition 和 GLFW 手柄导航。

这些交互语义应迁移，但现有结构不能直接视为 vNext 实现：

- EventSystem 仍承担部分 `UIContext` 所有权；
- UI header、Button icon、TextRenderer 和 UIRenderer 仍直接使用 bgfx 类型；
- layout dirty 传播偏粗，命中与绘制顺序存在分别排序的路径；
- route/layout/text/render 热点仍有动态 Vector、map、string copy 或即时 glyph/upload 行为；
- `Grid/VBox/HBox/MatchParent/WrapContent` 是 Legacy 布局语义，不与新 Flex-lite API 并存；
- 在线平均/自适应批策略会随 frame time 改变行为，不适合作为确定性性能基线。

因此迁移策略是保留经过测试的行为，重建数据和 backend 边界，而不是继续给 Legacy
`UIRenderer` 增加控件。

## 目标数据流

```text
ordered PlatformFrameView transitions
  -> route against previous CommittedUISnapshot
  -> InputTransitionConsumption + ContinuousControlClaims
  -> gameplay Action Mapping
  -> IGameState model/intent update
  -> UI structural command commit
  -> dirty style/measure/arrange/paint update
  -> persistent local PaintCache update
  -> next CommittedUISnapshot
  -> immutable UIDisplayListView
  -> order-preserving UI batching
  -> UI Pass
```

UI 的性能来自增量 dirty、缓存、可观测容量和稳定数据布局，不来自重新实现一套通用 STL，
也不通过跳过正确事件或破坏透明绘制顺序换取虚假的 draw call 数。

## 所有权与清晰公共接口

Runtime 的每个 `WindowRecord` 唯一拥有一个 `UIContext`：

```text
UIContext
  NodeRegistry / generation slots
  Root registrations
  Focus / PointerCapture / Modal scopes
  Dirty queues / active animations
  Text layout references / PaintCache
  Committed paint + hit-test + semantics snapshots
```

Platform/Event 只把 `PlatformFrameView` 的 UI-eligible transitions 交给 UIContext，不拥有节点、Focus、Capture 或 Layout。
`IGameState` 持有 move-only `UIRootOwner` 和非 owning `UINodeId`，不持有 UIContext、Renderer、
UINode 裸指针或跨帧 writer。

最小游戏侧类型：

```cpp
class UINodeId {         // 逻辑字段：owner WindowId + slot index + generation
public:
    [[nodiscard]] bool hasValue() const noexcept;

private:
    WindowId ownerWindow_;
    std::uint32_t index_ = 0;
    std::uint32_t generation_ = 0;
};

class UIRootOwner;       // move-only，销毁时安全注销整棵 root
class UIRootBuilder;     // 仅 GameStateEnter phase，可创建 root/tree/action
class UITreeUpdater;     // 仅 UIUpdate phase，绑定 State 已拥有的 root
struct UISemanticsView;  // 只读 committed snapshot
```

`hasValue()` 只判断句柄是否非空，不声称对应节点仍存活；真正的 owner + generation 校验由绑定
目标窗口 registry 的 `UIContext::contains(UINodeId)` 或 `UITreeUpdater::isAlive(UINodeId)` 完成。
公开 API 不提供一个无法访问 registry 却名为 `isValid()` 的误导性成员。

`UIDisplayListView` 只在 Tina UI/Render SPI 之间传递，普通游戏代码拿不到。所有构建都会先校验
`UINodeId.ownerWindow + generation`，Debug 额外携带 Engine/registry cookie 改善诊断；热点遍历在
一次校验后只使用 UIContext 内部的紧凑 slot index。generation 回绕的 slot 永久 retire。

示例：

```cpp
class SettingsState final : public Tina::IGameState {
public:
    Core::Status onEnter(Tina::GameStateEnterContext& context) override {
        auto& ui = context.primaryWindowUI();
        root_ = TRY(ui.createRoot());
        panel_ = TRY(ui.createPanel(root_.id(), PanelDesc::Settings()));
        volume_ = TRY(ui.createSlider(panel_, SliderDesc{0.0f, 1.0f, 0.01f}));
        apply_ = TRY(ui.createButton(panel_, u8"应用"));

        TRY(ui.setAction(apply_, UIActionKind::Activate, [this] {
            intent_ = SettingsIntent::Apply;
        }));
        return Core::success();
    }

    Core::Status updateUI(Tina::UIUpdateContext& context) override {
        auto ui = TRY(context.uiTree(root_));
        TRY(ui.setSliderValue(volume_, model_.masterVolume));
        return Core::success();
    }

    Core::Status updateFrame(Tina::FrameUpdateContext& context) override {
        if (std::exchange(intent_, SettingsIntent::None) == SettingsIntent::Apply) {
            TRY(context.gameStateCommands().requestPopSelf());
        }
        return Core::success();
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override {
        return {.blocksGameplayInputBelow = true,
                .blocksUIInputBelow = true,
                .blocksFixedUpdateBelow = true,
                .blocksFrameUpdateBelow = true,
                .blocksRenderBelow = false};
    }

    void onExit(Tina::GameStateExitContext&) noexcept override {
        root_.reset();
    }

private:
    Tina::UIRootOwner root_;
    Tina::UINodeId panel_{};
    Tina::UINodeId volume_{};
    Tina::UINodeId apply_{};
    SettingsIntent intent_ = SettingsIntent::None;
    SettingsModel model_;
};
```

UI action 只写 State intent/model；`IGameState::updateFrame()` 再提交 `GameStateCommands`。回调不访问
全局 Runtime，不直接 push/pop 状态，也不捕获 Phase Context。

Action 是节点拥有的 retained property：`setAction` 原子替换同 kind action，`clearAction`、节点
删除或 root 注销负责撤销，不返回容易被临时析构的 RAII token。需要观察 Runtime EventBus 的
非 UI 订阅仍使用独立 RAII token，二者不能混为一套生命周期。

## 节点存储与结构修改

公共契约不暴露具体容器，但首个实现以 `GenerationPool<NodeRecord>` 和 UI tagged persistent
memory 为基线。`NodeRecord` 使用内部 index 连接 parent/first-child/next-sibling，热路径不要求
每节点一个独立 heap object、`shared_ptr` 或 vtable。Style、Text、Action、Semantics 等可选数据
进入按职责组织的池，避免所有节点承担最大 Widget 的内存。

UI tree 只允许主线程修改。路由/layout/paint 遍历期间的 create/reparent/order 变化进入结构
command queue；destroy 会立即把 slot 标为 `PendingDestroy` 并令 generation lookup 失败，当前
route 后才物理回收。这样 Capture 阶段删除目标会停止后续 Target/Bubble，但不会 UAF。

新 root/节点完成结构 commit、layout 和 snapshot commit 后，从下一帧才参与命中。节点不能保存
FrameArena 指针；Action 注册期允许在 UI persistent memory 分配，dispatch/layout/paint 热点不得
产生稳态 heap allocation。

## 坐标、DPI 与可见性

vNext 只保留一套 UI 坐标语义：

- layout、hit-test、Pointer event 全部使用 window-logical coordinate；
- 左上原点、X 向右、Y 向下；
- Platform 输出 logical pointer，UI 不先放大到 framebuffer 再命中；
- content scale 只在 DisplayList extraction 把 logical rect/clip 转 framebuffer pixel，恰好一次；
- 用户 UI scale 通过 Theme typography/spacing/min-hit-size 影响布局，不等同 framebuffer scale；
- glyph raster pixel size 和 scissor 根据 content scale 计算，不能在 shader 再隐式翻转/缩放。

`UIVisibility` 固定为 `Visible`、`Hidden`、`Collapsed`：Hidden 参与布局但不画、不命中；Collapsed
不参与布局、绘制或语义树。Opacity=0 不自动等于 Hidden，Pointer behavior 必须显式配置。

## Dirty、布局和提交快照

```cpp
enum class UIDirty : std::uint16_t {
    None      = 0,
    Style     = 1u << 0,
    Measure   = 1u << 1,
    Arrange   = 1u << 2,
    Transform = 1u << 3,
    Clip      = 1u << 4,
    Paint     = 1u << 5,
    Composite = 1u << 6,
    HitTest   = 1u << 7,
    Order     = 1u << 8,
    Semantics = 1u << 9,
};
```

`UIDirty` 是固定宽度 bit mask；只通过显式 bitwise helper 组合，不能把枚举 ordinal 当索引，
也不能用动态集合保存 dirty 状态。

Mutation 先比较规范化后的旧值；值未变化不置 dirty。传播规则：

- intrinsic text/content 或 Auto 尺寸变化：Measure 向上收敛到首个 layout boundary/root；
- Arrange 只处理 dirty container，父输出 rect 真变化才向受影响子节点传播；
- position：Transform + Composite + HitTest，不触发 Measure 或 local PaintCache 重建；
- scroll/祖先 clip：Transform + Clip + Composite + HitTest，只更新受影响 subtree 的累计变换与
  effective clip，不重建后代 local PaintCache；
- color/hover/pressed：Paint，必要时 Semantics，不触发布局；
- opacity：当前节点和受 inherited opacity 影响的连续子树置 Composite，必要时 Semantics；不重建
  local PaintCache；
- sibling/z/root 变化：父 Order，并重建稳定 paint/hit 顺序；
- Theme revision 只把实际变化 token 映射为 Measure/Paint/Semantics，不无条件重建整树。
- Visible ↔ Hidden：受 effective visibility 影响的连续子树置 Composite + HitTest + Semantics，
  布局结果不变；
- 任意状态 ↔ Collapsed：父链 Measure/Arrange + Order，子树 Composite/HitTest/Semantics；
  不能只修改节点自身。

Local `PaintCache` 只保存节点局部几何与视觉数据；扁平 snapshot 通过 inherited transform/clip/
opacity/effective-visibility revision 更新累计值。祖先 revision 变化时仅扫描对应连续 subtree
range，不把每个后代错误标成 Paint dirty。

每窗口每帧最多一次 Measure/Arrange。布局中产生的外部新 dirty 留到下一帧，不能在 batch 末
清空丢失。`hitTest()`、Widget render、PaintCache/DisplayList build 都不能隐式调用 layout。

`CommittedUISnapshot` 保存：

- 稳定扁平 paint order；
- interactive hit entry：UINodeId、logical rect、effective clip、pointer flags；
- Focus/Modal 可解析状态；
- immutable Semantics snapshot revision。

当前帧全部输入只读取上一份 snapshot；布局完成后原子切换下一份。命中顺序与 paint order
完全一致，不维护第二套临时 z sort。

## Flex-lite v1

首期只实现游戏 UI 实际需要的确定子集：

- `Length { Px, Percent, Auto }`；
- width/height、Min/Max、Margin、Padding、Gap；
- Row/Column、grow；
- Justify：Start/Center/End/SpaceBetween；
- Align：Start/Center/End/Stretch；
- Absolute Overlay + inset；
- `Visible/Hidden/Collapsed`。

不实现 CSS cascade、selector、margin collapse、wrap、完整 Grid 或浏览器兼容算法。Percent 以
containing content box 为基准；Auto 轴的循环 Percent 返回确定诊断并按 Auto fallback，不递归
猜测。所有输入先验证 finite，Min/Max 冲突使用文档化 clamp 规则。

Text/Widget measure cache key 覆盖 available constraint、style revision、content revision 和字体
layout revision。虚拟列表只创建 visible + overscan item；100,000 行不产生100,000个 Node。

## 输入、路由与默认行为

Runtime 在每次状态命令提交后，根据 committed `GameStateStack`、`GameStatePolicy` 和 root
registration 生成每窗口不可变 `UIInputScopeSnapshot`。它按视觉层级列出 eligible roots，并在
首个 `blocksUIInputBelow` 处截断；UIContext 针对这一个集合做一次全局 hit-test 和一次路由，
不是对每个 State 依次 hit-test。这样“栈顶到栈底传播”表示 eligibility 计算顺序，不是重复分发。

State/root 变为 ineligible 时，在下一份 input snapshot 发布前完成以下事务：有效 Pointer Capture
先收到一次 `PointerCancel` 再释放；Focus 记录为该 root 的弱 `UINodeId` history，并转移到最上层 eligible
root 的默认焦点；Modal scope 不允许跨 root。重新变为 eligible 时，仅在 history 仍通过 owner +
generation 校验且未被新 modal 覆盖时恢复焦点。状态命令在路由结束后提交，因此当前 transition
始终使用同一份 scope snapshot。

- `PlatformFrameView` 保留 ordered transitions；每个需要 target 的 Pointer transition最多一次 hit-test；
- 已有 Pointer Capture 的 transition 直接解析 captured UINodeId，通常0次 hit-test；
- hit-test 逆 paint order 扫描 committed hit entries，以 bounds/clip 提前剪枝；首期不上 BVH，
  先记录 visited count，只有 profiling 证明需要才加分块索引；
- route path 使用预配置 fixed-capacity scratch，超过最大树深返回 `CapacityExceeded`，不 heap fallback；
- 每个路由阶段重新解析 UINodeId owner + generation；listener 按稳定注册顺序；
- Capture -> Target -> Bubble 后执行可被 `preventDefault()` 取消的 Widget default action；
- route 中新增 listener/child 从下一 transition/snapshot 生效；删除立即 tombstone、route 后回收；
- Pointer Capture 按 pointerId 保存。首期 Mouse 使用 pointerId=0，不把预留字段宣称为触摸支持；
- Focus、Modal、Keyboard、Gamepad Accept/BackNavigation 复用同一 default action 生命周期。

UI 返回只属于当前 Platform frame 的 `InputTransitionConsumption`，以及当帧
`ContinuousControlClaims`。Gameplay Action Mapping 只读取未消费 transition 与未 claim control；
被 UI 消费的 digital Down 由 Action Mapper 跨帧抑制到真实 release，axis 抑制到 neutral。
Focus lost、断连和 `InputStreamReset` 产生 `InputCancelTransition`，不伪造可激活 Button 的普通 Up。UI routed
listener 不复用 Runtime EventBus；二者的生命周期、顺序和 payload 不同。

## PaintCache、DisplayList 与批处理

每个可绘制节点持久保存 backend-neutral local `PaintCache`。只有 Paint dirty 才重建 Quad/Image/
TextRun 数据；Order、layout 或 clip 改变更新 committed paint entries。无变化帧仍需遍历 visible
paint entries 组装当前 `UIDisplayListView`，但不重复 style、layout、text shaping 或 local paint。

```text
UIDisplayList
  ClipRect[]        已求交并确定性 intern 的 framebuffer scissor，ClipId 0 表示无 clip
  DrawCommand[]     Quad / ImageQuad / GlyphRange，保持 paint order
  GlyphInstance[]   本帧 FrameArena 数据
```

Draw command 只含 framebuffer geometry、UV、premultiplied color、ClipId 和 packet-local
`FrameResourceRef`。DisplayList extraction 把所用 Texture/Sampler/Atlas generation pin 登记到
Render SPI 的 `FramePinSink`，由 Runtime-private owning `RenderFramePacket` 保活；UI 不访问 packet
类型。禁止 Widget/UINode 指针、AssetHandle、bgfx handle、ViewId、state flag、uniform location
或 backend vertex declaration。

Renderer 只合并相邻、兼容命令：

```text
UI pipeline kind + Texture/Atlas page + Sampler + Blend + ClipId
```

DisplayList extraction 以规范化整数 framebuffer scissor 值做确定性 interning；相同 effective
clip 必须得到同一 `ClipId`。因此 batch key 比较 `ClipId` 等价于比较 clip 值，不会因节点来源
不同而错误拆批。

批处理不能跨透明 paint order 全局排序。batch 前后 paint checksum 必须一致；空 clip 直接剪枝。
首期只支持 axis-aligned scissor，rounded/stencil clip 后置。Frame capacity 超限返回 sticky
`CapacityExceeded`，不提交半份 DisplayList，也不回退 heap。

UI 内建 pipeline 由 Render module 持有；游戏 Widget 不能选择 shader/pipeline。bgfx 只存在于
`tina_render_bgfx` 私有实现，详细门禁见[渲染架构](rendering.md)。

## UTF-8、中文与 Glyph Atlas

Runtime 不按路径打开字体。Cooked `FontAsset` 提供 owning bytes、face metadata 和确定 fallback
chain；UI 持有 `AssetLease<FontData>`。FreeType 类型只存在于 `tina_ui_freetype`。

缓存键至少覆盖：

```text
TextLayoutKey = Font AssetId+generation / fallback revision / logical size /
                text revision / wrap constraint / locale+layout mode
GlyphKey      = face identity / glyph index / raster pixel size / render mode
GlyphHandle   = atlas page / slot / generation
```

Text measure/layout 与 glyph raster 分离：主线程先得到 glyph index、advance、kerning 和 line
metrics；Worker 只把 owning font bytes/lease + generation request raster 成 CPU bitmap。Glyph 未
上传时绘制确定 fallback box，但沿用目标 advance。因此 raster/upload completion 只标记 Paint
dirty，不重新 Measure；只有 fallback face 或 layout metrics 真变化才触发布局，避免字体到达时
命中区域抖动。

Atlas 使用固定 page size/page count/byte budget，首期优先 R8 coverage。raster completion 和
GPU upload 都是有界队列；满容量不能覆盖 in-flight UV。Page eviction/reset 只在 Deferred
Cleanup 且 Render ticket 退役后发生，generation 随之递增。迟到任务重新校验 Font/Atlas
generation。

首屏中文和 fallback chain 在 root 开放输入前预热。非法 UTF-8 转 U+FFFD 并产生结构化诊断。
首期明确支持 Latin/CJK codepoint、kerning 和显式换行；不宣称 Arabic/Indic、emoji sequence、
combining cluster 的完整 shaping。出现真实需求后再评估 HarfBuzz，不手写复杂 shaping。

## Theme、动画与可访问性

Theme 是每窗口值对象，至少包含 Color、Typography、Spacing、Radius、Border、FocusRing、
MinimumHitSize、AnimationDuration 和 ReducedMotion token。Widget 保存 token id + 局部 override，
不保存指向全局可变 Theme 的裸指针。Theme/DPI revision 只 dirty 实际受影响属性。

`UIAnimator` 维护 active-node list，只 sample 活跃 tween、caret 和 scroll，不递归 update 全树。
动画使用 unscaled real delta：

- opacity -> Composite；color -> Paint；
- translation -> Transform + Composite + HitTest；
- scroll -> Transform + Clip + Composite + HitTest；
- size -> Measure + Arrange，允许但标记昂贵，默认控件不用；
- ReducedMotion 把非必要 duration 置0；完成 action 在阶段末提交并支持目标自销毁。

所有交互 Widget 从第一版生成 `Semantics`：Role、UTF-8 Name/Description、Value/Range、Checked、
Enabled、Focused、Actions 和可选 `labelledBy UINodeId`。装饰节点过滤；Hidden/Collapsed 不进入
语义树，Disabled 保留语义但 Action 不可执行。平台 UIA/AT-SPI adapter 只读 immutable snapshot，
不能跨线程访问 UINode。密码和 composition 正文不进入诊断。

首批 Role：Group、Label、Button、Checkbox、Slider、TextEdit、Dialog、List、ListItem。

## Widget 范围

迁移保留 Panel、Label、Button、Image、ProgressBar、Dialog、ScrollView、虚拟 ListView 和单行
TextEdit。完成后增加设置页真实需要的 Checkbox、Slider；Dropdown、TreeView、多行 TextEdit、
复杂 shaping 和触摸手势按真实产品需求增加。

Checkbox：Pointer/Space/Enter/Gamepad Accept 走同一 default action，值实际变化才通知；disabled、
preventDefault 或 Modal 拦截时不改变。Slider：finite min/max/value/step、clamp、以 min 为原点的
稳定量化；Pointer capture、方向键、Home/End 和 Gamepad 共用语义，单帧只提交最终 change。

不建设反射式 Data Binding。`SettingsState` 持有明确 `SettingsModel`，Action 调用窄 Window/
Audio settings command；backend 失败恢复 model 并显示 UTF-8 错误。

## 线程与内存

- UIContext、Node registry、route、layout、PaintCache、snapshot 和 DisplayList build 都在主线程；
- Worker 只执行字体/图片 CPU 工作，不持有 UINode/UIContext/FrameArena 指针；
- completion 在主线程重新校验 owner + generation 后发布；
- GPU staging 跨帧使用独立 owner；DisplayList 的资源/Atlas pin 通过 FramePinSink 转移并由
  Runtime-private RenderFramePacket 保活，不借用
  Worker scratch/UI FrameArena；
- retained node/style/text/action/PaintCache 属 UI persistent tag；
- layout scratch、route scratch、DisplayList 使用彼此独立且有上限的 arena/capacity；
- Arena 满返回结构化错误，不 heap fallback。

首期不并行 layout/route/DisplayList。只有 Tracy/tina_bench 证明单线程热点超过预算且可以保持
稳定顺序时，才讨论 immutable subtree 并行；不为了“看起来高性能”先引入锁。

## Metrics 与性能门禁

常驻 Metrics 至少记录：

```text
ui.nodes.total / visible / interactive
ui.dirty.measure / arrange / paint / order / semantics
ui.layout.pass_count / node_count
ui.input.transition_count / hit_test_count / hit_entries_visited / max_route_depth
ui.paint_cache.rebuild_count
ui.display.command / quad / glyph / clip / bytes
ui.batch.count / draw_count / texture_switch / clip_switch
ui.glyph.cache_hit / miss / raster_queued / upload_bytes / pages / evictions / failures
ui.memory.persistent / frame / atlas current+peak bytes
ui.time.route / layout / paint_cache / display_build / submit ns
```

p50/p95/p99 由 `tina_bench` 的预分配 sample buffer 离线计算，不在常驻 Registry 在线维护。

确定性硬门禁：

- 无变化 UI：Style resolve=0、Measure/Arrange=0、PaintCache rebuild=0、Tina heap allocation delta=0；
- 每窗口每帧 Layout pass <= 1；每个 Pointer transition hit-test <= 1；
- route/layout/DisplayList 无 heap fallback；
- dirty leaf 不无条件重排不相关 subtree；
- adjacent-compatible batching 数等于 compatibility runs + capacity split，paint checksum 不变；
- 5,000 logical node 和100,000行虚拟列表只处理 dirty/visible+overscan 集合；
- 无变化 UI、单 Label 更新、滚动列表、中文文本、Modal 和设置页分别记录 p50/p95/p99；
- 绝对毫秒预算等固定门禁机建立后再阻断，零分配、容量、顺序和资源归零立即阻断。

## 测试与实施顺序

必须直接 GoogleTest 覆盖：

- stale/cross-window UINodeId、所有构建的 owner 校验、generation 回绕 retire、Root owner rollback、route 自销毁；
- 每类 setter 的 dirty bit/传播、布局中新增 dirty 不丢、无变化0布局；
- Flex Px/Percent/Auto/MinMax/absolute、非有限输入和循环 Percent 诊断；
- ordered Down/Up/Wheel、capture/失焦、paint-hit 顺序、clip 和 consumption；
- UTF-8/fallback metrics 与 raster 分离、Atlas 满/退役、迟到 Worker；
- DisplayList 头文件无 backend 依赖、clip 求交、空剪枝、顺序保持 batch、capacity failure；
- active animation/reduced motion、Semantics 稳定顺序和敏感文本脱敏；
- 100%/150%/200% DPI 中文截图，Checkbox/Slider/Modal/TextEdit 和实体手柄人工矩阵；
- NullRenderDevice 连续300帧后 UI persistent/frame/atlas/resource count 归零。

实施顺序固定为：

1. 建立 vNext UIContext/UINodeId/root owner/committed snapshot；
2. 先输出后端无关 DisplayList，并让 Legacy/new Widget 不再 include bgfx；
3. 完成 dirty/Flex-lite/PaintCache/text layout + glyph atlas；
4. 迁移 Focus/Capture/Modal/Scroll/List/TextEdit/IME/手柄语义；
5. 增加 Checkbox/Slider、Semantics、动画与截图门禁；
6. 性能基准证明需要后再扩展布局、空间索引、shaping 或并行。

所以“完善 UI”首先是完成正确的数据和 backend 边界，然后才是增加 Widget 数量。
