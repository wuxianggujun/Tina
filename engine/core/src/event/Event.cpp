#include "tina/event/Event.hpp"
#include "tina/window/Window.hpp"

namespace Tina {

bool Event::isKeyPressed(KeyCode key) {
    return Window::isKeyPressed(static_cast<int32_t>(key));
}

bool Event::isMouseButtonPressed(MouseButton button) {
    return Window::isMouseButtonPressed(static_cast<int32_t>(button));
}

glm::vec2 Event::getMousePosition() {
    double x, y;
    Window::getMousePosition(x, y);
    return {static_cast<float>(x), static_cast<float>(y)};
}

void Event::setMousePosition(float x, float y) {
    Window::setMousePosition(static_cast<double>(x), static_cast<double>(y));
}

void Event::setMouseVisible(bool visible) {
    Window::setMouseCursor(visible);
}

} // namespace Tina 