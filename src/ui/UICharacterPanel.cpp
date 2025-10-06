//
// UICharacterPanel 实现
//

#include "UICharacterPanel.hpp"
#include "UICore.hpp"

namespace Tina::UI {

UICharacterPanel::UICharacterPanel(const std::string& name)
    : UINode(name)
{
    // 设置面板大小
    setSize(300, 150);

    // 创建背景
    m_background = new UIPanel("PanelBackground");
    m_background->setPosition(0, 0);
    m_background->setSize(300, 150);
    m_background->setColor(0.15f, 0.15f, 0.18f, 0.95f);
    addChild(m_background);

    // 创建标题标签
    m_titleLabel = new UILabel("TitleLabel");
    m_titleLabel->setPosition(10, 10);
    m_titleLabel->setSize(280, 30);
    m_titleLabel->setText("角色信息");
    m_titleLabel->setColor(1.0f, 1.0f, 0.5f, 1.0f);  // 金色
    m_background->addChild(m_titleLabel);

    // 创建名称标签
    m_nameLabel = new UILabel("NameLabel");
    m_nameLabel->setPosition(10, 45);
    m_nameLabel->setSize(280, 25);
    m_nameLabel->setText("名称: Unknown");
    m_nameLabel->setColor(1.0f, 1.0f, 1.0f, 1.0f);
    m_background->addChild(m_nameLabel);

    // 创建血量文本标签
    m_healthLabel = new UILabel("HealthLabel");
    m_healthLabel->setPosition(10, 75);
    m_healthLabel->setSize(280, 20);
    m_healthLabel->setText("生命值:");
    m_healthLabel->setColor(1.0f, 1.0f, 1.0f, 1.0f);
    m_background->addChild(m_healthLabel);

    // 创建血条
    m_healthBar = new UIProgressBar("HealthBar");
    m_healthBar->setPosition(10, 100);
    m_healthBar->setSize(280, 30);
    m_healthBar->setProgress(1.0f);
    m_healthBar->setBackgroundColor(0.3f, 0.1f, 0.1f, 0.8f);  // 深红色背景
    m_healthBar->setFillColor(0.2f, 0.8f, 0.2f, 1.0f);        // 绿色填充
    m_healthBar->setBorderColor(0.0f, 0.0f, 0.0f, 1.0f);
    m_healthBar->setBorderWidth(2.0f);
    m_background->addChild(m_healthBar);

    // 默认隐藏
    setVisible(false);
}

void UICharacterPanel::updateData(const std::string& name, float healthPercent) {
    // 更新名称
    m_nameLabel->setText("名称: " + name);

    // 更新血条
    m_healthBar->setProgress(healthPercent);

    // 根据血量改变血条颜色
    if (healthPercent > 0.6f) {
        m_healthBar->setFillColor(0.2f, 0.8f, 0.2f, 1.0f);  // 绿色（健康）
    } else if (healthPercent > 0.3f) {
        m_healthBar->setFillColor(0.9f, 0.7f, 0.2f, 1.0f);  // 黄色（受伤）
    } else {
        m_healthBar->setFillColor(0.9f, 0.2f, 0.2f, 1.0f);  // 红色（危险）
    }
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
