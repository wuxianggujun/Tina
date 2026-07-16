# Roadmap

## M0 可构建基线（已完成）

- 固定 GLFW、miniaudio、vcpkg、bgfx 与 Core 基线；
- 建立 Windows/Linux Preset、独立 GoogleTest 可执行文件和可回滚提交；
- 明确不使用 SDL/SDL3 和 CTest 调度。

## M1 构建与生命周期（已完成首轮）

- GoogleTest 独立测试程序迁移（已完成，直接运行 `tina_tests`）；
- 修复 Application 初始化、Frame Phase、Event/Resource pump 和 shutdown；
- 通过 2D/3D 退出冒烟验证资源释放；自动化资源计数仍在后续测试门禁中。

## M2 2D + 自研 UI（已完成首轮）

- 统一每个 Scene 的 UI 树所有权，并由 EventSystem 对当前 roots 做单次输入路由；
- 修复布局注册、显式事件上下文注入、hit-test 隐式布局和单次输入路由；
- 引入 generation NodeId，统一 Pointer Capture、Focus/Tab 和节点销毁安全失效；
- 引入每窗口 UIContext、Dark/Light/Custom Theme、DPI/content scale 和用户缩放，修正高 DPI 命中与场景布局；
- 增加嵌套 Clip、通用 ScrollView、ListView 可见行虚拟化和 Windows 原生 IME composition；
- 增加焦点 KeyDown/KeyUp routed event、可取消默认行为、Button Enter/Space pressed 生命周期、方向键空间导航和焦点视觉；
- 增加可嵌套 Modal Focus Scope、generation 失效与焦点恢复，移除 UIDialog 的全局键盘旁路，并修正 Scene roots 在 `onEnter`/`onResume` 前的激活时机；
- 增加 GLFW 标准手柄状态、摇杆回滞、方向长按重复和设备无关 Accept/Cancel 导航，且语义导航服从 Modal Focus Scope；
- 增加 UI 布局、唯一命中、路由顺序、动态子树上下文和交互生命周期 GoogleTest；
- 增加 `--smoke-ui`，实际运行虚拟列表、对话框、中文和已聚焦 TextEdit。

## M3 最小 3D（已完成）

- 增加 Perspective Camera、深度缓冲和静态 Mesh；
- 建立独立 `--smoke-3d` 运行入口，不影响现有 2D 游戏；
- 验证退出时 GPU 资源全部释放。

## M4 UI 可靠性（已完成首轮，暂停横向扩展）

- 完成 Button action 的独立重入保护、异常恢复和回调销毁目标安全门禁；
- Retained Tree、generation NodeId、路由、Focus/Capture、Modal、Theme/DPI、Scroll/List、
  TextEdit/IME 与 GLFW 手柄导航已有可运行基线；
- Checkbox、Slider、可注入手柄轮询测试、可访问语义和截图回归移到 M7，不继续用控件
  数量挤占 Runtime/Render/Asset 生命周期工作。

## M5 Carbon 取证与 Runtime/Scene 边界（当前）

- 官方 Carbon 对应模块已下载到被忽略的 `temp/carbon-engine`，并记录 URL、精确提交和
  研究用途；参考源码不进入 Tina 依赖或提交；
- 已完成 Runtime、Window/Input/UI、Render、Asset/Cooker、Scheduler、Simulation 和
  Audio 的采纳/拒绝矩阵，明确不复制 Blue/Python、全局对象、原始指针回调和历史 API；

- 为 Application 初始化阶段增加依赖注入 seam，补所有失败点的逆序回滚和析构顺序测试；
- 补 Scene 延迟 push/pop/replace、暂停/恢复与 UI roots 激活门禁；
- 以 generation `EntityId` 和明确的 World command/query 逐步替代 GameScene 对 EnTT registry 的直接访问；
- 为 fixed phase 增加 World mutation barrier/deferred command buffer 与 interpolation snapshot 门禁；
- 将 World 的具体输入、TileMap 和 bgfx 渲染依赖拆到适配或 extraction 边界；
- 明确服务所有权和关闭顺序，再按测试保护逐步提取 Engine Context；
- 不进行一次性 `EngineHost` 大重写。

## M6 Render 与 Asset 边界

- 统一 Render Pass/View 所有权、clear/load/store 规则、命名/统计、失败停止和 GPU 资源计数；
- 增加 typed generation handle、NullRenderDevice 和小型帧内 Pass Scheduler，不引入
  Carbon 的动态 Step 类型体系或自研多后端 RHI；
- 把异步资源拆成后台 CPU Decode 与主线程/GPU Upload 两个队列，并按任务数、字节数、
  时间三种预算；
- 把 shader/cooker 做成独立构建目标；
- 在 GPU 上传阶段稳定后实现 AssetId、schema、依赖清单、内容 Hash、增量 Cook 和最小
  静态 glTF；Cooker 必须先验证生成产物再原子写盘。

## M7 产品 UI 与设置接入

- 增加 Checkbox、Slider，将设置页的主音量、音乐、音效和全屏从占位按钮接入真实后端；
- 为 GLFW 手柄轮询、回滞和长按重复提供可注入测试，不用实体硬件替代自动化；
- 将 UI 绘制收敛为后端无关 Display List，再建立基础可访问语义和稳定截图回归；
- Dropdown、TreeView、多行文本和复杂 shaping 只按真实场景需求增加。

## 后续能力

- 现有 Box2D `Physics2D` 收敛为唯一 2D 后端并接入任务系统；Jolt 作为唯一 3D 后端在真实玩法出现后接入；PhysX、Bullet、Rapier 不进入依赖或构建；不统一 2D/3D Physics API；
- PBR、阴影、动画、脚本和编辑器均等待 Runtime、Render、Asset 基础契约稳定；
- Linux/GCC 告警先区分第三方与 Tina 自身，再按模块清理；Clang ASan/UBSan 需要可复现 preset 和实际门禁结果。
