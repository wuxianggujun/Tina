# Web (wasm32-emscripten) 移植现状

记录时间 2026-09-02。本文只记**实际执行验证过**的结论，标注 [已验证] 的都有真实编译/运行/退出码支撑。

## 结论速览

Headless（无渲染、无窗口）的 wasm 构建已经跑通：18 个模块全部编译、whole-archive 强链零未定义符号、真实 API 调用在 Node 下跑出正确结果。带 bgfx 渲染的构建目前卡在一个 upstream 的 flag 问题上（见"当前阻塞"）。

在此之上已经落地：`src/platform/html5/` 平台后端（窗口/键盘/鼠标/滚轮/Pointer Lock/触摸）、`samples/web/` 浏览器入口和 CDP gate，以及 `src/core/diagnostics/Diagnostics.cpp` 里一个真实缺陷的修复（见"顺带修掉的一个真实缺陷"）。帧循环、键盘和触摸已在真实 Chrome 里验证通过。

## 环境

- emsdk 6.0.9 装在 `D:/Programs/emsdk`
- vcpkg triplet `wasm32-emscripten`（community triplet），依赖 mikktspace / xxhash 已成功构建
- toolchain 走 `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` 挂在 vcpkg 之后

## 关键陷阱：preset 的 environment 要写两份

`environment`（`EMSDK` + PATH 含 emsdk/emscripten/python/node/ninja）**必须在 configurePreset 和 buildPreset 里各写一份**。CMake 不把 configure 阶段的 environment 传给 `cmake --build`。

只配 configurePreset 的症状极具误导性：configure 完全成功（emcc 版本探测通过、vcpkg 装完依赖、`build.ninja` 生成），随后每个编译单元都失败于

```
pylauncher: CreateProcess failed (2): "python.exe" -E -X utf8 "...\em++.py" ...
```

因为 `em++.exe` 只是个调 `em++.py` 的 Python launcher，build 阶段 PATH 里没有 python。18 个 target 报 18 条一样的错，而 `grep "error:"` 命中 0 条——不是编译器错误格式，看起来完全不像 PATH 问题。

所以必须用 `cmake --build --preset wasm32-emscripten`；用 `cmake --build <dir>` 会绕过 buildPreset，environment 不生效。改 PATH 时记得改两处。

## 已验证的事实

以下几条和直觉相反，都是真实编译结果：

- **[已验证] 18 个模块全部编过，零错误，含 `tina_network`。** Emscripten 提供 epoll 头，network 不需要为 wasm 排除。运行时能不能真跑 socket 是另一回事（浏览器沙箱），但编译不是障碍。
- **[已验证] 链接不需要 `-pthread`。** whole-archive 强链全部 18 个库，0 个未定义符号，没有 pthread/futex 缺失，也没有 GLFW 缺失。**但这个结论只在链接层成立**：运行时 `Diagnostics` 会构造线程并 abort，详见下面「顺带修掉的一个真实缺陷」。
- **[已验证] `src/core/io/*` 不需要任何头文件修复。** `PathUtilPlatform.hpp` / `FileSystemPlatform.hpp` 在 wasm 下原样编过。
- **[已验证] 真实选项名是 `TINA_BUILD_PLATFORM_GLFW`，默认 OFF**，所以 wasm configure 不需要 GLFW 就能过。
- **[已验证] 产物是真 wasm。** `libtina_core.a` 内的 `Assert.cpp.o` 经 `file` 确认为 `WebAssembly (wasm) binary module version 0x1 (MVP)`。

### 运行时验证方式

`include/tina/core/time/MonotonicClock.hpp` 的 `SteadyMonotonicClock::now()` 是 `libtina_core.a` 里的 out-of-line 虚函数，适合做端到端验证。实测输出 `elapsed=0.006478 advanced=1`，退出码 0。

注意 whole-archive 之后产物只有 10 KB —— wasm-ld 会 DCE。**"链接无未定义符号"不等于运行时可用**，必须另做调用真实 API 的测试。

## 渲染：不用自己写

bgfx upstream 已经备好两条路，`thirdparty/bgfx.cmake` submodule（v1.149.9360-557，干净、跟 `bkaradzic/bgfx.cmake`）里就有：

- WebGL2：`src/glcontext_html5.cpp`，配 `-sMAX_WEBGL_VERSION=2`（`cmake/bgfx/bgfx.cmake:69-71`）
- WebGPU：`src/renderer_webgpu.cpp`，走 Dawn 的 `--use-port=emdawnwebgpu`（`cmake/bgfx/bgfx.cmake:53-67`，upstream commit `ac1d708`）

所以 web 渲染路径不需要在 `src/platform/html5/` 里重写 WebGL，只剩平台层（窗口/输入/主循环）要做。

### 开 bgfx 需要 host shaderc

