//
// 进度条组件（用于血条、经验条等）
//

#pragma once

#include "UINode.hpp"
#include "../core/Color.hpp"
#include "UIColors.hpp"

namespace Tina::UI {

class UIProgressBar : public UINode {
public:
    UIProgressBar(const std::string& name = "ProgressBar")
        : UINode(name)
        , m_progress(1.0f)
        , m_bgColor(Tina::UI::UIColors::PanelBg)
        , m_fillColor(Tina::UI::UIColors::ProgressGreen)
        , m_borderColor(Tina::UI::UIColors::Border)
        , m_borderWidth(2.0f)
        , m_showBorder(true)
    {}

    // 设置进度（0.0 - 1.0）
    void setProgress(float progress) {
        m_progress = std::max(0.0f, std::min(1.0f, progress));
    }

    float getProgress() const { return m_progress; }

    // 设置颜色（直接接受Color对象）
    void setBackgroundColor(const Tina::Core::Color& c) { m_bgColor = c; }
    void setFillColor(const Tina::Core::Color& c) { m_fillColor = c; }
    void setBorderColor(const Tina::Core::Color& c) { m_borderColor = c; }

    // 兼容旧的RGBA参数接口
    void setBackgroundColor(float r, float g, float b, float a) { m_bgColor = Tina::Core::Color(r, g, b, a); }
    void setFillColor(float r, float g, float b, float a) { m_fillColor = Tina::Core::Color(r, g, b, a); }
    void setBorderColor(float r, float g, float b, float a) { m_borderColor = Tina::Core::Color(r, g, b, a); }

    void setBorderWidth(float width) { m_borderWidth = width; }
    void setShowBorder(bool show) { m_showBorder = show; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    float m_progress;                  // 当前进度（0-1）
    Tina::Core::Color m_bgColor;       // 背景颜色
    Tina::Core::Color m_fillColor;     // 填充颜色
    Tina::Core::Color m_borderColor;   // 边框颜色
    float m_borderWidth;               // 边框宽度
    bool m_showBorder;                 // 是否显示边框
};

} // namespace Tina::UI
