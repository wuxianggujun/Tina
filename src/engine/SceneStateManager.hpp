//
// 场景状态管理器 - 自动保存和恢复场景状态
// 解决场景切换时的状态恢复问题，简化onPause/onResume逻辑
//

#pragma once

#include <unordered_map>
#include <any>
#include <functional>
#include <string>
#include "../core/Singleton.hpp"
#include "../core/Log.hpp"

namespace Tina::Engine {

class SceneStateManager : public Core::Singleton<SceneStateManager> {
    friend class Core::Singleton<SceneStateManager>;

public:
    // 场景状态快照
    struct SceneState {
        // 窗口状态
        int windowWidth = 0;
        int windowHeight = 0;

        // 相机状态
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        float cameraZoom = 1.0f;

        // 组件状态（通用存储）
        std::unordered_map<std::string, std::any> componentStates;

        // 时间戳
        double timestamp = 0.0;
    };

    // 保存场景状态
    void saveState(const std::string& sceneId);

    // 恢复场景状态
    void restoreState(const std::string& sceneId);

    // 检查是否有保存的状态
    bool hasState(const std::string& sceneId) const;

    // 清除特定场景的状态
    void clearState(const std::string& sceneId);

    // 清除所有状态
    void clearAllStates();

    // 注册组件的保存/恢复回调
    template<typename T>
    void registerComponent(const std::string& componentId,
                          std::function<T()> saveFunc,
                          std::function<void(const T&)> restoreFunc)
    {
        // 保存函数包装
        auto wrappedSave = [saveFunc]() -> std::any {
            return saveFunc();
        };

        // 恢复函数包装
        auto wrappedRestore = [restoreFunc](const std::any& data) {
            try {
                const T& typedData = std::any_cast<const T&>(data);
                restoreFunc(typedData);
            } catch (const std::bad_any_cast& e) {
                TINA_ERROR("SceneStateManager: 类型转换失败 - {}", e.what());
            }
        };

        m_componentCallbacks[componentId] = {wrappedSave, wrappedRestore};
    }

    // 注销组件
    void unregisterComponent(const std::string& componentId);

    // 设置当前窗口尺寸（由Application更新）
    void setWindowSize(int width, int height) {
        m_currentWindowWidth = width;
        m_currentWindowHeight = height;
    }

    // 设置当前相机状态（由Camera更新）
    void setCameraState(float x, float y, float zoom) {
        m_currentCameraX = x;
        m_currentCameraY = y;
        m_currentCameraZoom = zoom;
    }

    // 获取统计信息
    size_t getStateCount() const { return m_states.size(); }
    size_t getComponentCount() const { return m_componentCallbacks.size(); }

private:
    SceneStateManager() = default;
    ~SceneStateManager() = default;

    // 获取当前时间戳
    double getCurrentTimestamp() const;

private:
    // 保存的场景状态
    std::unordered_map<std::string, SceneState> m_states;

    // 组件回调（保存和恢复）
    std::unordered_map<std::string, std::pair<
        std::function<std::any()>,              // 保存函数
        std::function<void(const std::any&)>    // 恢复函数
    >> m_componentCallbacks;

    // 当前状态（用于保存）
    int m_currentWindowWidth = 0;
    int m_currentWindowHeight = 0;
    float m_currentCameraX = 0.0f;
    float m_currentCameraY = 0.0f;
    float m_currentCameraZoom = 1.0f;
};

// 便捷访问
inline SceneStateManager& GetSceneStateManager() {
    return SceneStateManager::getInstance();
}

} // namespace Tina::Engine