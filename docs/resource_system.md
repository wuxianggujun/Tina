# Lumix 引擎资源系统：设计与实现

本文档总结 Lumix 引擎资源系统的整体设计与关键实现，并配以精选代码片段与设计思路，便于定位源码、理解扩展点与运行时行为。

## 目标与设计原则

- 统一抽象：所有资源统一继承 `Resource` 抽象，由对应 `ResourceManager` 管理，并通过 `ResourceManagerHub` 汇总、统一路由。
- 运行时读取编译产物：加载 `.lumix/resources/<hash>.res`（统一头部 + 可选 LZ4 压缩），避免直接解析源文件，提升加载与一致性。
- 异步 I/O 与主线程回调：文件读取在后台线程完成，主线程逐帧分发回调，保证与渲染/世界更新线程安全衔接。
- 依赖驱动的有限状态机：资源 READY 取决于自身数据与依赖资源的状态；失败/空依赖会驱动状态回退。
- 可编辑器钩挂：编辑器在加载前可拦截，触发编译，再继续加载，实现“所见即所得”的热编译/热重载。

---

## 核心结构

### ResourceType 与编译资源头

```cpp
// src/engine/resource.h
struct ResourceType {
    // 小写字符串哈希，区分资源类型（如 "texture"、"model"）
    explicit ResourceType(const char* type_name);
    RuntimeHash type; // 0 表示无效
};

#pragma pack(1)
struct CompiledResourceHeader {
    static constexpr u32 MAGIC = 'LRES';
    enum Flags { COMPRESSED = 1 << 0 };
    u32 magic = MAGIC;
    u32 version = 0;
    u32 flags = 0;
    u32 padding = 0;
    u64 decompressed_size = 0; // 若压缩，用于解压
};
#pragma pack()
```

### Resource 抽象（状态机 + 依赖）

```cpp
// src/engine/resource.h
struct Resource {
    enum class State : u32 { EMPTY = 0, READY, FAILURE };
    virtual ~Resource();
    virtual ResourceType getType() const = 0;

    // 引用计数与状态
    u32 incRefCount();
    u32 decRefCount(); // 当启用卸载且降至 0 时触发 doUnload
    State getState() const; // EMPTY/READY/FAILURE

protected:
    // 生命周期钩子
    virtual void onBeforeReady() {}
    virtual void unload() = 0;          // 释放 CPU/GPU 资源
    virtual bool load(Span<const u8>) = 0; // 解析已读入的编译资源

    void doLoad();    // 发起异步文件读取
    void doUnload();  // 取消异步、清理自身并重置计数

    // 依赖管理：父资源观察依赖资源状态变化
    void addDependency(Resource& dep);
    void removeDependency(Resource& dep);
    void checkState(); // 依据计数推进 EMPTY/READY/FAILURE
};
```

关键状态推进与文件回调（节选）：

```cpp
// src/engine/resource.cpp
void Resource::checkState() {
    // 有失败依赖 => 进入 FAILURE
    if (m_failed_dep_count > 0 && m_current_state != State::FAILURE) { /* ... */ }

    if (m_failed_dep_count == 0) {
        // 自身文件 + 所有依赖就绪，且期望为 READY => onBeforeReady -> READY
        if (m_empty_dep_count == 0 && m_current_state != State::READY && m_desired_state != State::EMPTY) {
            onBeforeReady();
            // 期间若新增依赖/变更期望会中断，最终才设置 READY
            m_current_state = State::READY; /* invoke callbacks */
        }
        // 存在空依赖 => 回到 EMPTY
        if (m_empty_dep_count > 0 && m_current_state != State::EMPTY) { /* -> EMPTY */ }
    }
}

void Resource::fileLoaded(Span<const u8> blob, bool success) {
    // 异步读取完成：校验、可选解压、交给派生类 load()
    if (!success) { /* 失败统计 + 状态推进 */ return; }
    const auto* header = (const CompiledResourceHeader*)blob.begin();
    if (header->flags & CompiledResourceHeader::COMPRESSED) {
        OutputMemoryStream tmp(m_resource_manager.m_allocator);
        tmp.resize(header->decompressed_size);
        Engine& engine = m_resource_manager.getOwner().getEngine();
        if (!engine.decompress(/*...*/) || !load(tmp)) ++m_failed_dep_count;
    } else {
        if (!load(blob.fromLeft(sizeof(*header)))) ++m_failed_dep_count;
    }
    --m_empty_dep_count; // 自身文件已完成
    checkState();
}
```

