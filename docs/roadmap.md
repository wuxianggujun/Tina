# Roadmap

## M0 基线保护（已完成）

- 删除旧 worktree；
- 以当前 GLFW/miniaudio/vcpkg/Core 修改为主建立独立分支；
- 创建不可丢失的基线提交。

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

## Later

近期 UI：焦点 KeyDown/KeyUp 路由、Button 键盘 pressed 生命周期、方向键/GLFW 标准手柄空间导航和 Modal Focus Scope 已完成首轮；下一步补实体手柄自动化注入、可访问语义和截图回归。Clip/ScrollView、ListView 虚拟化、Style/Theme、DPI 与 Windows IME composition 已完成首轮。

构建：编译/链接门禁已可通过 `TINA_BUILD_SHADERS=OFF` 跳过 shaderc/Tint，正常可运行构建仍默认离线编译 shader；下一步把 cooker/预编译 shader 包做成独立 target。Linux/GCC 现有告警债务包括 EASTL 模板告警、虚函数未使用参数、少量未使用局部变量和 RenderCommand 初始化顺序，后续应先隔离第三方告警，再按模块清理 Tina 自身告警，避免混入功能提交。

后续能力：Cooked Asset、Box2D/PhysX 模块、PBR、阴影、动画、脚本和编辑器。
