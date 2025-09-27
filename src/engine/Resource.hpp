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
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cctype>

namespace Tina::Engine {

// 轻量 Path 封装（便于后续替换为更完整实现）
struct Path {
    std::string str;
    Path() = default;
    explicit Path(const char* s) : str(s ? s : "") {}
    explicit Path(std::string s) : str(std::move(s)) {}
    bool isEmpty() const { return str.empty(); }
    const char* c_str() const { return str.c_str(); }
    std::size_t hash() const { return (std::size_t)Tina::Core::Hash::String64(str); }
};

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
    using Content = std::vector<Tina::u8>;
    using ContentCallback = std::function<void(const Content&, bool)>; // 主线程回调
    struct AsyncHandle { Tina::u64 id = 0; bool valid() const { return id != 0; } };
    virtual ~FileSystem() = default;
    virtual AsyncHandle getContent(const Path& file, ContentCallback cb) = 0;
    virtual void processCallbacks() = 0; // 每帧驱动
};

// 工厂：由实现文件提供
std::unique_ptr<FileSystem> CreateFileSystem();

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
        auto it = m_resources.find(path.str);
        if (it != m_resources.end()) { it->second->incRefCount(); return it->second.get(); }
        std::unique_ptr<Resource> res(createResource(path));
        Resource* out = res.get();
        m_resources.emplace(path.str, std::move(res));
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

    // 每帧驱动文件系统回调
    void update() { m_fs.processCallbacks(); }

    virtual Resource* createResource(const Path& path) = 0;

protected:
    FileSystem& m_fs;
    std::unordered_map<std::string, std::unique_ptr<Resource>> m_resources;
};

// Hub：将类型映射到具体管理器
class ResourceManagerHub {
public:
    void add(ResourceType t, ResourceManager* rm) { m_rms[t.type] = rm; }
    Resource* load(ResourceType t, const Path& p) {
        auto it = m_rms.find(t.type);
        return it == m_rms.end() ? nullptr : it->second->load(p);
    }
    void reloadAll() { for (auto& kv : m_rms) kv.second->reloadAll(); }
    void update() { for (auto& kv : m_rms) kv.second->update(); }
private:
    std::unordered_map<Tina::u64, ResourceManager*> m_rms;
};

// 示例资源：Blob（原样字节），便于快速验证系统
struct BlobResource : Resource {
    static inline const ResourceType TYPE{"blob"};
    using Resource::Resource;
    ResourceType getType() const override { return TYPE; }
    bool load(const FileSystem::Content& blob) override { data = blob; return true; }
    void unload() override { data.clear(); }
    std::vector<Tina::u8> data;
};

// Blob 管理器
struct BlobManager : ResourceManager {
    using ResourceManager::ResourceManager;
    Resource* createResource(const Path& path) override { return new BlobResource(path); }
};

} // namespace Tina::Engine
