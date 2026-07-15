# 资源与生命周期

## 当前实现

FileSystem 提供异步读取和 best-effort 取消，ResourceManagerHub 管理 Texture、Font 和 Audio 资源，AudioEngine 使用 miniaudio。资源管理器拥有缓存对象，ResourceRef 表达客户端引用。

当前资源状态为 Unloaded、Queued、Loading、ReadyCpu、UploadQueued、Ready、Failed、Cancelled。每次加载都有递增 generation，完成回调只有在资源仍存活、generation 匹配且请求未取消时才会提交结果。

异步 completion 由 Application 在固定的 Asset Completion 阶段通过 ResourceManagerHub 每帧泵送一次，默认预算为8个回调。各 ResourceManager 不再重复驱动共享 FileSystem。管理器销毁时会先取消未完成请求，再调用资源的 unload 释放 CPU/GPU 对象。

## 已知问题

- 当前解析、CPU Ready 和 GPU Upload 仍在同一个受预算约束的主线程 completion 回调内连续完成，还没有独立上传队列；
- best-effort 取消不能中断已经开始的底层文件读取，只能阻止其结果被提交；
- 资源监听仍逐项查询文件时间，缺少独立预算和指标；
- 尚未实现 Cooked Asset、稳定 AssetId、依赖清单、内容 Hash 和增量 Cooker。

## 下一阶段契约

将 Decode 与 Upload 拆成两个队列：后台线程只做文件读取和 CPU Decode，主线程 Asset Completion 只提交 CPU 结果，Render Upload 阶段按字节数与任务数预算创建 GPU 资源。状态转换、取消和 generation 校验必须贯穿两个队列，GPU 上传和销毁只发生在明确的 Render Phase。

退出顺序为：停止接收请求 → 取消后台任务 → 排空 completion → 停止 Scene/UI → 释放 GPU 资源 → 停止音频 → 销毁窗口与平台层。
