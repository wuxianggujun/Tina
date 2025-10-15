# Tina 文档索引

> 更新时间：2025-10-14  •  版本：3.x

已清理过时/重复/计划类文档；以下为与当前源码一致的最小文档集。

---

## 核心架构

- [architecture_design.md](architecture_design.md) — 总体架构设计（v3.2，已实现 95%）
- [rendering_architecture.md](rendering_architecture.md) — 渲染架构（RenderQueue / ViewID / 责任划分）
- [audio_system.md](audio_system.md) — 音频系统（SDL3_mixer 3.x）
- [event_system.md](event_system.md) — 统一事件系统（Engine::EventSystem + UI 事件分发）

## 游戏系统

- [world-coordinate-system.md](world-coordinate-system.md) — 世界坐标系定义（2D 横版）
- [frame_timing.md](frame_timing.md) — 帧率与时间步设计（固定逻辑步 + 可变渲染步）

---

## 已实现的架构组件

以下组件已完整实现，可参考源码和上述文档：

- ✅ **UILayoutManager** — UI 布局自动管理（[src/ui/UILayoutManager.hpp](../src/ui/UILayoutManager.hpp)）
- ✅ **SceneStateManager** — 场景状态自动保存/恢复（[src/engine/SceneStateManager.hpp](../src/engine/SceneStateManager.hpp)）
- ✅ **RenderQueue** — 渲染批处理优化（[src/renderer/RenderQueue.hpp](../src/renderer/RenderQueue.hpp)）
- ✅ **EventSystem** — 统一事件系统（[src/engine/EventSystem.hpp](../src/engine/EventSystem.hpp)）

---

## 使用指南

### 快速开始

1. **渲染系统**：查看 [rendering_architecture.md](rendering_architecture.md) 了解如何使用 RenderQueue 和场景渲染器
2. **音频播放**：查看 [audio_system.md](audio_system.md) 了解如何加载和播放音频
3. **事件处理**：查看 [event_system.md](event_system.md) 了解如何订阅和触发事件

### 架构原则

- **中心化资源管理**：引擎统一管理着色器、纹理、音频等资源
- **职责分离**：Scene 描述意图，Renderer 执行渲染，Manager 管理状态
- **批处理优化**：RenderQueue 自动合并绘制调用，减少 Draw Call

---

## 文档维护

- 所有文档必须与源码保持同步
- 更新源码时同步更新相关文档
- 过时文档应及时删除或归档
