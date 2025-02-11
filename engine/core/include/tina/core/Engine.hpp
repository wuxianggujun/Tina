//
// Created by wuxianggujun on 2025/1/31.
//
#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Context.hpp"
#include "tina/window/Window.hpp"
#include "tina/scene/Scene.hpp"
#include "tina/event/Event.hpp"
#include "tina/log/Logger.hpp"
#include <memory>
#include <string>
#include <filesystem>

namespace Tina::Core
{
    class TINA_CORE_API Engine {
    public:
        Engine();
        virtual ~Engine();

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

        // 获取主窗口句柄
        [[nodiscard]] WindowHandle getMainWindow() const { return m_mainWindow; }

        // 获取窗口尺寸
        void getWindowSize(uint32_t& width, uint32_t& height) const {
            width = m_windowWidth;
            height = m_windowHeight;
        }

        static Engine& get() { return *s_Instance; }

    private:
        Context& m_context;  // 改为引用
        WindowHandle m_mainWindow;
        std::unique_ptr<Scene> m_activeScene;  // 当前活动场景
        uint32_t m_windowWidth;
        uint32_t m_windowHeight;
        bool m_isShutdown;  // 添加标志位以防止重复调用 shutdown
        bool m_isInitialized;
        static Engine* s_Instance;
    };
}
