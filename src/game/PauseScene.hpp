//
// PauseScene - 暂停菜单场景
// - 职责：显示暂停界面，处理继续/退出操作
// - 特性：半透明背景，保持下层 GameScene 可见但暂停
//

#pragma once

#include "../engine/Scene.hpp"
#include "../core/Memory.hpp"
#include "../ui/UICore.hpp"
#include "../ui/UINode.hpp"
#include "../ui/UIComponents.hpp"
#include "../ui/UIEventSystem.hpp"
#include "../renderer/ShaderManager.hpp"

namespace Tina::Game {

/**
 * PauseScene - 暂停菜单场景
 * 
 * 生命周期：
 * - onEnter：创建暂停 UI
 * - onExit：清理资源
 * - handleEvent：处理 ESC 键和按钮点击
 */
class PauseScene : public Engine::Scene {
public:
    PauseScene();
    ~PauseScene() override;

    // 生命周期
    void onEnter() override;
    void onExit() override;

    // 主循环
    void update(float dt) override;
    void render() override;
    void handleEvent(const Tina::os::Event& event) override;

private:
    void createUI();
    void renderOverlay();  // 渲染半透明遮罩

    // 按钮回调
    void onContinueClicked();
    void onQuitClicked();
    // 调试：昼夜控制
    void onSetDay();
    void onSetNight();
    void onFwdTime();
    void onBackTime();

private:
    // UI 资源（着色器来自全局 ShaderManager）
    Memory::UniquePtr<UI::UIRenderer> m_uiRenderer;

    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;

    // UI 组件
    Memory::UniquePtr<UI::UINode> m_rootNode;
    Container::Vector<Memory::UniquePtr<UI::UINode>> m_ownedNodes;  // 统一管理所有节点生命周期
    UI::UIButton* m_btnContinue = nullptr;  // 继续游戏按钮
    UI::UIButton* m_btnQuit = nullptr;      // 退出按钮
    // 调试按钮
    UI::UIButton* m_btnDay = nullptr;
    UI::UIButton* m_btnNight = nullptr;
    UI::UIButton* m_btnFwd = nullptr;
    UI::UIButton* m_btnBack = nullptr;
    UI::UIEventSystem m_events;             // 统一 Hover/Click 处理

    // Signal 连接（必须保存，否则订阅失效）
    Core::Signal<>::Connection m_continueConnection;
    Core::Signal<>::Connection m_quitConnection;
    Core::Signal<>::Connection m_cDay;
    Core::Signal<>::Connection m_cNight;
    Core::Signal<>::Connection m_cFwd;
    Core::Signal<>::Connection m_cBack;

    
    // 需要访问 UIRenderer 的顶点布局
    bgfx::VertexLayout m_colorLayout;

    // 视口尺寸
    int m_pixelWidth = 1280;
    int m_pixelHeight = 720;
};

} // namespace Tina::Game
