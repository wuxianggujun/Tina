# ADR 0055：单一 Runtime 归档与显式 SDK 能力

- 状态：Accepted
- 日期：2026-09-09
- 决策依据：maintainer 要求移除旧模块发布设计，核心统一一个 lib，保留功能、精度和性能，并在全部源码迁移后集中编译测试。

## 背景

`Tina::GameSDK` 原来只是 INTERFACE target，安装包仍公开几十个模块与 adapter archive。
源码职责、编译单元、链接产品和可请求组件被混为一谈，游戏与工具维护重复的模块清单，Debug/Release
还曾向同名安装文件写入不同字节。已有公开头不泄漏第三方类型，但这并不能自动解决发布契约。

## 决定

1. 唯一公开 Runtime 链接目标为 **`Tina::GameSDK`，类型 STATIC_LIBRARY**；产物为 `Tina.lib` / `libTina.a`。
   所有已启用引擎模块与 Tina adapter 实现的对象仅归档一次。
2. `src/` 保留高内聚的实现分组。内部 OBJECT target 管理各自源文件、私有依赖和编译要求；它们不安装、
   不导出，不再发布独立模块静态库。Visual Studio 生成器仍可能在 OBJECT 分组的中间目录生成临时 `.lib`；
   这些不进入 SDK、不成为游戏链接 API，GameSDK 归档输入仍逐个引用对象。源码分层不以发布库数量表达。
3. Editor、host cooker、样例与游戏不是 Runtime 对象，不并入核心归档。现有第三方 runtime archive
   保持私有链接依赖，由 GameSDK 自动传递，不复制到核心归档形成重复打包；第三方 C++ authoring headers 不再随 SDK 发布。
4. `COMPONENTS Desktop` 等只校验**已经编入包的能力**。feature 决定 producer 构建图，不在 consumer
   侧假装从同一物理 archive 中选择不同模块包。Null 包不要求图形依赖；完整 Desktop 包加载自己的完整依赖闭包。
5. 删除旧的公共模块 target 和 `DesktopBootstrap` component，不保留 alias/deprecated/转发层。
   内部源码目标中的 `Tina::Core` 等名称仅是编译依赖，不是安装 API。普通游戏只链接 `Tina::GameSDK`。
6. Release 继续正常优化及精确浮点语义；按函数/数据 section 加链接器未使用代码删除与折叠，保持对象粒度。
   不使用 unity、WHOLE_ARCHIVE、关闭正确性检查或降低 shader 精度来制造体积数字。LTO 不未经测量地强加给 SDK 消费者。
7. 版本进入 **0.1.0**，保持 strict exact-version。安装 archive 按配置分目录；拒绝默认 Debug/Release
   回退。非 Debug 配置可由 consumer 显式声明经过选择的 Release-family mapping，不能跨 Debug CRT。
8. `Core::buildInfo()` 返回实际链接归档的不可变标识；CMake 暴露同源 build-id，安装 manifest 保存
   archive/header 内容哈希。build-id 覆盖源文件、构建输入、编译器与能力配置，不用 HEAD 或时间戳假装识别 dirty 构建。

## 对历史决定的影响

部分替代 ADR 0024 的独立 adapter component 发布及渐进 deprecation 迁移方式；保留 compatibility epoch、
严格版本选择和“不把 fresh consumer 成功写成通用二进制 ABI 承诺”的证据规则。旧 Accepted 理由不改写。
ADR 0041 的 Editor 位于引擎之上继续有效；公开 C++ 模块边界与第三方隔离不因单归档而取消。

## 验收

- producer 每个 runtime 对象只进入核心 archive 一次；installed package 只导出一个第一方 target；旧 target 不存在。
- 现有模块测试、Editor、产品样例与 Grimwold 全部迁到单一链接目标；不靠源码仓库中的旧库补齐符号。
- 用一个极小真实 consumer 检查 build-id，并计量其 executable 与 SDK archive，证明“链接大归档”不等于“全部代码进入 exe”。
- 版本、缺失能力、依赖隔离、配置隔离、公开头与 installed consumer 门禁分别取证。
- 明确区分第一方 archive 大小、第三方依赖、最终 executable 和压缩包大小。未有相同工作负载的测量，不宣称 FPS 提升。

统一构建入口 `tina_validation_artifacts` 只构建，不运行；`tools/validation/run_unified_tests.py` 只运行已编译
GoogleTest，不配置、不构建、不安装。功能代码、消费者、测试源码和文档先集中完成，再执行统一 gate。
