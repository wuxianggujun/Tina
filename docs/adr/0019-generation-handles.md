# ADR 0019：强类型 generation handle + owner 边界

- 状态：Accepted
- 日期：2026-07-16

## 背景

裸指针和单纯 slot index 无法阻止删除后复用；只有 index+generation 仍可能在另一个同类型
World/Window/Device registry 中碰巧命中。把所有 ID 做成一个通用整数又会允许 EntityId 与
UINodeId 误传。

## 决定

EntityId、UINodeId、WindowId、GamepadId、RenderHandle、AudioVoiceId 和 Asset runtime handle
都使用强类型 Tag 隔离的非零 owner token + index + 32位 generation。0 generation 和0 owner
均为 invalid；slot 每次复用先增加 generation，回绕时永久 retire 而不再次复用。owner token
由当前 `tina_core` 链接镜像内的单调计数器自动发放，pool 销毁后也不复用；它只负责生成身份，
不提供查找、生命周期或服务定位。32位 token 空间耗尽时创建新 pool 明确返回 `CapacityExceeded`。
因此当前单一 Core 链接模型下，Release
也能拒绝另一个 pool 中恰好相同的 index/generation。`UINodeId` 的 Game SDK 语义仍显式
组合 owner `WindowId`，内部 slot id 同时校验 UIContext token。Debug handle/调用点再携带更宽的
Engine/registry cookie 帮助定位，但 Debug 数据不承担 Release 正确性。
稳定序列化身份使用 AssetId/业务 ID，不能保存 runtime generation handle。
普通 Game SDK 只能从所属 registry 获得 handle，不能从 raw owner/index/generation 手工构造。

## 结果

- stale、wrong-owner 和跨类型误用确定失败；
- registry 需要 retire 计数、容量门禁、owner 唯一性和耗尽诊断；
- Debug cookie 增加少量诊断数据，但不成为 Release 正确性的唯一保障；
- 异步任务在执行与 completion 两端都必须重新解析 generation。
- 若未来允许多个插件/DLL 各自静态链接 Tina Core，token source 必须提升为 EngineHost-owned 或
  单一导出 allocator；在此之前不宣称跨多个 Core 链接镜像唯一。

## 被拒绝方案

- 裸指针/单 index：复用后 UAF 或命中新对象；
- 进程全局唯一 registry：形成全局服务并增加同步；
- 把 runtime handle 序列化到存档：跨运行不稳定，应使用稳定逻辑 ID。
