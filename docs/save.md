# Save：版本化存档槽与迁移

`Tina::Save` 保存游戏拥有的不透明 payload，不负责 Scene、Editor document 或游戏业务模型。
公共入口见 [SaveStore.hpp](../include/tina/save/SaveStore.hpp)、
[SaveTypes.hpp](../include/tina/save/SaveTypes.hpp) 和 [SaveMigration.hpp](../include/tina/save/SaveMigration.hpp)；
实现位于 [src/save](../src/save)，随唯一 `Tina::GameSDK` 归档发布。

## 模块边界与数据流

```text
游戏 owner 捕获一致快照
  -> payload + gameId + dataVersion + slot
  -> SaveStore 校验 / envelope / digest
  -> primary / backup 文件发布

loadSlot
  -> 校验并选择有效 copy
  -> metadata + payload + source + health
  -> 游戏显式调用 SaveMigrationPipeline
  -> 游戏恢复业务状态，按需要再次保存
```

- `gameId` 是稳定产品身份，不是显示名；`dataVersion` 是游戏 payload 版本，不等于 Save envelope 版本。
- `rootDirectoryUtf8` 由产品选择；slot 文件名由 SaveStore 生成，displayName/gameId 不成为文件名。
  `defaultSaveRootPath(applicationName)` 只组合用户状态目录路径，不创建文件。
- World2D snapshot 只是游戏可以选择存入 payload 的一种数据；Editor authoring 文件也不由 SaveStore 自动代管。
- [Serialization](serialization.md) 可把游戏 DTO/多态对象编码为 payload；它持有 codec/registry 逻辑，
  SaveStore 不注册类型，也不解析 JSON。恢复流程是 decode candidate → resolve IDs → 游戏校验 → 一次发布。
- 不需要数据库；持久化单元是一个 slot 的 primary/backup。不要在多个 owner 中复制存档目录命名和恢复策略。

## 接口与状态流转

| 操作 | 契约 |
| --- | --- |
| `SaveStore::Create` | 校验产品身份、根目录与 payload 字节预算，返回不可复制/移动的 owner |
| `saveSlot / loadSlot / listSlots` | owner-thread 同步事务；返回结构化 Result |
| `deleteSlot` | 幂等；先删除 backup，primary 删除失败时保留可加载主份 |
| `repairPrimaryFromBackup` | 显式恢复主份；load 不暗中写磁盘，有效 primary 保持不变 |
| `beginSave / beginLoad / beginList` | 需要配置 TaskSystem；提交前复制请求 payload/metadata，经 IO worker 执行 |
| operation 的 `ready / take` | 完成通过 handle 观察，不依赖 Main queue；take 是线程安全的一次性领取，未就绪/重复领取明确失败 |
| `pathsFor` | 纯路径组合，不访问磁盘，可跨线程查询 |

```text
Idle -> 同步事务或已接受的异步事务 -> Idle
Busy -> 拒绝同一 SaveStore 的另一个文件系统事务
Operation pending -> ready -> taken
```

异步 handle 与任务保留共享内部状态，因此 SaveStore facade 销毁不使已经返回的 handle 失效。
这**不是取消或 join**：TaskSystem 必须完成已接受的 IO 工作后才能释放相关执行资源。
同一 store 的 busy 约束不等于同路径多 store 或跨进程互斥。

## 恢复与迁移

`SaveSlotHealth` 区分 `Empty / Healthy / PrimaryOnly / RecoverableFromBackup / Unrecoverable`；
单份又区分 `Missing / Valid / Corrupt / Incompatible`。调用方应使用返回的 source/health，
而不是把任意读取错误都当“新游戏”或静默覆盖存档。

`allowBackupFallback` 默认允许回退；回退读取和修复 primary 是两个独立动作。
digest 用于损坏检测，不是 HMAC/签名，也不提供防作弊或保密能力。

`SaveMigrationPipeline` 由产品拥有：每个已注册源版本只有一条严格递增的边，拒绝降级；
迁移 callback 接收旧 payload 并返回新 bytes，失败保留结构化错误。
SaveStore 不自动推断游戏版本、不自动运行迁移，也不隐式重写旧数据。

## 容量、UTF-8 与原子性

- 不设置存档数量/初始槽表，全部 `u32 SaveSlotId` 均可使用。文件名数字至少四位，较大 ID 不截断：
  `slot-0000.tsave`、`slot-10000.tsave`、`slot-4294967295.tsave`；backup 在其后追加 `.bak`。
  `listSlots()` 非递归枚举实际规范 regular file，忽略目录、symlink 与非规范名称；同槽 primary/backup 去重，
  结果按 ID 排序，不生成稠密空槽。根目录尚不存在时返回空列表，实际 IO 错误不伪装为空。
- 单 payload 仍默认 16 MiB、最大 128 MiB；具体值以 `SaveTypes.hpp` 为准，这是输入/内存预算，不是存档数限制。
- 路径和文本 metadata 使用严格 UTF-8；文件按 bytes 读写。Windows 路径转换在 Core 内完成，
  MSVC 调用方保持 `/utf-8`，不要经系统 ANSI 编码中转中文名称。
- 底层 [WriteFile.cpp](../src/core/io/WriteFile.cpp) 使用同目录临时文件与原子替换：
  目标是读者看到完整旧文件或完整新文件，而不是半写文件。
- **原子可见性不等于掉电持久化**。当前流 flush/close + rename 没有完整的文件/目录
  fsync / FlushFileBuffers 协议；primary 与 backup 也不是一个跨文件的掉电事务。
  若产品要求“保存成功后突然断电也不能丢最新进度”，应先明确该契约，再实现与故障注入验收，不能只改宣传文案。

## 已有验证面与下一步

现有 `tests/save` 接入 `tina_save_tests`，覆盖 slot、备份、恢复、版本迁移与异步 owner 等场景；
**测试源码存在不表示本轮执行通过**。本次审查没有构建或运行它。

下一步先完成一个真实游戏 consumer：保存 → 退出/重进 → 主份损坏回退 → 显式修复 → dataVersion 升级；
同时验证异步 Busy、队列拒绝、facade 提前销毁、中文路径、容量边界和完整错误反馈。
相关建议见 [逐模块审查](module-audit-2026-09-13.md) 与 [Backlog](backlog.md) 的 `SAVE-CONSUMER-001`。
本主题文档描述现有实现，不新增 ADR，也不把云同步、加密、多进程写入或强持久性宣称为已实现。