`TINA_BUILD_RENDER_BGFX=ON` 时 configure 会明确报错：shaderc 在 build host 上跑，交叉编译时 in-tree 的那个无法执行。项目已有现成逃生口：

```
-DTINA_BGFX_SHADERC_EXECUTABLE=<host 构建树里的 shaderc.exe>
```

例如 `out/build/windows-msvc-vnext-bgfx-product-2d/bin/Debug/shaderc.exe`。带上后 configure 通过，`tina_render`（含 bgfx 后端）编译零错误。

## 当前阻塞：bx 把 -msse4.2 传给了 wasm

`thirdparty/bgfx.cmake/cmake/bx/bx.cmake:103-105` 按 `CMAKE_SYSTEM_PROCESSOR` 判 x86 就加 `-msse4.2`（因为 `bx/include/bx/simd_t.h` 无条件 include `smmintrin.h`）。而 Emscripten 的 toolchain 出于历史兼容把 `CMAKE_SYSTEM_PROCESSOR` 设成 `x86`（`Emscripten.cmake:31-37`），守卫误命中，于是 wasm 构建也吃到了 `-msse4.2`：

```
em++: error: passing any of -msse, -msse2, ... -msse4.2, ... flags also
requires passing -msimd128 (or -mrelaxed-simd)!
```

`bgfx` 目标 70 个错误全是这一条，单点修复即可解锁。

因为 submodule 应保持干净（不打本地补丁），修法从 Tina 侧注入 `-msimd128`——Emscripten 官方支持把 SSE 翻译成 wasm SIMD，`smmintrin.h` 在 `-msimd128` 下可用。代价是产物要求浏览器支持 WebAssembly SIMD（现代浏览器均已支持）。

**[已验证] `-msimd128` 解决了这个问题。** 加上 `-DCMAKE_CXX_FLAGS=-msimd128 -DCMAKE_C_FLAGS=-msimd128` 后 `bgfx` 目标 74 步全过、零错误，产出：

| 库 | 大小 |
| --- | --- |
| `libbgfx.a` | 3.0 MB |
| `libbimg.a` | 3.0 MB |
| `libbx.a` | 1.0 MB |

`glcontext_html5.cpp.o`（Emscripten 的 WebGL 上下文）确认编进了 `libbgfx.a`。

注意这个 flag 现在只能靠命令行传，还没固化进 preset —— 因为 headless preset 不需要它。要么给 bgfx 变体单独加一个 preset，要么在 `TINA_BUILD_RENDER_BGFX AND EMSCRIPTEN` 时由 CMake 自动补上（后者更稳，避免调用方漏传）。

## 另一类陷阱：绕过 preset 就丢 environment

同一个 environment 问题有三种踩法，都验证过：

| 命令形式 | 结果 |
| --- | --- |
| `cmake --preset wasm32-emscripten` | 正常 |
| `cmake -B <dir>`（不带 `--preset`） | `The emcc compiler not found in PATH` |
| `cmake --build --preset wasm32-emscripten` | 正常，但只作用于该 buildPreset 绑定的 binaryDir |
| `cmake --build <dir>` | 全部编译单元 pylauncher 失败 |

所以给 `wasm32-emscripten-bgfx` 这类旁支构建树编译时，`--build --preset` 会打错树（`ninja: error: unknown target 'bgfx'`），只能用 `export PATH=...` + 显式目录。

## 平台层：`src/platform/html5/` 已落地

选了自写 HTML5 后端而不是 GLFW 的 emscripten 后端。注册方式跟 Android 一致——`if(EMSCRIPTEN)` 而不是 option，理由和 `src/platform/CMakeLists.txt` 里 Android 那段注释相同：这个适配器只在该目标平台可构建，用 option 会宣传一个不存在的选择。

新增文件（都是纯新增，没有改动既有后端）：

| 文件 | 作用 |
| --- | --- |
| `include/tina/platform/html5/Html5PlatformFactory.hpp` | 工厂 + `Html5PlatformCreateParams`（canvas selector、是否跟随元素尺寸） |
| `src/platform/html5/Html5KeyTranslation.{hpp,cpp}` | DOM `KeyboardEvent.code` → `Key`，108 项有序表 |
| `src/platform/html5/Html5Platform.cpp` | 后端本体 |
| `src/platform/html5/CMakeLists.txt` | `tina_platform_html5` 目标 |

### 主循环不需要改造 `EngineHost`

这一点原先被我当成最大障碍，是错的。`EngineHost` 已经有 `start()` / `tick()`，头文件注释写明是"给不让调用方拥有循环的 host 用的外部驱动替代方案"。浏览器正是这种 host：`emscripten_set_main_loop` 每帧回调一次 `tick()` 即可，不需要 Asyncify，也不用碰 `run()`。

后端本身不注册主循环——那是嵌入它的可执行文件的事，不是后端的事。

### 几个必须这样做的设计点

