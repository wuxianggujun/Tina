# ADR 0047：内置 nlohmann/json 作为 Tina 唯一通用 JSON 后端

- 状态：Accepted
- 日期：2026-09-05
- 取代：[ADR 0038](0038-json-writer-without-json-library.md) 中“不引入 JSON 库”的实现选择；保留其迁移背景，不再保留手写 writer 实现

## 背景

ADR 0038 只覆盖样例、编辑器和 bench 的诊断 JSON 写出。当游戏运行时、配置系统、网络消息或工具需要读取 JSON
时，仓库只有手写 API，没有统一解析器，调用方不得不自行接入第三方库或重复实现解析逻辑。这已经成为真实的
引擎能力缺口。

## 决定

1. Tina 将 `nlohmann/json` v3.11.3 单头文件固定 vendored 到 `thirdparty/nlohmann/json.hpp`，不依赖 vcpkg、网络
   下载或消费者环境的 JSON package。
2. `Tina::Core::JsonDocument` 是游戏和工具侧唯一通用 JSON 解析入口。它使用 nlohmann 解析器构建 Tina-owned DOM，
   对外只暴露 `JsonDocument`、`JsonValue`、`JsonValueKind`、`JsonNumberKind` 和 `JsonParseOptions`。
3. nlohmann 类型、宏、异常和 include path 只能出现在 `src/core/text/JsonDocument.cpp`。公开头和安装后的 SDK 不暴露
   `nlohmann::json`，静态库消费者不需要额外依赖。
4. 所有解析错误、类型错误、成员缺失、数组越界和资源限制均转换为现有 `Core::Result`/`Core::Error`。异常不得穿过
   Tina API 边界。
5. 默认限制输入 16 MiB、嵌套深度 128、DOM 节点 1,000,000；不可信文本必须显式收紧 `JsonParseOptions`。
6. `JsonWriter` 同样改为使用 `nlohmann::ordered_json` 构造和序列化。它继续保持 Tina-owned API、插入顺序、紧凑
   单行输出和 `noexcept` 错误边界；字符串转义、浮点格式和 `rawMember` 校验统一由 nlohmann 的 `dump()`/parser
   负责。写入失败或非法 raw value 将 writer 置为 failed，不能继续产出部分 JSON。

## 结果

- 游戏代码拥有统一、可测试、无第三方 ABI 泄漏的 JSON 读取和写入能力。
- nlohmann 只有一份实现，RapidJSON 不再作为第二套候选接入，避免同一仓库出现两套 JSON DOM/serializer。
- 解析 DOM 与 JsonWriter 构造都会产生受控分配；高频帧路径仍不应每帧解析/序列化 JSON，应在加载、消息或
  报告边界完成并缓存结果。崩溃处理器仍使用自己的 `WriteFile` trailer，不在堆损坏后调用 JsonWriter。
- nlohmann 版本、许可证和文件清单记录在 `thirdparty/nlohmann/NOTICE.json`，升级需单独修改 ADR/NOTICE 并复核限制与错误映射。
