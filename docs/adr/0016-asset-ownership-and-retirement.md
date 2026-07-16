# ADR 0016：弱 Handle、强 Lease 与物理退役账本

- 状态：Proposed
- 日期：2026-07-16

## 背景

generation 只能阻止迟到结果逻辑提交，不能阻止 GPU 已提交 upload、Render frame 或 Audio
callback 继续读取 payload。若取消后立即释放，仍会 UAF；若所有 Handle 都是强引用，又会让
缓存和卸载语义不可控。

## 推荐决定

`AssetHandle<T>` 是弱 generation lookup，`AssetLease<T>` 是跨 Task/Render/Audio 的强引用；
GPU upload 用拥有 staging 的 UploadTicket，GPU/Audio 销毁进入 DestroyQueued→Retiring→Released
账本。逻辑 cancel/unload 先使新查询失效，物理内存/资源计数只有 backend fence/completion 或
audio ACK 后释放。Ready 只在下一帧 snapshot 发布，首期不做自动 LRU。

## 代价

- 需要区分 logical state 与 physical retirement，诊断面更大；
- 每个异步边界必须证明持有 Lease/Ticket；
- 退出要 drain 多个 backend completion，硬超时只能 fast-fail。

## 被拒绝方案

- Handle 自动强持有所有资源：隐藏生命周期并阻止可控卸载；
- cancel/generation 失效后立即 free：不能覆盖已提交 GPU/Audio 使用；
- 固定延迟 N 帧释放：不同 backend/设备不能保证安全。