**事件队列。** 浏览器回调在两次 poll 之间任意时刻触发，而帧必须在一对 `beginFrame`/`finishFrame` 之间组装完，所以回调只能记录、由 `pollFrame()` 重放。队列有上限，溢出时发一条 `InputStreamReset` 并清掉持久按键状态——丢单个事件会让上层状态静默错位，发 reset 是让它重新同步。

**持久状态必须和 transition 同步推进。** `finishFrame()` 跑 7 道交叉校验，其中一条要求我 append 的每个 Down/Up 和最终快照的 `heldKeys`/`heldButtons` 完全一致，另一条要求 `WindowMetricsChangedEvent` 的 revision 精确等于最终 metrics 的 revision。所以 resize 时必须先自增 revision 再发事件。这是 GLFW 后端 2415 行里相当一部分在做的事。

**焦点丢失必须取消全部按键。** 浏览器在 canvas 失焦后不再发 keyup，不取消的话按键永久卡住。

**Pointer Lock 和接口契约有实质冲突。** 桌面上 `setPointerCaptureMode(Locked)` 同步就锁上了；浏览器的 `requestPointerLock()` 只能在用户手势里调、是异步的、而且用户按 Esc 随时能解锁且网页无法阻止。这里的处理是接受 `EMSCRIPTEN_RESULT_DEFERRED`：请求挂起、下次点击时生效，而不是失败——否则 web 上的第一人称游戏根本没法做。调用方不会被误导，因为锁真正生效前指针仍按 Free 模式报 delta，这在指针流里看得见。Esc 解锁时后端把 `requestedCapture_` 退回 `Free`，不再声称自己是 Locked。

接口注释还要求切到 Locked 时丢掉跨越切换的位置差——浏览器捕获光标时会 warp 一次并把它报成移动，不丢的话第一人称相机会在第一帧被甩飞。实现里用 `dropNextPointerDelta_` 精确丢一次。

**滚轮单位。** 浏览器 delta 的单位由 `deltaMode` 决定，只有 `DOM_DELTA_PIXEL` 是长度。统一归一到"格"，并对 Y 取负，以和桌面后端的约定一致。

### 已验证到什么程度

- **[已验证] 编译零错误、零警告**（`-Wall -Wextra` 下），产出 `libtina_platform_html5.a` 2.5 MB。
- **[已验证] 链接零未定义符号**，这证明 5 个纯虚函数全部实现——漏一个会在链接期报 vtable 未定义。
- **[已验证] 工厂的两条拒绝路径正确**：canvas 不存在、selector 为空都返回错误而不是崩溃，在 Node 下实测 `absent_canvas_rejected=1`、`empty_selector_rejected=1`，退出码 0。
- **[已验证] 真浏览器里跑通了帧循环、键盘、鼠标三键、指针移动、滚轮、多指触摸和 Pointer Lock。** 见下面「浏览器验证」一节。

## 浏览器验证（2026-09-02，Chrome headless）

新增 `samples/web/`（`main.cpp` + `shell.html` + `verify_browser.mjs`），用 `emscripten_set_main_loop` 驱动 `EngineHost::tick()`。

**2026-09-02 更新：已从 Null 渲染设备换成 bgfx/WebGL2。** 原先刻意用 Null 是因为画面空白会在"没跑帧"和"跑了帧但没画"之间无法二义；现在 gate 直接读回 canvas 像素，这个二义性由像素判据消掉了，不再需要靠 Null 来规避。

**2026-09-02 再更新：sample 现在画一个真实 sprite，判据从 clear 颜色升级成贴图的四个色块。** 只验 clear 颜色不够——clear 那条路一行 shader 都不跑，所以 essl `300_es` 产物在 WebGL2 上是否正确渲染当时仍只是 [推断]。现在 sample 在 `onEnter` 里用 `Texture2DUploadDesc` 上传一张 2×2 RGBA8 贴图、`setTexture2DBinding` 绑到 key 1，并画一个铺满相机的 sprite；gate 断言四个屏幕象限分别等于四个 texel。

实测结果（`verify_browser.mjs` 退出码 0）：

实测结果（3 次连续运行，所有有效数值完全一致）：

```json
{"status":"ok","gate":"tina_sample_web browser","framesBeforeInput":34,"framesAfterInput":110,
 "keyPressesInjected":5,"keyPressesCounted":5,"tapsInjected":3,"tapsCounted":3,
 "touchStartsObserved":3,"touchStartsConsumedByBackend":3,"synthesizedMouseDowns":0,
 "framesAfterMultiTouch":219,"rightClicksInjected":2,"rightClicksCounted":2,
 "middleClicksInjected":2,"middleClicksCounted":2,"dragPixels":80,"pointerTravelCounted":80,
 "wheelTicksInjected":3,"framesAfterWheel":301,"pointerLock":"locked / requested",
 "canvasFocused":true,"state":"running","failures":[]}
```

bgfx 通路落地、且 sample 开始画贴图 sprite 之后（同日）的实测：

