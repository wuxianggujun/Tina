//
// SettingsScene - 设置/调试页面
// - 目的：快速切换昼夜、暂停/恢复昼夜推进等
//

#pragma once

#include "../engine/Scene.hpp"
#include "../core/Memory.hpp"
#include "../ui/UICore.hpp"
#include "../ui/UINode.hpp"
#include "../ui/UIComponents.hpp"
#include "../ui/UIEventSystem.hpp"

namespace Tina::Game {

class SettingsScene : public Engine::Scene {
public:
    SettingsScene();
    ~SettingsScene() override;

    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;
    // 事件处理已迁移到 InputSystem

    // 窗口尺寸更新
    void updateWindowSize(int width, int height) override;

protected:
    // 实际应用窗口调整（覆盖基类方法）
    void applyWindowResize(int width, int height) override;

private:
    void createUI();
    void handleInput();     // 处理输入
    void onBack();
    void onSetDay();
    void onSetNight();
    void onFwdTime();
    void onBackTime();

private:
    Memory::UniquePtr<UI::UINode> m_root;
    Container::Vector<Memory::UniquePtr<UI::UINode>> m_ownedNodes;  // 统一管理所有节点生命周期
    UI::UIButton* m_btnDay = nullptr;
    UI::UIButton* m_btnNight = nullptr;
    UI::UIButton* m_btnFwd = nullptr;
    UI::UIButton* m_btnBack = nullptr;
    UI::UIButton* m_btnClose = nullptr;
    UI::UIEventSystem m_events;

    int m_pixelWidth = 1280;
    int m_pixelHeight = 720;
};

} // namespace Tina::Game
