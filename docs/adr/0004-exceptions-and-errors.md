# ADR 0004：保持 C++ exception，模块边界转 Result/Status

- 状态：Accepted
- 日期：2026-07-16
- 接受日期：2026-07-17

## 背景

标准库、`std::pmr` 和部分第三方依赖可能抛异常。全项目关闭异常会迫使维护补丁或留下终止
路径；让异常任意穿过主循环、Worker 或 C callback 又会破坏生命周期和错误上下文。

## 决定

所有 Tina target 保持 C++ exception 能力。公共模块 API 使用 `Result/Status`；热点正常路径
不 throw。Engine create/run、`IGameApplication`/`IGameState`、Worker、Platform/Audio C callback 和 Cooker
命令入口捕获异常、追加上下文并转换。析构、rollback、`onExit`、`onShutdown` 和实时 callback
必须 `noexcept`。

## 结果

- 兼容标准库和固定第三方依赖；
- 错误能跨模块稳定表达并带 phase/task/asset 上下文；
- 每个线程/C callback 边界都需要测试；
- noexcept 路径抛出属于不可恢复 invariant，必须终止而不是吞掉。

## 被拒绝方案

- 全项目 `-fno-exceptions`/`/EHs-c-`：当前依赖和 pmr 需要额外维护成本；
- 公共 API 直接抛异常：难以控制 frame、task 和 shutdown 边界。