```json
{"status":"ok","gate":"tina_sample_web browser","framesBeforeInput":33,"framesAfterInput":106,
 "keyPressesCounted":5,"tapsCounted":3,"touchStartsConsumedByBackend":3,"synthesizedMouseDowns":0,
 "rightClicksCounted":2,"middleClicksCounted":2,"pointerTravelCounted":80,
 "maxConcurrentFingersSeenByGame":2,"pointerSlotsSeenByGame":"0b11","wheelNotchesCounted":3.6,
 "pointerLock":"locked / requested","spriteTextureUploaded":"uploaded",
 "quadrantsExpected":["top-left=rgb(255,0,0)","top-right=rgb(0,255,0)",
                      "bottom-left=rgb(0,0,255)","bottom-right=rgb(255,255,255)"],
 "quadrantsSampled":["top-left=rgb(255,0,0)","top-right=rgb(0,255,0)",
                     "bottom-left=rgb(0,0,255)","bottom-right=rgb(255,255,255)"],
 "canvasClearWhenBlank":"rgb(16,42,67)","canvasCentreSampled":"rgb(0,255,0)",
 "canvasDistinctColours":32,"state":"running","failures":[]}
```

- **[已验证] 帧循环真的在浏览器里推进**：canvas 尺寸测量、后端创建、`EngineHost::start()`、每帧 `tick()` 全部走通，且 `finishFrame()` 的 7 道交叉校验一帧都没有拒绝（否则 `tick()` 会返回错误、状态会变成 `tick-failed`）。
- **[已验证] DOM 事件到游戏动作的整条链路可用**：经 CDP 注入 5 次真实 keydown/keyup，游戏侧 `ForwardAction` 正好计到 5。这同时验证了 108 项 `code` → `Key` 映射表、事件队列、`pollFrame()` 重放、帧校验和 ActionMapper。
- **[已验证] 触摸真的走触摸路径**：3 次单指 tap，游戏侧 `TapAction` 正好计到 3。单看这个数字不够——浏览器会给"没人处理的 tap"补发兼容鼠标事件，那条路会驱动同一个槽位、同一个计数器。所以 gate 另外断言了两件事：3 个 touchstart 全部被后端 `preventDefault`（`touchStartsConsumedByBackend=3`，即后端 handler 真的跑了并返回 EM_TRUE），且整个过程 `mousedown` 一次都没有触发（`synthesizedMouseDowns=0`）。
- **[已验证] 多指真的到了游戏侧，不只是"帧没被拒"**：两指同时按下、各自移动、先抬一指再抬另一指，全程 `state` 保持 `running`、帧数从 320 推进到 427，且游戏侧从 `frameActions().pointers` 读到 `maxConcurrentFingersSeenByGame=2`、`pointerSlotsSeenByGame="0b11"`。后者是关键：只数 touchstart 次数分不清"两根手指同时按"和"一根手指按了两次"，而并发数只有在两个槽位同一帧都 `present` 时才会是 2。槽位是 0 和 1（不是 1 和 2）也顺带确认了后端没有把槽位 0 留给鼠标。这条同时仍然覆盖 per-slot 记账：某个槽位若在 `heldButtons` 还有位时变成 `present=false`，`setPrimaryWindowSnapshot` 会拒掉整帧、`tick()` 会失败。

