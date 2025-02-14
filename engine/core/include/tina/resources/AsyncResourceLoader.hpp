#pragma once

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <future>
#include <unordered_map>
#include "tina/utils/Profiler.hpp"

namespace Tina {

// 加载状态
enum class LoadState {
    Pending,
    Loading,
    Completed,
    Failed
};

// 加载任务
struct LoadTask {
    std::string path;
    std::function<void(std::shared_ptr<class Resource>)> callback;
    std::function<std::shared_ptr<Resource>()> loader;
    LoadState state = LoadState::Pending;
    float progress = 0.0f;
    std::string error;
};

class AsyncResourceLoader {
public:
    static AsyncResourceLoader& getInstance() {
        static AsyncResourceLoader instance;
        return instance;
    }

    // 异步加载资源
    template<typename T>
    std::future<std::shared_ptr<T>> loadAsync(const std::string& path,
                                            std::function<void(std::shared_ptr<T>)> callback = nullptr) {
        TINA_PROFILE_FUNCTION();
        
        auto promise = std::make_shared<std::promise<std::shared_ptr<T>>>();
        auto future = promise->get_future();

        auto task = std::make_shared<LoadTask>();
        task->path = path;
        task->state = LoadState::Pending;
        
        // 设置加载完成回调
        if (callback) {
            task->callback = [callback](std::shared_ptr<Resource> resource) {
                if (auto typedResource = std::dynamic_pointer_cast<T>(resource)) {
                    callback(typedResource);
                }
            };
        }

        // 设置加载函数
        task->loader = [this, path, promise]() {
            try {
                auto resource = ResourceManager::getInstance().load<T>(path);
                promise->set_value(resource);
                return std::static_pointer_cast<Resource>(resource);
            }
            catch (const std::exception& e) {
                promise->set_exception(std::current_exception());
                return std::shared_ptr<Resource>();
            }
        };

        // 添加到加载队列
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Tasks[path] = task;
            m_TaskQueue.push(task);
        }
        m_CondVar.notify_one();

        return future;
    }

    // 获取加载状态
    LoadState getLoadState(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Tasks.find(path);
        return it != m_Tasks.end() ? it->second->state : LoadState::Failed;
    }

    // 获取加载进度
    float getProgress(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Tasks.find(path);
        return it != m_Tasks.end() ? it->second->progress : 0.0f;
    }

    // 获取错误信息
    std::string getError(const std::string& path) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Tasks.find(path);
        return it != m_Tasks.end() ? it->second->error : "Resource not found";
    }

    // 启动加载线程
    void start(size_t threadCount = 1) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        if (!m_Workers.empty()) return;
        
        m_Running = true;
        for (size_t i = 0; i < threadCount; ++i) {
            m_Workers.emplace_back([this]() { workerThread(); });
        }
    }

    // 停止加载线程
    void stop() {
        TINA_PROFILE_FUNCTION();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Running = false;
        }
        m_CondVar.notify_all();
        
        for (auto& worker : m_Workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_Workers.clear();
    }

    // 更新加载状态
    void update() {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        // 清理已完成的任务
        for (auto it = m_Tasks.begin(); it != m_Tasks.end();) {
            if (it->second->state == LoadState::Completed ||
                it->second->state == LoadState::Failed) {
                it = m_Tasks.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    AsyncResourceLoader() {
        start();
    }
    
    ~AsyncResourceLoader() {
        stop();
    }

    AsyncResourceLoader(const AsyncResourceLoader&) = delete;
    AsyncResourceLoader& operator=(const AsyncResourceLoader&) = delete;

    void workerThread() {
        TINA_PROFILE_FUNCTION();
        while (true) {
            std::shared_ptr<LoadTask> task;
            
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CondVar.wait(lock, [this]() {
                    return !m_Running || !m_TaskQueue.empty();
                });

                if (!m_Running && m_TaskQueue.empty()) {
                    break;
                }

                if (!m_TaskQueue.empty()) {
                    task = m_TaskQueue.front();
                    m_TaskQueue.pop();
                    task->state = LoadState::Loading;
                }
            }

            if (task) {
                try {
                    auto resource = task->loader();
                    
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    if (resource) {
                        task->state = LoadState::Completed;
                        task->progress = 1.0f;
                        if (task->callback) {
                            task->callback(resource);
                        }
                    } else {
                        task->state = LoadState::Failed;
                        task->error = "Failed to load resource";
                    }
                }
                catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    task->state = LoadState::Failed;
                    task->error = e.what();
                }
            }
        }
    }

private:
    std::queue<std::shared_ptr<LoadTask>> m_TaskQueue;
    std::unordered_map<std::string, std::shared_ptr<LoadTask>> m_Tasks;
    std::vector<std::thread> m_Workers;
    mutable std::mutex m_Mutex;
    std::condition_variable m_CondVar;
    bool m_Running = false;
};

// 便捷函数
template<typename T>
inline std::future<std::shared_ptr<T>> loadResourceAsync(const std::string& path,
                                                        std::function<void(std::shared_ptr<T>)> callback = nullptr) {
    return AsyncResourceLoader::getInstance().loadAsync<T>(path, callback);
}

} // namespace Tina 