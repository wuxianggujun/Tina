#pragma once

#include "Scene.hpp"
#include "../core/Memory.hpp"
#include "../core/Container.hpp"

namespace Tina::Engine {

/**
 * SceneManager - 场景管理器
 *
 * 职责：
 * - 管理场景栈（push/pop/replace）
 * - 驱动场景生命周期（onEnter/onExit/onPause/onResume）
 * - 分发事件、更新、渲染到当前场景
 */
class SceneManager {
public:
    explicit SceneManager(Application* app);
    ~SceneManager();

    // 场景控制
    void push(Memory::UniquePtr<Scene> scene);
    void pop();
    void replace(Memory::UniquePtr<Scene> scene);
    void clear();

    // 获取当前场景
    Scene* currentScene() const;
    bool isEmpty() const { return m_scenes.empty(); }
    size_t sceneCount() const { return m_scenes.size(); }

    // 主循环分发
    void update(float dt);
    void render();
    void handleEvent(const Tina::os::Event& event);

private:
    Application* m_app = nullptr;
    Container::Vector<Memory::UniquePtr<Scene>> m_scenes;
};

} // namespace Tina::Engine
