//
// Window.hpp - GLFW window wrapper.
//

#pragma once

#include "../core/Core.hpp"
#include <string>

struct GLFWwindow;

namespace Tina::Engine {

struct WindowDesc {
    const char* title = "Tina Engine";
    int width = 1920;
    int height = 1080;
    int x = -1;
    int y = -1;
    bool fullscreen = false;
    bool resizable = true;
    bool visible = true;
    bool maximized = false;
    bool borderless = false;
    void* parent = nullptr;
};

enum class WindowEventType {
    None,
    Closed,
    Resized,
    Moved,
    Minimized,
    Maximized,
    Restored,
    FocusGained,
    FocusLost
};

class Window {
public:
    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const WindowDesc& desc);
    void destroy();
    bool isValid() const;

    void setTitle(const char* title);
    const char* getTitle() const;

    void setSize(int width, int height);
    void getSize(int& width, int& height) const;
    int getWidth() const;
    int getHeight() const;

    void getSizeInPixels(int& width, int& height) const;
    void getFramebufferSize(int& width, int& height) const;

    void setPosition(int x, int y);
    void getPosition(int& x, int& y) const;

    void show();
    void hide();
    bool isVisible() const;

    void minimize();
    void maximize();
    void restore();
    bool isMinimized() const;
    bool isMaximized() const;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;
    void toggleFullscreen();

    void setResizable(bool resizable);
    bool isResizable() const;

    void setBorderless(bool borderless);
    bool isBorderless() const;

    void focus();
    bool hasFocus() const;

    void* getNativeHandle() const;
    void* getNativeDisplayHandle() const;
    bool usesWayland() const;
    GLFWwindow* getGLFWWindow() const { return m_window; }

    void center();
    void flash();
    void setOpacity(float opacity);

    void pollEvents();
    bool shouldClose() const;
    void requestClose();

    const char* getClipboardText() const;
    void setClipboardText(const char* text);

private:
    GLFWwindow* m_window = nullptr;
    WindowDesc m_desc{};
    std::string m_title;

    int m_windowedX = 0;
    int m_windowedY = 0;
    int m_windowedWidth = 0;
    int m_windowedHeight = 0;
    bool m_ownsGlfw = false;
};

} // namespace Tina::Engine
