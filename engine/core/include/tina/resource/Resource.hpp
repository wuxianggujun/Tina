#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/RefCount.hpp"
#include "tina/core/RefPtr.hpp"
#include <string>
#include <chrono>

namespace Tina {

/**
 * @brief 资源状态枚举
 */
enum class ResourceState {
    Unloaded,   // 未加载
    Loading,    // 加载中
    Loaded,     // 已加载
    Failed,     // 加载失败
    Unloading   // 卸载中
};

/**
 * @brief 资源类型ID
 */
using ResourceTypeID = uintptr_t;

/**
 * @brief 资源类型注册宏
 */
#define TINA_REGISTER_RESOURCE_TYPE(ResourceType) \
    static ResourceTypeID getStaticTypeID() { \
        static ResourceTypeID typeID = reinterpret_cast<ResourceTypeID>(&getStaticTypeID); \
        return typeID; \
    } \
    ResourceTypeID getTypeID() const override { \
        return getStaticTypeID(); \
    }

/**
 * @brief 资源基类
 * 
 * 所有资源类型都必须继承自此类。
 * 提供基本的资源管理功能，包括：
 * - 资源状态管理
 * - 资源元数据
 * - 资源生命周期管理
 * - 资源依赖管理
 */
class TINA_CORE_API Resource {
public:
    /**
     * @brief 构造函数
     * @param name 资源名称
     * @param path 资源路径
     */
    Resource(const std::string& name, const std::string& path);
    
    /**
     * @brief 虚析构函数
     */
    virtual ~Resource();

    /**
     * @brief 获取资源名称
     * @return 资源名称
     */
    const std::string& getName() const;

    /**
     * @brief 获取资源路径
     * @return 资源路径
     */
    const std::string& getPath() const;

    /**
     * @brief 获取资源状态
     * @return 资源状态
     */
    ResourceState getState() const;

    /**
     * @brief 获取资源大小
     * @return 资源大小(字节)
     */
    size_t getSize() const;

    /**
     * @brief 获取最后修改时间
     * @return 最后修改时间
     */
    std::chrono::system_clock::time_point getLastModified() const;

    /**
     * @brief 检查资源是否已加载
     * @return 如果资源已加载则返回true
     */
    virtual bool isLoaded() const;

    /**
     * @brief 获取资源类型ID
     * @return 资源类型ID
     */
    virtual ResourceTypeID getTypeID() const = 0;

    /**
     * @brief 获取引用计数
     * @return 当前引用计数
     */
    int getRefCount() const;

    /**
     * @brief 增加引用计数
     */
    void addRef();

    /**
     * @brief 减少引用计数
     */
    void release();

protected:
    /**
     * @brief 加载资源
     * @return 如果加载成功则返回true
     */
    virtual bool load() = 0;

    /**
     * @brief 卸载资源
     */
    virtual void unload() = 0;

    /**
     * @brief 重新加载资源
     * @return 是否重新加载成功
     */
    bool reload();

    // 资源基本信息
    std::string m_name;                                  // 资源名称
    std::string m_path;                                  // 资源路径
    ResourceState m_state;                               // 资源状态
    size_t m_size;                                      // 资源大小
    std::chrono::system_clock::time_point m_lastModified; // 最后修改时间

    RefCount m_refCount;    ///< 引用计数

    friend class IResourceLoader;  // 允许资源加载器访问protected成员
    friend class ResourceManager;  // 允许资源管理器访问protected成员
    template<typename T> friend class RefPtr;
};

} // namespace Tina 