- **[已验证] DOM 按键号到引擎枚举的映射是对的**：右键 2 次、中键 2 次，分别计到 2 和 2。这条必须分开绑两个按键才有意义——浏览器把中键编号 1、右键编号 2，而引擎枚举顺序是 `Primary, Secondary, Middle`，所以翻译里存在一次真实的交错；只绑一个按键的话，映射对不对都能过。
- **[已验证] 指针移动的 delta 是真实距离**：横向拖 8 步 × 10px，游戏侧累计 `pointerTravel` 正好 80。这同时说明按帧累加没有跨帧丢事件也没有重复计数（差一帧就不会正好是 80）。
- **[已验证] 滚轮的归一化系数和符号都是对的**：注入 3 次 `deltaY = -120`（CDP 产生的是 `DOM_DELTA_PIXEL`），游戏侧 `frameActions().wheelDeltaY` 累计读到 **+3.60 格**，分布在 3 帧上。这个数字同时钉住两件事：除以 100（少除会读到 360）和对 Y 取负（少取负会读到 -3.60）。容差 0.02 格只覆盖表示误差（1.2 格在二进制里不精确，加上桥接量化到百分之一格），比最小的真实缺陷小约六十倍。
- **[已验证] `setMode(Locked)` 走通且锁真的生效**：headless Chrome 在点击手势之后**授予**了 pointer lock，游戏侧 `pointerCaptureSettings().mode()` 读到 `Locked`。
- **[已验证] essl `300_es` 的顶点和片元程序真的在 WebGL2 上执行了**：sample 上传一张 2×2 RGBA8 贴图（左上红、右上绿、左下蓝、右下白），画一个铺满相机的 sprite，gate 用 CDP `Page.captureScreenshot` 读回合成结果，四个屏幕象限**逐字节等于**四个 texel。象限之间颜色不同，只可能来自顶点级插值出的 UV 加片元级采样——这是这里第一个真正跑过着色器的判据。旁证：`canvasDistinctColours=32`（4 个色块 + 反锯齿边缘），中心点 `rgb(0,255,0)` 而不是 clear 的 `rgb(16,42,67)`。
  截图走合成器而不是 `toDataURL`：bgfx 不设 `preserveDrawingBuffer`，脚本能读到时 drawing buffer 已经被清空了。

  **判据有效性反证过两次，都是改完重跑、其余断言全绿：**
  1. 把 top-left 和 bottom-left 的期望值对调 → 只有这两条失败（`top-left quadrant is rgb(255,0,0), expected rgb(0,0,255)`）。这钉住了 V 方向：上传的第 0 行落在屏幕上方。
  2. 把 `setTexture2DBinding` 改绑到一个没人用的 key（贴图仍然上传成功）→ 四个象限全变成 `rgb(255,255,255)`，即 `BgfxRenderDevice.cpp` 里 1×1 白色 fallback 贴图，同时触发"四个象限只有 1 种颜色"这条断言。这钉住了画面确实取自我上传的那张贴图。

  **贴图每个通道只取 0 或 255 是刻意的。** sRGB 解码在 bgfx 里是采样器 flag，而 backbuffer 不是 sRGB（`kDefaultResetFlags` 不含 `BGFX_RESET_SRGB_BACKBUFFER`），所以中间值会被解码单向偏移一次、没有对应的再编码——期望像素就会取决于 ES 驱动有没有兑现那个 flag。0 和 255 是该传输函数的两个不动点，因此两种情况下期望值都等于 texel 字节，gate 才能断相等而不是断一个区间。容差 2 只覆盖 PNG 往返和合成缩放；最小的真实缺陷是象限取到邻居 texel 的颜色，至少偏 255。

  **采样器必须是 Point。** Linear 会在 texel 边界插值，只有象限中心还是原色，任何断言都会变成"取决于恰好采在哪"。

**[未验证] 仍未在浏览器里跑过的：** Pointer Lock 的**延迟授予**路径（本轮 Chrome 直接授予了，所以 `EMSCRIPTEN_RESULT_DEFERRED` 那条分支没被走到）、用户按 Esc 解锁后后端把 `requestedCapture_` 退回 `Free` 的语义、锁生效瞬间 `dropNextPointerDelta_` 是否真的只丢一次、`DOM_DELTA_LINE` / `DOM_DELTA_PAGE` 两种滚轮单位（CDP 只产生 `DOM_DELTA_PIXEL`，那两条 `switch` 分支没被走到）、HiDPI 下 devicePixelRatio 的实际取值、resize 跟随。这些代码路径都在，但没有被真实事件确认过。

### 两个静默失败的坑

**canvas 必须有 `tabindex`。** 后端把 keydown/keyup 注册在 canvas 而不是 window（为了让宿主页面保留自己的键盘处理），而不带 `tabindex` 的 canvas 永远拿不到焦点、收不到键盘事件，且没有任何报错。验证脚本因此显式断言 `document.activeElement === canvas`。

**`--dump-dom` 不足以验证。** 它抓不到 console，也没法按键；`--virtual-time-budget` 不加速 `requestAnimationFrame`，headless 下 120 秒预算只跑到 3 帧——**帧数少不等于卡死**。要验输入必须走 CDP 注入事件，并关掉后台节流。

**`file://` 下 shell 的 JS 会跑、wasm 不会。** gate 不带 `--allow-file-access-from-files`，所以用 file:// URL 时 HTML 正常加载、页面里的 JS 探针照常计数、canvas 也能拿到焦点，**只有 `.wasm` 的 fetch 被挡**。结果是 `state` 永远停在 `loading`、帧数 0，看起来像引擎启动失败。必须起 HTTP 服务。

**页面里的探针跑在后端 handler 之前。** shell 的 `<script>` 在 `{{{ SCRIPT }}}` 之上，所以 shell 注册的监听器早于模块注册的，同一元素上按注册顺序触发。想读 `event.defaultPrevented` 来确认后端消费了事件，必须放到 `setTimeout(..., 0)` 里等派发结束再读——直接在监听器里读永远是 `false`，而这会让"后端没消费"和"探针读早了"长得一模一样。

**bgfx 渲染路径会撑爆 emscripten 的默认栈。** emscripten 默认 `STACK_SIZE = 64 KiB`（`upstream/emscripten/src/settings.js:113`），而 bgfx 渲染路径第一帧就溢出：`preflightOpaque3D` 按值持有四级 shadow cascade 的矩阵，它的被调者又嵌套若干同样形状的栈帧。桌面目标默认有 1 MB 以上所以从来不会碰到。

