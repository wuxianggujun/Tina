#pragma once

#include "UINode.hpp"

namespace Tina::UI {

enum class UIScrollAxis : uint8_t {
    Vertical,
    Horizontal,
    Both
};

// Generic retained-mode viewport. Children belong under content(); the base
// UINode clips that subtree and EventSystem applies the same boundary to hit-test.
class UIScrollView : public UINode {
public:
    explicit UIScrollView(const std::string& name = "ScrollView");

    UINode* content() noexcept { return m_content; }
    const UINode* content() const noexcept { return m_content; }

    void setContentSize(float width, float height);
    Tina::Math::Vec2 contentSize() const noexcept { return m_contentSize; }

    void setScrollAxis(UIScrollAxis axis) noexcept { m_axis = axis; clampOffsets(); }
    UIScrollAxis scrollAxis() const noexcept { return m_axis; }

    void setSmoothScroll(bool enabled) noexcept;
    bool smoothScroll() const noexcept { return m_smoothScroll; }
    void setScrollResponse(float responsePerSecond) noexcept;
    float scrollResponse() const noexcept { return m_scrollResponse; }
    void setWheelStep(float logicalPixels) noexcept;
    float wheelStep() const noexcept { return m_wheelStep; }

    void scrollTo(float x, float y);
    void scrollBy(float deltaX, float deltaY);
    Tina::Math::Vec2 scrollOffset() const noexcept { return m_scrollOffset; }
    Tina::Math::Vec2 targetScrollOffset() const noexcept { return m_targetOffset; }
    Tina::Math::Vec2 maximumScrollOffset() const noexcept;

    bool acceptsMouseWheel() const override { return true; }
    void onMouseWheel(float dx, float dy) override;

protected:
    void onUpdate(float dt) override;

private:
    void clampOffsets();
    void applyContentOffset();

    UINode* m_content = nullptr;
    Tina::Math::Vec2 m_contentSize{100.0f, 100.0f};
    Tina::Math::Vec2 m_scrollOffset{0.0f, 0.0f};
    Tina::Math::Vec2 m_targetOffset{0.0f, 0.0f};
    UIScrollAxis m_axis = UIScrollAxis::Vertical;
    bool m_smoothScroll = true;
    float m_scrollResponse = 18.0f;
    float m_wheelStep = 48.0f;
};

} // namespace Tina::UI
