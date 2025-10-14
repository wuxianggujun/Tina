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
    float dialogW = 500;
    float dialogH = 300;

    // 对话框面板（使用流式API，简洁优雅）
    auto panel = createChild<UIPanel>("DialogPanel");
    m_dialogPanel = panel;
    m_dialogPanel->setSize(dialogW, dialogH)
                 ->center()
                 ->setColor(m_dialogBgColor)
                 ->setInteractable(true)
                 ->setClickable(true);  // ✅ 完美的链式调用！

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

    // 边框效果（通过一个稍大的底层面板实现）
    auto border = m_dialogPanel->createChild<UIPanel>("BorderPanel");
    border->setPosition(-3, -3);
    border->setSize(dialogW + 6, dialogH + 6);
    border->setColor(Tina::Core::Color(0.05f, 0.05f, 0.1f, 1.0f));  // 深色边框
    border->setZIndex(-1);  // 放到最底层

    // 标题栏
    auto titleLabel = m_dialogPanel->createChild<UILabel>("TitleLabel");
    m_titleLabel = titleLabel;
    m_titleLabel->setText(m_title);
    m_titleLabel->setColor(m_titleColor);
    m_titleLabel->setFontPx(24);
    m_titleLabel->setPosition(20, 20);
    m_titleLabel->setSize(460, 40);
    m_titleLabel->setAlignment(UILabel::TextAlignH::Center, UILabel::TextAlignV::Center);

    // 内容区域（空节点，由外部添加内容）
    auto content = m_dialogPanel->createChild<UINode>("ContentArea");
    m_contentArea = content;
    m_contentArea->setPosition(20, 70);
    m_contentArea->setSize(460, 160);

    // 按钮区域
    // 底部按钮：左右对称布局（间距=20，按钮宽=120）
    const float btnW = 120.0f;
    const float btnH = 40.0f;
    const float btnGap = 20.0f;
    const float btnY = 240.0f; // 距离面板顶部 240
    const float cx = dialogW * 0.5f;
    const float leftX  = cx - (btnGap * 0.5f) - btnW; // 250 - 10 - 120 = 120
    const float rightX = cx + (btnGap * 0.5f);        // 250 + 10        = 260

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

    // Anchor 会自动重新计算，所以不需要手动调整位置

    // 调用父类默认实现（递归通知子节点）
    UINode::onWindowSizeChanged(width, height);
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
