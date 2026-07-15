// 简化异步文件系统实现：后台线程读取，主线程分发回调

#include "Resource.hpp"
#include "../core/Log.hpp"
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "../core/Container.hpp"
#include <fstream>

namespace Tina::Engine {

struct FileSystemImpl : FileSystem {
    struct AsyncItem {
        AsyncHandle handle; Path path; ContentCallback cb;
    };
    struct FinishedItem { AsyncHandle handle; Content data; bool ok; ContentCallback cb; };

    FileSystemImpl() { m_worker = std::thread([this]{ this->threadMain(); }); }
    ~FileSystemImpl() override {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_quit = true;
        }
        m_cv.notify_one();
        if (m_worker.joinable()) m_worker.join();
    }

    AsyncHandle getContent(const Path& file, ContentCallback cb) override {
        AsyncHandle h{ m_nextId.fetch_add(1, std::memory_order_relaxed) + 1 };
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_queue.push(AsyncItem{h, file, std::move(cb)});
        }
        m_cv.notify_one();
        return h;
    }

    void cancel(AsyncHandle handle) override {
        if (!handle.valid()) return;
        std::lock_guard<std::mutex> lock(m_cancelledMutex);
        m_cancelled.emplace(handle.id);
    }

    size_t processCallbacks(size_t maxCallbacks) override {
        size_t processed = 0;
        while (maxCallbacks == 0 || processed < maxCallbacks) {
            FinishedItem it{};
            {
                std::lock_guard<std::mutex> lk(m_finished_mtx);
                if (m_finished.empty()) break;
                it = std::move(m_finished.front());
                m_finished.pop();
            }

            if (consumeCancellation(it.handle)) continue;
            if (it.cb) it.cb(it.data, it.ok);
            ++processed;
        }
        return processed;
    }

    void threadMain() {
        for (;;) {
            AsyncItem job;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [&]{ return m_quit || !m_queue.empty(); });
                if (m_quit && m_queue.empty()) break;
                job = std::move(m_queue.front()); m_queue.pop();
            }
            if (consumeCancellation(job.handle)) continue;

            FinishedItem fin{}; fin.handle = job.handle; fin.cb = job.cb; fin.ok = false;
            // std::filesystem::u8path 在 Windows 上显式将 UTF-8 转换为宽路径。
            const auto nativePath = std::filesystem::u8path(job.path.c_str());
            std::ifstream ifs(nativePath, std::ios::binary);
            if (ifs) {
                ifs.seekg(0, std::ios::end); std::streamsize sz = ifs.tellg(); ifs.seekg(0);
                if (sz > 0) {
                    fin.data.resize((size_t)sz);
                    ifs.read((char*)fin.data.data(), sz);
                    fin.ok = ifs.good() || ifs.eof();
                } else { fin.ok = true; }
            }
            if (consumeCancellation(job.handle)) continue;
            {
                std::lock_guard<std::mutex> lk(m_finished_mtx);
                m_finished.push(std::move(fin));
            }
        }
    }

    bool consumeCancellation(AsyncHandle handle) {
        std::lock_guard<std::mutex> lock(m_cancelledMutex);
        auto it = m_cancelled.find(handle.id);
        if (it == m_cancelled.end()) return false;
        m_cancelled.erase(it);
        return true;
    }

    std::thread m_worker;
    std::mutex m_mtx; std::condition_variable m_cv; Tina::Container::Queue<AsyncItem> m_queue; bool m_quit = false;
    std::mutex m_finished_mtx; Tina::Container::Queue<FinishedItem> m_finished;
    std::mutex m_cancelledMutex; Tina::Container::HashSet<Tina::u64> m_cancelled;
    std::atomic<Tina::u64> m_nextId{10};
};

// 工厂函数（暴露给主程序使用）
Tina::Memory::UniquePtr<FileSystem> CreateFileSystem() {
    return Tina::Memory::MakeUnique<FileSystemImpl>();
}

} // namespace Tina::Engine
