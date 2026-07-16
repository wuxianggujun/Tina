#pragma once

#include "UIStyle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace Tina::UI {

struct UIViewportMetrics {
    int logicalWidth = 1;
    int logicalHeight = 1;
    int framebufferWidth = 1;
    int framebufferHeight = 1;
    float framebufferScaleX = 1.0f;
    float framebufferScaleY = 1.0f;
    float userScale = 1.0f;

    float densityScale() const noexcept
    {
        return std::sqrt(framebufferScaleX * framebufferScaleY);
    }

    float effectiveScale() const noexcept
    {
        return densityScale() * userScale;
    }

    float toFramebufferX(float logicalX) const noexcept
    {
        return logicalX * framebufferScaleX;
    }

    float toFramebufferY(float logicalY) const noexcept
    {
        return logicalY * framebufferScaleY;
    }

    float dp(float logicalValue) const noexcept
    {
        return logicalValue * effectiveScale();
    }

    int fontPx(int logicalPixels) const noexcept
    {
        return std::max(1, static_cast<int>(std::lround(dp(static_cast<float>(logicalPixels)))));
    }
};

// Per-window UI state. EventSystem owns one instance and UINodes only observe
// it through their injected window context.
class UIContext {
public:
    const UITheme& theme() const noexcept { return m_theme; }
    const UIViewportMetrics& viewport() const noexcept { return m_viewport; }
    uint64_t themeRevision() const noexcept { return m_themeRevision; }
    uint64_t scaleRevision() const noexcept { return m_scaleRevision; }

    void setTheme(UITheme theme)
    {
        m_theme = std::move(theme);
        ++m_themeRevision;
    }

    bool updateViewport(int logicalWidth, int logicalHeight,
                        int framebufferWidth, int framebufferHeight)
    {
        if (logicalWidth <= 0 || logicalHeight <= 0 ||
            framebufferWidth <= 0 || framebufferHeight <= 0) {
            return false;
        }

        const float scaleX = static_cast<float>(framebufferWidth) /
                             static_cast<float>(logicalWidth);
        const float scaleY = static_cast<float>(framebufferHeight) /
                             static_cast<float>(logicalHeight);
        if (m_viewport.logicalWidth == logicalWidth &&
            m_viewport.logicalHeight == logicalHeight &&
            m_viewport.framebufferWidth == framebufferWidth &&
            m_viewport.framebufferHeight == framebufferHeight) {
            return false;
        }

        m_viewport.logicalWidth = logicalWidth;
        m_viewport.logicalHeight = logicalHeight;
        m_viewport.framebufferWidth = framebufferWidth;
        m_viewport.framebufferHeight = framebufferHeight;
        m_viewport.framebufferScaleX = std::clamp(scaleX, 0.25f, 8.0f);
        m_viewport.framebufferScaleY = std::clamp(scaleY, 0.25f, 8.0f);
        ++m_scaleRevision;
        return true;
    }

    bool setUserScale(float scale)
    {
        scale = std::clamp(scale, 0.5f, 3.0f);
        if (std::abs(m_viewport.userScale - scale) < 0.0001f) return false;
        m_viewport.userScale = scale;
        ++m_scaleRevision;
        return true;
    }

private:
    UITheme m_theme = UITheme::dark();
    UIViewportMetrics m_viewport;
    uint64_t m_themeRevision = 1;
    uint64_t m_scaleRevision = 1;
};

} // namespace Tina::UI
