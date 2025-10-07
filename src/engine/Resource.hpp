//
// 简化版资源系统（对齐 docs/resource_system.md 思想）
// - 统一 Resource 抽象：状态机 EMPTY/READY/FAILURE，引用计数
// - ResourceManager：按路径缓存、创建与销毁资源
// - ResourceManagerHub：按类型路由到对应管理器
// - FileSystem：异步读取到内存，主循环 processCallbacks 分发
//

#pragma once

#include "../core/Core.hpp"
#include "../core/Log.hpp"
#include "../core/Hash.hpp"
#include "../core/Path.hpp"
#include <functional>
#include <cctype>
#include <filesystem>
#include "../core/Container.hpp"

namespace Tina::Engine {

using Path = Tina::Core::Path;

// 资源类型（小写字符串哈希），用于路由到对应管理器
struct ResourceType {
    Tina::u64 type = 0; // 0 无效
    ResourceType() = default;
    explicit ResourceType(const char* name) {
        if (!name) { type = 0; return; }
        type = Tina::Core::Hash::StringLower64(name);
    }
    bool valid() const { return type != 0; }
    bool operator==(const ResourceType& rhs) const { return type == rhs.type; }
};

// 异步文件系统接口
struct FileSystem {
    using Content = Tina::Container::Vector<Tina::u8>;
    using ContentCallback = std::function<void(const Content&, bool)>; // 主线程回调
    struct AsyncHandle { Tina::u64 id = 0; bool valid() const { return id != 0; } };
    virtual ~FileSystem() = default;
    virtual AsyncHandle getContent(const Path& file, ContentCallback cb) = 0;
    virtual void processCallbacks() = 0; // 每帧驱动
};

// 工厂：由实现文件提供（统一 EASTL 智能指针）
Tina::Memory::UniquePtr<FileSystem> CreateFileSystem();

// 资源抽象
class Resource {
public:
    enum class State : Tina::u32 { EMPTY = 0, READY, FAILURE };
    explicit Resource(Path p) : m_path(std::move(p)) {}
    virtual ~Resource() = default;

    virtual ResourceType getType() const = 0;
    const Path& getPath() const { return m_path; }

    // 状态与引用
    State getState() const { return m_state; }
    Tina::u32 incRefCount() { return ++m_refcount; }
    Tina::u32 decRefCount() { return m_refcount > 0 ? --m_refcount : 0; }

    // 发起加载（由管理器调用），文件结果在主线程回调中推进状态
    void requestLoad(FileSystem& fs) {
        if (m_state != State::EMPTY) return;
        m_loading = true;
        m_handle = fs.getContent(m_path, [this](const FileSystem::Content& data, bool ok){
            if (!ok) { fail(); return; }
            if (!load(data)) { fail(); return; }
            m_state = State::READY;
        });
    }

    // 资源卸载
    void unloadNow() {
        if (m_state == State::EMPTY) return;
        unload();
        m_state = State::EMPTY;
    }

protected:
    virtual bool load(const FileSystem::Content& blob) = 0; // 解析数据
    virtual void unload() = 0;                               // 释放资源

    void fail() { m_state = State::FAILURE; }

    Path m_path;
    State m_state = State::EMPTY;
    bool m_loading = false;
    FileSystem::AsyncHandle m_handle{};
    Tina::u32 m_refcount = 0;
};

// 资源管理器基类
class ResourceManager {
public:
    explicit ResourceManager(FileSystem& fs) : m_fs(fs) {}
    virtual ~ResourceManager() = default;

    Resource* load(const Path& path) {
        if (path.isEmpty()) return nullptr;
        Tina::Container::String key(path.c_str());
        auto it = m_resources.find(key);
        if (it != m_resources.end()) { it->second->incRefCount(); return it->second.get(); }
        Tina::Memory::UniquePtr<Resource> res(createResource(path));
        Resource* out = res.get();
        m_resources.emplace(Tina::Container::String(path.c_str()), std::move(res));
        out->incRefCount();
        out->requestLoad(m_fs);
        return out;
    }

    void unload(Resource& r) {
        if (r.decRefCount() == 0) r.unloadNow();
    }

    void reloadAll() {
        for (auto& kv : m_resources) {
            kv.second->unloadNow();
            kv.second->requestLoad(m_fs);
        }
    }

