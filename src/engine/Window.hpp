//
// Window.hpp - 窗口管理类
// 职责：封装SDL窗口，提供跨平台的窗口操作接口
//

#pragma once

#include "../core/Core.hpp"
#include <string>

// 前向声明 SDL 类型
union SDL_Event;

namespace Tina::Engine {

// 窗口创建描述
struct WindowDesc {
    const char* title = "Tina Engine";
    int width = 1920;
    int height = 1080;
    int x = -1;  // -1 表示居中
    int y = -1;  // -1 表示居中
    bool fullscreen = false;
    bool resizable = true;
    bool visible = true;
    bool maximized = false;
    bool borderless = false;
    void* parent = nullptr;  // 父窗口句柄（可选）
};

// 窗口事件类型（简化版，详细事件在Event.hpp）
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

    // 禁止拷贝
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 创建和销毁
    bool create(const WindowDesc& desc);
    void destroy();
    bool isValid() const;

    // ==================== 窗口属性 ====================

    // 标题
    void setTitle(const char* title);
    const char* getTitle() const;

    // 尺寸
    void setSize(int width, int height);
    void getSize(int& width, int& height) const;
    int getWidth() const;
    int getHeight() const;

    // 位置
    void setPosition(int x, int y);
    void getPosition(int& x, int& y) const;

    // 显示状态
    void show();
    void hide();
    bool isVisible() const;

    void minimize();
    void maximize();
    void restore();
    bool isMinimized() const;
    bool isMaximized() const;

    // 全屏
    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;
    void toggleFullscreen();

    // 其他属性
    void setResizable(bool resizable);
    bool isResizable() const;

    void setBorderless(bool borderless);
    bool isBorderless() const;

    void focus();
    bool hasFocus() const;

    // ==================== 平台相关 ====================

    // 获取原生窗口句柄（用于渲染器初始化）
    void* getNativeHandle() const;
    void* getNativeDisplayHandle() const;  // X11/Wayland显示句柄

    // 获取SDL窗口指针（内部使用）
    void* getSDLWindow() const { return m_window; }

    // ==================== 工具方法 ====================

    // 设置窗口居中
    void center();

    // 闪烁窗口（提醒用户）
    void flash();

    // 设置窗口透明度（0.0-1.0）
    void setOpacity(float opacity);

    // ==================== 事件处理 ====================

    // 轮询窗口事件（静态方法，处理所有窗口的事件）
    static bool pollEvent(SDL_Event& outEvent);

    // 清空事件队列
    static void pumpEvents();

private:
    void* m_window;        // SDL_Window* (使用void*避免暴露SDL)
    WindowDesc m_desc;     // 创建时的描述
    std::string m_title;   // 窗口标题缓存
};

} // namespace Tina::Engine