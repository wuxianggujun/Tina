# stress_2d - 2D 大规模压力测试

## 概述

stress_2d 是 Tina 引擎的 2D 渲染管线压力测试样例，用于：

- 测试大规模 Sprite 批处理性能
- 验证场景提取和渲染管线的可扩展性
- 建立 2D 性能基线
- 通过 Tracy Profiler 识别瓶颈

## 功能特性

- **大规模精灵**：默认 10,000 个精灵同时渲染
- **物理模拟**：简单的速度驱动运动 + 边界反弹
- **多层渲染**：10 个渲染层级
- **Tracy 集成**：细粒度的 profiling zone
- **性能统计**：min/max/avg 帧时间，FPS 计算
- **JSON 输出**：结构化性能报告

## 命令行选项

```bash
tina_sample_stress_2d.exe [选项]

选项：
  --sprites=N          精灵数量（默认 10000）
  --ui-elements=N      UI 元素数量（默认 500，预留）
  --frames=N           目标帧数（默认 600，10秒@60fps）
  --frame-delay-ms=N   帧间延迟毫秒（默认 0）
  --no-tracy           禁用 Tracy profiling
```

## 使用示例

### 基础运行

```bash
# 默认配置：10K 精灵，600 帧
tina_sample_stress_2d.exe

# 输出示例：
# {
#   "status": "ok",
#   "sample": "tina_sample_stress_2d",
#   "schema": 1,
#   "config": {"spriteCount": 10000, ...},
#   "counters": {"frameUpdates": 600, ...},
#   "performance": {"avgFrameTimeMs": 12.345, "avgFPS": 81.0}
# }
```

### 扩展规模

```bash
# 测试更大规模：20K 精灵
tina_sample_stress_2d.exe --sprites=20000 --frames=300

# 测试极限：50K 精灵
tina_sample_stress_2d.exe --sprites=50000 --frames=100
```

### Tracy Profiling

```bash
# 1. 启动 Tracy Profiler GUI (Tracy.exe)
# 2. 运行样例（自动连接）
tina_sample_stress_2d.exe --sprites=10000 --frames=600

# 3. Tracy 中会显示：
#    - "tina_sample_stress_2d" 进程
#    - 实时帧时间图
#    - Zone 时间线
# 4. 捕获完成后保存 .tracy 文件
```

### 保存性能报告

```bash
# 重定向 JSON 输出
tina_sample_stress_2d.exe --sprites=10000 --frames=600 > baseline_2d.json

# 对比优化前后
tina_sample_stress_2d.exe --sprites=10000 --frames=600 > optimized_2d.json
diff baseline_2d.json optimized_2d.json
```

## Tracy Zone 说明

### 主循环

- **`GameState::onUpdate`**：整个 onUpdate() 函数的总耗时
- **`FrameMark`**：帧边界标记，用于 Tracy 的帧图

### 子阶段

- **`Update Sprites`**：物理模拟（位置更新、边界检测、旋转）
- **`Extract Render Scene`**：从 World 提取 RenderScene

## 性能基线参考

以下是不同硬件配置下的参考值（仅供参考）：

| 硬件 | 精灵数 | 平均帧时间 | 平均 FPS | 备注 |
|------|-------|-----------|---------|------|
| 高端桌面 (RTX 4090) | 10,000 | ~8ms | ~125 | GPU 裕量充足 |
| 中端桌面 (GTX 1660) | 10,000 | ~14ms | ~71 | 接近 60fps 目标 |
| 低端桌面 (集显) | 10,000 | ~20ms | ~50 | 低于 60fps |
| 高端桌面 (RTX 4090) | 50,000 | ~25ms | ~40 | 压力测试 |

**注意**：实际性能取决于 CPU、GPU、驱动版本、背景进程等因素。

## 预期瓶颈

根据 Tracy 分析，典型瓶颈点：

### CPU 瓶颈

