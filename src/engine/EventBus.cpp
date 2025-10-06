#include "EventBus.hpp"

namespace Tina::Engine {

void EventBus::dispatchOSEvent(const Tina::os::Event& event)
{
    using E = Tina::os::Event;
    switch (event.type) {
        case E::Type::WINDOW_SIZE:
            onWindowResized.emit(event.win_size.w, event.win_size.h);
            break;
        case E::Type::QUIT:
        case E::Type::WINDOW_CLOSE:
            onWindowClosed.emit();
            break;
        case E::Type::KEY:
            if (event.key.down) onKeyPressed.emit((int)event.key.key_code, event.key.is_repeat);
            else onKeyReleased.emit((int)event.key.key_code);
            break;
        case E::Type::MOUSE_BUTTON:
            if (event.mouse_button.down)
                onMouseButtonPressed.emit((int)event.mouse_button.button, 0, 0);
            else
                onMouseButtonReleased.emit((int)event.mouse_button.button, 0, 0);
            break;
        case E::Type::MOUSE_MOVE:
            onMouseMoved.emit(event.mouse_move.xrel, event.mouse_move.yrel);
            break;
        case E::Type::MOUSE_WHEEL:
            onMouseWheel.emit(event.mouse_wheel.amount);
            break;
        default:
            break;
    }
}

} // namespace Tina::Engine
