# ADR 0064：测量、校验与发布成本分离

- 状态：Accepted
- 日期：2026-09-13
- 承接：0051、0052、0063；不改变 wire schema，不保留旧 API wrapper。

## 背景

布局/hit/caret 为获取标量位置调用 raster，导致纯查询占用像素预算；文本 paint 另有栈上 4096 行/64 KiB
限制。资源虽已有映射，重复 view 仍重复 hash，异步发布在 owner thread 解析 Cooked。Render lighting view
含 owning vector，`publishedView() noexcept` 可能分配甚至 terminate，CPU 与 shader 的灯槽数量也不一致。

## 决定

1. `IUITextRasterizer::shape` 输出借用标量几何，不生成图像；measure 返回按值逻辑尺寸。独立 measure、Label
   wrap/clamp 与 paint 共用行布局，最终行按实际方向 shaping；fallback/字体同源，不把字体错误吞成 placeholder。
2. 行布局/IME scratch/face/fallback 使用 owner PMR 按需增长并复用；文本字节、图像与真正硬件预算仍显式保留。
   `initialFaceCapacity` 只预留。TextEdit 按顺序取行，不重复扫描文档前缀。
3. package immutable storage 对每个 entry 原子缓存摘要成功或失败；并发首读合流。`OnDemand` 默认开包，
   `OnOpen` 用于完整检查，发布/reload 的安全校验不弱化。worker 完成 Cooked 校验，owner 只核验身份并 move 发布。
   soft 在途/每 pump 发布字节预算以实际 extent 计费，单超大对象允许独占以避免饥饿。
4. lighting storage 归 Builder，view 只借用且不分配。bgfx CPU 上传与 shader 共享唯一真实槽位定义，超槽拒绝。
5. 测试使用统一非交互 main；通过必须有本次运行、正常退出及完整无 skip 的新报告。缺授权、缺报告、超时与
   用户未确认一律不是通过，不修改系统安全策略。

## 代价与边界

纯 shaping 仍可能有冷路径分配；每行重新 shaping 保证与 paint 一致，但不是完整 Unicode line-break 实现。
UI node/snapshot/grid/materialized pool、glyph cache 与 GPU uniform 仍有限制，不能宣称全 UI 无限容量。
校验状态消耗每 entry 的存储；mmap/hash 微基准不是 FPS、working set 或物理 IO 结论。测试/性能证据独立记录。
