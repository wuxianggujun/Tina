# 文档索引（Tina / docs）

本文档汇总 Tina 项目现有文档、适用范围与维护状态，便于团队快速定位与更新。

## 核心设计文档

- **`architecture_design.md`** ⭐ 新增
  - 主题：引擎架构设计（Application/Scene/EventBus/ResourceManager）
  - 适用：引擎整体架构、模块划分、重构规划
  - 状态：设计阶段（2025-10-06）
  - 内容：
    - 当前架构分析（main.cpp 534 行问题）
    - 存在的痛点与解决方案
    - 新架构设计（Application + Scene 系统）
    - 实施路线图与业界对比
  - 待办：根据实施进度更新状态

- **`game_design.md`**
  - 主题：玩法与系统设计（地形/液体/物理/渲染/UI/ECS）
  - 适用：整体设计与实现对齐
  - 状态：可用
  - 待办：补充"树冠自然化（半圆/圆掩码 + 边缘噪声打孔）"与"碰撞逻辑统一至 `src/game/Collision.hpp`"的说明

## 系统级文档

- **`audio_system.md`** ⭐ 新增
  - 主题：SDL3_mixer 音频系统（初始化/资源/播放控制/排错）
  - 适用：在游戏中播放音乐与音效
  - 状态：可用（2025-10-08）
  - 关联代码：
    - 初始化与注入：`src/engine/Application.cpp:95`, `src/engine/Application.cpp:100`, `src/engine/Application.cpp:107`, `src/engine/Application.cpp:111`
    - 资源加载与播放：`src/engine/AudioResource.cpp:22`, `src/engine/AudioResource.cpp:96`, `src/engine/AudioResource.cpp:115`, `src/engine/AudioResource.cpp:124`
    - 示例使用：`src/game/GameScene.cpp:40`, `src/game/GameScene.cpp:112`

- **`ui_system_architecture.md`**
  - 主题：UI 体系结构（UINode/UIRenderer/TextRenderer/组件）
  - 适用：UI 组件开发与事件接入
  - 状态：部分过时（仍以 RGBA/Vec4 为主）
  - 待办：
    - ✅ 已完成：`Tina::Core::Color` 与 `src/ui/UIColors.hpp` 的使用
    - ✅ 已完成：UIRenderer/TextRenderer Color 重载实现
    - ⏳ 待补充：`Signal/Slot` 事件机制（`src/core/Signal.hpp`）集成示例
    - ⏳ 待更新：UI 组件使用 Signal 替代回调函数

- **`水流系统技术文档.md`**
  - 主题：水体系统设计（0..255 水位/双缓冲/局部更新/可视化）
  - 适用：TileMap 液体模拟与渲染
  - 状态：可用
  - 待办：可补充 `stepWaterAdvanced/waterFlowAdvanced` 与 `m_seaLevel` 的设计说明

## 技术细节文档

- **`frame_timing.md`**
  - 主题：帧时序与固定步长（Accumulator）建议
  - 适用：主循环/计时/渲染同步
  - 状态：可用（已更新路径引用）
  - 待办：补充 VSync/视图重置在 `src/main.cpp` 的实际调用点

- **`world-coordinate-system.md`**
  - 主题：2D 世界坐标系/相机/分块/视图
  - 适用：坐标换算/相机投影/可见性裁剪
  - 状态：可用
  - 待办：
    - 说明 UI 使用独立视图（见 `src/main.cpp` 中 view=3 与 `SetupOrtho`）
    - 引用 `src/game/CoordinateMapper.hpp` 的屏幕-世界换算

- **`scene-best-practices.md`**
  - 主题：Scene 开发最佳实践与规范
  - 适用：创建新 Scene、使用 Scene 基类 API
  - 状态：可用（2025-10-10 更新）
  - 内容：
    - Scene 基类便捷方法使用（ui()/scene()/app()）
    - 资源管理规范
    - 生命周期管理
    - 常见错误和调试技巧

