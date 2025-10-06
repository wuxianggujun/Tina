//
// UICharacterPanel 实现
//

#include "UICharacterPanel.hpp"
#include "UICore.hpp"
#include "UIColors.hpp"

namespace Tina::UI {

using namespace Tina::UI::UIColors;

UICharacterPanel::UICharacterPanel(const std::string& name)
    : UINode(name)
{
    // 设置面板大小（增加高度以容纳按钮）
    setSize(320, 200);

    // 创建背景
    m_background = new UIPanel("PanelBackground");
    m_background->setPosition(0, 0);
    m_background->setSize(320, 200);
    m_background->setColor(PanelDarkBg);  // 使用预定义颜色
    addChild(m_background);

    // 创建标题标签
    m_titleLabel = new UILabel("TitleLabel");
    m_titleLabel->setPosition(15, 12);
    m_titleLabel->setSize(290, 28);
    m_titleLabel->setText("角色信息");
    m_titleLabel->setColor(TitleGold);  // 金色标题
    m_background->addChild(m_titleLabel);

    // 创建名称标签
    m_nameLabel = new UILabel("NameLabel");
    m_nameLabel->setPosition(15, 50);
    m_nameLabel->setSize(290, 22);
    m_nameLabel->setText("名称: Unknown");
    m_nameLabel->setColor(TextWhite);  // 白色文本
    m_background->addChild(m_nameLabel);

    // 创建控制状态标签
    m_controlledLabel = new UILabel("ControlledLabel");
    m_controlledLabel->setPosition(15, 75);
    m_controlledLabel->setSize(290, 20);
    m_controlledLabel->setText("状态: 未控制");
    m_controlledLabel->setColor(TextGray);  // 灰色文本
    m_background->addChild(m_controlledLabel);

    // 创建血量文本标签
    m_healthLabel = new UILabel("HealthLabel");
    m_healthLabel->setPosition(15, 105);
    m_healthLabel->setSize(290, 18);
    m_healthLabel->setText("生命值:");
    m_healthLabel->setColor(TextWhite);  // 白色文本
    m_background->addChild(m_healthLabel);

    // 创建血条
    m_healthBar = new UIProgressBar("HealthBar");
    m_healthBar->setPosition(15, 128);
    m_healthBar->setSize(290, 26);
    m_healthBar->setProgress(1.0f);
    m_healthBar->setBackgroundColor(ProgressBgDark);  // 深红色背景
    m_healthBar->setFillColor(ProgressGreen);         // 绿色填充（健康）
    m_healthBar->setBorderColor(Border);              // 黑色边框
    m_healthBar->setBorderWidth(2.0f);
    m_background->addChild(m_healthBar);

    // 创建切换控制按钮
    m_switchButton = new UIButton("SwitchButton");
    m_switchButton->setPosition(15, 162);
    m_switchButton->setSize(290, 28);
    m_switchButton->setText("切换控制");
    m_switchButton->setNormalColor(ButtonBlue);      // 蓝色按钮
    m_switchButton->setHoverColor(ButtonBlueHover);  // 蓝色按钮悬停
    m_switchButton->setTextColor(ButtonText);        // 白色文本
    m_switchButton->onClickCallback = [this]() {
        if (m_switchControlCallback) {
            m_switchControlCallback();
            // 点击后隐藏面板
            setVisible(false);
        }
    };
    m_background->addChild(m_switchButton);

    // 初始化事件系统（设置根节点为自己）
    m_events.setRoot(this);

    // 默认隐藏
    setVisible(false);
}

void UICharacterPanel::updateData(const std::string& name, float healthPercent, bool isControlled) {
    // 更新名称
    m_nameLabel->setText("名称: " + name);

    // 更新控制状态
    if (isControlled) {
        m_controlledLabel->setText("状态: 当前控制中");
        m_controlledLabel->setColor(ControlledGreen);  // 绿色（正在控制）
        m_switchButton->setEnabled(false);  // 已经在控制，禁用按钮
        m_switchButton->setNormalColor(ButtonDisabled);  // 灰色（禁用）
    } else {
        m_controlledLabel->setText("状态: 未控制");
        m_controlledLabel->setColor(TextGray);  // 灰色
        m_switchButton->setEnabled(true);   // 可以切换控制
        m_switchButton->setNormalColor(ButtonBlue);  // 蓝色
    }

    // 更新血条（使用Core::Color的HealthColor）
    m_healthBar->setProgress(healthPercent);
    m_healthBar->setFillColor(Tina::Core::Color::HealthColor(healthPercent));
}

void UICharacterPanel::centerOnScreen(int screenWidth, int screenHeight) {
    setAnchor(Anchor::MiddleCenter);
    float x = (screenWidth - getSize().x) * 0.5f;
    float y = (screenHeight - getSize().y) * 0.5f;
    setPosition(x, y);
}

void UICharacterPanel::onRender(uint16_t viewId, UIRenderer& renderer) {
    // 基类会自动渲染所有子节点
}

} // namespace Tina::UI
