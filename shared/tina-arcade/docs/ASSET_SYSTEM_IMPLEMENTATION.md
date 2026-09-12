# Tina Arcade 资产系统完整实施方案

**文档版本**: 1.0
**创建日期**: 2026-09-12
**状态**: Proposed
**适用范围**: Tina 引擎资产系统全面升级与 Arcade 游戏集成

---

## 执行摘要

本文档提出 Tina 引擎资产系统的完整实施路线，目标是在现有 `AssetSystem` / `CatalogSnapshot` / `AssetStore` 基础上，建立一个**产品级、可扩展、支持热重载**的资产管线，服务于 Arcade 类像素游戏的开发需求。

**核心原则**：
1. **保持现有架构决策**：遵循 ADR 0009 / 0016 / 0056 / 0058 确立的 Cooked Asset、弱 Handle/强 Lease、GPU retirement 与 stable borrow 契约
2. **零破坏性迁移**：新能力通过扩展实现，不改变已验证的 `AssetStore` / `CatalogChangePlan` / GPU registry 语义
3. **证据驱动验收**：每个切片必须有 Unit/Integration/Visual 证据，不接受"代码完成"等同"功能可用"

**预期交付物**：
- ✅ 完整的 2D/3D 资产 cook/load/bind/retire 闭环（已有，待补充验证）
- 🔨 运行时 Catalog 热重载与资产替换（部分实现，待产品验收）
- 🔨 Editor Source Import 全流程自动化（已接线，待 Linux 门禁）
- 📝 Arcade 游戏专用资产类型扩展（Tilemap/Sprite/Audio 已有，待游戏消费者）
- 📝 内存预算与 LRU 卸载策略（ADR 0052 已批准，待逐模块迁移）

---

## 1. 当前状态审计

### 1.1 已验证能力（2026-09-12）

| 模块 | 能力 | 证据 | 限制 |
|------|------|------|------|
| **AssetFormat** | Cooked wire v1、schema 严格校验、XXH3-128 | Build | little-endian only |
| **AssetSystem** | Handle/Lease、Catalog binding、async IO pump、GPU upload coordinator | `tina_asset_tests` **414/414** | 固定容量 queue |
| **Sprite2DBindingRegistry** | resident Lease/GPU/binding、Material alpha | Build + registry tests | 固定容量 |
| **Mesh3DBindingRegistry** | StaticMesh/SkinnedMesh/Material、HDR emissive | Build + registry tests | 固定容量 |
| **TileMapStream** | deferred chunk load、NavigationGrid2D 派生 | Build | chunk 缺失静默空白（已知）|
| **Editor Source Import** | recipe/glTF/texture/audio ingress、fresh stage、rollback | Build | Linux dialog 待补 |
| **CatalogReloadBindings** | Modified/Affected/Removed plan、participant transaction | Build | 需 quiescent boundary |

### 1.2 已知缺口（来自 `backlog.md` / memory）

1. **固定容量债务**（ADR 0052）：Asset queue、Sprite/Mesh registry、retirement ledger 仍是固定容量；按需增长迁移进行中
2. **TileMap residency 陷阱**：非 Requested 槽位对所有恢复路径不可见，chunk 永久空白
3. **LRU 卸载缺失**：`AssetSystem` 无自动 CPU 缓存回收，依赖显式 `unload()`
4. **Localization 零消费者**：`tina_localization_tests` 20/20，但无产品代码调用
5. **Navigation3D 零消费者**：`tina_navigation3d_tests` 38/38，但无 Scene/Gameplay 桥接
6. **AI 决策层零游戏消费者**：BehaviorTree/FSM 契约齐全，但无真实玩法 owner

### 1.3 架构决策依据

本方案严格遵循以下 ADR：

- **ADR 0009**：Runtime 只读 Cooked Asset，cgltf 只在 Cooker
- **ADR 0016**：弱 Handle、强 Lease 与物理退役账本
- **ADR 0056**：GPU retirement 不隐式 unload 逻辑 Asset
- **ADR 0058**：AssetSystem stable borrow 与可移动 facade
- **ADR 0052**：按需增长、热路径复用与预算化（迁移中）

