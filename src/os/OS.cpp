//
// Tina OS 平台抽象的 SDL3 实现
// 仅实现最小可用集合：窗口创建/销毁、显示隐藏、事件轮询、尺寸与标题、最大最小化、全屏
// 注意：键盘 KeyCode 映射目前仅提供基础按键，后续可按需补齐。

#include "OS.hpp"

#include <SDL3/SDL.h>
#include <deque>

namespace Tina::os {

    namespace {
        struct EventQueue {
            std::deque<Event> q;
            bool pop(Event& out) {
                if (q.empty()) return false;
                out = q.front();
                q.pop_front();
                return true;
            }
            void push(const Event& e) { q.push_back(e); }
        } g_queue;

        inline SDL_Window* as_sdl(WindowHandle h) {
            return static_cast<SDL_Window*>(h);
        }

        // 基础键位映射（可逐步扩展）
        KeyCode map_keycode(SDL_Scancode sc) {
            switch (sc) {
                case SDL_SCANCODE_A: return KeyCode::A;
                case SDL_SCANCODE_C: return KeyCode::C;
                case SDL_SCANCODE_D: return KeyCode::D;
                case SDL_SCANCODE_E: return KeyCode::E;
                case SDL_SCANCODE_F: return KeyCode::F;
                case SDL_SCANCODE_K: return KeyCode::K;
                case SDL_SCANCODE_P: return KeyCode::P;
                case SDL_SCANCODE_R: return KeyCode::R;
                case SDL_SCANCODE_S: return KeyCode::S;
                case SDL_SCANCODE_V: return KeyCode::V;
                case SDL_SCANCODE_W: return KeyCode::W;
                case SDL_SCANCODE_X: return KeyCode::X;
                case SDL_SCANCODE_Y: return KeyCode::Y;
                case SDL_SCANCODE_Z: return KeyCode::Z;
                case SDL_SCANCODE_LEFT: return KeyCode::LEFT;
                case SDL_SCANCODE_RIGHT: return KeyCode::RIGHT;
                case SDL_SCANCODE_UP: return KeyCode::UP;
                case SDL_SCANCODE_DOWN: return KeyCode::DOWN;
                case SDL_SCANCODE_RETURN: return KeyCode::RETURN;
                case SDL_SCANCODE_ESCAPE: return KeyCode::ESCAPE;
                case SDL_SCANCODE_SPACE: return KeyCode::SPACE;
                case SDL_SCANCODE_TAB: return KeyCode::TAB;
                case SDL_SCANCODE_BACKSPACE: return KeyCode::BACKSPACE;
                case SDL_SCANCODE_LSHIFT: return KeyCode::LSHIFT;
                case SDL_SCANCODE_RSHIFT: return KeyCode::RSHIFT;
                case SDL_SCANCODE_LCTRL: return KeyCode::LCTRL;
                case SDL_SCANCODE_RCTRL: return KeyCode::RCTRL;
                case SDL_SCANCODE_LALT: return KeyCode::LALT;
                case SDL_SCANCODE_RALT: return KeyCode::RALT;
                default: return KeyCode::INVALID;
            }
        }

        MouseButton map_mouse_button(uint8_t button) {
            switch (button) {
                case SDL_BUTTON_LEFT: return MouseButton::LEFT;
                case SDL_BUTTON_RIGHT: return MouseButton::RIGHT;
                case SDL_BUTTON_MIDDLE: return MouseButton::MIDDLE;
                case SDL_BUTTON_X1: return MouseButton::EXTENDED1;
                case SDL_BUTTON_X2: return MouseButton::EXTENDED2;
                default: return MouseButton::LEFT;
            }
        }