---

## 资源管理器与 Hub

### ResourceManager（每种资源一个）

职责：
- 缓存并工厂化创建资源实例；
- 首次加载/增加引用；
- 移除无引用资源；
- 资源热重载。

```cpp
// src/engine/resource_manager.cpp
Resource* ResourceManager::load(const Path& path) {
    if (path.isEmpty()) return nullptr;
    Resource* res = get(path);
    if (!res) {
        res = createResource(path); // 派生工厂
        m_resources.insert(path.getHash(), res);
    }
    if (res->isEmpty() && res->m_desired_state == Resource::State::EMPTY) {
        // 编辑器 Hook：可能延迟真正加载以先行编译
        if (m_owner->onBeforeLoad(*res) == LoadHook::Action::DEFERRED) { /* 标记 hooked */ return res; }
        res->doLoad();
    }
    res->incRefCount();
    return res;
}
```

### ResourceManagerHub（统一路由/批量操作/Hook）

```cpp
// src/engine/resource_manager.h
struct ResourceManagerHub {
    struct LoadHook {
        enum class Action { IMMEDIATE, DEFERRED };
        virtual void loadRaw(const Path& requester, const Path& path) = 0; // 记录编译期依赖
        virtual Action onBeforeLoad(Resource& res) = 0;                     // 加载前拦截
        void continueLoad(Resource& res, bool success);                     // 编译后继续
    };

    Resource* load(ResourceType type, const Path& path);   // 定位管理器并加载
    void reloadAll(); // 批量热重载（先卸载后重载）
};
```

---

## 文件系统与异步加载

- 统一接口 `FileSystem`，可创建常规或打包版（`.pak`）文件系统。
- `getContent` 将请求入队，后台线程读入内存，主线程 `processCallbacks` 逐步分发完成事件给资源。

```cpp
// src/engine/file_system.h
struct FileSystem {
    using ContentCallback = Delegate<void(Span<const u8>, bool)>;
    struct AsyncHandle { /* 句柄与有效性判断 */ };
    virtual AsyncHandle getContent(const Path& file, const ContentCallback& cb) = 0;
    virtual void processCallbacks() = 0; // 主线程调用，发出回调
};
```

```cpp
// src/engine/file_system.cpp（简化）
struct FileSystemImpl : FileSystem {
    Array<AsyncItem> m_queue, m_finished;
    Semaphore m_semaphore; Mutex m_mutex; u32 m_work_counter = 0;

    AsyncHandle getContent(const Path& file, const ContentCallback& cb) override {
        MutexGuard lock(m_mutex); ++m_work_counter;
        AsyncItem& item = m_queue.emplace(m_allocator);
        item.path = file; item.callback = cb; m_semaphore.signal();
        return AsyncHandle(item.id);
    }

    void processCallbacks() override {
        // 每帧取已完成项并调用回调
        while (!m_finished.empty()) { AsyncItem item = /*pop*/; --m_work_counter; item.callback(/*...*/); }
    }
};

int FSTask::task() {
    for (;;) { m_fs.m_semaphore.wait(); /* 读取 -> 放入 m_finished */ }
}
```

> 引擎主循环每帧都会调用 `m_file_system->processCallbacks()`（见 `EngineImpl::update`），因此资源加载是异步发起、主线程完成。

---

## 编译资源管线与 LoadHook（编辑器）

编辑器在 Hub 上设置 `LoadHook`，在加载 `.res` 前检测源文件/元数据是否更新，必要时触发编译并延迟加载：