表现极具误导性：**和"引擎没启动"完全一样**——`state` 停在 `loading`、帧数 0、HTTP 全 200、页面无 JS 报错。只有 console 里有一行 `Stack overflow detected. You can try increasing -sSTACK_SIZE`，以及一个 `Uncaught` exception，栈顶是 `preflightOpaque3D`。`samples/web/CMakeLists.txt` 现在显式给 `-sSTACK_SIZE=1048576`。

**注意这不是 `samples/web` 独有的。** 任何用 bgfx 的 web frontend 都需要，`tina_add_web_frontend()` 目前不加这个选项。

### 顺带修掉的一个真实缺陷

`EngineHost::Create` 硬编码 `.asyncQueueCapacity = 1024`，于是 `Diagnostics::Create` 必然构造 `std::thread`。Emscripten 不带 `-pthread` 时线程构造失败，而项目用 `-fno-exceptions` 编译——所以 `Diagnostics.cpp` 里那个 `catch (const std::system_error&)` 的优雅降级**是死代码**，实际行为是 abort 整个进程：

```
system_error was thrown in -fno-exceptions mode with error 138 and message "thread constructor failed"
Aborted(native code called abort())
```

表现是 wasm 下载成功（HTTP 200）、页面无 JS 报错、引擎一帧都跑不到，而且只有 console 里能看见。

改法放在拥有该线程的模块里（`src/core/diagnostics/Diagnostics.cpp`），用编译期常量 `kDrainThreadSupported` 判定，不支持时走既有的 `reportSinkFailureOnce` 降级成同步写、`isAsync()` 返回 false。理由和它上面的日志文件失败路径一致：不能异步就同步，比拒绝启动好。桌面侧常量为 true，行为完全不变——[已验证] MSVC 编译无警告，39 个 Diagnostic/Log 测试全过。

注意这也修正了本文档早先一条结论：**"不需要 `-pthread`"只在链接层成立**，运行层不成立。

### 触摸：槽位分配和一个反直觉的约束

`src/platform/html5/Html5TouchSlotTable.{hpp,cpp}` 照 `AndroidTouchSlotTable` 的思路做"手指 id → 密集槽位"映射（`PointerCapacity` 是 8）。几个不这样做就会错的点：

**触摸从槽位 0 开始分配，不给鼠标留 0。** 我最初的设计是反的——鼠标占 0、触摸用 1..7。读 `src/runtime/EngineConfig.cpp:29` 才发现 `PointerButtonBinding` 只接受 primary pointer，非 primary 的绑定会被拒。那么把触摸排除在 0 之外，等于纯触摸设备**一个 pointer-button 动作都触发不了**，而且是静默的。所以改成和 Android 一致：触摸从 0 开始分。不会和鼠标抢，因为触摸回调返回 `EM_TRUE`，浏览器不再补发兼容鼠标事件（gate 里 `synthesizedMouseDowns=0` 就是在盯这条）。

**delta 得自己算。** `EmscriptenTouchPoint` 没有 `movementX/Y`（只有 mouse 事件有），所以槽位表要存上一次的逻辑坐标，每次和它相减；touchstart 那次 delta 记 0。

**`touches[]` 带的是全部手指，不是变化的那些。** `EmscriptenTouchEvent.touches` 里是所有活跃手指（上限 32），要靠 `isChanged` 过滤，否则一次 touchmove 会给没动的手指也发一遍移动。

**`present` 必须在按键状态之后写。** 顺序反了就会出现"槽位已 absent 但 `heldButtons` 还有位"的中间态，那正是 `finishFrame()` 会拒掉整帧的形状。

**两条清理路径都要走。** 失焦（`cancelAllHeldInput`）和队列溢出都调 `releaseAllTouchSlots()`：把所有属于触摸的槽位置为 `present=false` 再清映射表。漏掉溢出那条，手指会永久卡在按下状态。

**槽位耗尽时丢事件，不编。** 第 9 根手指、或者 start 被溢出吞掉之后才来的 move/end，`find()` 返回 `InvalidSlot`，直接 return。给它编一个 Down 比丢掉更糟。

### 计数器回传：从 EM_ASM 变长参数改成堆块

计数器加到 12 个之后，`$0..$N` 这种位置参数已经读不动了，所以样例改成把一个 `int` 数组的地址传过去。两个点值得记：

- **堆读取必须写在 `EM_ASM` 里面**，不能写在 `shell.html` 里。`HEAP32` 能不能从页面作用域看见，取决于当前输出格式（非 MODULARIZE 的经典 script 恰好可以），那是实现细节而不是契约。现在 `EM_ASM` 内部把堆读成一个普通 JS 数组再交给页面。
- **字段顺序是双边契约**：C++ 侧 `ReportField` 和 `shell.html` 里的 `FIELD` 必须一致，加字段只能加在末尾。全整数传递也是有意的——这样 bridge 不依赖 `UTF8ToString` 在 DCE 之后还活着。

