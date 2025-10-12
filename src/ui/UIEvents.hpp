//
// UIEvents.hpp - UI 系统事件定义
// 用于替代旧的 Signal 机制
//

#pragma once

#include "../engine/EventCore.hpp"
#include "../core/Container.hpp"
#include <cstdint>
#include <cstring>  // for std::strlen, std::memcpy
#include <algorithm>  // for std::min

namespace Tina::UI {

using namespace Tina::Container;

// ==================== UI 事件类型 ID ====================

enum class UIEventType : uint32_t {
    // 鼠标事件
    ButtonClick = 1000,        // 按钮点击
    ButtonHoverEnter,          // 鼠标进入按钮
    ButtonHoverLeave,          // 鼠标离开按钮
    ButtonPressed,             // 按钮按下
    ButtonReleased,            // 按钮释放

    // 面板事件
    PanelSwitchControl,        // 角色面板切换控制

    // 滑动条事件
    SliderValueChanged,        // 滑动条值改变

    // 文本输入事件
    TextInputChanged,          // 文本输入改变
    TextInputSubmit,           // 文本输入提交

    // 通用 UI 事件
    UIElementFocused,          // UI 元素获得焦点
    UIElementLostFocus,        // UI 元素失去焦点

    MaxUIEventTypes
};

// ==================== UI 事件基类 ====================

struct UIEventBase {
    Engine::EventTypeId typeId;         // 事件类型 ID
    uint32_t timestamp = 0;              // 时间戳
    Engine::EventPriority priority = Engine::EventPriority::Medium;  // 优先级
    void* sender = nullptr;              // 发送事件的 UI 元素指针
    String elementName;                   // UI 元素名称

    UIEventBase(UIEventType type, void* sndr = nullptr, const String& name = "")
        : typeId(static_cast<Engine::EventTypeId>(type))
        , sender(sndr)
        , elementName(name) {
        timestamp = static_cast<uint32_t>(Engine::getCurrentTimeMs());
    }

    // 获取类型 ID
    Engine::EventTypeId getTypeId() const { return typeId; }

    virtual ~UIEventBase() = default;
};

// ==================== 具体 UI 事件 ====================

// 按钮点击事件（优化版本 - POD类型）
TINA_EVENT(ButtonClickEvent, UIButtonClicked) {
    uint32_t buttonId = 0;              // 按钮ID
    char buttonName[64] = {0};          // 按钮名称（固定大小）
    void* source = nullptr;              // 按钮指针（可选）
    int mouseX = 0;                      // 鼠标X坐标
    int mouseY = 0;                      // 鼠标Y坐标

    ButtonClickEvent() = default;
    ButtonClickEvent(uint32_t id, const char* name = nullptr, void* btn = nullptr)
        : buttonId(id), source(btn) {
        if (name) {
            size_t len = std::strlen(name);
            len = std::min(len, size_t(63));
            std::memcpy(buttonName, name, len);
        }
        this->priority = Engine::EventPriority::Medium;
    }
};

// 鼠标进入事件
struct ButtonHoverEnterEvent : UIEventBase {
    static constexpr Engine::EventTypeId TYPE_ID =
        static_cast<Engine::EventTypeId>(UIEventType::ButtonHoverEnter);

    ButtonHoverEnterEvent(void* button = nullptr, const String& name = "")
        : UIEventBase(UIEventType::ButtonHoverEnter, button, name) {}
};

// 鼠标离开事件
struct ButtonHoverLeaveEvent : UIEventBase {
    static constexpr Engine::EventTypeId TYPE_ID =
        static_cast<Engine::EventTypeId>(UIEventType::ButtonHoverLeave);

    ButtonHoverLeaveEvent(void* button = nullptr, const String& name = "")
        : UIEventBase(UIEventType::ButtonHoverLeave, button, name) {}
};

// 角色面板切换控制事件
struct PanelSwitchControlEvent : UIEventBase {
    static constexpr Engine::EventTypeId TYPE_ID =
        static_cast<Engine::EventTypeId>(UIEventType::PanelSwitchControl);

    String targetCharacter;    // 要切换到的角色名

    PanelSwitchControlEvent(void* panel = nullptr, const String& name = "")
        : UIEventBase(UIEventType::PanelSwitchControl, panel, name) {}
};

// 滑动条值改变事件
struct SliderValueChangedEvent : UIEventBase {
    static constexpr Engine::EventTypeId TYPE_ID =
        static_cast<Engine::EventTypeId>(UIEventType::SliderValueChanged);

    float oldValue = 0.0f;
    float newValue = 0.0f;

    SliderValueChangedEvent(void* slider = nullptr, float oldVal = 0, float newVal = 0)
        : UIEventBase(UIEventType::SliderValueChanged, slider)
        , oldValue(oldVal)
        , newValue(newVal) {}
};

// ==================== UI 事件辅助函数 ====================

inline const char* uiEventTypeToString(UIEventType type) {
    switch (type) {
        case UIEventType::ButtonClick: return "ButtonClick";
        case UIEventType::ButtonHoverEnter: return "ButtonHoverEnter";
        case UIEventType::ButtonHoverLeave: return "ButtonHoverLeave";
        case UIEventType::ButtonPressed: return "ButtonPressed";
        case UIEventType::ButtonReleased: return "ButtonReleased";
        case UIEventType::PanelSwitchControl: return "PanelSwitchControl";
        case UIEventType::SliderValueChanged: return "SliderValueChanged";
        case UIEventType::TextInputChanged: return "TextInputChanged";
        case UIEventType::TextInputSubmit: return "TextInputSubmit";
        case UIEventType::UIElementFocused: return "UIElementFocused";
        case UIEventType::UIElementLostFocus: return "UIElementLostFocus";
        default: return "UnknownUIEvent";
    }
}

} // namespace Tina::UI