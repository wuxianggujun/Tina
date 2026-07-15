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
    renderer.drawRect(viewId, x, y, w, h, m_bgColor);

    // 2. 绘制填充部分（根据进度）
    if (m_progress > 0.0f) {
        float fillWidth = w * m_progress;
        renderer.drawRect(viewId, x, y, fillWidth, h, m_fillColor);
    }

    // 3. 绘制边框（可选）
    if (m_showBorder && m_borderWidth > 0.0f) {
        float bw = m_borderWidth;

        // 上边框
        renderer.drawRect(viewId, x, y, w, bw, m_borderColor);
        // 下边框
        renderer.drawRect(viewId, x, y + h - bw, w, bw, m_borderColor);
        // 左边框
        renderer.drawRect(viewId, x, y, bw, h, m_borderColor);
        // 右边框
        renderer.drawRect(viewId, x + w - bw, y, bw, h, m_borderColor);
    }
}

Tina::Math::Vec2 UIProgressBar::measureContent(float availableWidth, float /*availableHeight*/)
{
    (void)availableWidth;
    // 进度条的自然尺寸：至少宽100，高度由边框与最小视觉高度决定
    const float minW = 100.0f;
    const float minH = std::max(12.0f, m_borderWidth * 2.0f + 6.0f);
    return {minW, minH};
}

} // namespace Tina::UI
