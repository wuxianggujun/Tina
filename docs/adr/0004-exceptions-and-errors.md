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

结构化 `Error` 本身使用 `std::string/vector`，因此“物理内存彻底耗尽”时无法诚实保证仍能分配
错误消息。Engine create/run 等最外层 API 同样标记 `noexcept`：正常 `bad_alloc` 在尚有错误报告
余量时转换为 `OutOfMemory`；若连 emergency Error 都无法构造，则由 `noexcept` 终止进程。首期不为
未经实际故障模型证明的需求建设全局无分配 emergency arena。这里的 fatal 例外只针对无法构造
错误对象的硬 OOM，不允许普通 factory/callback exception 越过边界。

## 结果

- 兼容标准库和固定第三方依赖；
- 错误能跨模块稳定表达并带 phase/task/asset 上下文；
- 每个线程/C callback 边界都需要测试；
- noexcept 路径抛出属于不可恢复 invariant，必须终止而不是吞掉。
- 硬 OOM 的最终 fallback 是终止；不能把所有内存耗尽错误宣传为可恢复。

## 被拒绝方案

- 全项目 `-fno-exceptions`/`/EHs-c-`：当前依赖和 pmr 需要额外维护成本；
- 公共 API 直接抛异常：难以控制 frame、task 和 shutdown 边界。
