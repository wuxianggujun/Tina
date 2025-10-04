//
// 轻量布局容器：水平/垂直栈
//

#pragma once

#include "UINode.hpp"

namespace Tina::UI {

enum class CrossAlign { Start, Center, End };

class UIHStack : public UINode {
public:
    UIHStack(const std::string& name = "HStack") : UINode(name) {}
    void setSpacing(float s) { m_spacing = s; m_dirty = true; }
    void setPadding(float px, float py) { m_padX = px; m_padY = py; m_dirty = true; }
    void setCrossAlign(CrossAlign a) { m_cross = a; m_dirty = true; }

protected:
    void onUpdate(float dt) override {
        (void)dt;
        // 简单流式布局
        float x = m_padX;
        const auto size = getSize();
        for (auto* child : getChildren()) {
            if (!child || !child->isVisible()) continue;
            auto cs = child->getSize();
            float y = m_padY;
            if (m_cross == CrossAlign::Center) y = (size.y - cs.y) * 0.5f;
            else if (m_cross == CrossAlign::End) y = size.y - m_padY - cs.y;
            child->setAnchor(Anchor::TopLeft);
            child->setPosition(x, y);
            x += cs.x + m_spacing;
        }
    }

private:
    float m_spacing = 6.0f;
    float m_padX = 8.0f, m_padY = 8.0f;
    CrossAlign m_cross = CrossAlign::Center;
};

class UIVStack : public UINode {
public:
    UIVStack(const std::string& name = "VStack") : UINode(name) {}
    void setSpacing(float s) { m_spacing = s; m_dirty = true; }
    void setPadding(float px, float py) { m_padX = px; m_padY = py; m_dirty = true; }
    void setCrossAlign(CrossAlign a) { m_cross = a; m_dirty = true; }

protected:
    void onUpdate(float dt) override {
        (void)dt;
        float y = m_padY;
        const auto size = getSize();
        for (auto* child : getChildren()) {
            if (!child || !child->isVisible()) continue;
            auto cs = child->getSize();
            float x = m_padX;
            if (m_cross == CrossAlign::Center) x = (size.x - cs.x) * 0.5f;
            else if (m_cross == CrossAlign::End) x = size.x - m_padX - cs.x;
            child->setAnchor(Anchor::TopLeft);
            child->setPosition(x, y);
            y += cs.y + m_spacing;
        }
    }

private:
    float m_spacing = 6.0f;
    float m_padX = 8.0f, m_padY = 8.0f;
    CrossAlign m_cross = CrossAlign::Center;
};

} // namespace Tina::UI

