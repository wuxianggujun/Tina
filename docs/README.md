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
  - 状态：可用（建议将历史引用的 engine 路径说明调整为 `src/main.cpp` + `Tina::Core::FrameTimer`）
  - 待办：补充 VSync/视图重置在 `src/main.cpp` 的实际调用点

- **`world-coordinate-system.md`**
  - 主题：2D 世界坐标系/相机/分块/视图
  - 适用：坐标换算/相机投影/可见性裁剪
  - 状态：可用
  - 待办：
    - 说明 UI 使用独立视图（见 `src/main.cpp` 中 view=3 与 `SetupOrtho`）
    - 引用 `src/game/CoordinateMapper.hpp` 的屏幕-世界换算

## 已删除文档（过时/不适用当前工程）

- `resource_system.md`
  - 原因：描述 Lumix 的资源体系/Hub/编译管线，与当前工程不符（Tina 仅使用 `ShaderManager` + 简单资源拷贝）

- `window_events.md`
  - 原因：基于 Lumix 平台层的事件/窗口处理；当前 Tina 为 SDL3 + `src/os/OS.hpp` 的封装，路径/接口不一致

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

- **`scene_system.md`** 🔥 高优先级（依赖架构重构）
  - 内容要点：
    - Scene 基类设计与生命周期
    - SceneManager 场景栈管理
    - 场景切换流程（push/pop/replace）
    - GameScene/MenuScene/PauseScene 示例
    - 场景间数据传递

- **`resource_management.md`**（依赖架构重构）
  - 内容要点：
    - ResourceManager 统一接口
    - ShaderManager/TextureManager/FontManager
    - 资源引用计数与自动卸载
    - 延迟加载与热重载

## 维护约定

- 每篇文档开头建议包含：简介 / 适用范围 / 当前状态 / 最后更新时间
- 代码路径统一使用工作区相对路径（如 `src/...`），避免外部工程引用
- 文档更新应与代码改动同步，提交信息需标注"影响范围"（渲染/物理/UI/工具等）
- 🔥 标记表示高优先级文档
- ✅ 标记表示已完成的待办事项
- ⏳ 标记表示进行中的待办事项

## 最近更新

- **2025-10-08**: 新增 `audio_system.md`，并将资源复制机制改为“拷贝 resources 全部，但排除 resources/shaders”
- **2025-10-06**: 添加 `architecture_design.md`，记录引擎架构重构设计
- **2025-10-06**: 完成 Core::Color 重构，更新 UI 系统使用 Color 接口
