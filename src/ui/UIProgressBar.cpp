//
// UIProgressBar 实现
//

#include "UIProgressBar.hpp"
#include "UICore.hpp"
#include <algorithm>

namespace Tina::UI {

void UIProgressBar::onRender(uint16_t viewId, UIRenderer& renderer) {
    auto worldPos = getWorldPosition();
    auto size = getSize();

    float x = worldPos.x;
    float y = worldPos.y;
    float w = size.x;
    float h = size.y;

    // 1. 绘制背景
    renderer.drawRect(viewId, x, y, w, h,
                      m_bgColor.x, m_bgColor.y, m_bgColor.z, m_bgColor.w);

    // 2. 绘制填充部分（根据进度）
    if (m_progress > 0.0f) {
        float fillWidth = w * m_progress;
        renderer.drawRect(viewId, x, y, fillWidth, h,
                          m_fillColor.x, m_fillColor.y, m_fillColor.z, m_fillColor.w);
    }

    // 3. 绘制边框（可选）
    if (m_showBorder && m_borderWidth > 0.0f) {
        float bw = m_borderWidth;

        // 上边框
        renderer.drawRect(viewId, x, y, w, bw,
                          m_borderColor.x, m_borderColor.y, m_borderColor.z, m_borderColor.w);
        // 下边框
        renderer.drawRect(viewId, x, y + h - bw, w, bw,
                          m_borderColor.x, m_borderColor.y, m_borderColor.z, m_borderColor.w);
        // 左边框
        renderer.drawRect(viewId, x, y, bw, h,
                          m_borderColor.x, m_borderColor.y, m_borderColor.z, m_borderColor.w);
        // 右边框
        renderer.drawRect(viewId, x + w - bw, y, bw, h,
                          m_borderColor.x, m_borderColor.y, m_borderColor.z, m_borderColor.w);
    }
}

} // namespace Tina::UI
