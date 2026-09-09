# ADR 0056：资源驻留、GPU 实例与窗口快照分账

- 状态：Accepted
- 日期：2026-09-09
- 决策依据：maintainer 授权核心 API 单轨重构；Grimwold 转场已复现共享图集失效及未提交 UI 布局查询问题。

## 决定

1. `AssetSystem` 拥有逻辑 Asset/CPU 驻留。`unload(handle)` 是显式的全体弱消费者失效操作；
   现存 `AssetLease` 仍保活原 payload，直到最后一个 lease 释放。
2. Texture/Mesh/Shader retirement 只退役指定 GPU 实例、转移该实例的 Lease 并等待 backend completion。
   不隐式 unload/forget Asset，也不取消同一 Handle 的独立 upload staging。成功消费 owner，失败保留 owner。
3. Sprite/Mesh/Shader registry 只拥有自己的 GPU binding、实例和 Lease。Material binding 的清除
   也不拥有全局 CPU 卸载权。两个 registry 可以引用同一个逻辑 Asset；销毁 A 不使 B 的 resolver 失效。
4. 回收 CPU 缓存由明确的资源/场景拥有者调用 `unload`，不是任意 GPU 实例析构顺带进行。
   CPU residency、active packet 借用、GPU retirement completion 三种账簿不互相冒充。
5. State `onEnter`、Frame update、Render extraction、UI update 提供 `primaryWindowMetrics()`，直接复制
   已提交的 `Platform::WindowMetricsSnapshot`，不增加重复 viewport 类型或另一个状态服务。
   Headless 返回 `nullopt`；suspend 的 framebuffer 0×0 如实返回。返回值可保留，但不会自动刷新。
6. Host 在初始 State enter 前采集 startup metrics，之后每个有效 Platform frame 更新一次。同帧 State
   replacement 在首次 UI layout 前即可读取当前窗口事实。相机不依赖 HUD 布局推断窗口大小。

## 历史决定

部分替代 ADR 0016 中 GPU 退役附带逻辑卸载的实现约定，保留 weak Handle / strong Lease、generation、
GPU backend completion 与失败回滚原则；不改写历史理由。Null upload ledger 仍是 staging 状态机夹具，
其 ReadyGpu 不能作为真实 GPU binding 或画面完成证据。

## 验收

- 同一 Asset 的两个 GPU owner/registry 独立退役；A 完成后 B 仍可解析并借用原资源。
- 显式 CPU unload 后 weak resolver 失效，但已有强 Lease 的 GPU owner 仍可退役，包括同步 completion。
- 分配失败、后端拒绝、错误线程、跨 Store、重复 retirement 不消费失败方 owner；独立 staging 不被误取消。
- 640×480 → suspended 0×0 → 800×600 在各消费阶段一致；suspended 帧内 replacement 读到 0×0，
   不读取旧 UI snapshot；保存的初始 metrics 值不被后续帧覆盖。
