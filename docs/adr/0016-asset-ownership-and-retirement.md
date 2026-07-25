# ADR 0016：弱 Handle、强 Lease 与物理退役账本

- 状态：Accepted
- 日期：2026-07-16
- Accepted：2026-07-20（M10-A3 CPU 侧弱 Handle/强 Lease 首切片落地；M10-A7/A8 Null UploadTicket；
  M10-A13 AssetRetirementLedger + unload cancelUpload/retire。bgfx fence/FramePin 仍后置）

## 背景

generation 只能阻止迟到结果逻辑提交，不能阻止 GPU 已提交 upload、RenderFramePacket 或 Audio
callback 继续读取 payload。若取消后立即释放，仍会 UAF；若所有 Handle 都是强引用，又会让
缓存和卸载语义不可控。

## 推荐决定

`AssetHandle<T>` 是弱 generation lookup，`AssetLease<T>` 是跨 Task/Render/Audio 的强引用；
Asset/UI 通过 Render SPI 的窄 FramePinSink 把类型擦除的 frame lease/Atlas pin 登记到每个
Runtime-private RenderFramePacket；Surface pin 由 Runtime 直接保存，SubmissionTicket 在
RenderDevice submit 后附加，二者都不经过 sink；
GPU upload 用拥有 staging 的 UploadTicket，GPU/Audio 销毁进入 DestroyQueued→Retiring→Released
账本。逻辑 cancel/unload 先使新查询失效，物理内存/资源计数只有 backend fence/completion 或
audio ACK 后释放。Ready 只在下一帧 snapshot 发布，首期不做自动 LRU。

## 当前实现边界（2026-07-26）

- `RenderFrame` 的 view 必须由 backend 在 `submitFrame()` 内同步消费；成功 `present()` 返回后，Host
  complete CPU submission ticket 并释放 FramePin。suspended skip 与失败路径也会确定性释放/abandon。
- 该完成点只关闭本帧 CPU 借用，不是 GPU fence，也不驱动 Asset 物理退役。
- 已删除固定延迟到下一 present 的 `FrameDeferred` 路径与 `bgfx::frame()` 假 token；这与本 ADR 拒绝
  “固定延迟 N 帧释放”的决定一致。
- 2026-07-26 实现补充：bgfx Texture/Mesh 的内部资源所有权继续由 backend 管理，但 retirement 已使用
  独立、可证明安全的 completion marker：最后一个 view 提交 1×1 blit + `readTexture()`，只认其 ready
  frame，不认普通 `bgfx::frame()` 或 `Stats::gpuFrameNum`。
- `retireTexture2D/retireStaticMesh` 成功才消费外部 pin；generation/binding 立即失效，marker ready 后
  backend 销毁 native handle 并释放 pin。无 marker capability 且存在外部 pin 时，在任何资源状态变化前
  原子拒绝且不消费 pin；无外部 pin 时立即使逻辑资源失效，并把 native handle 交给 `bgfx::destroy` 的
  backend-owned deferred destruction，不进入 marker timeline。suspend 以 completion-only flush 推进；
  shutdown 先尝试 marker drain，只有有界 drain 未完成时才以 `bgfx::shutdown()` 返回作为仍存活外部 pin
  的 hard completion fallback。
- `AssetSystem` 将 `AssetLease` 合并进上述 pin，形成 lookup invalidation → `UnloadPending/Retiring` →
  `Unloaded/Released`。Null 同步完成；拒绝路径不消费 pin。该实现不是通用 GPU submission fence。

## 代价

- 需要区分 logical state 与 physical retirement，诊断面更大；
- 每个异步边界必须证明持有 Lease/Ticket；
- 退出要 drain 多个 backend completion，硬超时只能 fast-fail。

## 被拒绝方案

- Handle 自动强持有所有资源：隐藏生命周期并阻止可控卸载；
- cancel/generation 失效后立即 free：不能覆盖已提交 GPU/Audio 使用；
- 固定延迟 N 帧释放：不同 backend/设备不能保证安全。
