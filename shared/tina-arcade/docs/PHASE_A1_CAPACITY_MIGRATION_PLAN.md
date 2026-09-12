# 阶段 A1：AssetSystem 固定容量迁移到预算化存储

**实施日期**: 2026-09-12
**负责模块**: `Tina::Asset::AssetSystem`
**依据**: ADR 0052, memory-policy.md

---

## 当前状态分析

### 固定容量字段

```cpp
// include/tina/asset/AssetSystem.hpp:63
Core::usize queueCapacity = 0;  // 队列硬容量上限

// src/asset/AssetSystem.cpp:196-197
m_queue.reserve(m_queueCapacity);
m_asyncRequests.reserve(m_queueCapacity);

// src/asset/AssetSystem.cpp:1294
if (m_queue.size() >= m_queueCapacity) {
    return Core::failure(AssetErrorCode::AssetQueueFull, "asset completion queue is full");
}
```

### 当前失效模式

- 固定 `queueCapacity` 导致正常批量加载返回 `AssetQueueFull`
- 用户必须预估最大并发加载数，过小浪费内存，过大仍可能失败
- `m_asyncRequests` 同样受限，但实际负载可能远小于预留容量

---

## 迁移策略

### 新 API 设计

```cpp
// include/tina/asset/AssetSystem.hpp
struct AssetSystemConfig final {
    Core::usize storeCapacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
    CookedAssetBatchLoadConfig batch{};

    // NEW: 软预算（字节），达到后触发背压
    Core::usize queueBudgetBytes = 64 * 1024 * 1024; // 64 MiB default

    // NEW: 硬上限（请求数），防止无限增长
    Core::usize maxPendingRequests = 4096; // 安全上限

    // DEPRECATED: 保留兼容性，将在 0.4.0 移除
    [[deprecated("Use queueBudgetBytes instead")]]
    Core::usize queueCapacity = 0;

    Core::u32 defaultPumpBudget = 8;
    Task::ITaskSystem* taskSystem = nullptr;
    Render::NullUploadLedger* uploadLedger = nullptr;
    AssetGpuUploadConfig gpuUpload{};
    bool autoGpuUpload = true;
    bool requireTyped2dPayloads = false;
};
```

### 迁移阶段

#### 第 1 步：添加新字段，保持向后兼容

- 添加 `queueBudgetBytes` 和 `maxPendingRequests`
- `queueCapacity` 标记为 deprecated 但仍有效
- 兼容逻辑：`queueCapacity != 0` 时覆盖新字段

#### 第 2 步：迁移内部实现

```cpp
// src/asset/AssetSystem.cpp 构造函数
AssetSystem::AssetSystem(...) {
    // 移除固定 reserve
    // m_queue.reserve(m_queueCapacity);  // DELETE

    // 按需增长，初始预留合理值
    m_queue.reserve(32);  // 典型批量大小
    m_asyncRequests.reserve(16);
}
```

#### 第 3 步：添加预算守卫

```cpp
// 新增私有方法
[[nodiscard]] Core::usize estimateQueueMemoryBytes() const noexcept {
    return m_queue.size() * sizeof(WorkItem) +
           m_asyncRequests.size() * sizeof(std::shared_ptr<AsyncRequestState>);
}

[[nodiscard]] bool isWithinQueueBudget() const noexcept {
    if (m_maxPendingRequests > 0 && m_queue.size() >= m_maxPendingRequests) {
        return false;
    }
    if (m_queueBudgetBytes > 0) {
        return estimateQueueMemoryBytes() < m_queueBudgetBytes;
    }
    return true;
}
```

#### 第 4 步：替换容量检查

```cpp
// src/asset/AssetSystem.cpp:1294 修改
// OLD: if (m_queue.size() >= m_queueCapacity)
// NEW:
if (!isWithinQueueBudget()) {
    return Core::failure(AssetErrorCode::AssetQueueBudgetExceeded,
                        "asset queue budget exceeded");
}
```

#### 第 5 步：添加错误码

```cpp
// include/tina/asset/AssetErrors.hpp
enum class AssetErrorCode : Core::u16 {
    // ... existing codes ...
    AssetQueueFull = 15,              // DEPRECATED
    AssetQueueBudgetExceeded = 16,    // NEW
};
```

---

## 测试策略

### 单元测试

#### T1: 超预算拒绝

