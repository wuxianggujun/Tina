# Runtime 停止与 Asset 退役收口

基线：`00cee22a`。本批不混入 bgfx 后处理、PBR sRGB、navmesh 或 Editor 功能。

## 已实现

- Host 统一 Stopping 状态；start/run/tick/stop 超时保留 State、scope、Application 身份和 backend，owner 通过
  `isStopping()` 识别并重试 `stop(app)`。全部 scope 先取消，再共享剩余预算 join，TaskSystem join 前不销毁依赖。
- startup/transition candidate 归 Host 持有，异常回退不再先析构带活动任务的局部对象；failed candidate 不调用 onExit。
- 退出原因与原始错误跨重试保留，成功时退出回调各一次；重复取消不不断推进 generation。
- retirement ledger 只保存 DestroyQueued/Retiring。完成无分配地 swap-remove，累计 releasedTotal 饱和计数。
  reserve 按需摊销增长，records 借用在 mutation 后失效，容量取决于峰值活动量而非历史释放次数。
- 旧 Released enum/计数字段直接迁移；不增加兼容身份历史。重复活动 enqueue 与重复 completion/cancel 幂等，
  已消费 GPU owner 不允许再次提交。GPU completion 与 Lease/pin 保活协议不变。

## 验证

2026-09-07 至 2026-09-08，主会话在常驻 Windows MSVC 19.50 Debug build tree 集中完成；未创建/复用测试 agent。

| 验证 | 结果 |
| --- | --- |
| 增量构建 | tina_tests、tina_asset_tests、tina_runtime_ui_tests、tina_sample_2d、tina_sample_3d 最终全部 exit 0 |
| Core/Runtime/Task/Render | 全套 658/658；最后补齐候选失败原始错误保留后，直接受影响 12/12；未重复整套 |
| Asset | 399/399，含 100000 次退役循环容量稳定、1024 活动记录摊销增长、GPU/上传分类型累计、精确资源身份和 owner 失败重试 |
| Runtime UI | 151/151 |
| 2D sample | 300 帧 status=ok、exit 0；3 个纹理退役完成、live=0；最后错误保留补丁只影响失败分支，未重复此正常退出路径 |
| 3D sample | 最终版本 30 帧 status=ok、exit 0；3 mesh + 3 texture 退役完成、live=0 |

首轮产品 build 发现两个样例仍读取 Released 历史 enum，已迁移为 `releasedCount(kind)` 饱和计数与活动记录。
统计为每种资源一个计数，不重新引入历史资源列表。样例实际消费与 GPU completion 结果均已验证。
XML 位于 `artifacts/lifecycle-retirement-20260907`；构建日志为 FastCtx jobs `j-hj5ofr`（首错）、`j-d4tbsf`、
`j-v0j75n`（最终增量）；sample 日志为 `j-00m9no`、`j-0ndqeu`。生成证据不提交。

## 资源状态

- buildTree：复用 `out/build/windows-msvc-vnext-bgfx-product-2d`；前 16,368,967,101 bytes，后
  16,385,138,767 bytes，保留用于增量构建；本批无新增临时 build tree。
- process：本仓库 CMake/MSBuild/compiler/test/sample 进程核验为 0，全部本批 FastCtx job 已退出；其他项目任务不干预。
- agent：仅原 ui_msdf_gate 记录保持 interrupted，本批新建/复用 0。
- container/volume/image/cache：本批未创建、未运行跨平台构建或依赖安装；已有 vcpkg cache 未清理/重新计量。
- 上批 SDK/consumer 临时目录的保留情况仍见 MSDF 报告，本批未再次尝试删除，不能据本表声称其已释放。

## 边界

- shutdownDeadline 只预算 worker 等待，不预算任意用户回调或 Audio/Render shutdown。
- Running/Stopping Host 的析构、错误线程析构、Create 发布前回滚仍 fail-stop，调用方必须履行显式停止义务。
- TaskGroup 包装分配 pending 泄漏已在基线修复；worker 异常的可观察错误通道仍待补，不以本批声明闭环。
- 不声明全引擎动态容量化、GPU 像素验证或其他平台编译完成。文档 UTF-8；新增 C++ 文案 ASCII，MSVC 沿用 /utf-8。
