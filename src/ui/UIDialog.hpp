// UIDialog：模态对话框组件
#pragma once

#include "UINode.hpp"
#include "UIComponents.hpp"
#include "../core/Color.hpp"
#include "../core/Container.hpp"
#include "../engine/SubscriptionToken.hpp"
#include "../engine/EngineEvents.hpp"
#include <functional>

namespace Tina::UI {

// === UIDialog：模态对话框 ===
// 特性：
// - 半透明背景遮罩（全屏）
// - 居中的对话框面板
// - 标题栏
// - 内容区域（可添加自定义组件）
// - 按钮区域（确定、取消）
class UIDialog : public UINode {
public:
    UIDialog(const std::string& name = "Dialog")
        : UINode(name)
        , m_maskColor(0.0f, 0.0f, 0.0f, 0.6f)
        , m_dialogBgColor(0.2f, 0.2f, 0.25f, 1.0f)
        , m_titleColor(1.0f, 1.0f, 1.0f, 1.0f)
        , m_visible(false)
    {
        setClickable(true);     // 允许点击遮罩关闭
        setInteractable(true);  // 阻止事件穿透到下层UI
    }

    // === 显示/隐藏 ===
    void show();
    void hide();
    bool isDialogVisible() const { return m_visible; }

    // === 内容设置 ===
    void setTitle(const std::string& title);
    const std::string& getTitle() const { return m_title; }

    // === 颜色配置 ===
    void setMaskColor(const Tina::Core::Color& c) { m_maskColor = c; }
    void setDialogBgColor(const Tina::Core::Color& c) { m_dialogBgColor = c; }
    void setTitleColor(const Tina::Core::Color& c) { m_titleColor = c; }

    // === 内容区域访问 ===
    // 返回内容区域节点，可以添加自定义UI组件
    UINode* getContentArea() const { return m_contentArea; }

    // === 回调设置 ===
    void setOnConfirm(std::function<void()> callback) { m_onConfirm = std::move(callback); }
    void setOnCancel(std::function<void()> callback) { m_onCancel = std::move(callback); }

    // === 按钮文本自定义 ===
    void setConfirmText(const std::string& text);
    void setCancelText(const std::string& text);

    // === 鼠标事件 ===
    void onClick() override;

    // === 窗口尺寸变化回调 ===
    void onWindowSizeChanged(int width, int height) override;

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    void createDialogUI();
    void updateDialogPosition();  // 更新对话框居中位置
    void onConfirmClicked();
    void onCancelClicked();
    void handleKeyPressed(const Tina::Engine::Events::KeyPressedEvent& e);

private:
    std::string m_title;
    Tina::Core::Color m_maskColor;      // 遮罩层颜色
    Tina::Core::Color m_dialogBgColor;  // 对话框背景颜色
    Tina::Core::Color m_titleColor;     // 标题颜色
    bool m_visible;

    // UI组件
    UIPanel* m_dialogPanel = nullptr;    // 对话框面板
    UILabel* m_titleLabel = nullptr;     // 标题标签
    UINode* m_contentArea = nullptr;     // 内容区域
    UIButton* m_btnConfirm = nullptr;    // 确定按钮
    UIButton* m_btnCancel = nullptr;     // 取消按钮

    // 回调
    std::function<void()> m_onConfirm;
    std::function<void()> m_onCancel;

    // 事件订阅（用于处理 Enter / Escape 快捷键）
    Tina::Engine::SubscriptionToken m_keyToken;
}; 

} // namespace Tina::UI
