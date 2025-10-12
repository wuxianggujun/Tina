#pragma once

namespace Tina::Engine {

// 前向声明
class EventSystem;

/**
 * 事件订阅者基类
 *
 * 任何需要订阅事件的类都应该继承这个基类
 * 在析构时会自动取消所有订阅
 */
class EventSubscriberBase {
public:
    EventSubscriberBase() = default;
    virtual ~EventSubscriberBase();

    // 设置关联的事件系统
    void setEventSystem(EventSystem* eventSystem) {
        m_eventSystem = eventSystem;
    }

protected:
    EventSystem* m_eventSystem = nullptr;

private:
    friend class EventSystem;
};

} // namespace Tina::Engine