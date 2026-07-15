# Carbon Engine 参考边界

本地 `temp/Carbon Engine` 只作为设计参考，不进入 Git 提交或 Tina 构建。

## 借鉴

- Render Step 使用明确状态、名称和 CPU/GPU profiling scope；
- 资源分为后台 Load、主线程 Prepare/GPU Upload；
- 队列支持取消、暂停、预算、pending 指标和主线程 time slice；
- GPU 资源具有显式 Prepare/Release 生命周期；
- Render Pass 明确颜色/深度 load/store 行为。

## 不复制

- Python 暴露与生成绑定；
- 完整自研多后端 RHI；
- 巨型静态 Renderer 和全局设备对象；
- 为历史兼容保留的双轨 API；
- 大量运行时可配置 RenderJob 类型。

Tina 采用固定、短小、可测试的 Frame Pipeline，并继续依赖 bgfx 处理图形后端差异。
