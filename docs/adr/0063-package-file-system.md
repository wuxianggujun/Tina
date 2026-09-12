# ADR 0063：不可变单文件虚拟资源包

## 状态

Accepted（2026-09-13）；实现与验证结果分别见源码及测试记录。
替代未完成的 GPCK/v1 草案，不修改 ADR 0053 的 Host shutdown 决策。

## 背景

此前包读取与散文件产出并存，同步和异步路径不同，Manifest 与对象无法原子替换。
256-byte 路径会截断，64 MiB 映射窗口不能覆盖大对象；每次重映射、复制数据和重复解析依赖产生多余工作。

## 决策

Cooker → 借用对象 spans → Core 分段原子写 → `catalog.pck` →
共享只读映射 → CatalogSnapshot → CookedAssetFile → AssetLease / GPU pin。

- 包内包含 `manifest.tmnft` 和 deterministic virtual artifact paths。
  Runtime 不读外置 Manifest，不保留散文件 fallback、旧 reader/writer wrapper 或版本嗅探。
  standalone cooked/manifest IO 仅用于显式工具，不赋予 metadata-only Snapshot 加载能力。
- `PackageReader::Open/FromMemory` 返回 Result；共享 immutable storage，支持并发 const 读取。
  `PackageFileView` 是 owning pin，不是随下一次读取失效的窗口。
- Windows 使用 UTF-8→UTF-16/W API，只共享 read/delete，不共享原地 write；POSIX 使用只读 mmap。
  整文件映射占虚拟地址空间，OS 按需调页，不是整包 heap 拷贝。OS/地址空间失败不回退复制。
- 必须通过原子替换发布，不能截断/修改已挂载文件；POSIX 外部发布者也必须遵守这个约定。
  旧 reader/view 保持旧快照，新 open 取得新快照。

### TPCK schema 2

所有整数 little-endian，不直接序列化 C++ struct。

| 区域 | 内容 |
| --- | --- |
| 64-byte header | TPCK magic、schema 2、header/entry stride、u64 count/index bytes/file bytes/names bytes、128-bit index digest |
| 48-byte entry × count | u64 path offset/length、data offset/length、128-bit payload digest |
| names | 紧凑、变长、strict UTF-8、区分大小写、严格递增且唯一的 canonical 相对路径 |
| index padding | 到 16-byte 边界的零字节 |
| payload | 按 entry 顺序，每项起点 16-byte 对齐，无重叠/越界/多余尾字节 |

检查全部乘加、范围、排序、重复、path traversal、NUL、UTF-8、schema。摘要使用 XXH3-128 v1，
不是 MD5，也不是密码学签名。Open 仅验证 metadata，view/read 校验对应 payload。
旧 GPCK/v1 直接拒绝，须重新 cook。

### 内存与调度

- 在映射索引上二分查找，lookup 不分配每项 string/hash-table 节点。
  wire 整数以安全的 `memcpy` + 显式 endian 转换读取，三路比较命中即返回；profile 对比后不增加
  每包路径缓存/哈希索引，避免为接近的查找耗时重新付出逐项内存与构建成本。
- 无固定 path/file-count 上限；metadata 默认 64 MiB 预算，0 表示不追加预算。
  wire/平台地址空间/真实 OOM 校验保留。
- Catalog count ceiling 为 0 时不附加上限；metadata 按实际 Manifest 大小分配。
- 同步/异步都保留 PackageFileView。worker 只捕获 pin、值路径与请求状态，不触碰 owner PMR/AssetSystem。
  Main 不复制 payload；CookedAssetFile 缓存已验证的解析视图。
- 队列用 head cursor 和摊销压缩代替逐次头删。默认无 4096 count ceiling；
  可配置计数/逻辑 metadata 字节预算覆盖 queued + in-flight，不等于 allocator 实际用量或 OS working set。
  Task QueueFull 保序重试，取消及 dispatch-order completion 不变。
- 分段写只持有 metadata/spans；增量 cook 保留 clean object pin，不创建第二份全包 heap。
- 跨帧资源不用 FrameArena，不造 Tina STL 或全局 allocator。

### 发布与失败

独占 sibling temp → 完整写入/flush/close → rename replace，失败不改变旧目标。
Windows 10+ 使用 `FileRenameInfoEx` 的 replace + POSIX semantics，不能用会拒绝 live mapping 的
`MoveFileEx(REPLACE_EXISTING)`，更不能先删旧包制造不可见窗口。
传给 Win32 的 UTF-16 文件名保留 NUL 终止符，`FileNameLength` 不包含终止符；只有 counted bytes
而无终止符会让 Win32 读取缓冲区尾部，导致错误目标名，即使 API 返回成功也不能证明发布到了预期路径。
这是单文件可见性事务，不承诺断电持久性。完整 Catalog/typed 校验以及 AssetSystem generation/registry
事务仍独立存在。ChangeDetector 比较包内完整 Manifest revision；watcher 仅监听包文件的 OS hint。

## 代价和范围

增量发布仍重写一个包；不实现压缩、加密、网络下载或多包 overlay。一个旧对象 pin 会保留整包虚拟映射，
但不等于全部物理页驻留。大包需要足够虚拟地址空间。性能结论必须注明 workload/build/cache 条件，
减少分配不能直接推导 FPS。

## 验证面

`tina_package_tests`：格式、超过 64 MiB 对象、长 UTF-8 路径、畸形输入、预算/OOM、并发读、
memory owner、原子替换与旧视图寿命。`tina_asset_tests`：产出/加载、增量 cook、无 fallback、
Snapshot move、Main 无 payload 分配、取消、type version、reload。
源码完成不等于测试通过；退出码、skip 和 profile 记录在本轮 validation 证据中。
