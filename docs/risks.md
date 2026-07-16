# vNext 风险登记

> 风险不是“以后再看”的列表。每项都有触发信号、缓解措施和关闭条件；状态由设计冻结清单
> 与 Roadmap 更新。

| 风险 | 等级 | 触发信号 | 缓解 | 关闭条件 |
| --- | --- | --- | --- | --- |
| Legacy 与 vNext 长期双架构 | P0 | 新功能同时改两套 Runtime、桥接层增长 | 垂直切片、Legacy 只修 blocker、零引用清单、`TINA_BUILD_LEGACY` 默认 OFF | vNext-only 全门禁后删除 Legacy |
| Factory/模块依赖形成环 | P0 | Runtime include 具体 GLFW/bgfx/miniaudio 或 Scene/Render 互相 include | backend factories、依赖图自动检查、public header compile test | 所有目标只按冻结图链接 |
| FrameArena 容量误判/UAF | P0 | failed count、跨 reset 指针、barrier timeout | 无 heap fallback、owner/barrier、peak×安全系数、ASan | 各 workload 10k 帧0失败且生命周期测试通过 |
| Tracy TU 配置不一致 | P0 | Debug/Profile 崩溃、Profiler 布局/锁异常 | 唯一 config target/client、Profile build/capture/shutdown test | MSVC/Linux Profile 门禁通过 |
| Diagnostics 干扰或递归失败 | P1 | 热点格式化日志、Worker sink 锁竞争、sink 失败递归、敏感正文进入 capture | owner channel、静态级别、主线程汇聚、emergency sink、字段策略 | Bench 日志 off/on A/B 与失败/过滤测试通过 |
| Benchmark 不可重复 | P0 | MAD 高、跨 fingerprint 比较、checksum 漂移 | versioned schema、固定机器、独立进程重复、baseline invalidation | 固定门禁机噪声校准通过 |
| Asset schema/Manifest 非事务 | P0 | crash 后清单指向半文件、旧 Runtime 误读新产物 | asset_format、staging + reread validate + atomic Manifest、schema reject | 损坏/中断/升级测试通过 |
| Cooker 路径逃逸/资源炸弹 | P0 | glTF URI 读取根外文件、count/size 乘法溢出或超量分配 | canonical root、URI policy、分配前上限/溢出检查、恶意 corpus | traversal/symlink/data URI/oversize 测试通过 |
| GPU/Audio 异步物理寿命 | P0 | logical cancel 后 staging/PCM UAF、退出挂起 | Lease、UploadTicket、retirement ledger、callback ACK | 取消竞争与300帧退出资源归零 |
| Shutdown deadline 后继续析构 | P0 | barrier 超时后 Arena/模块仍释放、后台线程继续访问 | 协作取消、owner 保活、CrashContext、硬 deadline 后 fast-fail | timeout 注入证明不会 reset/free 活跃内存 |
| 控制事件/完成队列饱和 | P0 | Close/Fatal/Stop/Completion 被普通流量挤掉，退出死锁 | 有界队列、控制预留容量、每类 full 策略、current/peak/rejected 指标 | 满容量与饥饿测试仍能停止并回收 |
| UI 输入穿透/首帧布局 | P1 | 点击菜单同时触发玩法、新 root 命中旧 geometry | 早期 UI routing、消费掩码、下一帧输入、显式首 layout | 自动化路由和场景切换测试通过 |
| Fixed 输入边沿丢失/重复 | P1 | 0步丢 pressed、4步触发4次、UI 消费后玩法仍响应 | 不可变 Snapshot、per-frame consumption、tick latch、回放 checksum | 0/1/4步与暂停/失焦测试通过 |
| 同帧输入顺序/批次溢出 | P1 | Down→Up 丢边沿、Wheel/Text 乱序、held/capture 卡住 | InputFrame 有序 transition、Move 安全合并、满容量受控 resync | 顺序/溢出/恢复与回放测试通过 |
| Audio callback 违反实时约束 | P1 | underrun、callback p99 接近 period、callback 中分配/锁等待 | 固定命令、SPSC、预分配、平台 profiler 交叉验证 | 1/32/128 voice 门禁0分配/0阻塞且 period 有余量 |
| 自研文本/IME 跨平台差异 | P1 | 中文缺字、composition 丢失、Linux preedit 不一致 | 打包字体、UTF-8 边界、IMM32 测试、Linux 明确降级 | Windows 完整门禁，Linux 支持范围文档化 |
| 跨 GPU 截图抖动 | P1 | driver/font 变化导致像素差 | 固定 reference profile、感知/区域阈值、逻辑测试优先 | 截图门禁误报率达到约定范围 |
| C++23 Linux 工具链不足 | P1 | GCC 11/Clang 14 缺库能力 | Legacy 兼容门禁与 vNext GCC13+/Clang17+ 分离 | 正式 vNext Linux preset 构建运行通过 |
| 第三方升级破坏 ABI/许可 | P1 | 新类型泄漏、包体/性能/notice 变化 | 固定版本、单依赖提交、license/CVE/bench gate | 升级 checklist 全通过 |

每个 P0 风险在首个受影响切片开始前必须有自动化门禁或明确的 fail-safe；只写日志但继续
使用可能失效内存不算缓解。风险关闭后保留记录和证据链接，不从历史中删除。
