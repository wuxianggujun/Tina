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
    // 对话框面板（居中）
    auto panel = createChild<UIPanel>("DialogPanel");
    m_dialogPanel = panel;
    m_dialogPanel->setColor(m_dialogBgColor);
    m_dialogPanel->setSize(500, 300);
    m_dialogPanel->setAnchor(Anchor::MiddleCenter);
    m_dialogPanel->setClickable(true);  // 阻止点击事件冒泡到遮罩层

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
