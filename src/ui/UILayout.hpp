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
        float usedW = 0.0f;
        for (auto* child : getChildren()) {
            if (!child || !child->isVisible()) continue;
            auto cs = child->getSize();
            const float ml = child->marginLeft();
            const float mr = child->marginRight();
            const float mt = child->marginTop();
            const float mb = child->marginBottom();
            // MatchParent 宽度：由父容器分配（减去边距）
            if (child->layoutWidth() == LayoutDim::MatchParent) {
                cs.x = std::max(0.0f, size.x - m_padX * 2.0f - ml - mr);
                child->setWidth(cs.x);
            }
            float y = m_padY + mt;
            if (m_cross == CrossAlign::Center) y = (size.y - cs.y) * 0.5f;
            else if (m_cross == CrossAlign::End) y = size.y - m_padY - cs.y - mb;
            child->setAnchor(Anchor::TopLeft);
            child->setPosition(x + ml, y);
            x += ml + cs.x + mr + m_spacing;
            usedW = x;
        }
        // WrapContent 自身宽度
        if (layoutWidth() == LayoutDim::WrapContent) {
            float desired = usedW > 0.0f ? (usedW - m_spacing + m_padX) : (m_padX * 2.0f);
            if (desired < 0.0f) desired = 0.0f;
            if (desired != size.x) setWidth(desired);
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
        float maxW = 0.0f;
        for (auto* child : getChildren()) {
            if (!child || !child->isVisible()) continue;
            auto cs = child->getSize();
            const float ml = child->marginLeft();
            const float mr = child->marginRight();
            const float mt = child->marginTop();
            const float mb = child->marginBottom();
            if (child->layoutWidth() == LayoutDim::MatchParent) {
                cs.x = std::max(0.0f, size.x - m_padX * 2.0f - ml - mr);
                child->setWidth(cs.x);
            }
            float x = m_padX + ml;
            if (m_cross == CrossAlign::Center) x = (size.x - cs.x) * 0.5f;
            else if (m_cross == CrossAlign::End) x = size.x - m_padX - cs.x - mr;
            child->setAnchor(Anchor::TopLeft);
            child->setPosition(x, y + mt);
            y += mt + cs.y + mb;
            // 下一个可见子项再加 spacing
            // 简化：无预先统计，直接在下次迭代中判断
            // 为了不多加一次 spacing，在本次就先加，并在循环结束后统一修正
            y += m_spacing;
            if (cs.x > maxW) maxW = cs.x;
        }
        // 移除最后一次多加的 spacing（如果有任何子项）
        if (!getChildren().empty()) y -= m_spacing;
        float desiredH = y + m_padY;
        if (desiredH < 0.0f) desiredH = 0.0f;
        if (layoutHeight() == LayoutDim::WrapContent && desiredH != size.y) setHeight(desiredH);
    }

private:
    float m_spacing = 6.0f;
    float m_padX = 8.0f, m_padY = 8.0f;
    CrossAlign m_cross = CrossAlign::Center;
};

} // namespace Tina::UI
