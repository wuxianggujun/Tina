//
// UIEvents.hpp - 统一的UI事件定义（集成到引擎事件系统）
// 替代原有的 ui/UIEvents.hpp
//

#pragma once

#include "EventSystem.hpp"
#include <cstring>
#include <algorithm>

namespace Tina::Engine {

// ==================== 鼠标事件 ====================

// 鼠标点击事件
struct MouseClickEvent : UIEvent<MouseClickEvent, EventTypeId::UIButtonClicked> {
    float mouseX = 0;
    float mouseY = 0;
    int button = 0;  // 0=left, 1=middle, 2=right
    
    MouseClickEvent() {
        this->priority = EventPriority::Low;  // UI事件低优先级
    }
};

// 鼠标进入事件
struct MouseEnterEvent : UIEvent<MouseEnterEvent, EventTypeId::UIHoverEnter> {
    float mouseX = 0;
    float mouseY = 0;
    
    MouseEnterEvent() {
        this->priority = EventPriority::Low;
    }
};

// 鼠标离开事件
struct MouseLeaveEvent : UIEvent<MouseLeaveEvent, EventTypeId::UIHoverLeave> {
    float mouseX = 0;
    float mouseY = 0;
    
    MouseLeaveEvent() {
        this->priority = EventPriority::Low;
    }
};

// ==================== 焦点事件 ====================

// 焦点获得事件
struct FocusGainedEvent : UIEvent<FocusGainedEvent, EventTypeId::UIFocusGained> {
    FocusGainedEvent() {
        this->priority = EventPriority::Low;
    }
};

// 焦点失去事件
struct FocusLostEvent : UIEvent<FocusLostEvent, EventTypeId::UIFocusLost> {
    FocusLostEvent() {
        this->priority = EventPriority::Low;
    }
};

// ==================== 按钮事件（高级封装） ====================

// 按钮点击事件（直接继承 UIEvent，包含鼠标信息）
struct ButtonClickEvent : UIEvent<ButtonClickEvent, EventTypeId::UIButtonClicked> {
    uint32_t buttonId = 0;
    char buttonName[64] = {0};
    float mouseX = 0;
    float mouseY = 0;
    int button = 0;  // 0=left, 1=middle, 2=right
    
    ButtonClickEvent() {
        this->priority = EventPriority::Low;
    }
    
    ButtonClickEvent(uint32_t id, const char* name = nullptr)
        : buttonId(id) {
        if (name) {
            size_t len = std::strlen(name);
            len = std::min(len, size_t(63));
            std::memcpy(buttonName, name, len);
            buttonName[len] = '\0';
        }
        this->priority = EventPriority::Low;
    }
};

// ==================== 文本输入事件 ====================

// 文本改变事件
struct TextChangedEvent : UIEvent<TextChangedEvent, EventTypeId::UITextChanged> {
    char oldText[256] = {0};
    char newText[256] = {0};
    
    TextChangedEvent() {
        this->priority = EventPriority::Low;
    }
};

} // namespace Tina::Engine
