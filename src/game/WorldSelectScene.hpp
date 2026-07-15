//
// WorldSelectScene - 世界选择/新建场景
//

#pragma once

#include "../engine/Scene.hpp"
#include "../ui/UICore.hpp"
#include "../ui/UIComponents.hpp"
#include "../ui/UIListView.hpp"
#include "../ui/UIDialog.hpp"
#include "../ui/UITextEdit.hpp"
#include "../core/Memory.hpp"

namespace Tina::Game {

class WorldSelectScene : public Engine::Scene {
public:
    WorldSelectScene();
    ~WorldSelectScene() override;

    // 生命周期
    void onEnter() override;
    void onExit() override;

    // 主循环
    void update(float dt) override;
    void render() override;

protected:
    Container::Vector<ViewSetup> getViewSetup() override;

private:
    // UI
    class RootNode : public UI::UINode {
    public:
        RootNode(WorldSelectScene* scene) : UINode("WorldSelectRoot"), m_scene(scene) {}
        void onWindowSizeChanged(int w, int h) override;
    private:
        WorldSelectScene* m_scene = nullptr;
    };

    void createUI();
    void updateLayout();
    void handleInput();

    // 交互
    void onBackClicked();
    void onCreateClicked();
    void onEnterClicked();

    // 辅助
    void loadWorldList();
    static uint32_t hashSeed(const std::string& name);

private:
    // 数据：世界条目（名称 + 种子）
    struct WorldItem { std::string name; uint32_t seed = 0; };
    Tina::Container::Vector<WorldItem> m_worlds;

    // UI 节点
    Memory::SharedPtr<RootNode> m_root;
    UI::UIListView* m_list = nullptr;
    UI::UIButton* m_btnEnter = nullptr;
    UI::UIButton* m_btnCreate = nullptr;
    UI::UIButton* m_btnBack = nullptr;
    UI::UIDialog* m_createDialog = nullptr;      // 新建世界对话框
    UI::UITextEdit* m_worldNameInput = nullptr;  // 世界名称输入框

    // 布局比例
    float m_uiScale = 1.0f;
};

} // namespace Tina::Game