```cpp
// src/editor/asset_compiler.cpp（节选）
ResourceManagerHub::LoadHook::Action onBeforeLoad(Resource& res) {
    StringView filepath = ResourcePath::getResource(res.getPath());
    if (!fs.fileExists(dst_res) || dst_res 比 源/元数据旧) {
        if (!m_init_finished) { /* 初始扫描：延迟加载 */ return DEFERRED; }
        pushToCompileQueue(Path(filepath)); // 异步编译
        return DEFERRED;                    // 编译完成后再 continueLoad
    }
    return IMMEDIATE;
}

// 编译结果写入 .lumix/resources/<hash>.res
bool writeCompiledResource(const Path& src, Span<const u8> data) {
    CompiledResourceHeader hdr; // 可选 LZ4 压缩
    file.write(&hdr, sizeof(hdr)); file.write(data ...);
}

// 编译完成后：
//   - 正在加载中的资源：调用 load_hook.continueLoad(res, success)
//   - 已就绪/失败的资源：直接 rm.reload(*res)
```

---

## 资源生命周期与状态机

- 初始：`EMPTY`；首次 `load()` 会增加“空依赖计数”并异步读取自身文件；文件读完计数减一；
- 依赖：父资源通过 `addDependency` 观察依赖资源状态；依赖进入 EMPTY/FAILURE 时父资源相应增加空/失败计数；
- READY：当 `m_failed_dep_count == 0 && m_empty_dep_count == 0 && desired == READY` 时，调用 `onBeforeReady()` 后进入 READY；
- 卸载：`enableUnload(true)` 时，`decRefCount()` 降到 0 触发 `doUnload()`，清理 GPU/CPU 资源并重置计数；
- 热重载：`ResourceManager::reload(res)` 先 `doUnload()` 再 `doLoad()`；`reloadAll()` 批量对 READY 资源执行上述流程。

```cpp
// src/engine/resource.cpp（依赖变更回调）
void Resource::onStateChanged(State old_s, State new_s, Resource&) {
    if (old_s == State::EMPTY)   --m_empty_dep_count;
    if (old_s == State::FAILURE) --m_failed_dep_count;
    if (new_s == State::EMPTY)   ++m_empty_dep_count;
    if (new_s == State::FAILURE) ++m_failed_dep_count;
    checkState();
}
```

---

## 典型资源实现

### Texture（纹理）

- 类型常量：`const ResourceType Texture::TYPE("texture");`
- 解析编译纹理（LBC/RAW/BSU 可选），创建 GPU 纹理句柄，支持 SRGB、3D/Cubemap、Mips；
- 可选 CPU 数据驻留（编辑器需要时自动 reload 以拉取 CPU 数据）。

```cpp
// src/renderer/texture.cpp
bool Texture::load(Span<const u8> mem) {
    InputMemoryStream file(mem);
    char ext[4] = {}; file.read(ext, 3); file.read(&flags, sizeof(flags));
    bool loaded = false;
    if (equalIStrings(ext, "lbc"))      loaded = loadLBC(*this, data_ptr, size);
    else if (equalIStrings(ext, "raw")) loaded = loadRaw(*this, file, allocator);
    else { logWarning(getPath(), ": unknown extension", ext); }
    return loaded;
}

void Texture::unload() {
    if (handle) renderer.getEndFrameDrawStream().destroy(handle);
    handle = gpu::INVALID_TEXTURE; data.clear();
}
```

### Material（材质）

- 依赖 Shader 与多张 Texture，通过 `addDependency` 建立依赖；
- `onBeforeReady`：为未显式提供的纹理补默认纹理，按纹理槽 define 设置 `m_define_mask`，并构建常量缓冲（含 bindless 纹理句柄）。

```cpp
// src/renderer/material.cpp（节选）
void Material::onBeforeReady() {
    if (!m_shader) return;
    // 1) 为空槽填充 shader 默认纹理，并建立依赖
    // 2) 依据是否有纹理设置/清理 define 位
    // 3) RollingHasher 计算排序 key，更新渲染常量数据
    updateRenderData(true);
}
```

### Model（模型）

- 解析 mesh/骨骼/LOD，创建 VB/IB；依赖其材质资源（建立依赖，驱动状态机）；
- `onBeforeReady`：根据骨骼/skin 数据标注 RIGID/SKINNED；为 LOD mesh 记录渲染排序/索引区间。

```cpp
// src/renderer/model.cpp（节选）
bool Model::parseMeshes(InputMemoryStream& file, FileVersion) {
    // 读取每个 mesh 的顶点声明、材质路径 -> 加载 Material 并 addDependency
    // 读取索引/顶点数据 -> 创建 GPU buffer
}

void Model::unload() {
    // 释放所有 Mesh 的 GPU 资源，解除对材质的依赖与引用
}
```

