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

    // 暂停当前场景
    if (!m_scenes.empty()) {
        m_scenes.back()->onPause();
        m_scenes.back()->m_active = false;
    }

    // 设置 Application 引用并进入新场景
    scene->m_app = m_app;
    scene->onEnter();
    m_scenes.push_back(std::move(scene));
    TINA_INFO("Scene pushed - Total scenes: {}", m_scenes.size());
}

void SceneManager::pop()
{
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

    // 退出当前场景
    if (!m_scenes.empty()) {
        m_scenes.back()->onExit();
        m_scenes.pop_back();
    }

    // 进入新场景
    scene->m_app = m_app;
    scene->onEnter();
    m_scenes.push_back(std::move(scene));
    TINA_INFO("Scene replaced - Total scenes: {}", m_scenes.size());
}

void SceneManager::clear()
{
    while (!m_scenes.empty()) {
        m_scenes.back()->onExit();
        m_scenes.pop_back();
    }
    TINA_INFO("All scenes cleared");
}

Scene* SceneManager::currentScene() const
{
    return m_scenes.empty() ? nullptr : m_scenes.back().get();
}

void SceneManager::update(float dt)
{
    if (Scene* scene = currentScene()) {
        scene->update(dt);
    }
}

void SceneManager::render()
{
    if (Scene* scene = currentScene()) {
        scene->render();
    }
}

void SceneManager::handleEvent(const Tina::os::Event& event)
{
    if (Scene* scene = currentScene()) {
        scene->handleEvent(event);
    }
}

} // namespace Tina::Engine
