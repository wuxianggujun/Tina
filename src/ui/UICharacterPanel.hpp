//
// 角色信息面板（显示角色名称、血条等）
//

#pragma once

#include "UINode.hpp"
#include "UIComponents.hpp"
#include "UIProgressBar.hpp"
#include "UIEventSystem.hpp"
#include "UIEvents.hpp"
#include "../engine/EventSystem.hpp"
#include "../ecs/Components.hpp"

namespace Tina::UI {

class UICharacterPanel : public UINode {
public:
    UICharacterPanel(const std::string& name = "CharacterPanel");
    ~UICharacterPanel() override = default;

    // 更新面板数据（从ECS读取）
    void updateData(const std::string& name, float healthPercent, bool isControlled);

    // 设置面板位置（居中显示）
    void centerOnScreen(int screenWidth, int screenHeight);

    // 覆盖UINode的onWindowSizeChanged()方法（框架自动调用）
    void onWindowSizeChanged(int screenWidth, int screenHeight) override {
        centerOnScreen(screenWidth, screenHeight);
    }

    // === 事件系统 ===
    void setEventSystem(Engine::EventSystem* eventSystem) {
        m_eventSystem = eventSystem;
        // 同时设置给按钮
        if (m_switchButton) {
            m_switchButton->setEventSystem(eventSystem);
        }
    }

    // 触发切换控制事件
    void onSwitchControlClicked();

    // 事件系统访问
    UIEventSystem& events() { return m_events; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    UIPanel* m_background = nullptr;       // 背景面板
    UILabel* m_titleLabel = nullptr;       // 标题（"角色信息"）
    UILabel* m_nameLabel = nullptr;        // 角色名称
    UILabel* m_healthLabel = nullptr;      // 血量文本（"生命值："）
    UIProgressBar* m_healthBar = nullptr;  // 血条
    UIButton* m_switchButton = nullptr;    // 切换控制按钮
    UILabel* m_controlledLabel = nullptr;  // 当前控制状态标签

    UIEventSystem m_events;                // UI事件系统（局部）
    Engine::EventSystem* m_eventSystem = nullptr; // 全局事件系统指针

    // 按钮点击事件订阅令牌
    Engine::SubscriptionToken m_switchButtonClickToken;
};

} // namespace Tina::UI
