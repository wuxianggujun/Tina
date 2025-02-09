//
// Created by wuxianggujun on 2025/1/31.
//
#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Context.hpp"
#include "tina/window/Window.hpp"
#include "tina/scene/Scene.hpp"
#include <memory>

namespace Tina::Core
{
    class TINA_CORE_API Engine {
    public:
        Engine();
        ~Engine();

        bool initialize();
        bool run();
        void shutdown();
        const char* getVersion();
        Context& getContext();

        // 场景管理
        Scene* createScene(const std::string& name);
        void setActiveScene(Scene* scene);
        Scene* getActiveScene() { return m_activeScene.get(); }

    private:
        Context m_context;
        WindowHandle m_mainWindow;
        std::unique_ptr<Scene> m_activeScene;  // 当前活动场景
    };

}
