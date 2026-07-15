#include "SceneStateManager.hpp"
#include "../core/Log.hpp"
#include <chrono>

namespace Tina::Engine {

void SceneStateManager::saveState(const std::string& sceneId)
{
    TINA_DEBUG("SceneStateManager: 保存场景状态 '{}'", sceneId);

    SceneState state;

    // 保存窗口状态
    state.windowWidth = m_currentWindowWidth;
    state.windowHeight = m_currentWindowHeight;

    // 保存相机状态
    state.cameraX = m_currentCameraX;
    state.cameraY = m_currentCameraY;
    state.cameraZoom = m_currentCameraZoom;

    // 保存所有组件状态
    for (const auto& [id, callbacks] : m_componentCallbacks) {
        try {
            const auto& [saveFunc, restoreFunc] = callbacks;
            if (saveFunc) {
                state.componentStates[id] = saveFunc();
                TINA_DEBUG("  - 保存组件 '{}' 状态", id);
            }
        } catch (const std::exception& e) {
            TINA_ERROR("SceneStateManager: 保存组件 '{}' 失败 - {}", id, e.what());
        }
    }

    // 记录时间戳
    state.timestamp = getCurrentTimestamp();

    // 存储状态
    m_states[sceneId] = std::move(state);

    TINA_INFO("SceneStateManager: 场景 '{}' 状态已保存 (窗口:{}x{}, 相机:({},{})@{}x, 组件:{})",
                  sceneId, state.windowWidth, state.windowHeight,
                  state.cameraX, state.cameraY, state.cameraZoom,
                  state.componentStates.size());
}

void SceneStateManager::restoreState(const std::string& sceneId)
{
    auto it = m_states.find(sceneId);
    if (it == m_states.end()) {
        TINA_WARN("SceneStateManager: 场景 '{}' 没有保存的状态", sceneId);
        return;
    }

    TINA_DEBUG("SceneStateManager: 恢复场景状态 '{}'", sceneId);

    const SceneState& state = it->second;

    // 恢复窗口状态（通常不需要，因为窗口尺寸是全局的）
    // 但可以用于验证或特殊处理

    // 恢复相机状态（通过回调）
    // 注意：这里假设相机会通过组件回调恢复

    // 恢复所有组件状态
    for (const auto& [id, data] : state.componentStates) {
        auto callbackIt = m_componentCallbacks.find(id);
        if (callbackIt != m_componentCallbacks.end()) {
            try {
                const auto& [saveFunc, restoreFunc] = callbackIt->second;
                if (restoreFunc) {
                    restoreFunc(data);
                    TINA_DEBUG("  - 恢复组件 '{}' 状态", id);
                }
            } catch (const std::exception& e) {
                TINA_ERROR("SceneStateManager: 恢复组件 '{}' 失败 - {}", id, e.what());
            }
        } else {
            TINA_WARN("SceneStateManager: 组件 '{}' 没有注册恢复回调", id);
        }
    }

    double elapsed = getCurrentTimestamp() - state.timestamp;
    TINA_INFO("SceneStateManager: 场景 '{}' 状态已恢复 (保存于 {:.1f} 秒前)",
                  sceneId, elapsed);
}

bool SceneStateManager::hasState(const std::string& sceneId) const
{
    return m_states.find(sceneId) != m_states.end();
}

void SceneStateManager::clearState(const std::string& sceneId)
{
    if (m_states.erase(sceneId)) {
        TINA_DEBUG("SceneStateManager: 清除场景 '{}' 的状态", sceneId);
    }
}

void SceneStateManager::clearAllStates()
{
    m_states.clear();
    TINA_DEBUG("SceneStateManager: 清除所有场景状态");
}

void SceneStateManager::unregisterComponent(const std::string& componentId)
{
    if (m_componentCallbacks.erase(componentId)) {
        TINA_DEBUG("SceneStateManager: 注销组件 '{}'", componentId);
    }
}

double SceneStateManager::getCurrentTimestamp() const
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    auto epoch = now.time_since_epoch();
    auto seconds = duration_cast<duration<double>>(epoch);
    return seconds.count();
}

} // namespace Tina::Engine