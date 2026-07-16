# ADR 0019：强类型 generation handle + owner 边界

- 状态：Accepted
- 日期：2026-07-16

## 背景

裸指针和单纯 slot index 无法阻止删除后复用；只有 index+generation 仍可能在另一个同类型
World/Window/Device registry 中碰巧命中。把所有 ID 做成一个通用整数又会允许 EntityId 与
UINodeId 误传。

## 决定

EntityId、UINodeId、WindowId、GamepadId、RenderHandle、AudioVoiceId 和 Asset runtime handle
都使用强类型 Tag 隔离的 index + 32位 generation。0 为 invalid；slot 每次复用先增加 generation，
回绕时永久 retire 而不再次复用。`UINodeId` 额外编码 owner `WindowId`，所有构建都校验
owner + generation；其他 API 同时受 owner capability/registry 限制。Debug handle/调用点再携带
Engine/registry cookie 诊断跨 Host/World/Device 使用，但 Debug 数据不承担 Release 正确性。
稳定序列化身份使用 AssetId/业务 ID，不能保存 runtime generation handle。

## 结果

- stale 和跨类型误用确定失败；
- registry 需要 retire 计数、容量门禁和 owner 测试；
- Debug cookie 增加少量诊断数据，但不成为 Release 正确性的唯一保障；
- 异步任务在执行与 completion 两端都必须重新解析 generation。

## 被拒绝方案

- 裸指针/单 index：复用后 UAF 或命中新对象；
- 进程全局唯一 registry：形成全局服务并增加同步；
- 把 runtime handle 序列化到存档：跨运行不稳定，应使用稳定逻辑 ID。
