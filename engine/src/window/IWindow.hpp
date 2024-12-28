#ifndef TINA_CORE_IWINDOW_HPP
#define TINA_CORE_IWINDOW_HPP

#include <string>
#include "EventHandler.hpp"
#include "math/Vector.hpp"

namespace Tina {
    class IWindow {
    public:
        struct WindowConfig {
            std::string title{};
            Vector2i resolution{};
            bool resizable{};
            bool maximized{};
            bool vsync{};
        };

        virtual ~IWindow() = default;
        virtual void create(const WindowConfig& config) = 0;
        virtual void destroy() = 0;
        virtual void pollEvents() = 0;
        virtual bool shouldClose() = 0;
        virtual void setEventHandler(ScopePtr<EventHandler> &&eventHandler) = 0;
        [[nodiscard]] virtual void* getNativeWindow() const = 0;
    };
}

#endif