1. **Update Sprites**（~40% 帧时间）
   - 10,000 个 entity 的位置/旋转更新
   - 边界检测和反弹计算
   - 优化方向：SIMD、多线程、SoA 布局

2. **Extract Render Scene**（~20% 帧时间）
   - 遍历 World 的所有 SpriteRenderer2D
   - 变换矩阵计算
   - 优化方向：脏标记、空间分区

### GPU 瓶颈

1. **Draw Call 数量**
   - 10,000 个精灵 → 可能数百个 draw call
   - 优化方向：批处理、实例化

2. **填充率**
   - 大量重叠的半透明精灵
   - 优化方向：深度排序、Early-Z

## 故障排除

### 窗口无法创建

```
错误：GLFW window creation failed
```

**原因**：缺少 GLFW 或显卡驱动问题

**解决**：
- 检查 TINA_BUILD_PLATFORM_GLFW=ON
- 更新显卡驱动

### Tracy 无法连接

**症状**：Tracy GUI 看不到程序

**原因**：
- 防火墙阻止网络连接
- Tracy 后端未编译

**解决**：
```bash
# 确认 Tracy 已启用
cmake -L out/build/windows-msvc-vnext-sdk | grep TINA_TRACE_BACKEND
# 应输出：TINA_TRACE_BACKEND:STRING=tracy

# 检查防火墙规则
# Tracy 使用 TCP 端口 8086
```

### 帧率极低（<10 FPS）

**原因**：
- Debug 构建（优化关闭）
- 精灵数量超出硬件能力
- 垂直同步锁定

**解决**：
```bash
# 使用 Release 构建
cmake --build out/build/windows-msvc-vnext-sdk --target tina_sample_stress_2d --config Release

# 减少精灵数量
tina_sample_stress_2d.exe --sprites=1000

# 关闭垂直同步（如果引擎支持）
```

## 相关文档

- [性能分析与 Tracy Profiler](../../docs/performance-profiling.md)
- [性能与内存模型](../../docs/performance-memory.md)
- [stress_3d 3D 压力测试](../stress_3d/README.md)

## 技术细节

### 场景布局

- 精灵随机分布在 [-2000, 2000] × [-2000, 2000] 区域
- 速度范围：[-50, 50] 单位/秒
- 缩放范围：[0.5, 2.0]
- 10 个渲染层级（layer 0-9）

### 物理模拟

简单的速度驱动模型：

```
position += velocity * deltaTime
if (position.x < -2000 || position.x > 2000)
    velocity.x = -velocity.x  // 反弹
```

### 渲染管线

1. World 维护所有 Entity 和 Component
2. onUpdate() 更新所有精灵的 Transform2D
3. extractRenderScene2D() 提取可见精灵到 RenderScene
4. bgfx 后端批处理并提交 draw call

## 扩展建议

### 添加新测试场景

可以修改 `Stress2DState::onEnter()` 来测试不同布局：

```cpp
// 网格布局（更密集）
const u32 gridSize = 100;  // 100x100 = 10K
for (u32 y = 0; y < gridSize; ++y) {
    for (u32 x = 0; x < gridSize; ++x) {
        // 创建精灵...
    }
}

// 圆形分布
for (u32 i = 0; i < options_.spriteCount; ++i) {
    const float angle = (i / float(options_.spriteCount)) * 6.28318F;
    const float radius = 1000.0F;
    const float x = std::cos(angle) * radius;
    const float y = std::sin(angle) * radius;
    // ...
}
```

### 添加新性能指标

修改 `Stress2DCounters` 和 JSON 输出：

```cpp
struct Stress2DCounters {
    // 新增指标
    u64 totalDrawCalls = 0;
    u64 totalVertices = 0;
    double gpuTimeMs = 0.0;  // 如果 bgfx 提供
};

// 在 main() 中输出
writer.key("gpu");
writer.beginObject();
writer.member("drawCalls", counters.totalDrawCalls);
writer.member("vertices", counters.totalVertices);
writer.endObject();
```
