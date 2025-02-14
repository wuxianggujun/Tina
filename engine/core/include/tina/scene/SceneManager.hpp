#pragma once

#include "Scene.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace Tina {

class SceneManager {
public:
    static SceneManager& getInstance() {
        static SceneManager instance;
        return instance;
    }

    // 创建新场景
    std::shared_ptr<Scene> createScene(const std::string& name);
    
    // 加载场景
    std::shared_ptr<Scene> loadScene(const std::string& name);
    
    // 卸载场景
    void unloadScene(const std::string& name);
    
    // 切换到指定场景
    void setActiveScene(const std::string& name);
    
    // 获取当前活动场景
    std::shared_ptr<Scene> getActiveScene() const { return m_ActiveScene; }
    
    // 获取指定场景
    std::shared_ptr<Scene> getScene(const std::string& name) const;
    
    // 更新当前场景
    void update(float deltaTime);
    
    // 渲染当前场景
    void render();
    
    // 处理事件
    void onEvent(Event& event);

private:
    SceneManager() = default;
    ~SceneManager() = default;
    
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

private:
    std::unordered_map<std::string, std::shared_ptr<Scene>> m_Scenes;
    std::shared_ptr<Scene> m_ActiveScene;
};

} // namespace Tina 