---

## 2. 实施路线图

### 阶段 A：核心资产管线稳定化（P1，2 周）

**目标**：将现有 Asset/Registry/Retirement 推进到产品门禁可验证状态

#### A1. 固定容量迁移到预算化存储

**范围**：
- `AssetSystem` queue: `queueCapacity` → 按需增长 + `maxPendingBudget` 字节上限
- `Sprite2DBindingRegistry` / `Mesh3DBindingRegistry`: 固定槽位 → PMR `Storage` + `capacityBudget`
- `AssetRetirementLedger`: 固定记录数 → 活动寿命决定规模

**实施**：
```cpp
// include/tina/asset/AssetSystem.hpp
struct AssetSystemConfig final {
    Core::usize maxPendingBudget = 64 * 1024 * 1024; // 64 MiB default
    // 移除: Core::usize queueCapacity = 0;
    // ...
};

// src/asset/AssetSystem.cpp
// queue 从 pmr::vector<WorkItem> 改为按需分配 + 总字节守卫
```

**验收条件**：
- [ ] `AssetSystemPumpTests` 补充超预算注入测试，返回 `BudgetExceeded` 而非 `abort()`
- [ ] `Sprite2DBindingRegistryTests` / `Mesh3DBindingRegistryTests` 补充动态扩容测试
- [ ] 所有现有 `tina_asset_tests` 414 项保持通过

**证据强度**: Unit + Integration

---

#### A2. TileMap chunk residency 修复

**问题**：`TileMapStream` 的非 `Requested` 槽位对 `recoverChunk()` 不可见，chunk 永久空白（memory `tina-tilemap-residency-traps.md`）

**实施**：
```cpp
// src/asset/TileMapStream.cpp
// 所有 residency 状态转换必须确保至少一条路径可达
enum class ChunkState { Requested, Loading, Ready, Failed };
// 补充 Failed → Requested 的显式重试路径
```

**验收条件**：
- [ ] 新增 `TileMapStreamResidencyTests.cpp`：注入 IO 失败 → 显式重试 → chunk 成功加载
- [ ] 现有 `TileMapStreamTests` 保持通过

**证据强度**: Unit

---

#### A3. Catalog 热重载产品验收

**当前状态**：`AssetSystem::reloadCatalog()` / `reloadPreparedCatalog()` 已接线，但无真实产品测试

**实施**：
- 补充 `tina_sample_2d` 热重载 smoke test：启动 → 修改 Sprite texture → watcher hint → reload → 截图验证新纹理显示
- 补充 `tina_editor_desktop` 自动化测试：Project Browser Refresh → Modified/Affected assets 标记正确

**验收条件**：
- [ ] `tina_sample_2d --catalog-root=... --enable-hot-reload` 运行时修改资产 → 自动重载成功
- [ ] Editor Project Browser Refresh → `CatalogChangePlan` 正确分类 Added/Modified/Affected/Removed
- [ ] Visual gate：截图对比热重载前后纹理变化

**证据强度**: Smoke + Visual

---

### 阶段 B：Arcade 游戏资产类型扩展（P1，3 周）

**目标**：为像素游戏提供专用资产类型与工具链

#### B1. ParticleSystem2D 资产化

**当前状态**：`ParticleSystem2D` 通过 `Fx2D` AssetKind 存在，但 cook/load 路径未验证

**实施**：
```cpp
// 新增 src/asset/ParticleSystem2DLoad.hpp
namespace Tina::Asset {
    Result<ParticleSystem2DDesc> loadParticleSystem2D(
        AssetLease lease,
        std::pmr::memory_resource* memory
    );
}
```

**验收条件**：
- [ ] `ParticleSystem2DLoadTests.cpp`：cook → payload → load → 参数完整往返
- [ ] `tina_sample_2d` 新增粒子效果 demo scene