### 滚轮和多指：从"后端做了但游戏看不到"到可见

这一节记录一次契约变更。**改之前**：`InputBinding` 的 variant 里没有滚轮，`FrameActionSnapshot` 只暴露动作状态和 **primary** pointer 的 `pointerLookDeltaX/Y`。于是后端归一化到"格"的逻辑没有任何游戏侧消费者，第 2..8 根手指也只有 UI 层（`UIContextPointerInput`）能看见。两者是同一类问题：后端做了工作，runtime 对外没有开口。

**改之后**（`include/tina/runtime/InputActions.hpp`）：

- `FrameActionSnapshot` 增加 `wheelDeltaX/Y`（primary pointer 的本帧滚轮合计）。
- 增加 `std::span<const FramePointerState> pointers` 和 `pointerState(PointerId)`，`FramePointerState` 带 `present` / `logicalX/Y` / `deltaX/Y` / `wheelDeltaX/Y` / `heldButtons`。
- 滚轮**按帧求和而非取最后一个**：一帧内可以有多个滚轮 transition，只留最后一个会把快速滚动悄悄压成一格。

两个设计取舍值得记下来：

1. **`FramePointerState` 是 runtime 自己的类型，不是转发 `Platform::PointerSnapshot`。** 因为 runtime 会在 UI 认领某个 pointer 时把该 pointer 的 delta / wheel 清零，它不能把一个自己改过的 Platform 值当成 Platform 的原值交出去。
2. **认领词汇一个字都没改。** `PointerContinuousControl { Delta, Wheel }` 和 per-pointer 的 `PointerContinuousControlIdentity` **本来就存在**；缺口纯粹在发布侧——`ActionMapper` 当时只处理 primary pointer 的 `Delta`，而且从不累积滚轮。所以这次改动是补上消费端，不是新增机制。

[已验证] `tests/runtime/InputActionTests.cpp` 新增 7 个用例覆盖：滚轮跨多个 transition 求和、滚轮不跨帧残留、被 UI 消费的滚轮不交给游戏、`Wheel` 认领清零滚轮但不动 motion、pointer 表报告非 primary pointer、per-pointer `Delta` 认领只影响被认领的槽位、suppressed 快照的 `pointers` 为空。`tina_tests` 586 项全过（原 579）。

顺带修正一处旧错误：本文档早先一版声称 `finishFrame()` 会交叉校验累积滚轮量，那是错的——`PointerSnapshot` 里从来没有滚轮字段，我把推断当成了已验证事实。

### 仍然存在的一个绑定侧限制

per-pointer 表解决了"读不到第 2..8 根手指"，但**绑定**侧还有一条限制没动：`PointerButtonBinding` 只接受 primary pointer，所以第 2 根手指按下**不能**触发 Action，只能通过 `frameActions().pointers` 自己读。这也正是 HTML5 后端把触摸槽位从 0 开始分配、而不是把 0 留给鼠标的原因——否则纯触摸设备一个 pointer-button Action 都发不出来。要做多指手势玩法，现在可以在 `updateFrame` 里遍历 pointer 表自己判定，不必再让 UI 层参与。

### 还没做的部分

- **IME。** `updateTextInputPlacement()` 当前接受并忽略。契约允许（注释明确说可以当成平台特定的 no-op），代价只是候选窗位置不对；失败会让每个 TextEdit 都坏掉，所以选了忽略。真做需要在 caret 位置盖一个隐藏 input 元素。
- **手柄。** 浏览器有 Gamepad API，但按 memory 里记的，rumble 在这套里本来就做不到。

## 怎么构建这个样例

`TINA_BUILD_EXAMPLES=ON` 在交叉构建上不可用：根 CMakeLists 把 `add_subdirectory(samples)` 和 `add_subdirectory(tools)` 绑在同一个 `if` 里，而 tools 是 build-host 可执行文件——根 CMakeLists 自己关于 Android 的注释已经写明这个耦合。

当前可用的构建方式（样例现在链接 bgfx，见上面的 bgfx 通路一节）：

```bash
cmake -S . -B out/build/wasm32-web-sample -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=D:/Programs/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
  -DCMAKE_CXX_FLAGS=-msimd128 -DCMAKE_C_FLAGS=-msimd128 \
  -DTINA_BUILD_EXAMPLES=ON -DTINA_BUILD_RENDER_BGFX=ON -DTINA_BUILD_SHADERS=OFF \
  -DTINA_BGFX_SHADERC_EXECUTABLE=<host 构建树>/bin/Debug/shaderc.exe \
  -DTINA_RENDER_BGFX_MOBILE_SHADERS=ON
cmake --build out/build/wasm32-web-sample
```

