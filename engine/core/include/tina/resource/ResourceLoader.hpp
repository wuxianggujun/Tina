#pragma once

#include "tina/core/Core.hpp"
#include "tina/resource/Resource.hpp"
#include <functional>
#include <future>

namespace Tina {

/**
 * @brief 资源加载进度回调函数类型
 * @param progress 加载进度(0-1)
 * @param message 进度消息
 */
using ResourceLoadProgressCallback = std::function<void(float progress, const std::string& message)>;

/**
 * @brief 资源加载器接口
 * 
 * 定义了资源加载器的基本接口。
 * 每种资源类型都需要实现自己的加载器。
 */
class TINA_CORE_API IResourceLoader {
public:
    virtual ~IResourceLoader() = default;

    /**
     * @brief 同步加载资源
     * @param resource 要加载的资源
     * @param progressCallback 进度回调函数
     * @return 是否加载成功
     */
    virtual bool loadSync(Resource* resource, 
        const ResourceLoadProgressCallback& progressCallback = nullptr) = 0;

    /**
     * @brief 异步加载资源
     * @param resource 要加载的资源
     * @param progressCallback 进度回调函数
     * @return 加载操作的future
     */
    virtual std::future<bool> loadAsync(Resource* resource,
        const ResourceLoadProgressCallback& progressCallback = nullptr) = 0;

    /**
     * @brief 卸载资源
     * @param resource 要卸载的资源
     */
    virtual void unload(Resource* resource) = 0;

    /**
     * @brief 验证资源
     * @param resource 要验证的资源
     * @return 资源是否有效
     */
    virtual bool validate(Resource* resource) = 0;

    /**
     * @brief 获取资源加载器支持的资源类型ID
     * @return 资源类型ID
     */
    virtual ResourceTypeID getResourceTypeID() const = 0;

protected:
    /**
     * @brief 更新资源状态
     * @param resource 资源
     * @param state 新状态
     */
    void updateResourceState(Resource* resource, ResourceState state) {
        if (resource) {
            resource->m_state = state;
        }
    }

    /**
     * @brief 更新资源大小
     * @param resource 资源
     * @param size 新大小
     */
    void updateResourceSize(Resource* resource, size_t size) {
        if (resource) {
            resource->m_size = size;
        }
    }

    /**
     * @brief 更新资源修改时间
     * @param resource 资源
     * @param time 新时间
     */
    void updateResourceModifiedTime(Resource* resource, 
        const std::chrono::system_clock::time_point& time) {
        if (resource) {
            resource->m_lastModified = time;
        }
    }
};

/**
 * @brief 资源加载器注册宏
 */
#define TINA_REGISTER_RESOURCE_LOADER(LoaderName, ResourceType) \
    ResourceTypeID getResourceTypeID() const override { \
        return ResourceType::getStaticTypeID(); \
    }

} // namespace Tina 