**证据强度**: Unit + Smoke

---

#### B2. SpriteAnimationClip 运行时驱动

**当前状态**：`SpriteAnimationClip` 可以在 Editor timeline 预览，但 Runtime 侧无 `SpriteAnimator` component

**实施**：
```cpp
// include/tina/scene/SpriteAnimator2D.hpp
struct SpriteAnimator2D final {
    Core::AssetId clipId{};
    float time = 0.0f;
    bool loop = true;
    float speed = 1.0f;
};

// src/scene/SpriteAnimator2DSystem.cpp
void updateSpriteAnimators(
    Scene::World2D& world,
    Asset::AssetSystem& assets,
    float deltaTime
) {
    // 读取 clip、推进 time、更新 SpriteRenderer2D frame
}
```

**验收条件**：
- [ ] `SpriteAnimator2DTests.cpp`：loop/once/reverse 模式验证
- [ ] `tina_sample_2d` 新增行走动画 demo

**证据强度**: Unit + Smoke + Visual

---

#### B3. AudioClip 空间化与混音

**当前状态**：`AudioClip` cook 已支持 5 种格式（ADR 0061），但 `AudioEngine` 无空间音频 API

**实施**：
```cpp
// include/tina/audio/AudioEngine.hpp
struct SpatialAudioConfig final {
    Math::Vec2 listenerPosition{};
    float maxDistance = 100.0f;
    float rolloffFactor = 1.0f;
};

Core::Result<Audio::VoiceId> playSpatial(
    Core::AssetId clipId,
    Math::Vec2 worldPosition,
    const SpatialAudioConfig& config
);
```

**验收条件**：
- [ ] `AudioEngineSpatialTests.cpp`：距离衰减、左右声道平衡验证
- [ ] `tina_sample_2d` 新增移动音源 demo

**证据强度**: Unit + Smoke + Manual（需耳机验证）

---

### 阶段 C：内存与性能优化（P2，2 周）

#### C1. LRU 卸载策略

**范围**：`AssetSystem` 新增可选 LRU 驱逐，达到 CPU 字节预算后自动 `unload()` 最久未用资产

**实施**：
```cpp
// include/tina/asset/AssetSystem.hpp
struct AssetSystemConfig final {
    Core::usize cpuBudgetBytes = 0; // 0 = unlimited
    bool enableLruEviction = false;
    // ...
};

// 每次 acquire() 更新 lastAccessFrame
// pump() 时检查总字节数，超限则按 LRU 顺序 unload
```

**验收条件**：
- [ ] `AssetSystemLruTests.cpp`：加载超预算 → 最久未用资产自动卸载
- [ ] Benchmark：1000 个 Texture2D 循环加载，内存峰值稳定在预算内

**证据强度**: Unit + Benchmark

---

#### C2. Async IO 性能剖析

**实施**：
- 新增 `AssetSystemBenchmark.cpp`：测量 sync vs async pump 吞吐量
- Tracy 集成：`AssetSystem::pump()` / IO dispatch / completion 打点

**验收条件**：
- [ ] Benchmark report：async pump 在 100+ 并发请求下吞吐量 > 2x sync
- [ ] Tracy flamegraph：IO wait 时间占比 < 30%

**证据强度**: Benchmark

---

### 阶段 D：Arcade 游戏产品集成（P1，4 周）

#### D1. 参考游戏原型：2D 平台跳跃

**范围**：实现一个最小可玩 Arcade 游戏，验证完整资产管线

**游戏设计**：
- 类型：横版平台跳跃（非 Minecraft 克隆，遵循 memory `pixel-game-original-not-minecraft.md`）
- 原创内容：自定义生物/方块/物品，技术约定可沿用但内容必须原创
- 资产需求：
  - TileMap：3 层（背景/地形/前景）
  - Sprite：玩家 4 方向 × 3 帧行走动画
  - Audio：跳跃/着陆/背景音乐
  - ParticleSystem2D：着陆尘土效果
  - NavigationGrid2D：敌人 AI 寻路