- **`future_development.md`**
  - 主题：引擎未来发展规划与扩展建议
  - 适用：功能规划、技术债务追踪
  - 状态：可用（持续更新）
  - 内容：
    - 模块扩展建议（配置系统、音频系统、存档系统等）
    - 技术债务清单
    - 长期愿景与路线图

- **`scene_system.md`** ✅ 已完成
  - 主题：Scene 系统架构与使用指南
  - 适用：场景开发、场景切换、生命周期管理
  - 状态：可用（2025-10-10）
  - 内容：
    - Scene/SceneManager/SceneRenderer 核心类设计
    - 生命周期管理（onEnter/onExit/onPause/onResume）
    - 场景切换流程（push/pop/replace/clear）
    - 便捷访问方法（app()/ui()/scene()）
    - 完整使用示例和最佳实践

- **`scene_ui_code_review.md`** ✅ 已完成
  - 主题：Scene 和 UI 系统代码审查报告
  - 适用：代码优化、性能提升
  - 状态：可用（2025-10-10）
  - 内容：
    - 发现 15 个问题（5个高优先级、6个中优先级、4个低优先级）
    - 详细的优化方案和代码示例
    - 预期性能提升评估

- **`optimization_progress.md`** ✅ 已完成
  - 主题：Scene 和 UI 系统优化进度跟踪
  - 适用：了解优化状态、后续工作规划
  - 状态：持续更新
  - 内容：
    - 已完成的优化（6项，性能提升 20-30%）
    - 进行中的优化
    - 待完成的优化
    - 代码变更统计

- **`manual_refactor_guide.md`** ✅ 已完成
  - 主题：GameScene 和 SettingsScene 手动重构指南
  - 适用：完成剩余 Scene 的重构
  - 状态：可用（2025-10-10）
  - 内容：
    - 详细的重构步骤（代码前后对比）
    - 快速替换命令
    - 验证清单

## 规划中的文档（建议后续新增）

- **`events_signal_slot.md`** 🔥 高优先级
  - 内容要点：
    - `src/core/Signal.hpp` 的设计与实现
    - Signal/Slot 使用方法（connect/connect_once/emit）
    - 生命周期管理与 Connection RAII
    - 与旧回调的迁移策略
    - UI/ECS/GameScene 事件接入示例
    - EventBus 统一事件分发设计

- **`color_and_theme.md`**
  - 内容要点：
    - ✅ `Tina::Core::Color` 能力与 API
    - ✅ `UIColors` 主题与调色约定
    - TileRenderer 色板来源
    - 昼夜/生物群系调制规划

- **`resource_management.md`**
  - 内容要点：
    - ResourceManager 统一接口
    - ShaderManager/TextureManager/FontManager
    - 资源引用计数与自动卸载
    - 延迟加载与热重载

## 维护约定

- 每篇文档开头建议包含：简介 / 适用范围 / 当前状态 / 最后更新时间
- 代码路径统一使用工作区相对路径（如 `src/...`），避免外部工程引用
- 文档更新应与代码改动同步，提交信息需标注"影响范围"（渲染/物理/UI/工具等）
- ✅ 标记表示已完成的待办事项
- ⏳ 标记表示进行中的待办事项

## 最近更新

- **2025-10-10**: 完成高优先级代码优化（性能提升 20-30%）
  - 新增 `scene_ui_code_review.md` - 代码审查报告（15个问题）
  - 新增 `optimization_progress.md` - 优化进度跟踪
  - 新增 `manual_refactor_guide.md` - 手动重构指南
- **2025-10-10**: 新增 `scene_system.md`，完整的 Scene 系统架构文档
- **2025-10-10**: 更新 `ui_system_architecture.md`，添加批处理优化和 Signal 事件系统说明
- **2025-10-10**: 清理过时文档，删除 6 个临时设计文档和重复分析文档
- **2025-10-08**: 新增 `audio_system.md`，并将资源复制机制改为"拷贝 resources 全部，但排除 resources/shaders"
- **2025-10-06**: 添加 `architecture_design.md`，记录引擎架构重构设计
- **2025-10-06**: 完成 Core::Color 重构，更新 UI 系统使用 Color 接口
