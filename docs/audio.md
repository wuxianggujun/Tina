# Audio 生命周期与实时线程契约

> 状态：vNext 契约文档。M11-A7 已落地后端无关 `AudioEngine` Disabled 生命周期基础；
> 真实设备仍固定为 miniaudio（ADR 0012），不引入 SDL_mixer 或第二套音频库。

## 模块边界

`tina_audio` 提供后端无关的 AudioEngine、Bus、Voice、Listener、实时队列与 Disabled backend；
`tina_audio_miniaudio` 才包含具体设备/callback/stream 实现，miniaudio 类型只存在于该 adapter。
Scene/World 不直接依赖音频后端；Gameplay 把播放、停止和参数修改提交给 AudioEngine。

首期范围：

- Master/Music/SFX Bus 音量与 mute；
- 短音效播放、停止、音量、pitch、loop 和基础3D位置；
- 一条可取消的流式 Music 路径；
- generation `AudioVoiceId`、主线程命令队列和 callback → main completion；
- 设备不可用时显式 Disabled 状态，使无声运行仍可测试。

混响、DSP Graph、空间遮挡、HRTF、编辑器预览和复杂 voice virtualization 后置。

## 状态与所有权

```text
Uninitialized -> Initializing -> Enabled
                         +-----> Disabled
Enabled/Disabled -> Stopping -> Stopped
```

- `EngineHost` 拥有 `AudioEngine`；初始化失败只有在配置声明 Audio 必需时才令 Engine 创建失败；
- `AudioVoiceId { owner, index, generation }` 只标识 AudioEngine voice slot，不是 AssetId；slot 复用前增加 generation；
- `AssetHandle<T>` 是可失效查询句柄，不能独自保证 payload 存活；正在播放或排队上传的声音
  必须持有 `AssetLease<AudioClip>` 强引用；
- AudioEngine 持有 active lease，直到 callback 确认 voice 停止并由主线程 completion 回收；
- Listener/Emitter 使用 Tina 右手、Y-up、-Z forward、米为单位；如果 miniaudio adapter 需要
  转换，只能在 adapter 边界一次完成并测试。

## 实时线程模型

所有公开 Audio API 从主线程提交固定大小命令；音频 callback 是唯一实时 consumer。若将来
出现多个 producer，它们必须先汇聚到主线程，不能未经证明把 SPSC 偷换成不安全的 MPSC。

```text
Gameplay / Asset main completion
  -> main-thread voice registry
  -> bounded AudioCommand SPSC
  -> miniaudio callback
  -> bounded AudioCompletion SPSC
  -> Runtime Audio Completion phase
```

Audio callback 中禁止：

- heap allocation/free；
- 文件 IO、Asset 查询或 Task wait；
- 可能阻塞的 mutex/condition variable；
- 格式化日志、异常传播和 UI/EventBus 回调；
- 销毁 Scene、Asset 或 Engine 对象。

命令包含 generation、固定大小参数和已准备好的 backend payload handle。callback 只写入固定
容量 completion；主线程在唯一阶段重新校验 generation 后执行回调、释放 lease 和更新状态。

## 队列满与失败策略

| 命令 | 队列满行为 |
| --- | --- |
| Stop/Shutdown/释放 lease | 不可丢；预留控制容量，失败触发受控 Audio fault |
| Play | 返回 `QueueFull`，Gameplay 可在下一帧重试或放弃 |
| 音量/pitch/位置更新 | 同 Voice/参数可合并为最新值；允许丢弃被覆盖的旧值 |
| Completion | 不可静默丢失；容量按最大 active voice + 控制事件设置 |

队列统计 current/peak、rejected、coalesced、callback underrun 和最长 command age。任何可丢弃
策略都必须在命令类型声明，不能在通用 queue 中猜测。

## Asset、流式 Music 与延迟销毁

短音效由 Asset 的 IO/CPU 阶段解码为拥有生命周期的 AudioClip payload；callback 不读取
FrameArena 或 Worker scratch。流式 Music 的 IO/Decode 在非实时线程填充有界 ring buffer，
callback 只消费已准备 PCM；buffer underrun 计数但不阻塞等待磁盘。

取消 Asset generation 后，已排队但尚未开始的 Play 返回失效；正在播放的 voice 由策略决定
自然结束或收到 Stop，但 payload 只有在 callback completion 后释放。热重载首期后置，不能
原地替换 callback 正在读取的内存。

## Frame Pipeline 与关闭顺序

Audio command 在 Frame Update/玩法阶段产生，在当帧末主线程批量 flush；Audio completion
在下一帧固定的 `Audio Completion` 子阶段提交，不直接从 callback 进入 EventBus。

关闭必须遵守：

1. `IGameApplication`/`IGameState`/Scene 停止产生 Play；
2. Asset 停止新请求，但保留 active Audio lease；
3. Audio 发送不可丢的 StopAll/Shutdown，停止 callback 与流式 producer；
4. 主线程 drain completion，释放全部 Audio lease 和 voice slot；
5. Asset 释放 Audio payload；
6. Audio backend、Asset、Task 按依赖顺序退出。

超过 shutdown deadline 时输出 active voice、command age、stream task 和 generation，不强杀
callback 线程，也不提前释放仍被设备读取的内存。

## 性能与验收

- callback p99 和最大耗时必须显著低于设备 period；结果记录 sample rate、buffer frames 和
  backend period，不能只写平均耗时；
- callback 稳态0分配、0阻塞锁、0 Task wait；
- 1/32/128 active voice、短音效 burst、Music underrun 和队列满分别建立 `tina_bench` workload；
- generation stale Play/Stop、Stop 与自然结束竞争、completion 满容量、设备 Disabled、重复
  shutdown 和 active Asset lease 关闭顺序有直接 GoogleTest；
- 300帧 2D 样例验证 SFX/Music、Master/Music/SFX 音量和退出资源归零；
- Tracy 可标记主线程 Audio phase，但首期不在实时 callback 发送高频动态字符串或 memory
  event，避免 profiler 反过来破坏实时性。
