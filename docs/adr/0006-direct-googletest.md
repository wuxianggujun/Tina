# ADR 0006：直接运行 GoogleTest，不使用 CTest 调度

- 状态：Accepted
- 日期：2026-07-16

## 背景

GoogleTest 是测试框架，CTest 是额外的测试发现与调度层。Tina 当前门禁需要可复现地直接
运行一个测试进程、读取退出码和 GoogleTest 报告，没有必须依赖 CTest 的多项目调度需求。

## 决定

固定 GoogleTest 1.17.0，构建 `tina_tests` 并在本地和 CI 中直接执行。筛选、重复、shuffle、
XML 输出和失败退出码使用 GoogleTest 自身参数；性能回归使用独立 `tina_bench`，不伪装成
单元测试。CMake 只负责生成测试 target，不调用 `gtest_discover_tests` 或 CTest。

## 结果

- 本地与 CI 执行路径一致，失败就是测试进程的非零退出码；
- CI 脚本必须显式保存日志、XML 和超时信息；
- 测试分片由 CI 传递 GoogleTest filter，不依赖 CTest 标签；
- 将来只有出现明确的跨项目调度需求，才通过新 ADR 重评。

## 被拒绝方案

- 同时维护 CTest 和直接运行两条门禁：结果、参数和超时语义容易分叉；
- 把 benchmark 放进 GoogleTest：噪声、统计和产物协议不适合单元测试框架。
