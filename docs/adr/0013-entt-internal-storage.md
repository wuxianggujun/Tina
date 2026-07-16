# ADR 0013：EnTT 只作为 Scene 内部 ECS 存储

- 状态：Accepted
- 日期：2026-07-16

## 背景

当前 World 暴露 `entt::registry`/`entt::entity`，导致 Gameplay、Render 和测试绕过 World
生命周期，无法统一 generation、阶段 mutation barrier 和并行提交规则。

## 决定

`tina_scene` 继续使用 EnTT 作为内部 ECS 存储，但公共接口只暴露 Tina 强类型
`EntityId(owner, index, generation)`、组件 query/view 和 command。固定更新中的结构变更进入
Worker-local command buffer，barrier 后稳定合并；Render 只消费 extraction 结果，不读取
registry。公共 header 和跨模块 descriptor 不出现 EnTT 类型。

## 结果

- 可以替换内部布局而不改变 Runtime/Render/UI 接口；
- World 能集中处理 stale ID、owner cookie、Transform 层级和 mutation phase；
- 某些 EnTT 高级 API 需要在 Scene 内包装，增加少量适配代码；
- Legacy getter 只能作为迁移桥，调用点清零后删除。

## 被拒绝方案

- 继续公开 registry：所有权与阶段边界无法执行；
- 首期自研完整 ECS：没有证据证明 EnTT 是性能瓶颈，也会扩大重构范围。
