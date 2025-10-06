//
// 进度条组件（用于血条、经验条等）
//

#pragma once

#include "UINode.hpp"

namespace Tina::UI {

class UIProgressBar : public UINode {
public:
    UIProgressBar(const std::string& name = "ProgressBar")
        : UINode(name)
        , m_progress(1.0f)
        , m_bgColor{0.2f, 0.2f, 0.2f, 0.8f}
        , m_fillColor{0.2f, 0.8f, 0.2f, 1.0f}
        , m_borderColor{0.0f, 0.0f, 0.0f, 1.0f}
        , m_borderWidth(2.0f)
        , m_showBorder(true)
    {}

    // 设置进度（0.0 - 1.0）
    void setProgress(float progress) {
        m_progress = std::max(0.0f, std::min(1.0f, progress));
    }

    float getProgress() const { return m_progress; }

    // 设置颜色
    void setBackgroundColor(float r, float g, float b, float a) {
        m_bgColor = {r, g, b, a};
    }

    void setFillColor(float r, float g, float b, float a) {
        m_fillColor = {r, g, b, a};
    }

    void setBorderColor(float r, float g, float b, float a) {
        m_borderColor = {r, g, b, a};
    }

    void setBorderWidth(float width) { m_borderWidth = width; }
    void setShowBorder(bool show) { m_showBorder = show; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    float m_progress;           // 当前进度（0-1）
    Tina::Math::Vec4 m_bgColor;     // 背景颜色
    Tina::Math::Vec4 m_fillColor;   // 填充颜色
    Tina::Math::Vec4 m_borderColor; // 边框颜色
    float m_borderWidth;        // 边框宽度
    bool m_showBorder;          // 是否显示边框
};

} // namespace Tina::UI
