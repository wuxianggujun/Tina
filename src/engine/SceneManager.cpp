#include "SceneManager.hpp"
#include "Application.hpp"
#include "../core/Log.hpp"

namespace Tina::Engine {

SceneManager::SceneManager(Application* app)
    : m_app(app)
{
}

SceneManager::~SceneManager()
{
    clear();
}

void SceneManager::push(Memory::UniquePtr<Scene> scene)
{
    if (!scene) { TINA_ERROR("SceneManager::push - scene is null"); return; }
    if (m_dispatching) { requestPush(std::move(scene)); return; }

    // 暂停当前场景
    if (!m_scenes.empty()) {
        m_scenes.back()->onPause();
        m_scenes.back()->m_active = false;
    }

    // 设置 Application 引用并进入新场景
    scene->m_app = m_app;

    // 初始化场景的窗口大小
    int w, h;
    m_app->getPixelSize(w, h);
    scene->updateWindowSize(w, h);

    scene->onEnter();
    m_scenes.push_back(std::move(scene));
    TINA_INFO("Scene pushed - Total scenes: {}", m_scenes.size());
}

void SceneManager::pop()
{
    if (m_dispatching) { requestPop(); return; }
    if (m_scenes.empty()) { TINA_WARN("SceneManager::pop - no scenes to pop"); return; }

    // 退出当前场景
    m_scenes.back()->onExit();
    m_scenes.pop_back();

    // 恢复上一个场景
    if (!m_scenes.empty()) { m_scenes.back()->m_active = true; m_scenes.back()->onResume(); }
    TINA_INFO("Scene popped - Total scenes: {}", m_scenes.size());
}

void SceneManager::replace(Memory::UniquePtr<Scene> scene)
{
    if (!scene) { TINA_ERROR("SceneManager::replace - scene is null"); return; }
    if (m_dispatching) { requestReplace(std::move(scene)); return; }

    // 退出当前场景
    if (!m_scenes.empty()) {
        m_scenes.back()->onExit();
        m_scenes.pop_back();
    }

    // 进入新场景
    scene->m_app = m_app;

    // 初始化场景的窗口大小
    int w, h;
    m_app->getPixelSize(w, h);
    scene->updateWindowSize(w, h);

    scene->onEnter();
    m_scenes.push_back(std::move(scene));
    TINA_INFO("Scene replaced - Total scenes: {}", m_scenes.size());
}

void SceneManager::clear()
{
    if (m_dispatching) { requestClear(); return; }
    while (!m_scenes.empty()) {
        m_scenes.back()->onExit();
        m_scenes.pop_back();
    }
    TINA_INFO("All scenes cleared");
}

void SceneManager::requestPush(Memory::UniquePtr<Scene> scene)
{
    PendingOp op{}; op.type = PendingOp::Type::Push; op.scene = std::move(scene);
    m_pending.push_back(std::move(op));
}

void SceneManager::requestPop()
{
    PendingOp op{}; op.type = PendingOp::Type::Pop;
    m_pending.push_back(std::move(op));
}

void SceneManager::requestReplace(Memory::UniquePtr<Scene> scene)
{
    PendingOp op{}; op.type = PendingOp::Type::Replace; op.scene = std::move(scene);
    m_pending.push_back(std::move(op));
}

void SceneManager::requestClear()
{
    PendingOp op{}; op.type = PendingOp::Type::Clear;
    m_pending.push_back(std::move(op));
}

Scene* SceneManager::currentScene() const
{
    return m_scenes.empty() ? nullptr : m_scenes.back().get();
}

void SceneManager::update(float dt)
{
    if (Scene* scene = currentScene()) {
        m_dispatching = true;
        scene->update(dt);
        m_dispatching = false;
    }
    applyPending();
}

void SceneManager::render()
{
    if (Scene* scene = currentScene()) {
        scene->renderFrame();  // 改为调用renderFrame()，由框架处理视图设置
    }
}

void SceneManager::handleEvent(const Tina::os::Event& event)
{
    if (Scene* scene = currentScene()) {
        m_dispatching = true;
        scene->handleEventFrame(event);  // 改为调用handleEventFrame()，由框架先处理
        m_dispatching = false;
    }
    applyPending();
}

void SceneManager::applyPending()
{
    if (m_pending.empty()) return;
    // 取出所有操作，按顺序应用
    Container::Vector<PendingOp> ops;
    ops.swap(m_pending);
    for (auto& op : ops) {
        switch (op.type) {
        case PendingOp::Type::Push:
            push(std::move(op.scene));
            break;
        case PendingOp::Type::Pop:
            pop();
            break;
        case PendingOp::Type::Replace:
            replace(std::move(op.scene));
            break;
        case PendingOp::Type::Clear:
            clear();
            break;
        }
    }
}

} // namespace Tina::Engine
