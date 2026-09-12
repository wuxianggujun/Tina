# stress_3d - 3D 大规模压力测试

## 概述

stress_3d 是 Tina 引擎的 3D 渲染管线压力测试样例，用于：

- 测试大规模 Mesh 批处理性能
- 验证 3D 场景提取和光照计算的可扩展性
- 建立 3D 性能基线
- 通过 Tracy Profiler 识别瓶颈

## 功能特性

- **大规模网格**：默认 1,000 个 mesh 同时渲染
- **动态光照**：20 个点光源，随机颜色和强度
- **四元数旋转**：每个 mesh 独立的角速度
- **轨道相机**：环绕场景，正弦波高度变化
- **Tracy 集成**：细粒度的 profiling zone
- **性能统计**：min/max/avg 帧时间，FPS 计算
- **JSON 输出**：结构化性能报告

## 命令行选项

```bash
tina_sample_stress_3d.exe [选项]

选项：
  --meshes=N           网格数量（默认 1000）
  --lights=N           点光源数量（默认 20）
  --frames=N           目标帧数（默认 600，10秒@60fps）
  --frame-delay-ms=N   帧间延迟毫秒（默认 0）
  --no-tracy           禁用 Tracy profiling
```

## 使用示例

### 基础运行

```bash
# 默认配置：1K meshes，20 点光源，600 帧
tina_sample_stress_3d.exe

# 输出示例：
# {
#   "status": "ok",
#   "sample": "tina_sample_stress_3d",
#   "schema": 1,
#   "config": {"meshCount": 1000, "lightCount": 20, ...},
#   "counters": {"frameUpdates": 600, ...},
#   "performance": {"avgFrameTimeMs": 15.678, "avgFPS": 63.8}
# }
```

### 扩展规模

```bash
# 测试更多网格：5K meshes
tina_sample_stress_3d.exe --meshes=5000 --frames=300

# 测试更多光源：100 点光源
tina_sample_stress_3d.exe --lights=100 --frames=300

# 极限测试：10K meshes + 200 光源
tina_sample_stress_3d.exe --meshes=10000 --lights=200 --frames=100
```

### Tracy Profiling

```bash
# 1. 启动 Tracy Profiler GUI (Tracy.exe)
# 2. 运行样例（自动连接）
tina_sample_stress_3d.exe --meshes=1000 --lights=20 --frames=600

# 3. Tracy 中会显示：
#    - "tina_sample_stress_3d" 进程
#    - 实时帧时间图
#    - Zone 时间线（Update Meshes、Update Camera、Extract Render Scene）
# 4. 捕获完成后保存 .tracy 文件
```

### 保存性能报告

```bash
# 重定向 JSON 输出
tina_sample_stress_3d.exe --meshes=1000 --lights=20 --frames=600 > baseline_3d.json

# 对比优化前后
tina_sample_stress_3d.exe --meshes=1000 --lights=20 --frames=600 > optimized_3d.json
```

## Tracy Zone 说明

### 主循环

- **`GameState::onUpdate`**：整个 onUpdate() 函数的总耗时
- **`FrameMark`**：帧边界标记，用于 Tracy 的帧图

### 子阶段

- **`Update Meshes`**：四元数旋转计算（1000 个 mesh）
- **`Update Camera`**：轨道相机运动
- **`Extract Render Scene`**：从 World 提取 3D 场景

## 性能基线参考

以下是不同硬件配置下的参考值（仅供参考）：

| 硬件 | Meshes | Lights | 平均帧时间 | 平均 FPS | 备注 |
|------|--------|--------|-----------|---------|------|
| 高端桌面 (RTX 4090) | 1,000 | 20 | ~10ms | ~100 | GPU 裕量充足 |
| 中端桌面 (GTX 1660) | 1,000 | 20 | ~18ms | ~55 | 接近 60fps 目标 |
| 低端桌面 (集显) | 1,000 | 20 | ~30ms | ~33 | 低于 60fps |
| 高端桌面 (RTX 4090) | 5,000 | 100 | ~40ms | ~25 | 压力测试 |

**注意**：实际性能取决于 CPU、GPU、驱动版本、背景进程等因素。

## 预期瓶颈

根据 Tracy 分析，典型瓶颈点：

### CPU 瓶颈

1. **Update Meshes**（~30% 帧时间）
   - 1,000 个四元数旋转更新
   - 矩阵计算
   - 优化方向：SIMD、多线程

2. **Extract Render Scene**（~25% 帧时间）
   - 遍历 World 的所有 MeshRenderer3D
   - 变换矩阵计算
   - 视锥体裁剪
   - 优化方向：脏标记、空间分区、八叉树

3. **光照计算**（GPU 侧，CPU 提交开销）
   - 20 个点光源 × 1000 个 mesh
   - 优化方向：延迟渲染、光源裁剪

### GPU 瓶颈

1. **Draw Call 数量**
   - 1,000 个 mesh → 可能数百到上千个 draw call
   - 优化方向：批处理、实例化

2. **片元着色器复杂度**
   - 多光源逐像素光照
   - 优化方向：延迟渲染、光源分块

3. **顶点处理**
   - 大量顶点变换
   - 优化方向：LOD、遮挡剔除

## 故障排除

### 窗口无法创建

```
错误：GLFW window creation failed
```

**原因**：缺少 GLFW 或显卡驱动问题

**解决**：
- 检查 TINA_BUILD_PLATFORM_GLFW=ON
- 检查 TINA_BUILD_RENDER_BGFX=ON
- 更新显卡驱动

### 帧率极低（<10 FPS）

**原因**：
- Debug 构建（优化关闭）
- Mesh/光源数量超出硬件能力
- 光照计算过于复杂

