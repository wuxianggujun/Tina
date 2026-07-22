# Tina 文档索引

这些文档区分当前代码事实、候选设计、已接受 ADR 和待确认门禁。旧的完成度报告、阶段修复
记录、自我评分和易失效代码行号已全部删除；决策状态以
[vNext 设计冻结清单](design-freeze.md)和 [ADR 索引](adr/README.md)为准。

- [架构总览](architecture.md)
- [设计导读与推进边界](design.md)
- [vNext 完整目标架构](vnext-architecture.md)
- [vNext 公共接口与生命周期](public-api.md)
- [游戏程序入口与状态栈](gameplay.md)
- [2D 游戏架构](game-2d.md)
- [3D 游戏架构](game-3d.md)
- [tina_core 设计与 Carbon Core 取证](core.md)
- [性能预算与内存系统](performance-memory.md)
- [Task System 与线程生命周期](task-system.md)
- [vNext 设计冻结清单](design-freeze.md)
- [Platform、Window 与 Input](platform-input.md)
- [构建与运行](building.md)
- [Runtime 与 Frame Pipeline](runtime.md)
- [Scene 与 ECS](scene-ecs.md)
- [物理系统设计与选型](physics.md)
- [渲染架构与 bgfx 边界](rendering.md)
- [资源与生命周期](resources.md)
- [高性能自研 UI](ui.md)
- [Audio 生命周期与实时线程](audio.md)
- [第三方依赖与版本治理](dependencies.md)
- [GoogleTest 与验证](testing.md)
- [Carbon Engine 参考边界](carbon-reference.md)
- [风险登记](risks.md)
- [Architecture Decision Records](adr/README.md)
- [Roadmap](roadmap.md)

主题设计文档会明确区分当前实现、已知问题和目标契约。当前 M9-A 已有 CPU/Null Perspective/Mesh3D
extraction foundation，M9-B/M9-C 已有私有 bgfx 3D/2D fixture；M10-A0 已完成独立 Cooked Header/Manifest
wire-format 与只读校验基础。**M11-E0–E5 最小产品 3D 已落地**：Catalog recipe StaticMesh cube +
Unlit solid/textured + `tina_sample_3d`（非 glTF）。Cooker **cgltf → glTF/Prefab/PBR** 仍 Deferred。
M12 删除门槛跟踪见 [m12-gate-checklist.md](m12-gate-checklist.md)。测试和实际运行结果优先于文档。
