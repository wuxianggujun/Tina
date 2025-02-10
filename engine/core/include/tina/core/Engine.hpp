//
// Created by wuxianggujun on 2025/1/31.
//
#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Context.hpp"
#include "tina/window/Window.hpp"
#include "tina/scene/Scene.hpp"
#include <memory>
#include <string>
#include <filesystem>

namespace Tina::Core
{
    class TINA_CORE_API Engine {
    public:
        Engine();
        ~Engine();

        bool initialize();
        bool run();
        void shutdown();
        const char* getVersion() const;
        Context& getContext();

        // 场景管理
        Scene* createScene(const std::string& name);
        void setActiveScene(Scene* scene);
        Scene* getActiveScene() { return m_activeScene.get(); }

        // 获取可执行文件路径
        static std::filesystem::path getExecutablePath();

    private:
        Context m_context;
        WindowHandle m_mainWindow;
        std::unique_ptr<Scene> m_activeScene;  // 当前活动场景
        bool m_isShutdown{false};  // 添加标志位以防止重复调用 shutdown
    };

}
