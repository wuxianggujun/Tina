//
// Tina OS 平台抽象的 SDL3 实现
// 仅实现最小可用集合：窗口创建/销毁、显示隐藏、事件轮询、尺寸与标题、最大最小化、全屏
// 注意：键盘 KeyCode 映射目前仅提供基础按键，后续可按需补齐。

#include "OS.hpp"

#include <SDL3/SDL.h>
#include <deque>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

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

        struct HitTestCtx {
            os::InitWindowArgs::HitTestCallback cb = nullptr;
            void* user = nullptr;
        };

        std::unordered_map<SDL_Window*, HitTestCtx> g_hittest;

        struct DropData {
            std::vector<std::string> files;
        };

        // 进行中的拖拽会话，按 windowID 管理
        std::unordered_map<SDL_WindowID, DropData*> g_drop_sessions;

        static SDL_HitTestResult SDLCALL sdl_hit_test(SDL_Window* win, const SDL_Point* area, void* data) {
            HitTestCtx* ctx = (HitTestCtx*)data;
            if (!ctx || !ctx->cb) return SDL_HITTEST_NORMAL;
            Point p{area->x, area->y};
            HitTestResult r = ctx->cb(ctx->user, (WindowHandle)win, p);
            switch (r) {
                case HitTestResult::CAPTION: return SDL_HITTEST_DRAGGABLE;
                case HitTestResult::CLIENT: return SDL_HITTEST_NORMAL;
                case HitTestResult::NONE: default: return SDL_HITTEST_NORMAL;
            }
        }

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
                case SDL_SCANCODE_F1: return KeyCode::F1;
                case SDL_SCANCODE_F2: return KeyCode::F2;
                case SDL_SCANCODE_F3: return KeyCode::F3;
                case SDL_SCANCODE_F4: return KeyCode::F4;
                case SDL_SCANCODE_F5: return KeyCode::F5;
                case SDL_SCANCODE_F6: return KeyCode::F6;
                case SDL_SCANCODE_F7: return KeyCode::F7;
                case SDL_SCANCODE_F8: return KeyCode::F8;
                case SDL_SCANCODE_F9: return KeyCode::F9;
                case SDL_SCANCODE_F10: return KeyCode::F10;
                case SDL_SCANCODE_F11: return KeyCode::F11;
                case SDL_SCANCODE_F12: return KeyCode::F12;
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

        // 解析 UTF-8 首个码点为 u32（简化，不处理错误路径的多字节截断）
        u32 utf8_first_codepoint(const char* txt) {
            const unsigned char* s = (const unsigned char*)txt;
            if (!s || *s == 0) return 0;
            if (s[0] < 0x80) return s[0];
            if ((s[0] & 0xE0) == 0xC0) return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            if ((s[0] & 0xF0) == 0xE0) return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            if ((s[0] & 0xF8) == 0xF0) return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            return 0;
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
                        Event ev{}; ev.type = Event::Type::KEY;
                        ev.window = SDL_GetWindowFromID(e.key.windowID);
                        ev.key.down = (e.type == SDL_EVENT_KEY_DOWN);
                        ev.key.is_repeat = e.key.repeat != 0;
                        ev.key.key_code = map_keycode(e.key.scancode);
                        g_queue.push(ev);
                    } break;
                    // 文本输入（UTF-8 首字节，简化存放：若需完整文本处理，请改用缓冲/回调）
                    case SDL_EVENT_TEXT_INPUT: {
                        Event ev{}; ev.type = Event::Type::CHAR;
                        ev.window = SDL_GetWindowFromID(e.text.windowID);
                        const char* txt = e.text.text; // UTF-8 字符串
                        ev.text_input.utf8 = utf8_first_codepoint(txt);
                        g_queue.push(ev);
                    } break;
                    // 鼠标按钮
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP: {
                        Event ev{}; ev.type = Event::Type::MOUSE_BUTTON;
                        ev.window = SDL_GetWindowFromID(e.button.windowID);
                        ev.mouse_button.down = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        ev.mouse_button.button = map_mouse_button(e.button.button);
                        g_queue.push(ev);
                    } break;
                    // 鼠标移动（相对）
                    case SDL_EVENT_MOUSE_MOTION: {
                        Event ev{}; ev.type = Event::Type::MOUSE_MOVE;
                        ev.window = SDL_GetWindowFromID(e.motion.windowID);
                        ev.mouse_move.xrel = e.motion.xrel;
                        ev.mouse_move.yrel = e.motion.yrel;
                        g_queue.push(ev);
                    } break;
                    // 鼠标滚轮
                    case SDL_EVENT_MOUSE_WHEEL: {
                        Event ev{}; ev.type = Event::Type::MOUSE_WHEEL;
                        ev.window = SDL_GetWindowFromID(e.wheel.windowID);
                        ev.mouse_wheel.amount = (float)e.wheel.y;
                        g_queue.push(ev);
                    } break;
                    // 拖拽文件与文本
                    case SDL_EVENT_DROP_BEGIN: {
                        SDL_WindowID wid = e.drop.windowID;
                        if (g_drop_sessions.count(wid)) { delete g_drop_sessions[wid]; }
                        g_drop_sessions[wid] = new DropData();
                    } break;
                    case SDL_EVENT_DROP_FILE:
                    case SDL_EVENT_DROP_TEXT: {
                        SDL_WindowID wid = e.drop.windowID;
                        auto it = g_drop_sessions.find(wid);
                        if (it != g_drop_sessions.end() && e.drop.data) {
                            it->second->files.emplace_back(e.drop.data);
                        }
                    } break;
                    case SDL_EVENT_DROP_COMPLETE: {
                        SDL_WindowID wid = e.drop.windowID;
                        auto it = g_drop_sessions.find(wid);
                        if (it != g_drop_sessions.end()) {
                            Event ev{}; ev.type = Event::Type::DROP_FILE; ev.window = SDL_GetWindowFromID(wid);
                            ev.file_drop.handle = (void*)it->second;
                            g_queue.push(ev);
                            g_drop_sessions.erase(it); // 所有权转交给事件消费者，通过 finishDrag 释放
                        }
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
        if (window && args.hit_test_callback) {
            // 安装命中测试回调（仅处理可拖动 CAPTION；边缘缩放可后续扩展）
            HitTestCtx ctx{ args.hit_test_callback, args.user_data };
            g_hittest[window] = ctx;
            SDL_SetWindowHitTest(window, sdl_hit_test, &g_hittest[window]);
        }
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
        if (!wnd) return;
        SDL_Window* w = as_sdl(wnd);
        auto it = g_hittest.find(w);
        if (it != g_hittest.end()) {
            // 解除回调并移除上下文
            SDL_SetWindowHitTest(w, nullptr, nullptr);
            g_hittest.erase(it);
        }
        SDL_DestroyWindow(w);
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

    void restoreWindow(WindowHandle win, const WindowState& prev) {
        if (!win) return;
        SDL_SetWindowFullscreen(as_sdl(win), false);
        SDL_SetWindowPosition(as_sdl(win), prev.rect.x(), prev.rect.y());
        SDL_SetWindowSize(as_sdl(win), prev.rect.width(), prev.rect.height());
    }

    Point clientToScreen(WindowHandle win, int x, int y) {
        if (!win) return Point{0,0};
        int wx=0, wy=0; SDL_GetWindowPosition(as_sdl(win), &wx, &wy);
        return Point{ wx + x, wy + y };
    }

    Point screenToClient(WindowHandle win, int x, int y) {
        if (!win) return Point{0,0};
        int wx=0, wy=0; SDL_GetWindowPosition(as_sdl(win), &wx, &wy);
        return Point{ x - wx, y - wy };
    }

    void showCursor(bool show) {
        if (show) SDL_ShowCursor(); else SDL_HideCursor();
    }

    bool setRelativeMouseMode(WindowHandle win, bool enabled) {
        if (!win) return false;
        return SDL_SetWindowRelativeMouseMode(as_sdl(win), enabled);
    }

    void clipCursor(WindowHandle win, const Rect& rect) {
        if (!win) return;
        SDL_Rect r{ rect.x(), rect.y(), rect.width(), rect.height() };
        if (rect.width() <= 0 || rect.height() <= 0) {
            SDL_SetWindowMouseRect(as_sdl(win), nullptr);
            SDL_SetWindowMouseGrab(as_sdl(win), false);
        } else {
            SDL_SetWindowMouseRect(as_sdl(win), &r);
            SDL_SetWindowMouseGrab(as_sdl(win), true);
        }
    }

    int getDropFileCount(void* handle) {
        if (!handle) return 0;
        return (int)((DropData*)handle)->files.size();
    }

    const char* getDropFile(void* handle, int index) {
        DropData* d = (DropData*)handle;
        if (!d) return nullptr;
        if (index < 0 || (size_t)index >= d->files.size()) return nullptr;
        return d->files[(size_t)index].c_str();
    }

    void finishDrag(void* handle) {
        DropData* d = (DropData*)handle;
        delete d;
    }
}