        void pump_sdl_to_queue() {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                    case SDL_EVENT_QUIT: {
                        Event ev{}; ev.type = Event::Type::QUIT; ev.window = INVALID_WINDOW_HANDLE; g_queue.push(ev);
                    } break;
                    // 键盘
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP: {
                        Event ev{}; ev.type = Event::Type::KEY; ev.window = INVALID_WINDOW_HANDLE;
                        ev.key.down = (e.type == SDL_EVENT_KEY_DOWN);
                        ev.key.is_repeat = e.key.repeat != 0;
                        ev.key.key_code = map_keycode(e.key.scancode);
                        g_queue.push(ev);
                    } break;
                    // 文本输入（UTF-8 首字节，简化存放：若需完整文本处理，请改用缓冲/回调）
                    case SDL_EVENT_TEXT_INPUT: {
                        Event ev{}; ev.type = Event::Type::CHAR; ev.window = INVALID_WINDOW_HANDLE;
                        // SDL3 文本输入是 UTF-8 字符串，这里仅取首个 UTF-8 编码的 32bit 容器（简化）
                        const char* txt = e.text.text;
                        if (txt && *txt) ev.text_input.utf8 = (u8)txt[0];
                        g_queue.push(ev);
                    } break;
                    // 鼠标按钮
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP: {
                        Event ev{}; ev.type = Event::Type::MOUSE_BUTTON; ev.window = INVALID_WINDOW_HANDLE;
                        ev.mouse_button.down = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        ev.mouse_button.button = map_mouse_button(e.button.button);
                        g_queue.push(ev);
                    } break;
                    // 鼠标移动（相对）
                    case SDL_EVENT_MOUSE_MOTION: {
                        Event ev{}; ev.type = Event::Type::MOUSE_MOVE; ev.window = INVALID_WINDOW_HANDLE;
                        ev.mouse_move.xrel = e.motion.xrel;
                        ev.mouse_move.yrel = e.motion.yrel;
                        g_queue.push(ev);
                    } break;
                    // 鼠标滚轮
                    case SDL_EVENT_MOUSE_WHEEL: {
                        Event ev{}; ev.type = Event::Type::MOUSE_WHEEL; ev.window = INVALID_WINDOW_HANDLE;
                        ev.mouse_wheel.amount = (float)e.wheel.y;
                        g_queue.push(ev);
                    } break;
                    // 窗口事件
                    case SDL_EVENT_WINDOW_RESIZED:
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                        Event ev{}; ev.type = Event::Type::WINDOW_SIZE;
                        ev.window = SDL_GetWindowFromID(e.window.windowID);
                        int w=0,h=0; SDL_GetWindowSizeInPixels(as_sdl(ev.window), &w, &h);
                        ev.win_size.w = w; ev.win_size.h = h;
                        g_queue.push(ev);
                    } break;
                    case SDL_EVENT_WINDOW_MOVED: {
                        Event ev{}; ev.type = Event::Type::WINDOW_MOVE;
                        ev.window = SDL_GetWindowFromID(e.window.windowID);
                        ev.win_move.x = e.window.data1; ev.win_move.y = e.window.data2;
                        g_queue.push(ev);
                    } break;
                    case SDL_EVENT_WINDOW_FOCUS_GAINED: {
                        Event ev{}; ev.type = Event::Type::FOCUS; ev.window = SDL_GetWindowFromID(e.window.windowID);
                        ev.focus.gained = true; g_queue.push(ev);
                    } break;
                    case SDL_EVENT_WINDOW_FOCUS_LOST: {
                        Event ev{}; ev.type = Event::Type::FOCUS; ev.window = SDL_GetWindowFromID(e.window.windowID);
                        ev.focus.gained = false; g_queue.push(ev);
                    } break;
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                        Event ev{}; ev.type = Event::Type::WINDOW_CLOSE; ev.window = SDL_GetWindowFromID(e.window.windowID);
                        g_queue.push(ev);
                    } break;
                    default: break;
                }
            }
        }
    } // namespace

    WindowHandle createWindow(const InitWindowArgs& args) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            return INVALID_WINDOW_HANDLE;
        }

        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
        if (args.is_hidden) flags = (SDL_WindowFlags)(flags | SDL_WINDOW_HIDDEN);
        if (args.is_maximized) flags = (SDL_WindowFlags)(flags | SDL_WINDOW_MAXIMIZED);
        // 需要高 DPI 时可打开：SDL_WINDOW_HIGH_PIXEL_DENSITY
        flags = (SDL_WindowFlags)(flags | SDL_WINDOW_HIGH_PIXEL_DENSITY);

        SDL_Window* window = SDL_CreateWindow(args.name && args.name[0] ? args.name : "Tina", (int)args.width, (int)args.height, flags);
        return (WindowHandle)window;
    }

    void showWindow(WindowHandle wnd) { if (wnd) SDL_ShowWindow(as_sdl(wnd)); }
    void hideWindow(WindowHandle wnd) { if (wnd) SDL_HideWindow(as_sdl(wnd)); }

    bool getEvent(Event& event) {
        if (g_queue.pop(event)) return true;
        pump_sdl_to_queue();
        return g_queue.pop(event);
    }

    void destroyWindow(WindowHandle wnd) {
        if (wnd) SDL_DestroyWindow(as_sdl(wnd));
    }

    Rect getWindowScreenRect(WindowHandle win) {
        if (!win) return Rect{0,0,0,0};
        int x=0,y=0; SDL_GetWindowPosition(as_sdl(win), &x, &y);
        int w=0,h=0; SDL_GetWindowSizeInPixels(as_sdl(win), &w, &h);
        return Rect{x,y,w,h};
    }

    Point getWindowClientSize(WindowHandle win) {
        if (!win) return Point{0,0};
        int w=0,h=0; SDL_GetWindowSizeInPixels(as_sdl(win), &w, &h);
        return Point{w,h};
    }

    void setWindowTitle(WindowHandle win, const char* title) {
        if (win) SDL_SetWindowTitle(as_sdl(win), title ? title : "Tina");
    }

    void maximizeWindow(WindowHandle win) { if (win) SDL_MaximizeWindow(as_sdl(win)); }
    void minimizeWindow(WindowHandle win) { if (win) SDL_MinimizeWindow(as_sdl(win)); }

    WindowState setFullScreen(WindowHandle win) {
        WindowState prev{};
        if (!win) return prev;
        // 保存当前窗口矩形
        const Rect rc = getWindowScreenRect(win);
        prev.style = 0; // SDL 不暴露样式，这里占位
        prev.rect = rc;
        SDL_SetWindowFullscreen(as_sdl(win), true);
        return prev;
    }
}

