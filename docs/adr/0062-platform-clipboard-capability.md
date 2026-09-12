# ADR 0062：平台剪贴板能力与文本编辑剪贴板路由

- 状态：Accepted
- 日期：2026-09-12
- 接受依据：用户要求补齐 UI 库缺口并不保留旧兼容路径。

## 背景

引擎此前没有任何剪贴板能力。两处缺口是自证的：

- Editor 的 `Copy AssetId` / `Copy Source Path` 命令已存在但显式禁用，文案写着
  "no platform clipboard adapter is registered"；
- TextEdit 已支持 `Ctrl+A` 全选、Shift 选区与 IME，但选中之后**无法复制**。用户会当成 bug，
  而不是当成未实现的功能。

约束来自 GLFW 与 Win32 的实际行为：`glfwGetClipboardString` 在四种完全不同的情况下都返回
NULL —— 剪贴板为空、内容不是文本、Win32 全局剪贴板锁被别的进程持有、GLFW 未初始化。只有
native error code 能区分前两者（可恢复的"没有文本"）和后两者（真正的失败）。它还只能在主线程调用。

另一个硬约束：`routeTextInput` 拒绝任何 `\r`。Windows 剪贴板用 CRLF，所以不做行尾归一化的话，
从记事本复制两行文本再粘进 Tina 会**整条失败**。

## 决定

1. `IClipboard` 是一个窄能力接口，只有 `readTextUtf8(std::span<char>)` 与
   `writeTextUtf8(std::string_view)`。读写一律是 strict UTF-8 + LF；行尾转换属于平台层，
   因为原生行尾是平台属性，不是调用方的选择。
2. `IPlatformBackend::clipboard()` 返回 `IClipboard*`，**纯虚、无默认实现**。`nullptr` 是能力缺失的
   陈述，而不是每次调用都失败：空指针是宿主的永久属性，调用方在接线时判一次即可。
   返回对象由 backend 拥有、继承其 owner thread、地址在 backend 整个活动期内稳定
   （`EngineModules` 用 `unique_ptr` 持有，move 不改地址），因此可以缓存到 shutdown。
3. 读结果是三个字段 `{bytesWritten, totalBytes, hasText}`，用来补回 GLFW 的 NULL 丢掉的信息。
   `hasText=false` 表示**完全没有文本**，与「持有一个空字符串」是两种不同状态；空 destination
   是长度查询；截断永不切开 UTF-8 序列，也不切开 CRLF 对。
4. `UITextClipboardCommand{Copy,Cut,Paste}` 是独立 enum，**不并入** `UITextEditCommand`。
   后者每一条都是 text+selection 的纯函数；剪贴板命令额外需要一个 `IClipboard`。分开之后
   `routeTextClipboardCommand` 的签名可以要求剪贴板，「忘记传」就是编译错误而不是静默失效。
5. Copy/Cut **先写后删**。写入被拒时选区保持原样。空选区上的 Copy/Cut 返回
   `consumed=true, applied=false` 且不清空剪贴板。
6. 单行 TextEdit 的 Paste 在首个 `'\n'` 处截断并置 `truncatedToFirstLine=true`。
7. `UIInputRouteProducer::produce()` 的剪贴板参数是**必填**的第三个形参，不给默认值；
   识别到的剪贴板组合键**无论平台有没有剪贴板都被认领**。
8. `ProcessLocalClipboard` 是公开的进程内实现，Headless 返回它，测试与无桌面宿主的 embedder 共用。
9. `GameStateEnterContext::clipboard()` 转发同一个指针，生命周期规则与 `renderDevice()` 相同。
   Editor 在 `onEnter` 记录这个 borrow，其余非相位 helper 直接用。

## 边界与代价

- 剪贴板不做监听/变更通知、富文本、图片、文件列表，也没有自定义格式。跨实例粘贴场景节点
  （editor-feature-plan 的 E5）仍是独立切片。
- Html5/Android/iOS 返回 `nullptr`：浏览器剪贴板是异步 + 权限门控 + 需要 transient activation，
  与这个同步接口形状不兼容；Android 需要一对 JNI 方法与匹配注册计数；iOS 需要 ObjC 宿主。
  三者都是独立 slice，没有伪造。
- 每次 Paste 会做一次 probe + 一次 read 两次系统调用，并按 `read->bytesWritten` 收缩缓冲
  —— 两次调用之间剪贴板可能被别的进程改写，honour 实际落地的字节而不是先前的长度。
- `produce()` 加必填形参使 147 处测试调用点全部需要显式传参。这是刻意的：给默认值会让新增
  调用点静默漏掉剪贴板，表现为「粘贴莫名失效」。
- Editor 的 `Locate Source` 仍显式禁用，本 ADR 不含 shell reveal adapter。

## 验证

编译期：`ClipboardTextNormalization.hpp` 的截断与行尾规则全部是 `constexpr static_assert`，
包含两个反例 —— `"A€B"` 截到 4 字节必须保留完整的 `€`（写坏成"末字节是 continuation 就后退"
会只剩 `"A"`），`"AB€"` 截到 4 字节必须丢掉不完整的 `€`。header isolation TU 钉住
`clipboard()` 的返回类型与 `noexcept`。

运行期 GoogleTest（`tina_tests` / `tina_ui_tests` / `tina_runtime_ui_tests`）：
`ProcessLocalClipboard` 的 never-written vs 空字符串、LF 归一化、非法 UTF-8 拒绝、截断读回
所需长度后重试成功；UI 层的 Copy/Cut/空选区不清空/写入被拒时选区不动/Paste 替换选区/
单行截断/多行保留/CRLF→LF/无焦点不消费；Runtime 层的 Ctrl+C/X/V、legacy
Ctrl+Insert/Shift+Insert/Shift+Delete、无剪贴板时 Shift+Delete 不退化成 Delete。

GLFW 的四种 NULL 分支需要真实 Win32 锁竞争才能全覆盖，无窗口测试不能替代；该部分只有
代码路径与 native code 映射，没有实机证据。

## 被拒绝方案

- **每次调用返回失败而不是 `nullptr` 能力查询**：会让每个粘贴点都长出一条错误处理分支，
  而缺失是永久的、判一次就够。
- **把 Copy/Cut/Paste 塞进 `UITextEditCommand`**：那个 enum 的实现签名不带剪贴板，
  塞进去只能让剪贴板命令在缺参数时静默变成 no-op。
- **`produce()` 的剪贴板参数给默认 `nullptr`**：新增调用点会静默漏传。
- **Cut 先删后写**：Win32 锁竞争下用户会同时失去选区和剪贴板内容。
- **单行 Paste 整条拒绝或把多行拼接**：前者丢掉用户明确要求的数据，后者伪造剪贴板上不存在的文本。
- **让每个测试自带 clipboard fake**：被测行为会变成每处都不同的实现；
  `ProcessLocalClipboard` 公开出来就是为了避免这一点。
