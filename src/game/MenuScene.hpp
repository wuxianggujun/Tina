//
// MenuScene - 主菜单场景
// - 职责：显示游戏标题、提供开始游戏/设置/退出选项
// - 特性：粒子背景、按钮动画、键盘/鼠标交互
//

#pragma once

#include "../engine/Scene.hpp"
#include "../core/Memory.hpp"
#include "../ui/UICore.hpp"
#include "../ui/UINode.hpp"
#include "../ui/UIComponents.hpp"
#include "../ui/UIEventSystem.hpp"
#include "../particles/ParticleSystem.hpp"
#include "../renderer/ShaderManager.hpp"

namespace Tina::Game {

/**
 * MenuScene - 主菜单场景
 * 
 * 生命周期：
 * - onEnter：创建 UI、初始化动画
 * - onExit：清理资源
 * - handleEvent：处理键盘/鼠标输入
 */
class MenuScene : public Engine::Scene {
public:
    MenuScene();
    ~MenuScene() override;

    // 生命周期
    void onEnter() override;
    void onExit() override;
    // 注意：不再需要覆盖onWindowSizeChanged，MenuRootNode会自动处理

    // 主循环
    void update(float dt) override;
    void render() override;
    // 事件处理已迁移到 InputSystem

protected:
    // 视图配置（使用新架构）
    Container::Vector<ViewSetup> getViewSetup() override;

private:
    // UI 创建
    void createUI();
    void updateUILayout();  // 更新UI布局（窗口大小改变时）
    void computeUIScale();  // 计算 UI 缩放系数（基于参考分辨率）

    // 输入处理
    void handleInput();

    // 按钮回调
    void onStartClicked();
    void onSettingsClicked();
    void onQuitClicked();

    void selectPreviousButton();
    void selectNextButton();
    void activateSelectedButton();
    // 鼠标交互改为 UIEventSystem 统一处理（不再手写命中与 emit）

private:
    // 自定义根节点类（覆盖onWindowSizeChanged以调用updateUILayout）
    class MenuRootNode : public UI::UINode {
    public:
        MenuRootNode(MenuScene* scene) : UINode("MenuRoot"), m_scene(scene) {}

        void onWindowSizeChanged(int width, int height) override {
            // 更新尺寸
            setSize((float)width, (float)height);
            // 调用MenuScene的布局更新
            if (m_scene) {
                m_scene->computeUIScale();
                m_scene->updateUILayout();
            }
            // 递归通知子节点
            UINode::onWindowSizeChanged(width, height);
        }

    private:
        MenuScene* m_scene = nullptr;
    };

    // 渲染资源
    Memory::UniquePtr<Particles::ParticleSystem2D> m_bgParticles;


    Memory::UniquePtr<MenuRootNode> m_rootNode;
    Container::Vector<Memory::UniquePtr<UI::UINode>> m_ownedNodes;  // 统一管理所有节点生命周期
    UI::UIButton* m_btnStart = nullptr;
    UI::UIButton* m_btnSettings = nullptr;
    UI::UIButton* m_btnQuit = nullptr;
    UI::UIEventSystem m_events;  // 统一处理 hover/click

    // 动画状态
    float m_titleAlpha = 0.0f;
    float m_titleScale = 0.8f;
    float m_particleTimer = 0.0f;
    
    // 键盘导航
    int m_selectedButtonIndex = 0;
    Container::Vector<UI::UIButton*> m_buttons;
    
    // UI 缩放
    float m_uiScale = 1.0f;
};

} // namespace Tina::Game
