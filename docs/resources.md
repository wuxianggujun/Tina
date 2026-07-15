# 资源与生命周期

## 当前实现

FileSystem 提供异步读取，ResourceManagerHub 管理 Texture、Font 和 Audio 资源，AudioEngine 使用 miniaudio。资源管理器拥有缓存对象，ResourceRef 表达客户端引用。

## 已知问题

- Resource 状态不足以表达 Queued、Loading、CPU Ready、Upload 和 Cancelled；
- 异步回调存在捕获资源裸指针的生命周期风险；
- 多个 Manager 可能重复泵送同一个 FileSystem 回调队列；
- 资源监听逐项查询文件时间，缺少预算和指标。

## 目标契约

状态统一为 Unloaded、Queued、Loading、ReadyCpu、UploadQueued、Ready、Failed、Cancelled。后台任务通过 generation 与取消令牌校验；FileSystem completion 每帧只泵送一次；GPU 上传和销毁只发生在明确的 Render Phase。

退出顺序为：停止接收请求 → 取消后台任务 → 排空 completion → 停止 Scene/UI → 释放 GPU 资源 → 停止音频 → 销毁窗口与平台层。
