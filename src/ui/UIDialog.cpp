#include "UIDialog.hpp"
#include "UICore.hpp"

namespace Tina::UI {

// === 显示/隐藏 ===
void UIDialog::show() {
    if (m_visible) return;
    m_visible = true;
    ensureUICreated();
    setVisible(true);

    // 订阅键盘按下事件：支持 Enter 确认、Escape 取消
    if (eventSystem() && !m_keyToken) {
        m_keyToken = eventSystem()->subscribe<Tina::Engine::Events::KeyPressedEvent>(
            [this](const Tina::Engine::Events::KeyPressedEvent& e) {
                if (m_visible) handleKeyPressed(e);
            }
        );
    }
}

void UIDialog::hide() {
    if (!m_visible) return;
    m_visible = false;
    setVisible(false);
    // 取消键盘订阅
    m_keyToken.reset();
}

// === 内容区域访问 ===
UINode* UIDialog::getContentArea() {
    // 如果UI还未创建，先创建
    ensureUICreated();
    return m_contentArea;
}

// === 内容设置 ===
void UIDialog::setTitle(const std::string& title) {
    m_title = title;
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

void UIDialog::setConfirmText(const std::string& text) {
    if (m_btnConfirm) {
        m_btnConfirm->setText(text);
    }
}

void UIDialog::setCancelText(const std::string& text) {
    if (m_btnCancel) {
        m_btnCancel->setText(text);
    }
}

// === 鼠标事件 ===
void UIDialog::onClick() {
    // 点击遮罩层关闭对话框
    hide();
    if (m_onCancel) {
        m_onCancel();
    }
}

// === 渲染 ===
void UIDialog::onRender(uint16_t viewId, UIRenderer& renderer) {
    if (!m_visible) return;

    // 使用分层渲染：对话框设置在更高层，无需手动 flush。

    // 1. 绘制半透明遮罩层（全屏）
    auto size = getSize();
    renderer.drawRect(viewId, 0, 0, size.x, size.y, m_maskColor);

    // 2. 子节点会自动渲染（对话框面板等）
}

// === 创建对话框UI ===
void UIDialog::createDialogUI() {
    // ✅ 响应式尺寸：根据屏幕大小自动调整
    // - 占屏幕的50%宽和40%高（更大一些）
    // - 最小尺寸：500x400（防止太小）
    // - 最大尺寸：1200x800（允许更大）
    
    // 对话框面板（使用响应式尺寸）
    auto panel = createChild<UIPanel>("DialogPanel");
    m_dialogPanel = panel;
    m_dialogPanel->setSizePercent(0.5f, 0.4f)  // 50%宽，40%高（更大）
                 ->setMinSize(500, 400)         // 最小尺寸（更大）
                 ->setMaxSize(1200, 800)        // 最大尺寸（允许更大）
                 ->center()
                 ->setColor(m_dialogBgColor)
                 ->setInteractable(true)
                 ->setClickable(true);  // ✅ 完美的响应式布局！
    
    // 获取实际计算后的尺寸（用于后续布局）
    auto dialogSize = m_dialogPanel->getSize();
    float dialogW = dialogSize.x;
    float dialogH = dialogSize.y;

    // 渲染层：将整个对话框（含子节点）置于更高层，确保覆盖下层文本
    setLayer(LAYER_DIALOG); // 使用统一常量

    // 调试：打印父节点尺寸和对话框位置
    auto parentSize = getSize();
    auto panelPos = m_dialogPanel->getPosition();
    auto panelWorldPos = m_dialogPanel->getWorldPosition();
    TINA_DEBUG("对话框居中调试:");
    TINA_DEBUG("  父节点尺寸: {}x{}", parentSize.x, parentSize.y);
    TINA_DEBUG("  对话框尺寸: {}x{}", dialogW, dialogH);
    TINA_DEBUG("  对话框本地位置: ({}, {})", panelPos.x, panelPos.y);
    TINA_DEBUG("  对话框世界位置: ({}, {})", panelWorldPos.x, panelWorldPos.y);
    TINA_DEBUG("  预期中心: ({}, {})", parentSize.x * 0.5f, parentSize.y * 0.5f);

    // ✅ 使用响应式布局：所有子元素相对于对话框面板
    
    // 边框效果（通过一个稍大的底层面板实现）
    auto border = m_dialogPanel->createChild<UIPanel>("BorderPanel");
    border->setPosition(-3, -3);
    border->setSizePercent(1.0f, 1.0f);  // ✅ 100%填充（会被position偏移）
    border->setColor(Tina::Core::Color(0.05f, 0.05f, 0.1f, 1.0f));
    border->setZIndex(-1);

    // 标题栏（使用百分比宽度，固定高度）
    auto titleLabel = m_dialogPanel->createChild<UILabel>("TitleLabel");
    m_titleLabel = titleLabel;
    m_titleLabel->setText(m_title);
    m_titleLabel->setColor(m_titleColor);
    m_titleLabel->setFontPx(24);
    m_titleLabel->setPosition(20, 20);
    m_titleLabel->setSize(dialogW - 40, 40);  // 宽度 = 对话框宽度 - 左右边距
    m_titleLabel->setAlignment(UILabel::TextAlignH::Center, UILabel::TextAlignV::Center);

    // 内容区域（使用百分比尺寸）
    auto content = m_dialogPanel->createChild<UINode>("ContentArea");
    m_contentArea = content;
    m_contentArea->setPosition(20, 70);
    m_contentArea->setSize(dialogW - 40, dialogH - 130);  // 动态计算尺寸

    // 按钮区域（使用相对布局）
    const float btnW = 120.0f;
    const float btnH = 40.0f;
    const float btnGap = 20.0f;
    const float btnY = dialogH - 60.0f;  // ✅ 距离底部60px（动态计算）
    const float cx = dialogW * 0.5f;
    const float leftX  = cx - (btnGap * 0.5f) - btnW;
    const float rightX = cx + (btnGap * 0.5f);

    auto btnCancel = m_dialogPanel->createChild<UIButton>("BtnCancel");
    m_btnCancel = btnCancel;
    m_btnCancel->setText("取消");
    m_btnCancel->setSize(btnW, btnH);
    m_btnCancel->setPosition(leftX, btnY);
    m_btnCancel->setFontPx(20);
    m_btnCancel->setOnClick([this]{ onCancelClicked(); });

    auto btnConfirm = m_dialogPanel->createChild<UIButton>("BtnConfirm");
    m_btnConfirm = btnConfirm;
    m_btnConfirm->setText("确定");
    m_btnConfirm->setSize(btnW, btnH);
    m_btnConfirm->setPosition(rightX, btnY);
    m_btnConfirm->setFontPx(20);
    m_btnConfirm->setOnClick([this]{ onConfirmClicked(); });
}

// === 窗口尺寸变化回调 ===
void UIDialog::onWindowSizeChanged(int width, int height) {
    // 更新遮罩层尺寸
    setSize(static_cast<float>(width), static_cast<float>(height));

    // 调用父类默认实现（递归通知子节点，会触发百分比尺寸更新）
    UINode::onWindowSizeChanged(width, height);
    
    // ✅ 重新布局对话框内部元素
    if (m_dialogPanel) {
        relayoutDialogContent();
    }
}

// === 重新布局对话框内容 ===
void UIDialog::relayoutDialogContent() {
    auto dialogSize = m_dialogPanel->getSize();
    float dialogW = dialogSize.x;
    float dialogH = dialogSize.y;
    
    // 更新标题栏尺寸
    if (m_titleLabel) {
        m_titleLabel->setSize(dialogW - 40, 40);
    }
    
    // 更新内容区域尺寸
    if (m_contentArea) {
        m_contentArea->setSize(dialogW - 40, dialogH - 130);
        
        // ✅ 重要：更新ContentArea的所有子元素宽度
        // 因为它们的宽度应该跟随ContentArea
        auto& children = m_contentArea->getChildren();
        for (auto& child : children) {
            if (child) {
                auto childSize = child->getSize();
                // 保持高度不变，只更新宽度
                child->setSize(dialogW - 40, childSize.y);
            }
        }
    }
    
    // 更新按钮位置
    if (m_btnCancel && m_btnConfirm) {
        const float btnW = 120.0f;
        const float btnGap = 20.0f;
        const float btnY = dialogH - 60.0f;
        const float cx = dialogW * 0.5f;
        const float leftX  = cx - (btnGap * 0.5f) - btnW;
        const float rightX = cx + (btnGap * 0.5f);
        
        m_btnCancel->setPosition(leftX, btnY);
        m_btnConfirm->setPosition(rightX, btnY);
    }
}

// === 更新对话框位置 ===
void UIDialog::updateDialogPosition() {
    // 使用 Anchor 后不再需要手动更新位置
    // Anchor 会自动处理居中
}

// === 按钮回调 ===
void UIDialog::onConfirmClicked() {
    hide();
    if (m_onConfirm) {
        m_onConfirm();
    }
}

void UIDialog::onCancelClicked() {
    hide();
    if (m_onCancel) {
        m_onCancel();
    }
}

void UIDialog::handleKeyPressed(const Tina::Engine::Events::KeyPressedEvent& e) {
    using Tina::Engine::Events::KeyCode;
    if (e.isRepeat) return;  // 避免长按重复触发
    if (e.key == KeyCode::Enter) {
        onConfirmClicked();
    } else if (e.key == KeyCode::Escape) {
        onCancelClicked();
    }
}

} // namespace Tina::UI