---

## 扩展新资源类型的步骤

1) 定义派生类型：

```cpp
struct MyRes : Resource {
    static const ResourceType TYPE; // 例如 "myres"
    MyRes(const Path& p, ResourceManager& rm, IAllocator& a) : Resource(p, rm, a) {}
    ResourceType getType() const override { return TYPE; }
    bool load(Span<const u8> blob) override { /* 解析 blob，addDependency(...) */ return true; }
    void unload() override { /* 清理 CPU/GPU 资源 */ }
    void onBeforeReady() override { /* 可选：READY 前的补完 */ }
};
```

2) 定义对应管理器并注册到 Hub：

```cpp
struct MyResManager : ResourceManager {
    explicit MyResManager(IAllocator& a) : ResourceManager(a), m_allocator(a) {}
    Resource* createResource(const Path& path) override { return LUMIX_NEW(m_allocator, MyRes)(path, *this, m_allocator); }
    void destroyResource(Resource& r) override { LUMIX_DELETE(m_allocator, &static_cast<MyRes&>(r)); }
    IAllocator& m_allocator;
};

// 初始化阶段：
rmhub.add(MyRes::TYPE, &my_res_manager);
```

3) 如需从源文件编译 `.res`，在编辑器侧实现 `AssetCompiler::IPlugin`：
- 解析源文件，产出目标二进制布局；
- 通过 `writeCompiledResource` 写入 `.res`（携带 `CompiledResourceHeader`，可选压缩）；
- 在编译时若读取其它文件，通过 `ResourceManagerHub::loadRaw(included_from, path)` 登记依赖，变更时自动反编译/重载。

---

## 热重载与卸载策略

- 批量重载：`ResourceManagerHub::reloadAll()` 将 READY 资源全部卸载，再逐一加载，确保一致性。
- 编辑器切换游戏模式时会切换卸载策略，避免场景销毁/重建期间资源频繁抖动：
  - 进入停止游戏：`enableUnload(false)`；
  - 完成切换：`enableUnload(true)`。

---

## 典型加载时序图（简化）

```
Hub.load(type,path)
  -> ResourceManager::load
     -> (Hook? compile DEFERRED :) Resource::doLoad
         -> FileSystem.getContent(.lumix/resources/<hash>.res)
[Worker 线程读取完成]
Engine.update -> FileSystem.processCallbacks
  -> Resource::fileLoaded
     -> (可选解压) Resource::load(blob)
     -> addDependency(...) （如材质依赖纹理）
     -> checkState -> onBeforeReady -> READY
```

---

## 最佳实践与注意事项

- 解析失败时返回 false 即计为失败依赖，父资源会进入 FAILURE；日志中应输出路径与原因。
- 使用其它资源时务必 `addDependency`，并在 `unload` 中解除依赖与 `decRefCount`。
- GPU 句柄/CPU 缓冲应在 `unload` 中妥善销毁/清空，防止泄漏与脏状态。
- `refresh()` 用于资源在不重新读取文件的情况下触发“再就绪”（如仅渲染常量变更）。
- 编辑器 Hook 仅在 editor 模式下启用；运行时（打包）直接读取 `.res` 或 `.pak`。

---

## 代码索引（便于查阅）

- 资源抽象与状态机：`src/engine/resource.h`, `src/engine/resource.cpp`
- 资源管理器与 Hub：`src/engine/resource_manager.h`, `src/engine/resource_manager.cpp`
- 文件系统与异步加载：`src/engine/file_system.h`, `src/engine/file_system.cpp`
- 编译器与加载钩子：`src/editor/asset_compiler.cpp`
- 典型资源：
  - `src/renderer/texture.h`, `src/renderer/texture.cpp`
  - `src/renderer/material.h`, `src/renderer/material.cpp`
  - `src/renderer/model.h`, `src/renderer/model.cpp`

如需进一步深入（例如 Shader/PhysicsMaterial 解析格式、`.res` 二进制布局约定），可在上述文件基础上逐函数阅读；也欢迎提出具体模块，我可以生成更细粒度的读取流程与数据结构文档。

