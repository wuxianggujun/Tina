#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <chrono>
#include "tina/utils/Profiler.hpp"

namespace Tina {

class FileWatcher {
public:
    using Callback = std::function<void()>;

    static FileWatcher& getInstance() {
        static FileWatcher instance;
        return instance;
    }

    // 添加文件监视
    void addWatch(const std::string& path, Callback callback) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        auto fullPath = std::filesystem::absolute(path).string();
        auto& info = m_WatchedFiles[fullPath];
        info.callback = std::move(callback);
        info.lastWriteTime = std::filesystem::last_write_time(fullPath);
    }

    // 移除文件监视
    void removeWatch(const std::string& path) {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        auto fullPath = std::filesystem::absolute(path).string();
        m_WatchedFiles.erase(fullPath);
    }

    // 启动监视线程
    void start() {
        TINA_PROFILE_FUNCTION();
        if (m_WatchThread.joinable()) return;
        
        m_Running = true;
        m_WatchThread = std::thread([this]() {
            TINA_PROFILE_FUNCTION();
            while (m_Running) {
                checkFiles();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    // 停止监视线程
    void stop() {
        TINA_PROFILE_FUNCTION();
        if (!m_WatchThread.joinable()) return;
        
        m_Running = false;
        m_WatchThread.join();
    }

private:
    struct FileInfo {
        Callback callback;
        std::filesystem::file_time_type lastWriteTime;
    };

    void checkFiles() {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        for (auto& [path, info] : m_WatchedFiles) {
            try {
                auto currentWriteTime = std::filesystem::last_write_time(path);
                if (currentWriteTime != info.lastWriteTime) {
                    info.lastWriteTime = currentWriteTime;
                    if (info.callback) {
                        info.callback();
                    }
                }
            }
            catch (const std::filesystem::filesystem_error& e) {
                // 文件可能被删除或移动
                continue;
            }
        }
    }

private:
    FileWatcher() = default;
    ~FileWatcher() {
        stop();
    }

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

private:
    std::unordered_map<std::string, FileInfo> m_WatchedFiles;
    std::mutex m_Mutex;
    std::thread m_WatchThread;
    std::atomic<bool> m_Running{false};
};

} // namespace Tina 