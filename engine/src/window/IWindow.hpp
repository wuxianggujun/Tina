#pragma once

#include "EventHandler.hpp"
#include "math/Vector.hpp"
#include "core/Core.hpp"

namespace Tina
{
    class IWindow
    {
    public:
        struct WindowConfig
        {
            std::string title;
            Vector2i resolution;
            bool resizable{false};
            bool maximized{false};
            bool vsync{true};
        };

        virtual ~IWindow() = default;

        // 窗口管理
        virtual void create(const WindowConfig& config) = 0;
        virtual void destroy() = 0;
        virtual void pollEvents() = 0;
        virtual bool shouldClose() = 0;

        // 窗口属性
        [[nodiscard]] virtual void* getNativeWindow() const = 0;
        [[nodiscard]] virtual Vector2i getResolution() const = 0;

        // 事件处理
        virtual void setEventHandler(ScopePtr<EventHandler>&& eventHandler) = 0;

        // 窗口属性设置和获取
        virtual void setTitle(const std::string& title) = 0;
        virtual void setSize(const Vector2i& size) = 0;
        virtual void setVSync(bool enabled) = 0;
        virtual void setFullscreen(bool fullscreen) = 0;

        [[nodiscard]] virtual std::string getTitle() const = 0;
        [[nodiscard]] virtual bool isFullscreen() const = 0;
        [[nodiscard]] virtual bool isVSync() const = 0;
        [[nodiscard]] virtual bool isVisible() const = 0;
    };
} //
