#include "UIDialog.hpp"
#include "UICore.hpp"

namespace Tina::UI {

// === 显示/隐藏 ===
void UIDialog::show() {
    if (isVisible()) return;
    ensureUICreated();
    setVisible(true);

    if (auto* events = eventSystem();
        events && !events->beginFocusScope(nodeId())) {
        TINA_WARN("UIDialog '{}': 无法建立模态焦点范围", getName());
    }
}

void UIDialog::hide() {
    if (!isVisible()) return;
    if (auto* events = eventSystem()) events->endFocusScope(nodeId());
    setVisible(false);
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

bool UIDialog::onKeyPressed(Tina::Engine::KeyCode key, bool isRepeat,
                            bool shift, bool ctrl, bool alt)
{
    (void)shift;
    if (!isVisible() || isRepeat || ctrl || alt ||
        key != Tina::Engine::KeyCode::Escape) {
        return false;
    }
    onCancelClicked();
    return true;
}

// === 渲染 ===
void UIDialog::onRender(uint16_t viewId, UIRenderer& renderer) {
    if (!isVisible()) return;

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

    // ✅ 使用布局容器：简洁优雅的布局
    
    // 边框效果
    auto border = m_dialogPanel->createChild<UIPanel>("BorderPanel");
    border->setPosition(-3, -3);
    border->setSizePercent(1.0f, 1.0f);
    border->setColor(Tina::Core::Color(0.05f, 0.05f, 0.1f, 1.0f));
    border->setZIndex(-1);

    // 标题栏
    auto titleLabel = m_dialogPanel->createChild<UILabel>("TitleLabel");
    m_titleLabel = titleLabel;
    m_titleLabel->setText(m_title);
    m_titleLabel->setColor(m_titleColor);
    m_titleLabel->setFontPx(24);
    m_titleLabel->setPosition(20, 20);
    m_titleLabel->setSize(dialogW - 40, 40);
    m_titleLabel->setAlignment(UILabel::TextAlignH::Center, UILabel::TextAlignV::Center);

    // ✅ 内容区域（使用VBox）
    auto contentVBox = m_dialogPanel->createChild<UIVBox>("ContentVBox");
    m_contentArea = contentVBox;
    m_contentArea->setPosition(20, 70);
    m_contentArea->setSpacing(20);       // 子元素间距20px
    m_contentArea->setPadding(0);        // 无内边距（外部已有边距）
    m_contentArea->setFillWidth(true);   // 子元素宽度自动填充
    m_contentArea->setSize(dialogW - 40, dialogH - 130);  // ✅ 最后设置尺寸，触发布局

    // ✅ 按钮区域（使用HBox）
    auto buttonHBox = m_dialogPanel->createChild<UIHBox>("ButtonHBox");
    m_buttonBox = buttonHBox;
    m_buttonBox->setSize(dialogW, 60);
    m_buttonBox->setPosition(0, dialogH - 60);
    m_buttonBox->setSpacing(20);
    m_buttonBox->setJustify(UIHBox::Justify::Center);  // 居中对齐

    auto btnCancel = m_buttonBox->createChild<UIButton>("BtnCancel");
    m_btnCancel = btnCancel;
    m_btnCancel->setText("取消");
    m_btnCancel->setSize(120, 40);
    m_btnCancel->setFontPx(20);
    m_btnCancel->setOnClick([this]{ onCancelClicked(); });

    auto btnConfirm = m_buttonBox->createChild<UIButton>("BtnConfirm");
    m_btnConfirm = btnConfirm;
    m_btnConfirm->setText("确定");
    m_btnConfirm->setSize(120, 40);
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
    
    // ✅ 更新内容区域尺寸（VBox会自动重新布局子元素）
    if (m_contentArea) {
        m_contentArea->setSize(dialogW - 40, dialogH - 130);
        // ✅ 不需要手动更新子元素！VBox自动处理！
    }
    
    // ✅ 更新按钮区域尺寸（HBox会自动重新布局按钮）
    if (m_buttonBox) {
        m_buttonBox->setSize(dialogW, 60);
        m_buttonBox->setPosition(0, dialogH - 60);
        // ✅ 不需要手动更新按钮位置！HBox自动处理！
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

} // namespace Tina::UI
