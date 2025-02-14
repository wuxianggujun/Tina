#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "tina/utils/Profiler.hpp"
#include <glm/glm.hpp>
#include "tina/renderer/Color.hpp"

namespace Tina
{
    class BatchRenderer2D;

    // 渲染命令基类
    class RenderCommand
    {
    public:
        virtual ~RenderCommand() = default;
        virtual void execute() = 0;
    };

    // 渲染命令队列
    class RenderCommandQueue
    {
    public:
        static RenderCommandQueue& getInstance();

        void submit(std::unique_ptr<RenderCommand> command);
        void executeAll();
        void startRenderThread();
        void stopRenderThread();
        void setBatchRenderer(BatchRenderer2D* renderer);
        BatchRenderer2D* getBatchRenderer() const;

    private:
        RenderCommandQueue() = default;
        ~RenderCommandQueue();

        RenderCommandQueue(const RenderCommandQueue&) = delete;
        RenderCommandQueue& operator=(const RenderCommandQueue&) = delete;

    private:
        std::queue<std::unique_ptr<RenderCommand>> m_CommandQueue;
        std::mutex m_QueueMutex;
        std::condition_variable m_CondVar;
        std::thread m_RenderThread;
        bool m_Running = false;
        BatchRenderer2D* m_BatchRenderer = nullptr;
    };

    // 具体渲染命令示例
    class DrawQuadCommand : public RenderCommand
    {
    public:
        DrawQuadCommand(const glm::vec2& position, const glm::vec2& size, const Color& color);
        void execute() override;

    private:
        glm::vec2 m_Position;
        glm::vec2 m_Size;
        Color m_Color;
    };

    // 便捷的命令提交函数
    template <typename T, typename... Args>
    void submitRenderCommand(Args&&... args)
    {
        RenderCommandQueue::getInstance().submit(
            std::make_unique<T>(std::forward<Args>(args)...)
        );
    }
} // namespace Tina