**解决**：
```bash
# 使用 Release 构建
cmake --build out/build/windows-msvc-vnext-sdk --target tina_sample_stress_3d --config Release

# 减少规模
tina_sample_stress_3d.exe --meshes=100 --lights=5

# 渐进式测试
tina_sample_stress_3d.exe --meshes=500 --lights=10
tina_sample_stress_3d.exe --meshes=1000 --lights=20
tina_sample_stress_3d.exe --meshes=2000 --lights=50
```

### 黑屏或闪烁

**原因**：
- Mesh 资源未正确设置（使用 fixture 资源）
- 相机配置错误
- 光照强度过低

**检查**：
- 确认 `internFixtureFrameResource` 正确调用
- 验证相机视锥体（near=0.1, far=1000）
- 检查光源强度（50-200 范围）

## 相关文档

- [性能分析与 Tracy Profiler](../../docs/performance-profiling.md)
- [性能与内存模型](../../docs/performance-memory.md)
- [stress_2d 2D 压力测试](../stress_2d/README.md)

## 技术细节

### 场景布局

- Mesh 网格分布，间距 10 单位
- 网格大小：sqrt(meshCount) × sqrt(meshCount)
- 位置范围：约 [-gridSize*5, gridSize*5]
- Y 轴高度：基于 X、Z 的正弦波（模拟起伏地形）

### 四元数旋转

每个 mesh 有独立的角速度向量 `(ωx, ωy, ωz)`：

```cpp
// 角速度 → 旋转角度
float angle = length(ω) * deltaTime;

// 轴角表示 → 四元数
quat delta = {axis * sin(angle/2), cos(angle/2)};

// 四元数乘法（累积旋转）
quat newRotation = oldRotation * delta;
```

### 轨道相机

相机围绕原点旋转，高度随时间变化：

```cpp
float time = frameCount * (1.0f / 60.0f);
float radius = 150.0f;
float height = 50.0f + sin(time * 0.2f) * 20.0f;

camera.position = {
    cos(time * 0.3f) * radius,
    height,
    sin(time * 0.3f) * radius
};
```

### 点光源配置

- 位置：随机分布在 [-50, 50]³ 立方体内
- 颜色：随机 RGB，范围 [0.5, 1.0]
- 强度：随机 [50, 200]
- 半径：固定 50.0

## 扩展建议

### 添加不同运动模式

修改 `Stress3DState::onUpdate()` 测试不同动画：

```cpp
// 正弦波运动（所有 mesh 同步）
for (size_t i = 0; i < meshEntities_.size(); ++i) {
    Transform3D t = *world_->transform3D(meshEntities_[i]);
    t.translation[1] = sin(time + i * 0.1f) * 10.0f;
    world_->setTransform3D(meshEntities_[i], t);
}

// 螺旋运动
float angle = time * 0.5f + i * 0.01f;
t.translation[0] = cos(angle) * radius;
t.translation[2] = sin(angle) * radius;
```

### 测试不同光源配置

```cpp
// 更密集的光源（100 个）
--lights=100

// 更高强度（500-1000）
PointLight3D light{
    .intensity = intensityDist(rng) * 5.0f,  // 乘以 5 倍
    .radius = 100.0f,  // 更大范围
};

// 动态移动的光源
for (auto& lightEntity : lightEntities_) {
    Transform3D t = *world_->transform3D(lightEntity);
    t.translation[0] += sin(time) * 0.5f;
    t.translation[2] += cos(time) * 0.5f;
    world_->setTransform3D(lightEntity, t);
}
```

### 添加新性能指标

```cpp
struct Stress3DCounters {
    // 新增指标
    u64 totalDrawCalls = 0;
    u64 totalTriangles = 0;
    u64 culledMeshes = 0;  // 视锥体剔除数量
    double gpuTimeMs = 0.0;
};
```

## 与 stress_2d 的对比

| 特性 | stress_2d | stress_3d |
|------|-----------|-----------|
| 默认实体数 | 10,000 sprites | 1,000 meshes |
| 渲染复杂度 | 2D 矩形 | 3D mesh + 光照 |
| 物理模拟 | 简单速度 | 四元数旋转 |
| 光照 | 无 | 20 点光源 |
| 相机 | 固定 2D | 轨道 3D |
| CPU 瓶颈 | 位置更新 | 四元数计算 |
| GPU 瓶颈 | 填充率 | 光照计算 |
| 典型帧时间 | ~12ms | ~16ms |

**选择建议**：
- 测试 2D 游戏性能 → stress_2d
- 测试 3D 游戏性能 → stress_3d
- 对比 2D/3D 管线差异 → 同时运行

## 已知限制

1. **Mesh 资源为 fixture**：使用占位符资源，不是真实 mesh
   - 顶点数未计入统计
   - 无法测试不同 mesh 复杂度的影响

2. **光照为前向渲染**：每个 mesh 计算所有光源
   - 未使用延迟渲染优化
   - 光源数量线性增加开销

3. **无阴影**：未启用阴影计算
   - 实际游戏可能有更高开销

4. **无后处理**：未测试 post-processing 管线
   - Bloom、SSAO、HDR 等未包含

5. **固定分辨率**：1920×1080
   - 不同分辨率性能会有显著差异

## 下一步

完成 stress_3d 基线测试后，可以：

1. **识别瓶颈**：用 Tracy 火焰图找到最慢的 zone
2. **实施优化**：
   - CPU：SIMD 四元数、多线程更新
   - GPU：实例化渲染、延迟渲染
3. **验证改进**：重新运行测试，对比 JSON 输出
4. **扩展测试**：添加阴影、后处理、更复杂的场景
