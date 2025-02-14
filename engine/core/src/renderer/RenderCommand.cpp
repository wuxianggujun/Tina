#include "tina/renderer/RenderCommand.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"

namespace Tina {

RenderCommandQueue& RenderCommandQueue::getInstance() {
    static RenderCommandQueue instance;
    return instance;
}

void RenderCommandQueue::submit(std::unique_ptr<RenderCommand> command) {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    m_CommandQueue.push(std::move(command));
    m_CondVar.notify_one();
}

void RenderCommandQueue::executeAll() {
    TINA_PROFILE_FUNCTION();
    std::queue<std::unique_ptr<RenderCommand>> commands;

    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        std::swap(commands, m_CommandQueue);
    }

    while (!commands.empty()) {
        TINA_PROFILE_SCOPE("Execute Render Command");
        commands.front()->execute();
        commands.pop();
    }
}

void RenderCommandQueue::startRenderThread() {
    if (m_RenderThread.joinable()) return;

    m_Running = true;
    m_RenderThread = std::thread([this]() {
        TINA_PROFILE_FUNCTION();
        while (m_Running) {
            std::unique_lock<std::mutex> lock(m_QueueMutex);
            m_CondVar.wait(lock, [this]() {
                return !m_CommandQueue.empty() || !m_Running;
            });

            if (!m_Running) break;

            while (!m_CommandQueue.empty()) {
                TINA_PROFILE_SCOPE("Execute Render Command");
                auto command = std::move(m_CommandQueue.front());
                m_CommandQueue.pop();
                lock.unlock();

                command->execute();

                lock.lock();
            }
        }
    });
}

void RenderCommandQueue::stopRenderThread() {
    if (!m_RenderThread.joinable()) return;

    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_Running = false;
    }

    m_CondVar.notify_one();
    m_RenderThread.join();
}

void RenderCommandQueue::setBatchRenderer(BatchRenderer2D* renderer) {
    m_BatchRenderer = renderer;
}

BatchRenderer2D* RenderCommandQueue::getBatchRenderer() const {
    return m_BatchRenderer;
}

RenderCommandQueue::~RenderCommandQueue() {
    stopRenderThread();
}

DrawQuadCommand::DrawQuadCommand(const glm::vec2& position, const glm::vec2& size, const Color& color)
    : m_Position(position), m_Size(size), m_Color(color) {
}

void DrawQuadCommand::execute() {
    TINA_PROFILE_FUNCTION();
    if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer()) {
        renderer->drawQuad(m_Position, m_Size, m_Color);
    }
}

} // namespace Tina 