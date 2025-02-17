#include "tina/scene/SceneManager.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

std::shared_ptr<Scene> SceneManager::createScene(const std::string& name) {
    if (m_Scenes.find(name) != m_Scenes.end()) {
        TINA_CORE_LOG_WARN("Scene '{}' already exists", name);
        return m_Scenes[name];
    }

    auto scene = std::make_shared<Scene>(name);
    m_Scenes[name] = scene;

    TINA_CORE_LOG_INFO("Created scene '{}'", name);
    return scene;
}

std::shared_ptr<Scene> SceneManager::loadScene(const std::string& name) {
    // TODO: 实现场景序列化和加载
    return createScene(name);
}

void SceneManager::unloadScene(const std::string& name) {
    auto it = m_Scenes.find(name);
    if (it == m_Scenes.end()) {
        TINA_CORE_LOG_WARN("Attempting to unload non-existent scene '{}'", name);
        return;
    }

    if (m_ActiveScene == it->second) {
        m_ActiveScene = nullptr;
    }

    m_Scenes.erase(it);
    TINA_CORE_LOG_INFO("Unloaded scene '{}'", name);
}

void SceneManager::setActiveScene(const std::string& name) {
    auto it = m_Scenes.find(name);
    if (it == m_Scenes.end()) {
        TINA_CORE_LOG_ERROR("Cannot set active scene: scene '{}' does not exist", name);
        return;
    }

    if (m_ActiveScene == it->second) {
        TINA_CORE_LOG_WARN("Scene '{}' is already active", name);
        return;
    }

    m_ActiveScene = it->second;
    TINA_CORE_LOG_INFO("Set active scene to '{}'", name);
}

std::shared_ptr<Scene> SceneManager::getScene(const std::string& name) const {
    auto it = m_Scenes.find(name);
    return it != m_Scenes.end() ? it->second : nullptr;
}

void SceneManager::update(float deltaTime) {
    if (m_ActiveScene) {
        m_ActiveScene->onUpdate(deltaTime);
    }
}

void SceneManager::render() {
    if (m_ActiveScene) {
        m_ActiveScene->onRender();
    }
}

void SceneManager::onEvent(Event& event) {
    if (m_ActiveScene) {
        m_ActiveScene->onEvent(event);
    }
}

} // namespace Tina 