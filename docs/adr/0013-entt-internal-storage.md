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

> **更正（2026-09-01）：** 上文「决定」段里的**内部存储选型**与**并发变更机制**两条已失效，
> **公共接口边界那部分决定仍然成立且已实现**。这个区分是本更正的重点：失效的是内部实现选型，
> 不是接口决定。历史理由保持原样。
>
> - **EnTT 已完全移除。** `entt::` 在 `src/` 与 `include/` 零命中；`src/scene/CMakeLists.txt:55`
>   链接的是 `PUBLIC Tina::Core Tina::Math Tina::Render Tina::AssetFormat Tina::AssetTypes`，
>   不含 EnTT。`tina_scene` 现在用自有 pmr 槽位存储：`World.hpp:78` 的构造取
>   `std::pmr::memory_resource&`，可选 POD 组件与 entity 槽位共享容量（`World.hpp:133`
>   「Optional POD component storage shares the entity slot capacity」），遍历面是
>   `std::span<const EntityId>`（`World.hpp:263` 的 `liveEntities()`）。
>   `docs/dependencies.md:58`、`docs/backlog.md:177`（CLEAN-001）、`docs/design-freeze.md:33`
>   都已把 EnTT 记为已删除的死依赖。因此下文「结果」里「某些 EnTT 高级 API 需要在 Scene 内包装」
>   与「Legacy getter 只能作为迁移桥」两条也随之失效。
> - **Worker-local command buffer 与 barrier 后合并从未实现。** `src/scene/` 下搜索
>   `command buffer`/`CommandBuffer`/`barrier` 零命中，`include/tina/scene/` 下唯一命中是
>   `World.hpp:98` 的注释。实际语义是**立即应用**：`World.hpp:96-98` 写明「Hierarchy edits are
>   owner-thread mutations. This standalone foundation applies them immediately;
>   updateWorldTransforms() is still the explicit publication barrier used by the Runtime phase
>   integration.」即结构变更由 owner 线程同步生效，保留下来的显式 barrier 只是世界变换的**发布**
>   （`World.hpp:108` 的 `updateWorldTransforms()`），不是结构变更的合并点。
> - **仍然成立的三条：** 公共接口只暴露 `EntityId(owner, index, generation)`、Render 只消费
>   extraction 结果不读 registry、公共 header 与跨模块 descriptor 不出现 EnTT 类型。下文「结果」
>   里「可以替换内部布局而不改变 Runtime/Render/UI 接口」这一条，恰好被 EnTT 的移除本身证明了。
> - **下游误读：** `docs/adr/0030-gameplay-2d-binding-and-physics-bridge.md:37` 把本 ADR 重述成条件句
>   「ADR 0013 对 ECS 的约束是条件式的（『若使用 EnTT，只能是 Scene 私有存储』）」，
>   `docs/design-freeze.md:33` 同样写成条件句。本 ADR 原文（上文第 13 行）是无条件陈述
>   「`tina_scene` 继续使用 EnTT 作为内部 ECS 存储」，并非条件句。两处的**结论**（Scene 不链接
>   EnTT）与现状一致，但推导路径与本 ADR 原文不符。此处仅作标注，未改动那两个文件。

## 结果

- 可以替换内部布局而不改变 Runtime/Render/UI 接口；
- World 能集中处理 stale ID、owner cookie、Transform 层级和 mutation phase；
- 某些 EnTT 高级 API 需要在 Scene 内包装，增加少量适配代码；
- Legacy getter 只能作为迁移桥，调用点清零后删除。

## 被拒绝方案

- 继续公开 registry：所有权与阶段边界无法执行；
- 首期自研完整 ECS：没有证据证明 EnTT 是性能瓶颈，也会扩大重构范围。
