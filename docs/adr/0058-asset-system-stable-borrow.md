# ADR 0058：AssetSystem stable borrow 与可移动 facade

- 状态：Accepted
- 日期：2026-09-09
- 范围：Asset / Scene / Gameplay2D / Render

## 背景

`AssetSystem` 的 facade 可以在 owner thread 上移动，但 Sprite/Mesh binding registry、TileMap stream
和 `Scene2DRuntime` 需要长期保存对 facade 的访问关系。若这些 owner 直接保存 `AssetSystem*`，facade
移动后指针会悬空；若把整个 AssetSystem 固定为不可移动对象，又会把 Store、异步请求与产品 owner 的
生命周期绑死在一个地址上。

## 决定

1. `AssetSystem` 继续允许 owner-thread move；move 保留稳定的 `AssetStore` 与异步请求状态，active
   `AssetLease` 和 detached async IO 在 facade move 后继续有效。
2. 需要长期访问 facade 的 owner 必须取得 move-only `AssetSystemBorrow`。borrow 固定 facade 地址并
   在 borrow 存在时让 `AssetSystem::canMove()` fail-closed；borrow 与 facade 必须在同一 owner thread
   释放。
3. `AssetLease` 是 move-only 强引用，保活稳定 Store/CPU payload；Lease 存在时逻辑 unload 进入
   `UnloadPending`。弱 `AssetHandle` 只表示 generation identity，不延长 payload 生命周期。
4. Lease release、borrow release、query 与 AssetSystem 析构都遵守 owner-thread 契约。错线程 release
   在访问 Store 前终止，不能用锁或隐式跨线程转发掩盖错误 owner。
5. GPU binding registry 继续唯一拥有自己的 resident Lease/GPU binding，并通过 packet-local borrow pin
   阻止 active frame retirement；registry 不复制全局 Asset owner，也不提供旧的裸指针兼容入口。

## 代价与验证边界

- facade move 在仍有 stable borrow 时会被拒绝，调用方必须先显式释放 registry/stream/runtime borrow；
- Store、PMR resource 与所有 Lease 的寿命必须覆盖 detached async read 的完成；
- 生命周期和线程错误使用 focused death tests 验证，GPU backend retirement 仍需单独验证；
- 这是单轨生命周期契约，不为旧的长期裸 `AssetSystem*` 使用者保留包装或兼容分支。

关联实现说明见 [资源文档](../resources.md)、[架构总览](../architecture.md) 与
[ASSET-LEASE-MOVE-001](../backlog.md)。
