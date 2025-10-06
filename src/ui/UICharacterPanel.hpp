//
// 角色信息面板（显示角色名称、血条等）
//

#pragma once

#include "UINode.hpp"
#include "UIComponents.hpp"
#include "UIProgressBar.hpp"
#include "../ecs/Components.hpp"

namespace Tina::UI {

class UICharacterPanel : public UINode {
public:
    UICharacterPanel(const std::string& name = "CharacterPanel");
    ~UICharacterPanel() override = default;

    // 更新面板数据（从ECS读取）
    void updateData(const std::string& name, float healthPercent);

    // 设置面板位置（居中显示）
    void centerOnScreen(int screenWidth, int screenHeight);

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    UIPanel* m_background = nullptr;       // 背景面板
    UILabel* m_titleLabel = nullptr;       // 标题（"角色信息"）
    UILabel* m_nameLabel = nullptr;        // 角色名称
    UILabel* m_healthLabel = nullptr;      // 血量文本（"生命值："）
    UIProgressBar* m_healthBar = nullptr;  // 血条
};

} // namespace Tina::UI
