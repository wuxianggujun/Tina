# 性能分析与 Tracy Profiler

## 概述

Tina 引擎集成了 [Tracy Profiler](https://github.com/wolfpld/tracy)，用于实时性能分析和瓶颈识别。Tracy 提供微秒级精度的 CPU/GPU 时间线、帧时间统计、内存分配追踪等功能。

## Tracy 集成架构

### 核心组件

- **TraceTracy 模块** (`src/trace/tracy/`)：Tracy 后端实现
- **Trace 抽象层** (`include/tina/core/trace/Trace.hpp`)：统一的 trace API
- **TINA_TRACE_ZONE 宏**：引擎内部使用的 profiling 标记

### 编译配置

Tracy 支持通过 CMake 选项控制：

```cmake
option(TINA_BUILD_TRACE_TRACY "Enable Tracy profiler backend" ON)
```

- `ON`：启用 Tracy，生成 profiling 数据（默认，Release 构建推荐）
- `OFF`：禁用 Tracy，零运行时开销（调试构建可选）

## 现有 Profiling Zone

引擎核心路径已包含 23+ Tracy zone，覆盖所有关键阶段：

### Runtime 主循环 (EngineHost.cpp)

| Zone 名称 | 位置 | 含义 |
|-----------|------|------|
| `Runtime.Frame` | 主循环外层 | 单帧总耗时 |
| `Runtime.Platform.Poll` | 事件轮询 | 平台事件采集 |
| `Runtime.Platform.DispatchEvents` | 事件分发 | 输入事件路由 |
| `Runtime.Input.RouteAndMap` | 输入映射 | 绑定到动作的转换 |
| `Runtime.GameState.FixedUpdate` | 固定更新 | 物理模拟步进 |
| `Runtime.GameState.UpdateFrame` | 帧更新 | 游戏逻辑更新 |
| `Runtime.Audio.PumpCompletions` | 音频泵送 | 音频回调处理 |
| `Runtime.GameState.ExtractRenderScene` | 场景提取 | 游戏状态→渲染场景 |
| `Runtime.GameState.UpdateUI` | UI 更新 | UI 逻辑更新 |
| `Runtime.UI.CommitLayout` | 布局计算 | Flexbox 布局求解 |
| `Runtime.UI.BuildDisplayList` | 显示列表 | UI 绘制指令生成 |
| `Runtime.Render.Submit` | 渲染提交 | 提交到 GPU |
| `Runtime.Render.Present` | 呈现 | 交换缓冲区 |
| `Runtime.GameState.CommitCommands` | 命令提交 | 延迟命令执行 |

### 其他模块

- **UI 系统**：`UILayout.cpp`, `UIPaint.cpp` 中的细粒度 zone
- **物理系统**：`Physics2D` 模块的碰撞检测 zone
- **资源加载**：`AssetStore` 的异步加载 zone

## 压力测试样例

### stress_2d：2D 大规模压力测试

**用途**：测试 2D 渲染管线、Sprite 批处理、物理更新性能

**配置**：
```bash
tina_sample_stress_2d.exe [选项]
  --sprites=N          精灵数量（默认 10000）
  --ui-elements=N      UI 元素数量（默认 500）
  --frames=N           目标帧数（默认 600，10秒@60fps）
  --frame-delay-ms=N   帧间延迟毫秒（默认 0）
  --no-tracy           禁用 Tracy profiling
```

**场景特征**：
- 10,000 个精灵，随机位置、速度、旋转、缩放
- 简单物理：速度驱动的运动 + 边界反弹
- 10 个渲染层级
- 每帧更新所有精灵的位置和旋转

**Tracy Zone**：
- `GameState::onUpdate`：总更新时间
- `Update Sprites`：物理模拟耗时
- `Extract Render Scene`：场景提取耗时
- `FrameMark`：帧边界标记

**输出**：JSON 性能统计
```json
{
  "status": "ok",
  "sample": "tina_sample_stress_2d",
  "schema": 1,
  "config": {
    "spriteCount": 10000,
    "uiElementCount": 500,
    "targetFrames": 600
  },
  "counters": {
    "frameUpdates": 600,
    "renderExtractions": 600,
    "totalSpritesRendered": 6000000
  },
  "performance": {
    "minFrameTimeMs": 8.234,
    "maxFrameTimeMs": 18.567,
    "avgFrameTimeMs": 12.345,
    "avgFPS": 81.0
  }
}
```

### stress_3d：3D 大规模压力测试

**用途**：测试 3D 渲染管线、Mesh 批处理、光照计算性能

**配置**：
```bash
tina_sample_stress_3d.exe [选项]
  --meshes=N           网格数量（默认 1000）
  --lights=N           点光源数量（默认 20）
  --frames=N           目标帧数（默认 600）
  --frame-delay-ms=N   帧间延迟毫秒（默认 0）
  --no-tracy           禁用 Tracy profiling
```

**场景特征**：
- 1,000 个网格，网格分布，独立角速度
- 20 个点光源，随机颜色和强度（50-200）
- 四元数旋转更新
- 轨道相机，正弦波高度变化

**Tracy Zone**：
- `GameState::onUpdate`：总更新时间
- `Update Meshes`：四元数旋转计算
- `Update Camera`：相机运动
- `Extract Render Scene`：场景提取耗时
- `FrameMark`：帧边界标记

**输出**：JSON 性能统计（格式同 stress_2d）

## 使用 Tracy Profiler

### 1. 编译并运行压力测试

```bash
# 配置构建（确保 TINA_BUILD_TRACE_TRACY=ON）
cmake --preset product-2d

# 编译压力测试
cmake --build out/build/windows-msvc-vnext-bgfx-product-2d --target tina_sample_stress_2d tina_sample_stress_3d

# 运行 2D 压力测试
cd out/build/windows-msvc-vnext-bgfx-product-2d/bin/Debug
./tina_sample_stress_2d.exe --sprites=10000 --frames=600

# 运行 3D 压力测试
./tina_sample_stress_3d.exe --meshes=1000 --lights=20 --frames=600
```

### 2. 启动 Tracy Profiler GUI

从 [Tracy Releases](https://github.com/wolfpld/tracy/releases) 下载预编译的 `Tracy.exe`，或从源码编译：

```bash
git clone https://github.com/wolfpld/tracy.git
cd tracy/profiler/build/win32
msbuild Tracy.sln /p:Configuration=Release
```

启动 Tracy：
```bash
Tracy.exe
```

### 3. 连接并捕获

Tracy 会自动检测网络上运行的 Tracy instrumented 程序：

1. 启动 Tracy GUI
2. 运行 stress test 样例
3. Tracy 窗口中会显示 "tina_sample_stress_2d" 或 "tina_sample_stress_3d"
4. 点击连接，开始实时捕获
5. 等待测试完成（600 帧）
6. 保存 `.tracy` 文件供离线分析

### 4. 分析性能数据

**时间线视图**：
- 查看每帧的 zone 耗时分布
- 识别耗时最长的阶段（如 `Update Sprites`、`Extract Render Scene`）
- 检查帧时间是否稳定，是否有突发延迟

**统计视图**：
- 查看每个 zone 的平均/最小/最大耗时
- 识别累计耗时最高的热点函数
- 对比不同配置（sprite 数量、光源数量）下的性能差异

**帧图**：
- 查看帧时间曲线，识别性能尖峰
- 对比目标帧时间（16.67ms for 60fps）

**火焰图**：
- 自底向上查看调用栈耗时
- 快速定位最深层的性能瓶颈

## 性能优化流程

### 1. 建立基线

运行压力测试并记录基线性能：

```bash
# 2D 基线
./tina_sample_stress_2d.exe --sprites=10000 --frames=600 > baseline_2d.json

# 3D 基线
./tina_sample_stress_3d.exe --meshes=1000 --lights=20 --frames=600 > baseline_3d.json
```

记录关键指标：
- 平均帧时间
- 第 95 百分位帧时间（p95）
- 最大帧时间
- 平均 FPS

### 2. 识别瓶颈

使用 Tracy 火焰图识别：
- **CPU 瓶颈**：哪个 zone 占用帧时间最多？
  - 逻辑更新（`Update Sprites`、`Update Meshes`）
  - 场景提取（`Extract Render Scene`）
  - UI 布局（`UI.CommitLayout`）
- **GPU 瓶颈**：`Render.Submit` 时间长 + CPU 空闲 → GPU 受限
  - Draw call 过多
  - 填充率瓶颈
  - 着色器复杂度过高

### 3. 实施优化

根据瓶颈类型选择优化策略：

**CPU 优化**：
- **SIMD 向量化**：四元数、矩阵运算
- **批处理**：减少虚函数调用
- **缓存友好**：SoA 替代 AoS
- **多线程**：并行化独立的 entity 更新

**GPU 优化**：
- **批处理合并**：减少 draw call
- **实例化渲染**：同材质物体合批
- **LOD**：距离分级，减少顶点数
- **裁剪优化**：视锥体剔除、遮挡剔除

**内存优化**：
- **对象池**：减少分配开销
- **固定容量**：避免动态增长
- **内存预分配**：FrameArena 用于临时数据

### 4. 验证改进

优化后重新运行压力测试：

```bash
./tina_sample_stress_2d.exe --sprites=10000 --frames=600 > optimized_2d.json
```

对比 baseline 和 optimized 的 JSON 输出：
- 帧时间是否降低？
- FPS 是否提升？
- 瓶颈 zone 的耗时是否减少？

使用 Tracy 对比优化前后的火焰图，验证瓶颈是否真正解决。

## 添加自定义 Profiling Zone

### 在 C++ 代码中添加 Zone

```cpp
#include <tina/core/trace/Trace.hpp>

void myExpensiveFunction() {
    TINA_TRACE_ZONE("MyModule.ExpensiveFunction");

    // 您的代码...

    // Zone 在函数返回时自动结束
}

void myFunctionWithSubZones() {
    TINA_TRACE_ZONE("MyModule.ParentFunction");

    {
        TINA_TRACE_ZONE("MyModule.SubTask1");
        // 子任务 1...
    }

    {
        TINA_TRACE_ZONE("MyModule.SubTask2");
        // 子任务 2...
    }
}
```

### Zone 命名约定

遵循引擎现有的命名模式：`<Module>.<Phase>.<Detail>`

示例：
- `Runtime.Frame`：顶层帧循环
- `Runtime.GameState.UpdateFrame`：游戏状态更新
- `Physics2D.BroadPhase.SweepAndPrune`：物理宽相位扫描
- `UI.CommitLayout.FlexboxSolve`：Flexbox 布局求解

**避免**：
- 过短的名称（如 `Update`）→ 无法区分哪个模块
- 过长的名称（如 `MyVeryLongFunctionNameThatDoesEverything`）→ 时间线难以阅读
- 动态字符串（如 `std::to_string(entityId)`）→ 性能开销

### 编译时启用/禁用

`TINA_TRACE_ZONE` 宏会根据 `TINA_TRACE_BACKEND` 自动选择：
- **TINA_TRACE_BACKEND_ENABLED**：编译 Tracy zone（Release 推荐）
- **TINA_TRACE_BACKEND_NONE**：编译为空操作（Debug 可选）

CMake 选项控制：
```cmake
-DTINA_BUILD_TRACE_TRACY=OFF  # 完全禁用 Tracy
```

## 性能目标

### 目标帧时间（60 FPS）

| 平台 | 目标帧时间 | 裕量 |
|------|-----------|------|
| Desktop（高端） | 16.67ms | 建议 < 14ms |
| Desktop（中端） | 16.67ms | 建议 < 15ms |
| Mobile（高端） | 16.67ms | 建议 < 15ms |
| Mobile（中端） | 33.33ms (30 FPS) | 建议 < 30ms |

### 各阶段预算（参考）

基于 16.67ms 总预算的建议分配：

| 阶段 | 预算 | 备注 |
|------|------|------|
| 游戏逻辑更新 | 4ms | GameState.UpdateFrame |
| 物理模拟 | 3ms | Physics2D/3D |
| 场景提取 | 2ms | ExtractRenderScene |
| UI 布局 | 2ms | UI.CommitLayout |
| UI 显示列表 | 1ms | UI.BuildDisplayList |
| 渲染提交 | 3ms | Render.Submit |
| GPU 执行 | 留给 Present | 异步执行 |
| 其他（输入、音频） | 1.67ms | 剩余预算 |

**注意**：这些是参考值，实际项目应根据具体需求调整。

## 常见性能问题

### 1. 帧时间不稳定（抖动）

**症状**：Tracy 帧图显示锯齿状波动

**可能原因**：
- 垃圾回收或内存分配尖峰
- 资源异步加载完成时的突发工作
- 操作系统调度器抢占

**解决方案**：
- 使用对象池和固定容量容器
- 预加载关键资源
- 分帧处理大批量任务

### 2. Extract Render Scene 耗时过长

**症状**：`Runtime.GameState.ExtractRenderScene` zone 占用 >5ms

**可能原因**：
- Entity 数量过多
- 每帧重新计算变换矩阵（即使没有移动）
- 场景图遍历缓存未命中

**解决方案**：
- 脏标记优化：只更新移动的 entity
- 空间分区：只提取可见区域
- SoA 数据布局：改善缓存局部性

### 3. UI 布局耗时过长

**症状**：`Runtime.UI.CommitLayout` zone 占用 >3ms

**可能原因**：
- 深层嵌套的 Flexbox 布局
- 每帧重新布局整棵树（即使内容未变）
- 文本测量开销（复杂字体、大量文本）

**解决方案**：
- 布局缓存：只在内容变化时重新布局
- 扁平化层级：减少嵌套深度
- 虚拟化长列表：只布局可见项

### 4. Draw Call 过多

**症状**：`Runtime.Render.Submit` 耗时长，但单个 draw 很快

**可能原因**：
- 材质切换频繁
- 小批次提交（每个 sprite 一个 draw call）
- 状态变更过多

**解决方案**：
- 批处理：合并同材质的 draw
- 实例化：使用 instanced rendering
- 纹理图集：减少纹理切换

## 参考资料

- [Tracy Profiler 官方文档](https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf)
- [Tina 性能与内存模型](performance-memory.md)
- [Tina Trace 系统设计](../include/tina/core/trace/Trace.hpp)
- [ADR 0018: tina_bench JSON schema](adr/0018-tina-bench-schema.md)
