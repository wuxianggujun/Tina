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

    // 建议使用的“延迟场景操作”接口：
    // 在事件回调或任意时刻调用，SceneManager 会在安全点（事件分发/更新之后）统一应用，避免在回调栈内销毁当前场景
    void requestPush(Memory::UniquePtr<Scene> scene);
    void requestPop();
    void requestReplace(Memory::UniquePtr<Scene> scene);
    void requestClear();

    // 获取当前场景
    Scene* currentScene() const;
    bool isEmpty() const { return m_scenes.empty(); }
    size_t sceneCount() const { return m_scenes.size(); }

    // 主循环分发
    void update(float dt);
    void render();
    // void handleEvent(const Event& event);  // TODO: 更新为使用新的Event系统

    // 窗口尺寸更新（通知所有场景）
    void updateAllScenesWindowSize(int width, int height);

private:
    // 立即应用所有挂起的场景操作（在非回调栈内的安全点调用）
    void applyPending();

    Application* m_app = nullptr;
    Container::Vector<Memory::UniquePtr<Scene>> m_scenes;

    // 延迟操作队列
    struct PendingOp {
        enum class Type { Push, Pop, Replace, Clear } type;
        Memory::UniquePtr<Scene> scene; // 仅 Push/Replace 使用
    };
    Container::Vector<PendingOp> m_pending;
    bool m_dispatching = false; // 保护：在回调分发期间不直接修改栈
};

} // namespace Tina::Engine