```cpp
// tests/asset/AssetSystemBudgetTests.cpp
TEST(AssetSystemBudgetTests, ExceedingBudgetReturnsError) {
    AssetSystemConfig config{
        .queueBudgetBytes = 1024,  // 非常小的预算
        .maxPendingRequests = 10,
    };

    // 加载超过 10 个资产应失败
    for (int i = 0; i < 20; ++i) {
        auto result = system.request(assetIds[i]);
        if (i < 10) {
            EXPECT_TRUE(result.has_value());
        } else {
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code(), AssetErrorCode::AssetQueueBudgetExceeded);
        }
    }
}
```

#### T2: 按需增长验证

```cpp
TEST(AssetSystemBudgetTests, QueueGrowsBeyondInitialReserve) {
    AssetSystemConfig config{
        .queueBudgetBytes = 10 * 1024 * 1024,  // 足够大的预算
        .maxPendingRequests = 1000,
    };

    // 加载 100 个资产（超过初始 reserve(32)）
    auto handles = system.request(make_span(assetIds, 100));
    EXPECT_TRUE(handles.has_value());
    EXPECT_EQ(handles->size(), 100);
}
```

#### T3: 向后兼容

```cpp
TEST(AssetSystemBudgetTests, LegacyQueueCapacityStillWorks) {
    AssetSystemConfig config{
        .queueCapacity = 50,  // 使用旧 API
    };

    // 应该自动转换为新的预算字段
    auto handles = system.request(make_span(assetIds, 50));
    EXPECT_TRUE(handles.has_value());

    auto failedHandle = system.request(assetIds[50]);
    EXPECT_FALSE(failedHandle.has_value());
}
```

### 集成测试

#### I1: 批量加载稳定性

```cpp
TEST(AssetSystemIntegrationTests, LoadUnloadCycle) {
    // 反复加载/卸载 200 次
    for (int cycle = 0; cycle < 200; ++cycle) {
        auto handles = system.request(assetIds);
        ASSERT_TRUE(handles.has_value());

        // pump 直到全部 ready
        while (hasPendingWork(system)) {
            system.pump();
        }

        // 卸载
        for (auto& handle : *handles) {
            system.unload(handle);
        }
    }

    // 验证内存未增长（活动对象为 0）
    EXPECT_EQ(system.activeAssetCount(), 0);
}
```

### 性能基准

#### B1: 吞吐量对比

```cpp
// 测量 sync vs async pump 在不同队列大小下的吞吐量
BENCHMARK(AssetSystemPump_Sync_100Assets);
BENCHMARK(AssetSystemPump_Async_100Assets);
BENCHMARK(AssetSystemPump_Sync_1000Assets);
BENCHMARK(AssetSystemPump_Async_1000Assets);
```

---

## 验收标准

### 功能验收

- [ ] `AssetSystemBudgetTests` 全部通过（至少 10 个测试）
- [ ] 现有 `tina_asset_tests` 414 项保持通过
- [ ] 向后兼容：使用 `queueCapacity` 的旧代码无需修改

### 性能验收

- [ ] 批量加载 1000 个资产的峰值内存 < 1.2x 固定容量版本
- [ ] async pump 吞吐量 >= 固定容量版本（容差 ±5%）
- [ ] 加载/卸载 200 轮后无内存泄漏（Valgrind 零报告）

### 文档验收

- [ ] `AssetSystem.hpp` 添加新字段的详细注释
- [ ] `docs/resources.md` 更新为新的预算模型
- [ ] `CHANGELOG.md` 记录 breaking change 和迁移路径

---

## 风险与缓解

| 风险 | 缓解 |
|------|------|
| 破坏现有 414 项测试 | 每次提交前运行全量测试，保持绿色 |
| 预算估算不准确 | 只计算容器自身内存，不计算 payload（保守估计） |
| 性能回退 | Benchmark 对比，回退 < 5% 才合并 |
| 向后兼容性破坏 | deprecated 字段保留至少两个版本 |

---

## 实施时间表

| 阶段 | 任务 | 预计时间 |
|------|------|----------|
| 1 | 添加新字段和兼容逻辑 | 2 小时 |
| 2 | 迁移内部实现 | 3 小时 |
| 3 | 编写单元测试 | 4 小时 |
| 4 | 集成测试和性能基准 | 3 小时 |
| 5 | 文档更新 | 2 小时 |
| **总计** | | **14 小时（2 工作日）** |

---

## 后续工作

- **0.4.0 版本**：完全移除 `queueCapacity` 字段
- **Sprite2DBindingRegistry 迁移**：应用相同的预算化策略
- **Mesh3DBindingRegistry 迁移**：应用相同的预算化策略