**实施**：
```
samples/arcade_platformer/
├── core/
│   ├── GameState.hpp           // 玩家状态机
│   ├── PhysicsController.cpp   // Physics2D 桥接
│   └── main.cpp
├── resources/
│   ├── sprites/                // 原创像素艺术
│   ├── tilemaps/
│   ├── audio/
│   └── catalog.recipe
└── CMakeLists.txt
```

**验收条件**：
- [ ] 可编译、可运行、可玩（至少 3 个关卡）
- [ ] 所有资产通过 `tina_assetc` cook
- [ ] 热重载：运行时修改 TileMap → 自动刷新关卡
- [ ] 性能：1920×1080@60fps，内存 < 512 MiB
- [ ] Visual gate：录屏展示完整玩法循环

**证据强度**: Smoke + Visual + Manual + Benchmark

---

#### D2. Editor 工作流验证

**实施**：
- 使用 `TinaEditor.exe` 完整制作 `arcade_platformer` 的一个关卡
- 记录工作流瓶颈（UI 响应、导入速度、预览准确性）
- 修复前 3 个最严重的可用性问题

**验收条件**：
- [ ] Narrator 文档：从空项目到可玩关卡的完整步骤（< 1 小时）
- [ ] 至少 3 个 Editor 改进提交（基于实际使用痛点）

**证据强度**: Manual

---

## 3. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 固定容量迁移破坏现有测试 | 高 | 中 | 每个模块独立迁移，保持 414/414 通过 |
| 热重载 participant transaction 死锁 | 高 | 低 | 补充超时与 deadlock detection 测试 |
| LRU 卸载触发 GPU UAF | 严重 | 低 | 只卸载 `refCount == 0` 且无 retirement pin 的资产 |
| Arcade 游戏性能不达标 | 中 | 中 | 提前做 prototype benchmark，60fps 为硬性门槛 |
| Linux Editor dialog 缺失阻断发布 | 中 | 高 | `zenity` / `kdialog` adapter 已规划，排入 A 阶段 |

---

## 4. 验收标准

### 4.1 门禁通过标准

**所有切片必须满足**：
- ✅ 编译零警告（MSVC `/W4` + Clang `-Wall -Wextra`）
- ✅ 单元测试 100% 通过（不接受 skip）
- ✅ 内存泄漏检测（Valgrind / ASAN）零报告
- ✅ 回归测试：现有 414 项 Asset tests 保持通过

### 4.2 产品级验收

**至少一个完整 Arcade 游戏**：
- 可编译、可运行、可玩
- 60fps @ 1920×1080
- 内存 < 512 MiB
- 支持热重载
- 原创美术/音效（非占位符）

---

## 5. 实施优先级

```
P1 (Must Have)  : A1, A2, A3, B2, D1, D2
P2 (Should Have): B1, B3, C1, C2
P3 (Nice to Have): Localization 产品消费者, Navigation3D 游戏桥接
```

---

## 6. 后续工作（不在本批范围）

1. **多平台发布**：Android APK / iOS IPA / WebAssembly
2. **网络多人**：基于现有 `Tina::Network` 模块的同步方案
3. **Mod 支持**：外部 Catalog 加载与沙盒化
4. **性能剖析工具**：Editor 集成 Tracy / RenderDoc

---

## 7. 参考资料

- [ADR 索引](../../docs/adr/README.md)
- [资源与生命周期](../../docs/resources.md)
- [Backlog](../../docs/backlog.md)
- [Memory 政策](../../docs/memory-policy.md)

---

**审阅者**：请在开始实施前确认：
- [ ] 架构决策与现有 ADR 一致
- [ ] 验收标准可测量、可验证
- [ ] 风险缓解措施充分
- [ ] 实施顺序合理（依赖关系正确）

**批准签字**：____________
**日期**：____________
