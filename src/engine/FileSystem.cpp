// 简化异步文件系统实现：后台线程读取，主线程分发回调

#include "Resource.hpp"
#include "../core/Log.hpp"
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
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
        AsyncHandle h{ ++m_next_id };
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_queue.push(AsyncItem{h, file, std::move(cb)});
        }
        m_cv.notify_one();
        return h;
    }

    void processCallbacks() override {
        Tina::Container::Queue<FinishedItem> local;
        {
            std::lock_guard<std::mutex> lk(m_finished_mtx);
            // eastl::queue 不支持 std::swap 完整容器语义，改为逐个移动
            while (!m_finished.empty()) { local.push(std::move(m_finished.front())); m_finished.pop(); }
        }
        while (!local.empty()) {
            FinishedItem it = std::move(local.front()); local.pop();
            if (it.cb) it.cb(it.data, it.ok);
        }
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
            FinishedItem fin{}; fin.handle = job.handle; fin.cb = job.cb; fin.ok = false;
            // 简单读取文件到内存
            std::ifstream ifs(job.path.c_str(), std::ios::binary);
            if (ifs) {
                ifs.seekg(0, std::ios::end); std::streamsize sz = ifs.tellg(); ifs.seekg(0);
                if (sz > 0) {
                    fin.data.resize((size_t)sz);
                    ifs.read((char*)fin.data.data(), sz);
                    fin.ok = ifs.good() || ifs.eof();
                } else { fin.ok = true; }
            }
            {
                std::lock_guard<std::mutex> lk(m_finished_mtx);
                m_finished.push(std::move(fin));
            }
        }
    }

    std::thread m_worker;
    std::mutex m_mtx; std::condition_variable m_cv; Tina::Container::Queue<AsyncItem> m_queue; bool m_quit = false;
    std::mutex m_finished_mtx; Tina::Container::Queue<FinishedItem> m_finished;
    Tina::u64 m_next_id = 10;
};

// 工厂函数（暴露给主程序使用）
Tina::Container::UniquePtr<FileSystem> CreateFileSystem() {
    return Tina::Container::MakeUnique<FileSystemImpl>();
}

} // namespace Tina::Engine
