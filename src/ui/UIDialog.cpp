#include "UIDialog.hpp"
#include "UICore.hpp"

namespace Tina::UI {

// === 显示/隐藏 ===
void UIDialog::show() {
    if (m_visible) return;
    m_visible = true;
    setVisible(true);

    // 如果还没创建UI，现在创建
    if (!m_dialogPanel) {
        createDialogUI();
    }
}

void UIDialog::hide() {
    if (!m_visible) return;
    m_visible = false;
    setVisible(false);
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

    // 1. 绘制半透明遮罩层（全屏）
    auto size = getSize();
    renderer.drawRect(viewId, 0, 0, size.x, size.y, m_maskColor);

    // 2. 子节点会自动渲染（对话框面板等）
}

// === 创建对话框UI ===
void UIDialog::createDialogUI() {
    float dialogW = 500;
    float dialogH = 300;

    // 对话框面板（使用 Anchor 居中）
    // 原理：Anchor::MiddleCenter 提供父节点中心点的偏移
    //      position 设置为 -size/2 让自身中心对齐父节点中心
    auto panel = createChild<UIPanel>("DialogPanel");
    m_dialogPanel = panel;
    m_dialogPanel->setAnchor(Anchor::MiddleCenter);
    m_dialogPanel->setPosition(-dialogW * 0.5f, -dialogH * 0.5f);
    m_dialogPanel->setSize(dialogW, dialogH);
    m_dialogPanel->setColor(m_dialogBgColor);
    m_dialogPanel->setInteractable(true);  // 阻止事件穿透到下层
    m_dialogPanel->setClickable(true);

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
    auto btnCancel = m_dialogPanel->createChild<UIButton>("BtnCancel");
    m_btnCancel = btnCancel;
    m_btnCancel->setText("取消");
    m_btnCancel->setSize(120, 40);
    m_btnCancel->setPosition(140, 240);
    m_btnCancel->setFontPx(20);
    m_btnCancel->setOnClick([this]{ onCancelClicked(); });

    auto btnConfirm = m_dialogPanel->createChild<UIButton>("BtnConfirm");
    m_btnConfirm = btnConfirm;
    m_btnConfirm->setText("确定");
    m_btnConfirm->setSize(120, 40);
    m_btnConfirm->setPosition(280, 240);
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

} // namespace Tina::UI