    // 按路径增量热重载（若存在）
    void reload(const Path& path) {
        Tina::Container::String key(path.c_str());
        auto it = m_resources.find(key);
        if (it != m_resources.end()) {
            it->second->unloadNow();
            it->second->requestLoad(m_fs);
        }
    }

    // 每帧驱动文件系统回调
    void update() {
        m_fs.processCallbacks();
        // 基础文件监视：检测文件 mtime 变化后自动重载（小项目足够）
        if (m_enableWatch) {
            watchAndReloadChanged();
        }
    }

    virtual Resource* createResource(const Path& path) = 0;

protected:
    FileSystem& m_fs;
    Tina::Container::HashMap<Tina::Container::String, Tina::Memory::UniquePtr<Resource>> m_resources;

private:
    void watchAndReloadChanged() {
        namespace fs = std::filesystem;
        for (auto& kv : m_resources) {
            const char* cpath = kv.first.c_str();
            std::error_code ec;
            auto cur = fs::last_write_time(cpath, ec);
            if (ec) continue;
            auto it = m_mtime.find(kv.first);
            if (it == m_mtime.end()) {
                m_mtime.emplace(kv.first, cur);
                continue;
            }
            if (cur != it->second) {
                it->second = cur;
                kv.second->unloadNow();
                kv.second->requestLoad(m_fs);
                TINA_INFO("资源热重载: {}", cpath);
            }
        }
    }

    bool m_enableWatch = true;
    Tina::Container::HashMap<Tina::Container::String, std::filesystem::file_time_type> m_mtime;
};

// Hub：将类型映射到具体管理器
class ResourceManagerHub {
public:
    void add(ResourceType t, ResourceManager* rm) { m_rms[t.type] = rm; }
    Resource* load(ResourceType t, const Path& p) {
        auto it = m_rms.find(t.type);
        return it == m_rms.end() ? nullptr : it->second->load(p);
    }
    template<typename T>
    T* load(const Path& p) {
        return static_cast<T*>(load(T::TYPE, p));
    }
    void unload(Resource& r) {
        auto it = m_rms.find(r.getType().type);
        if (it != m_rms.end()) it->second->unload(r);
    }
    void reload(const Path& p) {
        for (auto& kv : m_rms) kv.second->reload(p);
    }
    void reloadAll() { for (auto& kv : m_rms) kv.second->reloadAll(); }
    void update() { for (auto& kv : m_rms) kv.second->update(); }
private:
    Tina::Container::HashMap<Tina::u64, ResourceManager*> m_rms;
};

// 资源安全句柄：RAII 持有引用计数，离开作用域自动释放引用
template<typename T>
class ResourceRef {
public:
    ResourceRef() = default;
    ResourceRef(ResourceManagerHub* hub, T* ptr) : m_hub(hub), m_ptr(ptr) {}
    ~ResourceRef() { reset(); }

    ResourceRef(const ResourceRef&) = delete;
    ResourceRef& operator=(const ResourceRef&) = delete;

    ResourceRef(ResourceRef&& other) noexcept { m_hub = other.m_hub; m_ptr = other.m_ptr; other.m_ptr = nullptr; other.m_hub = nullptr; }
    ResourceRef& operator=(ResourceRef&& other) noexcept {
        if (this != &other) { reset(); m_hub = other.m_hub; m_ptr = other.m_ptr; other.m_ptr = nullptr; other.m_hub = nullptr; }
        return *this; }

    void reset() {
        if (m_ptr && m_hub) { m_hub->unload(*m_ptr); }
        m_ptr = nullptr; m_hub = nullptr;
    }

    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

private:
    ResourceManagerHub* m_hub = nullptr;
    T* m_ptr = nullptr;
};

// 示例资源：Blob（原样字节），便于快速验证系统
struct BlobResource : Resource {
    static inline const ResourceType TYPE{"blob"};
    using Resource::Resource;
    ResourceType getType() const override { return TYPE; }
    bool load(const FileSystem::Content& blob) override { data = blob; return true; }
    void unload() override { data.clear(); }
    Tina::Container::Vector<Tina::u8> data;
};

// Blob 管理器
struct BlobManager : ResourceManager {
    using ResourceManager::ResourceManager;
    Resource* createResource(const Path& path) override { return new BlobResource(path); }
};

} // namespace Tina::Engine