`TINA_RENDER_BGFX_MOBILE_SHADERS=ON` 是 bgfx 通路的前提：它让 `cmake/TinaBgfxEmbeddedShaders.cmake` 追加 `essl|android|300_es` profile。不开的话 embedded shader 表里没有 `_essl` 变体，而 bgfx 在 WebGL 上把 renderer 报成 `OpenGLES`，于是每个 program 创建都失败在"没有烹好的着色器"。

**不带 `--target` 的全量构建是通的**（2026-09-02 起）。之前不通，原因和引擎无关：upstream 的 `cmake/bimg/CMakeLists.txt` 无条件创建 `bimg_encode`，而它的 etcpak 源码无条件包含 x86 intrinsic 头（`__builtin_ia32_*` 在 wasm 上不存在）。这个目标只有 `texturec` 会链接，而 Tina 已经设了 `BGFX_BUILD_TOOLS_TEXTURE=OFF`，所以它是为谁都不服务地在编。`thirdparty/CMakeLists.txt` 现在给 `bimg_decode`/`bimg_encode` 加了 `EXCLUDE_FROM_ALL`。

**顺带**：`ninja` 默认失败即停，所以这个问题一直掩盖着另一个不相关的失败。用 `-- -k 0` 跑完才看全：`tools/bench/UIBenchmarkWorkloads.cpp:1673` 把 `u64` 和 `usize` 直接传给 `std::max`，在 wasm 上 `usize` 是 32 位、两者不再统一因而模板推导失败（桌面上 64 位所以一直编得过）。同一文件其它十几处 high-water 更新都写了 `static_cast<u64>`，只有这一处漏了。查这类问题要先 `-k 0`，否则你只会看见第一个。

`TINA_BGFX_SHADERC_EXECUTABLE` 是必需的：`TINA_BUILD_SHADERS=OFF` 不管用，`src/render` 的 embedded shader 是另一条路径，configure 阶段就明确报错要求这个变量。

**环境里必须有 `EMSDK`（或 `EMSCRIPTEN_ROOT`），仅把 emcc 放进 PATH 不够。** vcpkg 的 `wasm32-emscripten` triplet 用 `find_path(EMSCRIPTEN_ROOT "emcc")` 找工具链，这个调用不会像 `find_program` 那样扫 PATH，找不到就 `FATAL_ERROR: The emcc compiler not found in PATH`。表现很有误导性：ninja 只在 `build.ninja` 变脏、需要 re-generate 时才走 configure，所以**改一次 CMakeLists 之前一直是好的，之后突然全树构不起来**，而且报的是"emcc 找不到"而不是"环境变量缺了"。

**PATH 里还必须有 emsdk 自带的 `python.exe`。** `em++.exe` 只是个 pylauncher，它去 PATH 里找 `python.exe` 再执行 `em++.py`。缺了就每个编译单元都失败在 `pylauncher: CreateProcess failed (2): "python.exe"`——注意这不是 CMake 报错，是**每个 .o 分别失败**，看起来像编译错误而不是环境问题。Windows 的 `py.exe` launcher 不满足要求，名字必须是 `python.exe`。本机路径：`$EMSDK/python/3.13.3_64bit`（版本号会随 emsdk 升级变，用 `$EMSDK/.emscripten` 里的 `PYTHON =` 一行确认，那里也能查到 node 的版本目录）。

跑 gate：起一个 HTTP 服务（`file://` 加载不了 wasm），然后
`node samples/web/verify_browser.mjs <chrome 路径> http://localhost:<port>/tina_sample_web.html 30`。

## 下一步

1. 纯触摸设备开局有个幻影 hover：默认 `WindowInputSnapshot` 把槽位 0 设成 `present=true` 且坐标 (0,0)，第一次真实输入前一直是这个状态。GLFW 后端靠 cursor enter/leave 显式维护 `present`，html5 后端没处理 `mouseenter`/`mouseleave`。这是既有问题，和触摸无关，但在移动端会实际表现出来。
2. 补掉 Pointer Lock 剩下的未验证项：延迟授予路径、Esc 解锁后的状态回退、`dropNextPointerDelta_` 只丢一次。前两条要能在 CDP 里控制 pointer lock 的授予时机，可能得改用非 headless 或换手势顺序。
3. `PointerButtonBinding` 仍只接受 primary pointer，第 2 根手指按下发不出 Action，只能自己读 pointer 表。要不要放开是个 API 决策，不是缺陷。另外 `DOM_DELTA_LINE` / `DOM_DELTA_PAGE` 两条归一化分支 CDP 走不到，需要别的注入手段。
4. [推断] 文件 I/O 与资源：wasm 下同步读文件要靠 MEMFS/预加载或 `--preload-file` 打包，需要和 `tina_assetc` 的产物布局对齐。
5. [推断] 网络：编译能过不代表能用，浏览器里只有 fetch / WebSocket。真要支持得规划一层适配，而不是指望 BSD socket。
