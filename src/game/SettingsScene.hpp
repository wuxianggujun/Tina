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
    void handleEvent(const Tina::os::Event& event) override;

private:
    void createUI();
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

    // Signal 连接保存
    Core::Signal<>::Connection m_cDay;
    Core::Signal<>::Connection m_cNight;
    Core::Signal<>::Connection m_cFwd;
    Core::Signal<>::Connection m_cBack;
    Core::Signal<>::Connection m_cClose;

    int m_pixelWidth = 1280;
    int m_pixelHeight = 720;
};

} // namespace Tina::Game
